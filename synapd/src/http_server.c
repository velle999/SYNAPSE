/*
 * http_server.c — llama.cpp-shaped HTTP over the resident model. See
 * http_server.h for why this exists and why it is not a port.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#include "http_server.h"
#include "inference.h"
#include "context.h"
#include "log.h"

#include <errno.h>
#include <grp.h>
#include <json-c/json.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

/* ⚠ A CAP ON CONCURRENT CONNECTIONS, because this is thread-per-connection and
 * inference serialises anyway. Without one, a client in a retry loop spawns
 * threads until the daemon dies — and it would die holding a model somebody's
 * desktop is using. */
#define HTTP_MAX_CONNS   16
#define HTTP_MAX_BODY    (1024 * 1024)
#define HTTP_HEADER_MAX  8192

static int                g_listen_fd = -1;
static pthread_t          g_accept_thread;
static atomic_bool        g_running;
static atomic_int         g_conns;
static synapd_state_t    *g_state;

/* ── writing ────────────────────────────────────────────────────────────── */

/* ⛔ WRITES ARE PARTIAL. A single write() of a long answer over a socket
 * returns short, and a caller that treats that as done truncates the JSON —
 * which a client reports as a parse error, blaming the model. */
static int write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && (errno == EINTR)) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd p = { fd, POLLOUT, 0 };
            if (poll(&p, 1, 30000) <= 0) return -1;
            continue;
        }
        return -1;
    }
    return 0;
}

static void send_raw(int fd, int status, const char *reason,
                     const char *ctype, const char *body, size_t blen)
{
    char head[512];
    int hn = snprintf(head, sizeof head,
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      /* ⚠ CORS, because a compatibility layer that a local web
                       * UI cannot call is not compatible with the clients most
                       * likely to want it. The socket is the boundary here, not
                       * the origin header: nothing reaches this without already
                       * being able to open a 0660 socket owned by group
                       * synapse. */
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Headers: *\r\n"
                      "Connection: close\r\n\r\n",
                      status, reason, ctype, blen);
    if (hn < 0) return;
    if (write_all(fd, head, (size_t)hn) == 0 && blen)
        write_all(fd, body, blen);
}

static void send_json(int fd, int status, const char *reason, json_object *o)
{
    const char *s = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
    send_raw(fd, status, reason, "application/json", s, strlen(s));
}

/* The error shape both llama.cpp's server and the OpenAI API use, so a client's
 * own error handling says something useful instead of "unexpected response". */
static void send_err(int fd, int status, const char *reason,
                     const char *type, const char *msg)
{
    json_object *e = json_object_new_object();
    json_object *inner = json_object_new_object();
    json_object_object_add(inner, "message", json_object_new_string(msg));
    json_object_object_add(inner, "type", json_object_new_string(type));
    json_object_object_add(inner, "code", json_object_new_int(status));
    json_object_object_add(e, "error", inner);
    send_json(fd, status, reason, e);
    json_object_put(e);
}

/* ── the model, as this layer sees it ───────────────────────────────────── */

static const char *model_name(void)
{
    /* The basename of what is loaded, which is what a client shows in a picker
     * and sends back in `model`. */
    const char *p = g_state && g_state->config.model_path ? g_state->config.model_path : NULL;
    if (!p || !*p) return "synapse";
    const char *slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}

/* Is the model in a state that can answer at all? Mirrors handle_query()'s
 * checks in socket_server.c, and says the same things — a client asking over
 * HTTP deserves the same answer as one asking over the socket. */
static const char *not_ready_reason(void)
{
    if (!g_state) return "the daemon is not ready";
    if (atomic_load(&g_state->model_sleeping))
        return "the model was released for suspend and is reloading — try again in a moment";
    if (!g_state->model_loaded && atomic_load(&g_state->model_loading))
        return "the model is still loading — try again in a moment";
    if (!g_state->model_loaded)
        return "no model is loaded";
    return NULL;
}

/* ── running one ────────────────────────────────────────────────────────── */

/* Answer `prompt`, already rendered if `pre_templated`. Returns a malloc'd
 * string or NULL. */
static char *run_inference(const char *system_ctx, const char *prompt,
                           int max_tokens, int pre_templated)
{
    char *out = malloc(SYN_MAX_PAYLOAD);
    if (!out) return NULL;
    out[0] = '\0';

    int n = inference_run(g_state, pre_templated ? NULL : system_ctx, prompt,
                          out, SYN_MAX_PAYLOAD - 1, max_tokens, pre_templated);
    if (n < 0) { free(out); return NULL; }
    return out;
}

