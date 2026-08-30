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
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
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

/* Arm cycles allowed per login screen. Each is one greetd worker and one PAM
 * stack, so a machine left at the login screen all night must not cycle
 * forever; past this the password prompt stands on its own, which is what a
 * machine with no reader does anyway. At pam_fprintd's timeout=10 this is
 * roughly seven minutes of the finger staying live. */
#define GREETER_MAX_REARMS 40

static void greeter_arm(syn_server_t *s);

/* ── teardown ────────────────────────────────────────────── */

static void greeter_ipc_close(syn_server_t *s)
{
    if (s->greetd.src) { wl_event_source_remove(s->greetd.src); s->greetd.src = NULL; }
    if (s->greetd.sock >= 0) { close(s->greetd.sock); s->greetd.sock = -1; }
    s->greetd.rlen  = 0;
    s->greetd.state = GREETD_IDLE;
    s->greetd.busy  = 0;
    s->greetd.armed          = 0;
    s->greetd.pending_secret = 0;
    s->greetd.submitted      = 0;
    s->greetd.saw_fp_prompt  = 0;
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

    /* ⚠ AND OFFER THE FINGER AGAIN. A rejection ends the conversation, so
     * without this the reader is dead for the rest of the screen's life and
     * "it worked once" becomes the bug report. */
    greeter_arm(s);
}

/* ── response handling ───────────────────────────────────── */

/* Extract the string value of a top-level "key" field into out. Tolerant of
 * whitespace around ':' — greetd is compact JSON, but don't bank on it.
 * Returns 1 if the field was found. */
/* Extract the string value of a top-level "key" field into out. Tolerant of
 * whitespace around ':' — greetd is compact JSON, but don't bank on it.
 * Returns 1 if the field was found.
 *
 * ⛔ EVERY OCCURRENCE IS TRIED, NOT JUST THE FIRST, AND THAT IS THE WHOLE BUG
 * THIS FUNCTION ONCE HAD. greetd's message is
 *
 *     {"type":"auth_message","auth_message_type":"info","auth_message":"Place your finger…"}
 *
 * and a search for "auth_message" matches the VALUE of "type" before it reaches
 * the field of that name — the message type is literally the string
 * auth_message. The old code took that first hit, found a comma where a colon
 * should be, and returned "not found". So the prompt text was ALWAYS empty:
 * pam_fprintd asked for a finger, waited its ten seconds, and the login screen
 * drew nothing, because the words never survived the parse. Measured on the
 * ThinkPad, where the reader works at the lock and looked dead at login:
 *
 *     synui greeter: pam says [info] ""
 *
 * A hit that is not followed by ':' is a VALUE that happens to read like a key.
 * Skip it and keep looking.
 */
static int json_field(const char *json, const char *key, char *out, size_t n)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    size_t patlen = strlen(pat);

    for (const char *p = json; (p = strstr(p, pat)) != NULL; p += patlen) {
        const char *q = p + patlen;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != ':') continue;          /* a value, not the key — keep looking */
        q++;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '"') return 0;
        q++;

        /* ⚠ Backslash escapes are UNDONE here, not copied through. A PAM prompt
         * is arbitrary text from a module; one containing a quote would
         * otherwise end the value early and truncate the message mid-word. */
        size_t o = 0;
        while (*q && *q != '"' && o + 1 < n) {
            if (*q == '\\' && q[1]) {
                q++;
                switch (*q) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = '\t'; break;
                case 'r': out[o++] = '\r'; break;
                case 'b': out[o++] = '\b'; break;
                case 'f': out[o++] = '\f'; break;
                /* \uXXXX is left as-is: greetd does not emit it for these
                 * strings, and half-decoding UTF-16 pairs here would be more
                 * ways to be wrong than it is worth. */
                default:  out[o++] = *q;   break;
                }
                q++;
                continue;
            }
            out[o++] = *q++;
        }
        out[o] = 0;
        return 1;
    }
    return 0;
}

static int msg_is(const char *json, const char *type)
{
    char t[48];
    return json_field(json, "type", t, sizeof(t)) && strcmp(t, type) == 0;
}

/* Send the response to whatever PAM last asked. `text` is NULL for the empty
 * acknowledgement an info/error message wants. */
