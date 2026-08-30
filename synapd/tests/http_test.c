/*
 * http_test.c — the llama.cpp-compatible face, without a model behind it.
 *
 * ⛔ NO llama, NO GGUF, NO INFERENCE. Everything worth testing here is the
 * translation: what the routes are, what a client is told when the model cannot
 * answer, and what happens to a request that lies about its own size. Linking
 * the real inference would mean this suite only runs on a machine with a
 * multi-gigabyte model on it, which is another way of saying it never runs.
 *
 * The stubs below stand in for it, and the socket path is redirected at a
 * scratch file — a test that bound the real /run/synapd/http.sock would take
 * the running daemon's place on a developer's own desktop.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "http_server.h"
#include "inference.h"
#include "context.h"
#include "log.h"

#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int fails = 0, total = 0;
static void ok_(const char *what, int cond)
{
    total++;
    if (cond) printf("  ok    %s\n", what);
    else { printf("  FAIL  %s\n", what); fails++; }
}

/* ── what the daemon would have done ────────────────────────────────────── */

static int   g_infer_calls;
static char  g_last_prompt[4096];
static int   g_last_raw;
static int   g_last_max_tokens;
static int   g_render_works = 1;

void syn_log(int p, const char *fmt, ...) { (void)p; (void)fmt; }
void syn_log_init(int l) { (void)l; }

void context_get_summary(synapd_state_t *s, char *out, size_t len)
{
    (void)s;
    snprintf(out, len, "os-context");
}

int inference_run(synapd_state_t *s, const char *sys, const char *prompt,
                  char *out, size_t out_len, int max_tokens, int raw)
{
    (void)s; (void)sys;
    g_infer_calls++;
    g_last_raw = raw;
    g_last_max_tokens = max_tokens;
    snprintf(g_last_prompt, sizeof g_last_prompt, "%s", prompt ? prompt : "");
    int n = snprintf(out, out_len, "an answer");
    return n;
}

/* The real one returns NULL when there is no template; both paths matter. */
char *inference_render_chat(synapd_state_t *s, const syn_chat_msg_t *m, size_t n)
{
    (void)s;
    if (!g_render_works) return NULL;
    char *buf = malloc(4096);
    if (!buf) return NULL;
    buf[0] = '\0';
    for (size_t i = 0; i < n; i++) {
        size_t have = strlen(buf);
        snprintf(buf + have, 4096 - have, "<%s>%s", m[i].role, m[i].content);
    }
    return buf;
}

void inference_describe(synapd_state_t *s, char *buf, size_t len)
{
    (void)s;
    snprintf(buf, len, "stub-model");
}

/* ── talking to it ──────────────────────────────────────────────────────── */

/* One request, one response, and the connection closes — which is what the
 * server promises with Connection: close. */
static char *request(const char *raw_req)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", SYNAPD_HTTP_SOCKET_PATH);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return NULL; }

    if (write(fd, raw_req, strlen(raw_req)) < 0) { close(fd); return NULL; }

    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) { close(fd); return NULL; }
    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 5000) <= 0) break;
        ssize_t n = read(fd, buf + len, cap - len - 1);
        if (n <= 0) break;
        len += (size_t)n;
        if (len + 1 >= cap) break;
    }
    buf[len] = '\0';
    close(fd);
    return buf;
}

static char *post(const char *path, const char *body)
{
    char *req = NULL;
    if (asprintf(&req, "POST %s HTTP/1.1\r\nHost: localhost\r\n"
                       "Content-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
                 path, strlen(body), body) < 0) return NULL;
    char *r = request(req);
    free(req);
    return r;
}

static int status_is(const char *resp, int code)
{
    if (!resp) return 0;
    char want[32];
    snprintf(want, sizeof want, "HTTP/1.1 %d ", code);
    return strncmp(resp, want, strlen(want)) == 0;
}

static int has(const char *resp, const char *needle)
{
    return resp && strstr(resp, needle) != NULL;
}