/* messages[] → one prompt, through the model's own template where there is one.
 * Sets *pre_templated when the template did the work, which is the flag
 * inference_run needs to leave the string alone. */
static char *prompt_from_messages(json_object *msgs, int *pre_templated,
                                  char *sys_out, size_t sys_len)
{
    *pre_templated = 0;
    sys_out[0] = '\0';

    size_t n = (size_t)json_object_array_length(msgs);
    if (n == 0) return NULL;

    syn_chat_msg_t *m = calloc(n, sizeof *m);
    if (!m) return NULL;

    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        json_object *e = json_object_array_get_idx(msgs, i), *r = NULL, *c = NULL;
        if (!json_object_object_get_ex(e, "role", &r)) continue;
        if (!json_object_object_get_ex(e, "content", &c)) continue;
        /* ⚠ CONTENT CAN BE AN ARRAY of typed parts in the newer OpenAI shape.
         * Only text is understood here; anything else is skipped rather than
         * stringified, because "[object]" in a prompt is worse than absent. */
        if (!json_object_is_type(c, json_type_string)) continue;
        m[used].role = json_object_get_string(r);
        m[used].content = json_object_get_string(c);
        used++;
    }
    if (used == 0) { free(m); return NULL; }

    char *rendered = inference_render_chat(g_state, m, used);
    if (rendered) {
        *pre_templated = 1;
        free(m);
        return rendered;
    }

    /* No template, or the model is in flux: fall back to the route the socket
     * protocol uses — system context plus the last user turn. Earlier turns are
     * folded in with plain labels, which is what a template-less model saw
     * before this file existed. */
    size_t cap = 1;
    for (size_t i = 0; i < used; i++) cap += strlen(m[i].content) + 16;
    char *flat = malloc(cap);
    if (!flat) { free(m); return NULL; }
    flat[0] = '\0';

    for (size_t i = 0; i < used; i++) {
        const char *role = m[i].role ? m[i].role : "user";
        if (!strcmp(role, "system")) {
            size_t have = strlen(sys_out);
            snprintf(sys_out + have, sys_len - have, "%s%s", have ? "\n" : "", m[i].content);
            continue;
        }
        size_t have = strlen(flat);
        snprintf(flat + have, cap - have, "%s%s: %s",
                 have ? "\n" : "",
                 !strcmp(role, "assistant") ? "Assistant" : "User",
                 m[i].content);
    }
    free(m);
    return flat;
}

/* ── the endpoints ──────────────────────────────────────────────────────── */

static int int_field(json_object *o, const char *key, int fallback)
{
    json_object *v = NULL;
    if (o && json_object_object_get_ex(o, key, &v) &&
        (json_object_is_type(v, json_type_int) || json_object_is_type(v, json_type_double)))
        return json_object_get_int(v);
    return fallback;
}

static int bool_field(json_object *o, const char *key)
{
    json_object *v = NULL;
    return o && json_object_object_get_ex(o, key, &v) && json_object_get_boolean(v);
}

/* ⚠ CLAMPED, AND NOT SILENTLY. SYN_QF_TOKENS_MASK is what the socket protocol
 * can carry; a client asking for more than that gets the most this daemon can
 * do rather than an argument. Zero means "the inference default". */
static int clamp_tokens(int n)
{
    if (n <= 0) return 0;
    if (n > SYN_QF_TOKENS_MASK) return SYN_QF_TOKENS_MASK;
    return n;
}

