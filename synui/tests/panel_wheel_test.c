/*
 * panel_wheel_test.c — the wheel over a WINDOWED panel is the panel's, and the
 * wheel anywhere else is not.
 *
 * A windowed panel (SYN_PANEL_CLOSE_WINDOW) is not modal: clicks off it go to
 * whatever is under them, which is the whole point of the mode. input.c
 * implements that by leaving windowed panels out of `panel_pointer_active()` —
 * and that took the wheel away with everything else, so the control panel's
 * rows, the calculator's tape and the task manager's process list did not
 * scroll AT ALL in window mode. The event went past them to whatever client was
 * under the pointer, which usually scrolled instead: the panel looked frozen and
 * the window behind it moved.
 *
 * The wheel now falls through to each panel's `_scroll()` when nothing modal is
 * open, so the rule that keeps that honest lives in the panels:
 *
 *   modal    — the wheel is the panel's from ANYWHERE on the desktop. That is
 *              the old contract and panel_pointer_test.c pins it on power.c: a
 *              modal panel must not let the window underneath scroll.
 *   windowed — the wheel is the panel's only where the panel IS. Off it the
 *              handler declines, and input.c hands the event to the client.
 *              Without this half a windowed panel would swallow every client's
 *              scroll for as long as it sat open in a corner, which is exactly
 *              the "forces focus" complaint the mode was built to answer.
 *
 * calc.c is the panel under test: it is the cheapest of the three that HAS a
 * mode (panel.c's three switches are the whole list), and the guard is the same
 * three lines in all of them. Driven by calling the handler with coordinates,
 * for the reason panel_pointer_test.c gives at length — nothing can synthesise a
 * pointer into a headless synui, and uinput would be picked up by the LIVE
 * session.
 *
 * WHAT THIS TEST CANNOT SEE, so that nobody reads it as covering more than it
 * does: `panel_pointer_active()` and `panel_pointer_scroll()` are static in
 * input.c and need a seat, a cursor and a real wlr_pointer_axis_event. The half
 * of the fix that OFFERS the wheel to a windowed panel is therefore not
 * exercised here — only the rule the panels enforce once it is offered. The
 * off-panel assertions are the new ones; the on-panel one held before the fix
 * too, because the handler used to answer 1 from everywhere.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* ── The compositor, stubbed ─────────────────────────────────
 * calc.c, panel.c and hit.c are linked alone; nothing here is exercised. */

void synui_render_calc(syn_server_t *s)     { (void)s; }
void synui_render_ctlpanel(syn_server_t *s) { (void)s; }
void synui_render_taskmgr(syn_server_t *s)  { (void)s; }
void synui_spawn(const char *cmd)           { (void)cmd; }
void synui_child_reset_signals(void)        { }
void syn_config_ensure_dir(void)            { }
bool syn_config_path(char *buf, size_t n, const char *leaf)
{ (void)buf; (void)n; (void)leaf; return false; }
void ctlpanel_child_closed(syn_server_t *s, const char *a) { (void)s; (void)a; }
const char *clipboard_current_text(syn_server_t *s) { (void)s; return NULL; }

/* The expression engine is its own file and the wheel never reaches it: this
 * test scrolls a tape, it does not do arithmetic. */
bool calc_eval(const char *expr, double ans, double *out, const char **err)
{ (void)expr; (void)ans; (void)out; (void)err; return false; }
void calc_format(double v, char *buf, size_t n)
{ (void)v; if (n) buf[0] = '\0'; }

/* ── The panel, placed ───────────────────────────────────────
 *
 * A 300x400 panel at (500,300), so "off it" can be a point on either side
 * rather than only the origin corner — an off-by-one in the box test that
 * happened to pass at (0,0) would still be caught.
 */
#define PX 500
#define PY 300
#define PW 300
#define PH 400

static const double ON_X  = PX + PW / 2.0, ON_Y  = PY + PH / 2.0;
static const double OFF_X = 100,         OFF_Y = 100;

static void setup(syn_server_t *s, int mode)
{
    memset(s, 0, sizeof(*s));
    s->config.calc_close = mode;
    s->calc.visible = 1;
    /* Enough tape to have somewhere to scroll to: the handler clamps at
     * hist_count - CALC_TAPE_ROWS, so a short history would make every
     * assertion below pass for the wrong reason. */
    s->calc.hist_count = CALC_TAPE_ROWS + 10;
    s->calc.scroll = 0;
    hit_set_panel(&s->calc.hit, PX, PY, PW, PH);
}

int main(void)
{
    syn_server_t s;

    /* ── Modal: the wheel is the panel's from anywhere ───────────────────── */
    setup(&s, SYN_PANEL_CLOSE_CLICKOFF);
    CHECK(calc_scroll(&s, OFF_X, OFF_Y, 1) == 1,
          "a modal panel must take the wheel from off it — the window "
          "underneath must not scroll while a modal panel is up");
    CHECK(s.calc.scroll == 1,
          "modal, off-panel: expected the tape to scroll, got %d", s.calc.scroll);

    setup(&s, SYN_PANEL_CLOSE_CLICKOFF);
    CHECK(calc_scroll(&s, ON_X, ON_Y, 1) == 1 && s.calc.scroll == 1,
          "modal, on-panel: the wheel scrolls the tape");

    /* ── Windowed: the wheel is the panel's only where the panel is ──────── */
    setup(&s, SYN_PANEL_CLOSE_WINDOW);
    CHECK(calc_scroll(&s, ON_X, ON_Y, 1) == 1,
          "a windowed panel must take the wheel over its own rows");
    CHECK(s.calc.scroll == 1,
          "windowed, on-panel: expected the tape to scroll, got %d",
          s.calc.scroll);

    setup(&s, SYN_PANEL_CLOSE_WINDOW);
    CHECK(calc_scroll(&s, OFF_X, OFF_Y, 1) == 0,
          "a windowed panel must DECLINE the wheel off it — taking it would "
          "swallow every client's scroll while the panel sat open");
    CHECK(s.calc.scroll == 0,
          "windowed, off-panel: the tape must not have moved, got %d",
          s.calc.scroll);

    /* Each edge is exclusive on the far side (hit_in_panel is a half-open
     * box), so the first pixel outside is not the panel's. */
    setup(&s, SYN_PANEL_CLOSE_WINDOW);
    CHECK(calc_scroll(&s, PX + PW, PY, 1) == 0,
          "the pixel past the right edge is off the panel");
    setup(&s, SYN_PANEL_CLOSE_WINDOW);
    CHECK(calc_scroll(&s, PX, PY, 1) == 1,
          "the top-left corner IS the panel");

    /* ── Shut: nothing takes anything ────────────────────────────────────── */
    setup(&s, SYN_PANEL_CLOSE_WINDOW);
    s.calc.visible = 0;
    CHECK(calc_scroll(&s, ON_X, ON_Y, 1) == 0,
          "a closed panel answers 0 so the pointer chain falls through");
    setup(&s, SYN_PANEL_CLOSE_CLICKOFF);
    s.calc.visible = 0;
    CHECK(calc_scroll(&s, ON_X, ON_Y, 1) == 0,
          "a closed modal panel answers 0 too");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("panel_wheel_test: OK\n");
    return 0;
}
