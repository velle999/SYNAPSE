/*
 * greeter.c — synui as the greetd login greeter (synui --greeter).
 *
 * The login screen and the lock screen are the SAME screen: greeter_start()
 * calls synui_lock(), so the clock/date/password panel drawn by lock.c is
 * exactly what greets you at boot. The one difference is what Enter does —
 * lock.c unlocks a running session via PAM; here there is no session yet, so we
 * speak the greetd IPC protocol on $GREETD_SOCK to CREATE one.
 *
 * greetd runs this as the unprivileged `greeter` user and owns everything
 * privileged: PAM, the VT/seat, and spawning the session. We only collect the
 * password and drive create_session -> post_auth_message_response ->
 * start_session, non-blocking, off the wl_event_loop — greetd's PAM fail delay
 * must not stall the compositor, exactly as the lock's child-helper avoids.
 *
 * Wire format: a native-endian uint32 byte-length, then that many bytes of
 * JSON. Messages are small and greetd's output is well-formed, so we hand-roll
 * the encode and match responses by substring rather than link a JSON library.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "synui.h"

/* The session command greetd runs once authentication succeeds. Overridable so
 * the ISO/installer can point it elsewhere without a rebuild; defaults to the
 * same wrapper tuigreet used. */
static const char *greeter_session_cmd(void)
{
    const char *e = getenv("SYNUI_GREETER_SESSION");
    return (e && *e) ? e : "/usr/local/bin/synui-session";
}

/* Which account to log in. Explicit override wins; otherwise the lowest-UID
 * human account (>=1000, the login range), falling back to uid 1000's name. */
static void greeter_pick_user(char *out, size_t n)
{
    const char *e = getenv("SYNUI_GREETER_USER");
    if (e && *e) { snprintf(out, n, "%s", e); return; }

    struct passwd *pw;
    char best[64] = {0};
    uid_t best_uid = 0;
    setpwent();
    while ((pw = getpwent())) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 60000) continue;
        if (!pw->pw_name || !*pw->pw_name) continue;
        if (best[0] == 0 || pw->pw_uid < best_uid) {
            best_uid = pw->pw_uid;
            snprintf(best, sizeof(best), "%s", pw->pw_name);
        }
    }
    endpwent();

    if (best[0]) { snprintf(out, n, "%s", best); return; }
    struct passwd *p1000 = getpwuid(1000);
    snprintf(out, n, "%s", (p1000 && p1000->pw_name) ? p1000->pw_name : "user");
}

/* ── framing ─────────────────────────────────────────────── */

/* Write all len bytes, retrying short writes. Returns 0 on success. */
static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    while (len) {
        ssize_t w = write(fd, p, len);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; len -= (size_t)w;
    }
    return 0;
}

static int greetd_send(int fd, const char *json)
{
    uint32_t len = (uint32_t)strlen(json);
    if (write_all(fd, &len, sizeof(len)) < 0) return -1;
    return write_all(fd, json, len);
}

/* Escape a string into a JSON double-quoted literal (into out, bounded). Only
 * the characters JSON requires; passwords can contain any of them. */
