/*
 * panel.c — windowed panels: dragging, and who has the keyboard
 *
 * Every compositor-drawn panel began as modal chrome: it took the pointer, it
 * took the keyboard, it sat in the middle of the screen, and a click anywhere
 * else dismissed it. That is right for a menu — you point at a row, you are
 * done — and wrong for anything you sit and WORK in.
 *
 * velle, on the calculator: "the menus don't need to force focus they can just
 * be a normal window that you can still click other places and drag around."
 *
 * So SYN_PANEL_CLOSE_WINDOW makes a panel behave like one: it has a close
 * button, a header you drag it by, and no claim on anything else. Clicks
 * outside it are not swallowed and not treated as "dismiss" — they go to
 * whatever is under them, exactly as they would if the panel were a floating
 * window. The panel stays open, where you left it.
 *
 * THE FOCUS MODEL IS ONE FLAG, AND THAT IS ON PURPOSE
 *
 * A windowed panel takes the keyboard when it is opened or clicked, and gives
 * it back the moment a click lands anywhere else. That is the whole of it.
 *
 * It has to do at least that much: a calculator you cannot type into is not a
 * calculator, and Super+X followed by "12+30" has to work without a click in
 * between. It must not do more: a panel that kept the keyboard after you
 * clicked into a terminal would be the "forces focus" complaint again, wearing
 * a different hat.
 *
 * Anything a real window manager does beyond this — focus follows mouse, a
 * focus stack, raising on click — is deliberately absent. These are three
 * panels, not clients; the compositor draws them and knows where they are.
 *
 * WHY ONE FILE FOR THREE PANELS
 *
 * The rules are identical and the state is not: three settings, three offsets,
 * three keyboard flags. Written per panel that would be three chances to get
 * "clicking off gives the keyboard back" subtly different, and the difference
 * would only be visible as one panel behaving oddly. Same argument as hit.c,
 * one layer up: the panels ask, they do not decide.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdlib.h>

#include "synui.h"

/* ── Which panel is which ────────────────────────────────────
 *
 * The three switch statements below are the only places that map a syn_pdrag_t
 * onto a config field and a struct member. Everything else in this file, and
 * every call site outside it, works in terms of the enum.
 */

int panel_mode(syn_server_t *s, syn_pdrag_t which)
{
    switch (which) {
    case SYN_PDRAG_CALC:     return s->config.calc_close;
    case SYN_PDRAG_CTLPANEL: return s->config.ctlpanel_close;
    case SYN_PDRAG_TASKMGR:  return s->config.taskmgr_close;
    default:                 return SYN_PANEL_CLOSE_CLICKOFF;
    }
}

syn_panel_win_t *panel_win(syn_server_t *s, syn_pdrag_t which)
{
    switch (which) {
    case SYN_PDRAG_CALC:     return &s->calc.win;
    case SYN_PDRAG_CTLPANEL: return &s->ctlpanel.win;
    case SYN_PDRAG_TASKMGR:  return &s->taskmgr.win;
    default:                 return NULL;
    }
}

syn_hit_t *panel_hit(syn_server_t *s, syn_pdrag_t which)
{
    switch (which) {
    case SYN_PDRAG_CALC:     return &s->calc.hit;
    case SYN_PDRAG_CTLPANEL: return &s->ctlpanel.hit;
    case SYN_PDRAG_TASKMGR:  return &s->taskmgr.hit;
    default:                 return NULL;
    }
}

void panel_render(syn_server_t *s, syn_pdrag_t which)
{
    switch (which) {
    case SYN_PDRAG_CALC:     synui_render_calc(s);     break;
    case SYN_PDRAG_CTLPANEL: synui_render_ctlpanel(s); break;
    case SYN_PDRAG_TASKMGR:  synui_render_taskmgr(s);  break;
    default: break;
    }
}

int panel_is_windowed(syn_server_t *s, syn_pdrag_t which)
{
    return panel_mode(s, which) == SYN_PANEL_CLOSE_WINDOW;
}

/* A close button is drawn in both of the non-clickoff modes: the difference
 * between them is what happens OUTSIDE the panel, not what is drawn on it. */
int panel_has_button(syn_server_t *s, syn_pdrag_t which)
{
    int m = panel_mode(s, which);
    return m == SYN_PANEL_CLOSE_BUTTON || m == SYN_PANEL_CLOSE_WINDOW;
}

