/* calevents.c — what is on, in the calendar panel, without stalling the desktop.
 *
 * The panel is compositor-drawn and syn-cal owns the calendars, so this is the
 * seam between them: spawn `syn-cal --rec agenda`, read its records off a pipe
 * through the wl_event_loop, and hand the result to the renderer.
 *
 * ⛔ ASYNCHRONOUS, AND THAT IS NOT A REFINEMENT. A synchronous popen() on a key
 * or draw path blocks the wl_event_loop, which is the one thing no panel is
 * allowed to do — cursor.c and eq.c both carry the same note. Opening the
 * calendar would freeze every window on the machine for as long as syn-cal took
 * to expand a year of recurrence rules, and it would freeze them again on every
 * press of the arrow keys.
 *
 * So the panel draws immediately with whatever it has, the events arrive a few
 * milliseconds later, and the panel is redrawn. A month with no events looks
 * exactly like a month whose events have not landed yet, for those few
 * milliseconds, and that is the right trade: the alternative is a compositor
 * that hitches every time somebody looks at a date.
 *
 * ⚠ AND ONLY ONE FETCH IS EVER IN FLIGHT. Holding down the arrow key steps the
 * month faster than syn-cal can answer; without cancelling the previous read,
 * the answers arrive out of order and the panel settles on whichever month
 * happened to finish last.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "synui.h"

/* The read in flight, if any. One per compositor: the panel is modal and there
 * is exactly one calendar. */
struct cal_fetch {
    syn_server_t *s;
    int fd;
    pid_t pid;
    int year, mon;                 /* which month this answer is FOR */
    struct wl_event_source *ev;
    char *buf;
    size_t len, cap;
};

static struct cal_fetch *in_flight;

static void fetch_close(struct cal_fetch *f)
{
    if (!f) return;
    if (f->ev) wl_event_source_remove(f->ev);
    if (f->fd >= 0) close(f->fd);
    if (f->pid > 0) {
        /* ⚠ REAPED, ALWAYS. syn-cal exits on its own the moment the pipe
         * closes, but a compositor that leaves zombies accumulates one per
         * month the user steps through. */
        int st;
        kill(f->pid, SIGTERM);
        waitpid(f->pid, &st, 0);
    }
    free(f->buf);
    free(f);
    if (in_flight == f) in_flight = NULL;
}

void calevents_cancel(void)
{
    fetch_close(in_flight);
    in_flight = NULL;
}

/* ── parsing ────────────────────────────────────────────────────────────── */

/* Percent-decode in place, up to `cap` bytes out. Every field syn-cal emits is
 * encoded — see its own header for why — and a summary is arbitrary text. */
static void pct_into(char *out, size_t cap, const char *in)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < cap; p++) {
        if (*p == '%' && p[1] && p[2]) {
            char hex[3] = { p[1], p[2], 0 };
            char *end = NULL;
            long v = strtol(hex, &end, 16);
            if (end && *end == '\0') { out[o++] = (char)v; p += 2; continue; }
        }
        out[o++] = *p;
    }
    out[o] = '\0';
}

static void parse_records(syn_server_t *s, const char *text, int year, int mon)
{
    syn_cal_t *cal = &s->cal;
    cal->nev = 0;
    memset(cal->busy, 0, sizeof cal->busy);

    const char *p = text;
    bool first_line = true;

    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);

        /* Row 0 is the header. A body with only a header is an empty month,
         * not a failure. */
        if (first_line) { first_line = false; p = eol ? eol + 1 : NULL; continue; }
        if (linelen == 0) { p = eol ? eol + 1 : NULL; continue; }

        char line[1024];
        size_t n = linelen < sizeof line - 1 ? linelen : sizeof line - 1;
        memcpy(line, p, n);
        line[n] = '\0';

        /* start end all_day recurring account calendar summary location uid */
        char *f[9] = { NULL };
        int nf = 0;
        char *q = line;
        while (nf < 9) {
            f[nf++] = q;
            char *tab = strchr(q, '\t');
            if (!tab) break;
            *tab = '\0';
            q = tab + 1;
        }

        if (nf >= 7 && cal->nev < CAL_EVENTS_MAX) {
            syn_cal_event_t *e = &cal->ev[cal->nev];
            memset(e, 0, sizeof *e);
            e->start = (time_t)strtoll(f[0], NULL, 10);
            e->all_day = f[2] && f[2][0] == '1';
            pct_into(e->summary, sizeof e->summary, f[6] ? f[6] : "");

            /* ⚠ THE LOCAL DAY, NOT THE UTC ONE. An event at 23:30 UTC is
             * tomorrow in Sydney and today in London, and the grid this marks
             * is drawn in the user's own days. */
            struct tm lt;
            localtime_r(&e->start, &lt);
            e->day = (lt.tm_year + 1900 == year && lt.tm_mon == mon)
                   ? lt.tm_mday : 0;
            e->hour = lt.tm_hour;
            e->min  = lt.tm_min;

            if (e->day >= 1 && e->day <= 31) cal->busy[e->day] = 1;
            cal->nev++;
        }

        p = eol ? eol + 1 : NULL;
    }

    cal->loaded_year = year;
    cal->loaded_mon = mon;
    cal->loading = 0;
}