static void json_escape(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (; *in && o + 2 < n; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            if (o + 3 >= n) break;
            out[o++] = '\\'; out[o++] = (char)c;
        } else if (c < 0x20) {
            if (o + 7 >= n) break;
            o += (size_t)snprintf(out + o, n - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = 0;
}

/* ── teardown ────────────────────────────────────────────── */

static void greeter_ipc_close(syn_server_t *s)
{
    if (s->greetd.src) { wl_event_source_remove(s->greetd.src); s->greetd.src = NULL; }
    if (s->greetd.sock >= 0) { close(s->greetd.sock); s->greetd.sock = -1; }
    s->greetd.rlen  = 0;
    s->greetd.state = GREETD_IDLE;
    s->greetd.busy  = 0;
    explicit_bzero(s->greetd.pw, sizeof(s->greetd.pw));
    s->greetd.pw_len = 0;
}

/* A rejection: wipe, drop the connection, and light "Wrong password" on the
 * panel (nlock.failed, since we draw through lock.c). */
static void greeter_reject(syn_server_t *s)
{
    if (s->greetd.sock >= 0) greetd_send(s->greetd.sock, "{\"type\":\"cancel_session\"}");
    greeter_ipc_close(s);
    s->nlock.busy   = 0;
    s->nlock.failed = 1;
    s->nlock.bright = 1.0;
    s->greetd.editing_user = 0;  /* land back on the password — the common retry */
    lock_render(s);            /* show "Wrong password" at once, not on the next tick */
}

/* ── response handling ───────────────────────────────────── */

/* Extract the string value of a top-level "key" field into out. Tolerant of
 * whitespace around ':' — greetd is compact JSON, but don't bank on it.
 * Returns 1 if the field was found. */
static int json_field(const char *json, const char *key, char *out, size_t n)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < n) out[o++] = *p++;
    out[o] = 0;
    return 1;
}

static int msg_is(const char *json, const char *type)
{
    char t[48];
    return json_field(json, "type", t, sizeof(t)) && strcmp(t, type) == 0;
}

/* Answer one greetd auth_message: secret prompts get the password, everything
 * else (info/error/visible) gets an empty response so the exchange proceeds. */
static void greeter_answer_auth(syn_server_t *s, const char *json)
{
    char amt[32];
    int secret = json_field(json, "auth_message_type", amt, sizeof(amt))
                 && strcmp(amt, "secret") == 0;

    char esc[1100];
    json_escape(secret ? s->greetd.pw : "", esc, sizeof(esc));
    char req[1200];
    snprintf(req, sizeof(req),
             "{\"type\":\"post_auth_message_response\",\"response\":\"%s\"}", esc);
    if (greetd_send(s->greetd.sock, req) < 0) greeter_reject(s);
    explicit_bzero(req, sizeof(req));
    explicit_bzero(esc, sizeof(esc));
}

static void greeter_start_session(syn_server_t *s)
{
    char esc[512];
    json_escape(greeter_session_cmd(), esc, sizeof(esc));
    char req[640];
    snprintf(req, sizeof(req),
             "{\"type\":\"start_session\",\"cmd\":[\"%s\"],\"env\":[]}", esc);
    if (greetd_send(s->greetd.sock, req) < 0) { greeter_reject(s); return; }
    s->greetd.state = GREETD_WAIT_START;
}

static void greeter_handle_msg(syn_server_t *s, const char *json)
{
    if (msg_is(json, "auth_message")) {
        greeter_answer_auth(s, json);
        return;
    }
    if (msg_is(json, "success")) {
        if (s->greetd.state == GREETD_WAIT_START) {
            /* greetd will now start the session and SIGTERM us. Nothing left to
             * do but stop reading and let it. Wipe the password regardless. */
            explicit_bzero(s->greetd.pw, sizeof(s->greetd.pw));
            s->greetd.pw_len = 0;
            wlr_log(WLR_INFO, "synui greeter: session starting");
            return;
        }
        greeter_start_session(s);   /* auth ok — ask greetd to start the session */
        return;
    }
    /* error, or anything unexpected — treat as a failed attempt. */
    wlr_log(WLR_INFO, "synui greeter: auth rejected");
    greeter_reject(s);
}