static void greeter_send_response(syn_server_t *s, const char *text)
{
    char esc[1100];
    json_escape(text ? text : "", esc, sizeof(esc));
    char req[1200];
    snprintf(req, sizeof(req),
             "{\"type\":\"post_auth_message_response\",\"response\":\"%s\"}", esc);
    if (greetd_send(s->greetd.sock, req) < 0) greeter_reject(s);
    explicit_bzero(req, sizeof(req));
    explicit_bzero(esc, sizeof(esc));
}

/*
 * Answer one greetd auth_message.
 *
 * ⛔ A `secret` PROMPT IS NOT ANSWERED UNTIL THE USER HAS SUBMITTED SOMETHING.
 * The old code replied with whatever was in the password buffer the instant the
 * prompt arrived — which was correct only because the exchange never began
 * until Enter had already filled it. Arming at idle breaks that assumption:
 * the buffer is empty, and an empty response is a FAILED PASSWORD ATTEMPT.
 * This machine runs faillock. A greeter that burned an attempt every time it
 * armed the reader would lock the account it exists to log in.
 *
 * ⚠ AND info IS WHERE THE FINGERPRINT PROMPT LIVES. pam_fprintd calls
 * pam_info() with "Place your finger on <device>", which greetd forwards as
 * auth_message_type "info". The old code threw the TEXT away and acked — so
 * the reader was live and nothing on screen ever said so. It goes to
 * nlock.fp_msg, which lock.c already draws under the clock for the session
 * lock's own fingerprint.
 */
static void greeter_answer_auth(syn_server_t *s, const char *json)
{
    char amt[32];
    if (!json_field(json, "auth_message_type", amt, sizeof(amt))) amt[0] = 0;

    char text[256];
    if (!json_field(json, "auth_message", text, sizeof(text))) text[0] = 0;

    /* ⛔ LOGGED, AND PERMANENTLY. This exchange has been the opaque middle of
     * every fingerprint-at-login problem so far: the reader works at the lock
     * and not here, and from outside the process "pam_fprintd never prompted",
     * "it prompted and we dropped the text" and "it prompted and nobody could
     * see it" are indistinguishable. They are three different bugs. One line
     * per message costs nothing on a screen that appears once a boot and turns
     * all three into something readable. */
    wlr_log(WLR_INFO, "synui greeter: pam says [%s] \"%s\"",
            amt[0] ? amt : "?", text);

    if (strcmp(amt, "secret") == 0) {
        s->greetd.pending_secret = 1;
        if (!s->greetd.submitted) {
            wlr_log(WLR_INFO, "synui greeter: holding the password prompt "
                              "(nothing typed yet, fp_prompt_seen=%d)",
                    s->greetd.saw_fp_prompt);
            s->nlock.busy  = 0;
            s->greetd.busy = 0;

            /* ⚠ THE READER HAS JUST GIVEN UP. Reaching the password prompt is
             * how pam_fprintd's timeout is observed from out here, and the
             * reader is cold from this moment on. If nobody has started typing,
             * end this conversation and open a fresh one so it comes back —
             * otherwise the finger works for one timeout after boot and never
             * again while the screen sits there.
             *
             * ⛔ ONLY WITH AN EMPTY FIELD. Cancelling under a half-typed
             * password would throw the characters away mid-word. Somebody who
             * has started typing has chosen the password path. */
            if (s->greetd.saw_fp_prompt && s->nlock.pw_len == 0 &&
                s->greetd.rearms < GREETER_MAX_REARMS) {
                greeter_ipc_close(s);
                greeter_arm(s);
                return;
            }
            lock_render(s);
            return;
        }
        s->greetd.pending_secret = 0;
        greeter_send_response(s, s->greetd.pw);
        return;
    }

    /* info / error / visible. Show what it said — this is the finger prompt —
     * and acknowledge so PAM proceeds. */
    if (text[0]) {
        snprintf(s->nlock.fp_msg, sizeof(s->nlock.fp_msg), "%s", text);
        wlr_log(WLR_INFO, "synui greeter: drawing it under the clock");
        /* "Place your finger on …" is how pam_fprintd announces itself, and it
         * is the only evidence out here that a reader took part at all. It
         * gates the re-arm; see saw_fp_prompt in synui.h. */
        if (strcasestr(text, "finger") || strcasestr(text, "swipe"))
            s->greetd.saw_fp_prompt = 1;
        lock_render(s);
    }
    greeter_send_response(s, NULL);
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

    /* Same pixels AND the same background. The login screen and the lock
     * screen are one screen, so `lock_background` decides both — but the
     * greeter runs as another account and cannot read this user's config or
     * their home, so the answer is published by their session and read back
     * here. BEFORE synui_lock(), which builds the background panes. */
    greeterbg_adopt(s, s->greetd.user);

    /* ⚠ ADOPTING A LAYOUT IS NOT APPLYING ONE. greeterbg_adopt writes
     * xkb_layout/variant/options into the config, and the keyboards were
     * attached back at wlr_backend_start with the keymap the config had THEN —
     * the system default. Without this call the login screen would show a chip
     * saying `no` while the keys stayed `us`, which is worse than showing
     * nothing: it would be a label that lies about the very thing it exists to
     * tell the truth about. */
    input_reload_config(s);

    /*
     * ⚠ AND NEITHER IS ADOPTING A MONITOR LAYOUT. Same shape as the keymap
     * above, and the same reason: every output was created back at
     * wlr_backend_start and placed by wlr_output_layout_add_auto(), because at
     * that point this process had no outputs.conf to read — the greeter's home
     * is `/`. greeterbg_adopt() has just pointed output_persist at the copy
     * the user's session published, so this is where it takes effect.
     *
     * ⛔ BEFORE synui_lock(), which builds one background pane per output and
     * positions the panel from each output's layout box. Applying afterwards
     * would move the screens out from under panes already placed against the
     * old arrangement.
     */
    syn_output_t *o;
    int placed = 0;
    wl_list_for_each(o, &s->outputs, link)
        if (output_persist_apply(s, o)) placed++;
    if (placed)
        wlr_log(WLR_INFO, "synui greeter: placed %d monitor(s) the way the "
                          "desktop has them", placed);

    /* Draw the lock panel — same pixels as the in-session lock screen. */
    synui_lock(s);

    /* A login screen should not fade to black while someone is standing at it,
     * so drop the lock's idle-fade timer and hold full brightness. */
    if (s->nlock.t_fade) {
        wl_event_source_remove(s->nlock.t_fade);
        s->nlock.t_fade = NULL;
    }
    s->nlock.bright = 1.0;

    /* ⛔ AND ARM THE READER. Everything above draws a login screen; this is what
     * makes a fingerprint reach it. Last, because it needs the panel to exist
     * to write a prompt onto. */
    greeter_arm(s);
}