/* One answer, in whichever of the two shapes was asked for. */
static void send_answer(int fd, const char *answer, int chat_shape, int stream)
{
    if (!stream) {
        json_object *o = json_object_new_object();
        if (chat_shape) {
            json_object *choices = json_object_new_array();
            json_object *choice = json_object_new_object();
            json_object *msg = json_object_new_object();
            json_object_object_add(msg, "role", json_object_new_string("assistant"));
            json_object_object_add(msg, "content", json_object_new_string(answer));
            json_object_object_add(choice, "index", json_object_new_int(0));
            json_object_object_add(choice, "message", msg);
            json_object_object_add(choice, "finish_reason", json_object_new_string("stop"));
            json_object_array_add(choices, choice);

            json_object_object_add(o, "id", json_object_new_string("chatcmpl-synapd"));
            json_object_object_add(o, "object", json_object_new_string("chat.completion"));
            json_object_object_add(o, "created", json_object_new_int64((int64_t)time(NULL)));
            json_object_object_add(o, "model", json_object_new_string(model_name()));
            json_object_object_add(o, "choices", choices);
        } else {
            /* llama.cpp's native /completion shape. */
            json_object_object_add(o, "content", json_object_new_string(answer));
            json_object_object_add(o, "model", json_object_new_string(model_name()));
            json_object_object_add(o, "stop", json_object_new_boolean(1));
            json_object_object_add(o, "stopped_eos", json_object_new_boolean(1));
        }
        send_json(fd, 200, "OK", o);
        json_object_put(o);
        return;
    }

    /* ⚠ ONE EVENT, THEN [DONE]. See the note in http_server.h: the framing is
     * real so clients that require a stream work, but there is nothing to
     * stream token by token behind it. */
    const char *head =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n";
    if (write_all(fd, head, strlen(head)) != 0) return;

    json_object *o = json_object_new_object();
    if (chat_shape) {
        json_object *choices = json_object_new_array();
        json_object *choice = json_object_new_object();
        json_object *delta = json_object_new_object();
        json_object_object_add(delta, "role", json_object_new_string("assistant"));
        json_object_object_add(delta, "content", json_object_new_string(answer));
        json_object_object_add(choice, "index", json_object_new_int(0));
        json_object_object_add(choice, "delta", delta);
        json_object_object_add(choice, "finish_reason", json_object_new_string("stop"));
        json_object_array_add(choices, choice);
        json_object_object_add(o, "id", json_object_new_string("chatcmpl-synapd"));
        json_object_object_add(o, "object", json_object_new_string("chat.completion.chunk"));
        json_object_object_add(o, "created", json_object_new_int64((int64_t)time(NULL)));
        json_object_object_add(o, "model", json_object_new_string(model_name()));
        json_object_object_add(o, "choices", choices);
    } else {
        json_object_object_add(o, "content", json_object_new_string(answer));
        json_object_object_add(o, "stop", json_object_new_boolean(1));
    }

    const char *body = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
    char *evt = NULL;
    if (asprintf(&evt, "data: %s\n\n", body) > 0) {
        write_all(fd, evt, strlen(evt));
        free(evt);
    }
    json_object_put(o);

    static const char done[] = "data: [DONE]\n\n";
    write_all(fd, done, sizeof done - 1);
}

static void handle_completion(int fd, json_object *req, int chat_shape)
{
    const char *why = not_ready_reason();
    if (why) { send_err(fd, 503, "Service Unavailable", "unavailable_error", why); return; }

    int stream = bool_field(req, "stream");
    int max_tokens = clamp_tokens(int_field(req, "max_tokens",
                                  int_field(req, "n_predict", 0)));

    char sys_ctx[1024] = {0};
    char *prompt = NULL;
    int pre_templated = 0;

    json_object *msgs = NULL;
    if (chat_shape && json_object_object_get_ex(req, "messages", &msgs) &&
        json_object_is_type(msgs, json_type_array)) {
        prompt = prompt_from_messages(msgs, &pre_templated, sys_ctx, sizeof sys_ctx);
    } else {
        json_object *p = NULL;
        if (json_object_object_get_ex(req, "prompt", &p) &&
            json_object_is_type(p, json_type_string))
            prompt = strdup(json_object_get_string(p));
    }

    if (!prompt || !*prompt) {
        free(prompt);
        send_err(fd, 400, "Bad Request", "invalid_request_error",
                 chat_shape ? "no messages with text content"
                            : "no prompt");
        return;
    }

    /* Without a system message, the rolling OS context is what the socket
     * protocol would have used — the same assistant, answering the same way. */
    if (!pre_templated && !sys_ctx[0])
        context_get_summary(g_state, sys_ctx, sizeof sys_ctx);

    char *answer = run_inference(sys_ctx, prompt, max_tokens, pre_templated);
    free(prompt);

    if (!answer) {
        send_err(fd, 500, "Internal Server Error", "server_error", "inference failed");
        return;
    }
    send_answer(fd, answer, chat_shape, stream);
    free(answer);
}