static int greeter_readable(int fd, uint32_t mask, void *data)
{
    syn_server_t *s = data;
    (void)fd;

    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) { greeter_reject(s); return 0; }

    ssize_t n = read(s->greetd.sock, s->greetd.rbuf + s->greetd.rlen,
                     sizeof(s->greetd.rbuf) - s->greetd.rlen);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
        greeter_reject(s);
        return 0;
    }
    s->greetd.rlen += (size_t)n;

    /* Drain every complete uint32-length-prefixed frame in the buffer. */
    for (;;) {
        if (s->greetd.rlen < 4) break;
        uint32_t len;
        memcpy(&len, s->greetd.rbuf, 4);
        if (len >= sizeof(s->greetd.rbuf) - 4) {   /* absurd — desync, bail */
            greeter_reject(s);
            return 0;
        }
        if (s->greetd.rlen < 4 + len) break;       /* frame incomplete */

        char json[sizeof(s->greetd.rbuf)];
        memcpy(json, s->greetd.rbuf + 4, len);
        json[len] = 0;

        size_t consumed = 4 + len;
        memmove(s->greetd.rbuf, s->greetd.rbuf + consumed, s->greetd.rlen - consumed);
        s->greetd.rlen -= consumed;

        greeter_handle_msg(s, json);
        if (s->greetd.sock < 0) break;             /* closed by a reject */
    }
    return 0;
}

/* ── entry points ────────────────────────────────────────── */

void greeter_start(syn_server_t *s)
{
    s->greetd.sock  = -1;
    s->greetd.state = GREETD_IDLE;
    s->greetd.editing_user = 0;   /* start on the password: the username is pre-filled */
    greeter_pick_user(s->greetd.user, sizeof(s->greetd.user));
    wlr_log(WLR_INFO, "synui greeter: login for '%s', session '%s'",
            s->greetd.user, greeter_session_cmd());

    /* Draw the lock panel — same pixels as the in-session lock screen. */
    synui_lock(s);

    /* A login screen should not fade to black while someone is standing at it,
     * so drop the lock's idle-fade timer and hold full brightness. */
    if (s->nlock.t_fade) {
        wl_event_source_remove(s->nlock.t_fade);
        s->nlock.t_fade = NULL;
    }
    s->nlock.bright = 1.0;
}

void greeter_submit(syn_server_t *s)
{
    if (s->greetd.busy) return;                    /* an exchange is in flight */
    if (s->greetd.user[0] == 0) {                  /* no account to log in — focus it */
        s->greetd.editing_user = 1;
        lock_render(s);
        return;
    }
    if (s->nlock.pw_len == 0) return;              /* nothing typed */

    const char *sockpath = getenv("GREETD_SOCK");
    if (!sockpath || !*sockpath) {
        wlr_log(WLR_ERROR, "synui greeter: GREETD_SOCK unset — not under greetd");
        s->nlock.failed = 1;
        lock_render(s);
        return;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { s->nlock.failed = 1; lock_render(s); return; }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sockpath);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        wlr_log(WLR_ERROR, "synui greeter: connect(%s): %s", sockpath, strerror(errno));
        close(fd);
        s->nlock.failed = 1; lock_render(s);
        return;
    }

    /* Take the typed password off the lock buffer and onto ours for the
     * (possibly multi-round) auth, then wipe the lock's copy immediately. */
    s->greetd.pw_len = s->nlock.pw_len;
    if (s->greetd.pw_len > (int)sizeof(s->greetd.pw) - 1)
        s->greetd.pw_len = sizeof(s->greetd.pw) - 1;
    memcpy(s->greetd.pw, s->nlock.pw, s->greetd.pw_len);
    s->greetd.pw[s->greetd.pw_len] = 0;
    explicit_bzero(s->nlock.pw, sizeof(s->nlock.pw));
    s->nlock.pw_len = 0;

    s->greetd.sock  = fd;
    s->greetd.rlen  = 0;
    s->greetd.state = GREETD_WAIT_CREATE;
    s->greetd.busy  = 1;
    s->nlock.busy   = 1;      /* draws "Checking…", swallows keys, via lock.c */
    s->nlock.failed = 0;

    char esc[80], req[160];
    json_escape(s->greetd.user, esc, sizeof(esc));
    snprintf(req, sizeof(req), "{\"type\":\"create_session\",\"username\":\"%s\"}", esc);
    if (greetd_send(fd, req) < 0) { greeter_reject(s); return; }

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->greetd.src = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE,
                                         greeter_readable, s);
    lock_render(s);   /* show "Checking…" */
}