/*
 * Open a greetd session for `user` and start the conversation.
 *
 * ⛔ THIS IS WHAT ARMS THE FINGERPRINT READER. greetd owns PAM and only runs a
 * conversation while a session is being created, so `pam_fprintd` in
 * /etc/pam.d/greetd does nothing at all until this has been sent. That is why
 * adding the PAM line alone changed nothing: it was necessary and never
 * sufficient.
 *
 * Called with no password at greeter start (armed, waiting for a finger) and
 * again after a rejection. Returns 0 if the socket could not be opened, in
 * which case the caller must leave the password path exactly as it was — a
 * greeter that cannot reach greetd must still look and behave like one, or the
 * machine has no way in at all.
 */
static int greeter_open_session(syn_server_t *s)
{
    const char *sockpath = getenv("GREETD_SOCK");
    if (!sockpath || !*sockpath) {
        wlr_log(WLR_ERROR, "synui greeter: GREETD_SOCK unset — not under greetd");
        return 0;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        wlr_log(WLR_ERROR, "synui greeter: socket: %s", strerror(errno));
        return 0;
    }
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof(a.sun_path), "%s", sockpath);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        wlr_log(WLR_ERROR, "synui greeter: connect(%s): %s", sockpath, strerror(errno));
        close(fd);
        return 0;
    }

    s->greetd.sock           = fd;
    s->greetd.rlen           = 0;
    s->greetd.state          = GREETD_WAIT_CREATE;
    s->greetd.pending_secret = 0;

    char esc[80], req[160];
    json_escape(s->greetd.user, esc, sizeof(esc));
    snprintf(req, sizeof(req), "{\"type\":\"create_session\",\"username\":\"%s\"}", esc);
    if (greetd_send(fd, req) < 0) { greeter_reject(s); return 0; }

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->greetd.src = wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE,
                                         greeter_readable, s);
    return 1;
}