static void handle_models(int fd)
{
    json_object *o = json_object_new_object();
    json_object *data = json_object_new_array();
    json_object *m = json_object_new_object();
    json_object_object_add(m, "id", json_object_new_string(model_name()));
    json_object_object_add(m, "object", json_object_new_string("model"));
    json_object_object_add(m, "owned_by", json_object_new_string("synapd"));
    json_object_object_add(m, "created", json_object_new_int64(0));
    json_object_array_add(data, m);
    json_object_object_add(o, "object", json_object_new_string("list"));
    json_object_object_add(o, "data", data);
    send_json(fd, 200, "OK", o);
    json_object_put(o);
}

static void handle_health(int fd)
{
    const char *why = not_ready_reason();
    if (why) {
        /* llama-server answers 503 with this exact shape while a model loads,
         * and clients poll it waiting for readiness. */
        send_err(fd, 503, "Service Unavailable", "unavailable_error", why);
        return;
    }
    json_object *o = json_object_new_object();
    json_object_object_add(o, "status", json_object_new_string("ok"));
    send_json(fd, 200, "OK", o);
    json_object_put(o);
}

static void handle_props(int fd)
{
    char desc[512] = {0};
    inference_describe(g_state, desc, sizeof desc);

    json_object *o = json_object_new_object();
    json_object_object_add(o, "model_path", json_object_new_string(model_name()));
    json_object_object_add(o, "total_slots", json_object_new_int(1));
    /* ⚠ SAID OUT LOUD, because a client cannot discover it. Anything reading
     * /props to decide whether to stream should know the stream arrives whole. */
    json_object_object_add(o, "synapd_streaming",
                           json_object_new_string("single-chunk"));
    json_object_object_add(o, "synapd_model", json_object_new_string(desc));
    send_json(fd, 200, "OK", o);
    json_object_put(o);
}

/* ── one connection ─────────────────────────────────────────────────────── */

static void serve(int fd)
{
    char head[HTTP_HEADER_MAX + 1];
    size_t got = 0;
    char *hdr_end = NULL;

    /* The headers, and whatever of the body arrived with them. */
    while (got < sizeof head - 1) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 15000) <= 0) return;
        ssize_t n = read(fd, head + got, sizeof head - 1 - got);
        if (n <= 0) return;
        got += (size_t)n;
        head[got] = '\0';
        if ((hdr_end = strstr(head, "\r\n\r\n")) != NULL) break;
    }
    if (!hdr_end) { send_err(fd, 431, "Request Header Fields Too Large",
                             "invalid_request_error", "headers too long"); return; }

    char method[16] = {0}, path[256] = {0};
    if (sscanf(head, "%15s %255s", method, path) != 2) {
        send_err(fd, 400, "Bad Request", "invalid_request_error", "unreadable request line");
        return;
    }

    /* A browser-based client sends this before anything else. */
    if (!strcmp(method, "OPTIONS")) { send_raw(fd, 204, "No Content", "text/plain", "", 0); return; }

    if (!strcmp(method, "GET")) {
        if (!strcmp(path, "/health"))     { handle_health(fd); return; }
        if (!strcmp(path, "/props"))      { handle_props(fd);  return; }
        if (!strcmp(path, "/v1/models") ||
            !strcmp(path, "/models"))     { handle_models(fd); return; }
        send_err(fd, 404, "Not Found", "invalid_request_error", "no such endpoint");
        return;
    }

    if (strcmp(method, "POST") != 0) {
        send_err(fd, 405, "Method Not Allowed", "invalid_request_error", "use GET or POST");
        return;
    }

    /* ⛔ Content-Length IS A CLAIM, NOT A FACT. It is bounded before it is
     * believed: an unchecked one is a malloc of whatever a client says. */
    long clen = 0;
    const char *cl = strcasestr(head, "\r\ncontent-length:");
    if (cl) clen = strtol(cl + 17, NULL, 10);
    if (clen < 0 || clen > HTTP_MAX_BODY) {
        send_err(fd, 413, "Payload Too Large", "invalid_request_error", "body too large");
        return;
    }

    size_t have = got - (size_t)(hdr_end + 4 - head);
    char *body = malloc((size_t)clen + 1);
    if (!body) { send_err(fd, 500, "Internal Server Error", "server_error", "oom"); return; }
    if (have > (size_t)clen) have = (size_t)clen;
    memcpy(body, hdr_end + 4, have);

    while (have < (size_t)clen) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 15000) <= 0) { free(body); return; }
        ssize_t n = read(fd, body + have, (size_t)clen - have);
        if (n <= 0) { free(body); return; }
        have += (size_t)n;
    }
    body[clen] = '\0';

    json_object *req = json_tokener_parse(body);
    free(body);
    if (!req) {
        send_err(fd, 400, "Bad Request", "invalid_request_error", "body is not JSON");
        return;
    }

    if (!strcmp(path, "/v1/chat/completions") || !strcmp(path, "/chat/completions"))
        handle_completion(fd, req, 1);
    else if (!strcmp(path, "/completion") || !strcmp(path, "/completions") ||
             !strcmp(path, "/v1/completions"))
        handle_completion(fd, req, 0);
    else
        send_err(fd, 404, "Not Found", "invalid_request_error", "no such endpoint");

    json_object_put(req);
}