int main(void)
{
    synapd_state_t st;
    memset(&st, 0, sizeof st);
    st.config.model_path = "/var/lib/synapd/models/synapse.gguf";
    pthread_rwlock_init(&st.model_rw, NULL);

    if (http_server_start(&st) != 0) {
        fprintf(stderr, "http_test: could not start the server\n");
        return 2;
    }

    printf("http\n");

    /* ── while no model is loaded ──────────────────────────────────────── */

    char *r = request("GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n");
    /* ⛔ 503, NOT 200-with-a-sad-message. Clients poll /health waiting for a
     * model and treat any 200 as ready, then send a request that fails. */
    ok_("health says 503 while nothing is loaded", status_is(r, 503));
    ok_("…and says which of the reasons it is", has(r, "no model is loaded"));
    free(r);

    r = post("/v1/chat/completions", "{\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}");
    ok_("a chat request is refused the same way", status_is(r, 503));
    free(r);

    /* ── with one ──────────────────────────────────────────────────────── */

    st.model_loaded = 1;

    r = request("GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ok_("health is ok once a model is loaded", status_is(r, 200) && has(r, "\"status\":\"ok\""));
    free(r);

    r = request("GET /v1/models HTTP/1.1\r\nHost: localhost\r\n\r\n");
    /* The basename, because that is what a client puts in its picker. */
    ok_("the model list names the loaded model",
        status_is(r, 200) && has(r, "synapse.gguf") && has(r, "\"object\":\"list\""));
    free(r);

    g_infer_calls = 0;
    r = post("/v1/chat/completions",
             "{\"messages\":[{\"role\":\"system\",\"content\":\"be brief\"},"
             "{\"role\":\"user\",\"content\":\"hello\"}],\"max_tokens\":64}");
    ok_("a chat completion answers in the OpenAI shape",
        status_is(r, 200) && has(r, "\"object\":\"chat.completion\"") &&
        has(r, "\"content\":\"an answer\"") && has(r, "\"finish_reason\":\"stop\""));
    ok_("…having actually run inference once", g_infer_calls == 1);
    /* ⛔ THE WHOLE CONVERSATION WENT THROUGH THE TEMPLATE, and raw=1 says so —
     * that flag is what stops inference_run wrapping an already-templated
     * string in one more user turn. */
    ok_("…through the model's own template, marked as pre-templated",
        g_last_raw == 1 && strstr(g_last_prompt, "<system>be brief") &&
        strstr(g_last_prompt, "<user>hello"));
    ok_("…and passed the token limit on", g_last_max_tokens == 64);
    free(r);

    /* ── the same request when the model has no chat template ──────────── */

    g_render_works = 0;
    g_infer_calls = 0;
    r = post("/v1/chat/completions",
             "{\"messages\":[{\"role\":\"system\",\"content\":\"be brief\"},"
             "{\"role\":\"user\",\"content\":\"hello\"}]}");
    ok_("a template-less model still answers", status_is(r, 200) && g_infer_calls == 1);
    /* ⚠ AND NOT AS RAW. Without a template there is nothing pre-formatted, so
     * inference_run must do its own wrapping or the model sees a bare string. */
    ok_("…by the plain route, not as pre-templated", g_last_raw == 0);
    ok_("…with the turns labelled", strstr(g_last_prompt, "User: hello") != NULL);
    free(r);
    g_render_works = 1;

    /* ── llama.cpp's own shape ─────────────────────────────────────────── */

    g_infer_calls = 0;
    r = post("/completion", "{\"prompt\":\"once upon a\",\"n_predict\":32}");
    ok_("llama.cpp's /completion answers in its own shape",
        status_is(r, 200) && has(r, "\"content\":\"an answer\"") && has(r, "\"stop\":true"));
    ok_("…and n_predict is the token limit there", g_last_max_tokens == 32);
    free(r);

    /* ── streaming ─────────────────────────────────────────────────────── */

    r = post("/v1/chat/completions",
             "{\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}],\"stream\":true}");
    /* ⚠ ONE EVENT, BUT REAL FRAMING. A client that asked for a stream and got
     * application/json gives up before it reads the answer. */
    ok_("a stream request gets an event stream", has(r, "text/event-stream"));
    ok_("…carrying the answer as a delta", has(r, "\"delta\"") && has(r, "an answer"));
    ok_("…and terminated the way clients look for", has(r, "data: [DONE]"));
    free(r);

    /* ── what a client does wrong ──────────────────────────────────────── */

    r = post("/v1/chat/completions", "{\"messages\":[]}");
    ok_("an empty conversation is a 400, not a crash", status_is(r, 400));
    free(r);

    r = post("/v1/chat/completions", "not json at all");
    ok_("a body that is not JSON is a 400", status_is(r, 400));
    ok_("…in the error shape a client can read", has(r, "\"error\"") && has(r, "\"code\":400"));
    free(r);

    r = request("GET /nope HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ok_("an unknown route is a 404", status_is(r, 404));
    free(r);

    r = request("DELETE /v1/models HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ok_("an unsupported method is a 405", status_is(r, 405));
    free(r);

    /* ⛔ Content-Length IS A CLAIM. Believing a huge one is a malloc of whatever
     * the client says, from a socket every local process in the group can open. */
    r = request("POST /v1/chat/completions HTTP/1.1\r\nHost: localhost\r\n"
                "Content-Length: 999999999\r\n\r\n{}");
    ok_("an oversized Content-Length is refused before it is allocated",
        status_is(r, 413));
    free(r);

    /* A browser-based client sends this before its first real request. */
    r = request("OPTIONS /v1/chat/completions HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ok_("a preflight is answered", status_is(r, 204));
    free(r);

    /* ── while the model is away ───────────────────────────────────────── */

    atomic_store(&st.model_sleeping, 1);
    r = request("GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n");
    ok_("a sleeping model reports 503, not a wrong answer",
        status_is(r, 503) && has(r, "released for suspend"));
    free(r);
    atomic_store(&st.model_sleeping, 0);

    http_server_stop(&st);

    /* The socket goes with it: a stale one left behind is what a client
     * connects to and hangs on. */
    ok_("the socket is removed on stop", access(SYNAPD_HTTP_SOCKET_PATH, F_OK) != 0);

    printf("%d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