/* ── The keyboard ────────────────────────────────────────── */

void panel_take_kbd(syn_server_t *s, syn_pdrag_t which)
{
    /* Exclusive, so two windowed panels open at once cannot both answer for a
     * keystroke — the first one in input.c's chain would win and the other
     * would look dead. */
    s->calc.win.kbd     = (which == SYN_PDRAG_CALC);
    s->ctlpanel.win.kbd = (which == SYN_PDRAG_CTLPANEL);
    s->taskmgr.win.kbd  = (which == SYN_PDRAG_TASKMGR);
}

void panel_drop_kbd(syn_server_t *s)
{
    s->calc.win.kbd = s->ctlpanel.win.kbd = s->taskmgr.win.kbd = 0;
}

/*
 * Does this panel answer for keys right now?
 *
 * Always yes when it is not windowed — the modal panels predate all of this and
 * must keep behaving exactly as they did, which is what makes this setting safe
 * to leave alone.
 */
int panel_wants_keys(syn_server_t *s, syn_pdrag_t which)
{
    if (!panel_is_windowed(s, which)) return 1;
    syn_panel_win_t *w = panel_win(s, which);
    return w && w->kbd;
}

/* ── Dragging ────────────────────────────────────────────── */

void panel_drag_begin(syn_server_t *s, syn_pdrag_t which, double lx, double ly)
{
    syn_panel_win_t *w = panel_win(s, which);
    if (!w) return;

    s->panel_drag.active  = 1;
    s->panel_drag.which   = which;
    s->panel_drag.grab_lx = lx;
    s->panel_drag.grab_ly = ly;
    /* The offset is captured at the press and added to, rather than recomputed
     * from the pointer each frame: the grab point is wherever on the header you
     * took hold of it, and a panel that jumped so its corner met the cursor
     * would be the thing every bad drag implementation does. */
    s->panel_drag.base_dx = w->dx;
    s->panel_drag.base_dy = w->dy;
}

void panel_drag_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->panel_drag.active) return;

    syn_panel_win_t *w = panel_win(s, s->panel_drag.which);
    if (!w) { s->panel_drag.active = 0; return; }

    w->dx = s->panel_drag.base_dx + (int)(lx - s->panel_drag.grab_lx);
    w->dy = s->panel_drag.base_dy + (int)(ly - s->panel_drag.grab_ly);

    /* The clamp lives in the render pass, which is the only place that knows
     * how big the panel is right now — the calculator's height changes as its
     * tape fills. So this just repaints and lets that pull the offset back. */
    panel_render(s, s->panel_drag.which);
}

void panel_drag_end(syn_server_t *s)
{
    s->panel_drag.active = 0;
    s->panel_drag.which  = SYN_PDRAG_NONE;
}

/*
 * Keep the panel reachable.
 *
 * Not "keep it fully on screen" — dragging a panel half off the edge is a
 * perfectly ordinary thing to want, and forbidding it makes the drag feel like
 * it is fighting you. The rule is weaker and is the one that matters: enough of
 * the HEADER must stay on the output that you can take hold of it again, since
 * the header is the only way to drag it back.
 *
 * Applied every render rather than at the end of the drag, so a panel that
 * grows (the tape) or an output that shrinks (a monitor unplugged) cannot leave
 * a panel stranded somewhere the pointer cannot reach.
 */
void panel_clamp(syn_panel_win_t *w, const struct wlr_box *ob,
                 int px, int py, int pw, int ph)
{
    (void)ph;

    /* How much of the panel must remain inside the output. Wide enough to grab,
     * tall enough to be the header rather than a hairline. */
    const int KEEP_X = 120, KEEP_Y = 28;

    int min_dx = ob->x - px - (pw - KEEP_X);
    int max_dx = ob->x + ob->width - px - KEEP_X;
    int min_dy = ob->y - py;                       /* never above the output */
    int max_dy = ob->y + ob->height - py - KEEP_Y;

    if (w->dx < min_dx) w->dx = min_dx;
    if (w->dx > max_dx) w->dx = max_dx;
    if (w->dy < min_dy) w->dy = min_dy;
    if (w->dy > max_dy) w->dy = max_dy;
}