/*
 * Arm the reader: a session with no password behind it, so pam_fprintd runs
 * while the login screen is simply sitting there.
 *
 * ⚠ BOUNDED. pam_fprintd gives up after its `timeout=`, PAM then falls through
 * to the password prompt, and this re-arms so the reader comes back — but a
 * screen left alone all night must not cycle greetd workers forever, so the
 * count is capped. After that the password prompt stands on its own, which is
 * exactly the behaviour of every login screen that has no reader at all.
 */
static void greeter_arm(syn_server_t *s)
{
    if (s->greetd.sock >= 0) return;              /* already in a conversation */
    if (s->greetd.user[0] == 0) return;
    if (s->greetd.rearms >= GREETER_MAX_REARMS) return;
    s->greetd.rearms++;

    if (!greeter_open_session(s)) return;

    s->greetd.armed     = 1;
    s->greetd.submitted = 0;
    /* ⛔ NOT busy. `busy` swallows keystrokes and draws "Checking…"; the whole
     * point of arming is that somebody can still walk up and type. */
    s->greetd.busy  = 0;
    s->nlock.busy   = 0;
}

void greeter_submit(syn_server_t *s)
{
    if (s->greetd.busy) return;                    /* an exchange is in flight */
    if (s->greetd.user[0] == 0) {                  /* no account to log in — focus it */
        s->greetd.editing_user = 1;
        lock_render(s);
        return;
    }
    /* ⛔ STILL NOTHING ON AN EMPTY FIELD. An empty submission is a failed
     * password attempt against faillock, and it is never what somebody meant. */
    if (s->nlock.pw_len == 0) return;

    /* Take the typed password off the lock buffer and onto ours for the
     * (possibly multi-round) auth, then wipe the lock's copy immediately. */
    s->greetd.pw_len = s->nlock.pw_len;
    if (s->greetd.pw_len > (int)sizeof(s->greetd.pw) - 1)
        s->greetd.pw_len = sizeof(s->greetd.pw) - 1;
    memcpy(s->greetd.pw, s->nlock.pw, s->greetd.pw_len);
    s->greetd.pw[s->greetd.pw_len] = 0;
    explicit_bzero(s->nlock.pw, sizeof(s->nlock.pw));
    s->nlock.pw_len = 0;

    s->greetd.submitted = 1;
    s->greetd.failed    = 0;
    s->nlock.failed     = 0;

    /*
     * Three ways in, and the first two are the arming case.
     *
     * ⚠ PAM MAY ALREADY BE WAITING FOR THIS. greeter_answer_auth() parks on a
     * `secret` prompt rather than answering it with an empty buffer, so the
     * common path after the reader has timed out is that the conversation is
     * open and one response short. Answer it and nothing else has to happen.
     */
    if (s->greetd.sock >= 0 && s->greetd.pending_secret) {
        s->greetd.pending_secret = 0;
        s->greetd.busy = 1;
        s->nlock.busy  = 1;                        /* "Checking…", keys swallowed */
        greeter_send_response(s, s->greetd.pw);
        lock_render(s);
        return;
    }

    /* Armed and still inside pam_fprintd: the password prompt has not arrived
     * yet. Hold it — answer_auth() sends it the moment PAM asks, which is when
     * the reader gives up. Nothing is lost and no attempt is burned. */
    if (s->greetd.sock >= 0) {
        s->greetd.busy = 1;
        s->nlock.busy  = 1;
        lock_render(s);
        return;
    }

    /* No conversation at all — the arm never happened (no GREETD_SOCK, or the
     * cap was reached). Open one now: this is exactly the pre-arming
     * behaviour, and it is the path that must keep working whatever else here
     * is wrong. */
    if (!greeter_open_session(s)) {
        s->nlock.failed = 1;
        lock_render(s);
        return;
    }
    s->greetd.busy = 1;
    s->nlock.busy  = 1;
    lock_render(s);   /* show "Checking…" */
}