static void *conn_thread(void *arg)
{
    int fd = (int)(intptr_t)arg;
    serve(fd);
    close(fd);
    atomic_fetch_sub(&g_conns, 1);
    return NULL;
}

static void *accept_thread(void *arg)
{
    (void)arg;
    while (atomic_load(&g_running)) {
        struct pollfd p = { g_listen_fd, POLLIN, 0 };
        int r = poll(&p, 1, 500);
        if (r <= 0) continue;

        int cfd = accept4(g_listen_fd, NULL, NULL, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            if (!atomic_load(&g_running)) break;
            continue;
        }

        if (atomic_fetch_add(&g_conns, 1) >= HTTP_MAX_CONNS) {
            atomic_fetch_sub(&g_conns, 1);
            send_err(cfd, 503, "Service Unavailable", "unavailable_error",
                     "too many connections");
            close(cfd);
            continue;
        }

        pthread_t t;
        if (pthread_create(&t, NULL, conn_thread, (void *)(intptr_t)cfd) != 0) {
            atomic_fetch_sub(&g_conns, 1);
            close(cfd);
            continue;
        }
        pthread_detach(t);
    }
    return NULL;
}

/* ── lifecycle ──────────────────────────────────────────────────────────── */

int http_server_start(synapd_state_t *s)
{
    g_state = s;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        syn_log(LOG_WARNING, "http: socket(): %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", SYNAPD_HTTP_SOCKET_PATH);

    unlink(SYNAPD_HTTP_SOCKET_PATH);

    /* ⛔ THE MODE IS SET BEFORE THE SOCKET EXISTS, not after. Between a bind()
     * and a chmod() the socket is whatever the umask left it, and on this path
     * that window is world-writable — which is a window in which anything on
     * the machine can ask the model questions. */
    mode_t old = umask(0117);
    int rc = bind(fd, (struct sockaddr *)&a, sizeof a);
    umask(old);

    if (rc < 0) {
        syn_log(LOG_WARNING, "http: bind(%s): %s", SYNAPD_HTTP_SOCKET_PATH, strerror(errno));
        close(fd);
        return -1;
    }

    struct group *gr = getgrnam("synapse");
    if (gr && chown(SYNAPD_HTTP_SOCKET_PATH, -1, gr->gr_gid) < 0)
        syn_log(LOG_WARNING, "http: chown(%s, :synapse): %s",
                SYNAPD_HTTP_SOCKET_PATH, strerror(errno));
    chmod(SYNAPD_HTTP_SOCKET_PATH, 0660);

    if (listen(fd, 8) < 0) {
        syn_log(LOG_WARNING, "http: listen(): %s", strerror(errno));
        close(fd);
        unlink(SYNAPD_HTTP_SOCKET_PATH);
        return -1;
    }

    g_listen_fd = fd;
    atomic_store(&g_conns, 0);
    atomic_store(&g_running, true);

    if (pthread_create(&g_accept_thread, NULL, accept_thread, NULL) != 0) {
        syn_log(LOG_WARNING, "http: pthread_create(): %s", strerror(errno));
        atomic_store(&g_running, false);
        close(fd);
        g_listen_fd = -1;
        unlink(SYNAPD_HTTP_SOCKET_PATH);
        return -1;
    }

    syn_log(LOG_INFO, "http: llama.cpp-compatible API on %s", SYNAPD_HTTP_SOCKET_PATH);
    return 0;
}

void http_server_stop(synapd_state_t *s)
{
    (void)s;
    if (!atomic_load(&g_running)) return;
    atomic_store(&g_running, false);
    pthread_join(g_accept_thread, NULL);
    if (g_listen_fd >= 0) close(g_listen_fd);
    g_listen_fd = -1;
    unlink(SYNAPD_HTTP_SOCKET_PATH);
}