/* ── the read ───────────────────────────────────────────────────────────── */

static int read_cb(int fd, uint32_t mask, void *data)
{
    struct cal_fetch *f = data;

    if (mask & WL_EVENT_READABLE) {
        for (;;) {
            if (f->len + 4096 + 1 > f->cap) {
                size_t want = f->cap ? f->cap * 2 : 8192;
                while (want < f->len + 4096 + 1) want *= 2;
                char *nb = realloc(f->buf, want);
                if (!nb) break;
                f->buf = nb;
                f->cap = want;
            }
            ssize_t r = read(fd, f->buf + f->len, 4096);
            if (r > 0) { f->len += (size_t)r; f->buf[f->len] = '\0'; continue; }
            if (r == 0) { mask |= WL_EVENT_HANGUP; break; }
            break;                      /* EAGAIN: come back when there is more */
        }
    }

    /* ⛔ THE HANGUP ARRIVES WITH THE LAST OF THE DATA, not after it. Treating
     * HANGUP as "stop, discard" loses the final read — the same trap already
     * written down for greeter.c and ipc.c. The read above runs first, and only
     * then is the hangup acted on. */
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        syn_server_t *s = f->s;
        int year = f->year, mon = f->mon;
        char *text = f->buf ? f->buf : (char *)"";

        parse_records(s, text, year, mon);
        fetch_close(f);
        in_flight = NULL;

        /* Redraw with what arrived. Only if the panel is still open and still
         * looking at the month that was asked for — the user may have stepped
         * on, and a stale answer must not repaint the new month's grid. */
        if (s->cal.visible && s->cal.year == year && s->cal.mon == mon)
            synui_render_calendar(s);
        return 0;
    }
    return 0;
}

/* ── the spawn ──────────────────────────────────────────────────────────── */

void calevents_fetch(syn_server_t *s, int year, int mon)
{
    calevents_cancel();

    syn_cal_t *cal = &s->cal;

    /* Already have this month. Stepping back and forth across two months is the
     * common gesture and must not spawn a process each way. */
    if (cal->loaded_year == year && cal->loaded_mon == mon) return;

    cal->loading = 1;
    cal->nev = 0;
    memset(cal->busy, 0, sizeof cal->busy);

    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) { cal->loading = 0; return; }

    char from[32];
    snprintf(from, sizeof from, "--from=%04d-%02d-01", year, mon + 1);

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); cal->loading = 0; return; }

    if (pid == 0) {
        /* ⚠ stderr TO /dev/null, NOT INHERITED. syn-cal warns about an account
         * with no password, and on the compositor's stderr that is a line in
         * the journal every time anybody opens the calendar. */
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 2); if (null > 2) close(null); }
        dup2(fds[1], 1);
        if (fds[0] > 2) close(fds[0]);
        if (fds[1] > 2) close(fds[1]);
        /* 31 days covers any month; the day mapping drops what falls outside. */
        execlp("syn-cal", "syn-cal", "--rec", "agenda", from, "--days=31", (char *)NULL);
        _exit(127);
    }

    close(fds[1]);

    struct cal_fetch *f = calloc(1, sizeof *f);
    if (!f) { close(fds[0]); kill(pid, SIGKILL); waitpid(pid, NULL, 0); cal->loading = 0; return; }
    f->s = s;
    f->fd = fds[0];
    f->pid = pid;
    f->year = year;
    f->mon = mon;

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    f->ev = wl_event_loop_add_fd(loop, fds[0], WL_EVENT_READABLE, read_cb, f);
    if (!f->ev) { close(fds[0]); kill(pid, SIGKILL); waitpid(pid, NULL, 0); free(f); cal->loading = 0; return; }

    in_flight = f;
}

/* Everything on one day of the loaded month, in start order. `out` is filled
 * with pointers into the panel's own array; the caller does not free them. */
int calevents_for_day(const syn_cal_t *cal, int day, const syn_cal_event_t **out, int max)
{
    int n = 0;
    for (int i = 0; i < cal->nev && n < max; i++)
        if (cal->ev[i].day == day) out[n++] = &cal->ev[i];
    return n;
}
