/*
 * panel_pointer_test.c — the panel pointer contract, on a real panel.
 *
 * synui's settings panels were keyboard-only and now take the mouse. That work
 * cannot be tested end-to-end the way the keyboard path can: the smoke test
 * drives Super+C into a headless compositor over virtual-keyboard-v1, and there
 * is no equivalent for the pointer — synui advertises no virtual-pointer
 * protocol, the headless backend has no input devices, and the only other way
 * to synthesise a click is uinput, which the LIVE session's synui would pick up
 * and act on. So the contract is pinned here instead, by calling the handlers
 * with coordinates, exactly as input.c's pointer chain does.
 *
 * power.c is the panel under test because it is the cheapest one to link (see
 * lid_test.c, whose stubs these are) and because it is the representative case:
 * a plain column of rows whose Enter is a second spelling of Esc, so a left
 * click steps the value on and a right click steps it back.
 *
 * What is being pinned, from the contract in synui.h:
 *
 *   - hover selects the row under the pointer, and nothing else
 *   - a left click on a row is that row's Right key
 *   - a right click on a row is its Left key
 *   - a click ANYWHERE off the panel closes it (this is the whole point)
 *   - the wheel moves the selection and stops at the ends
 *   - every one of them answers 0 when the panel is shut, so the pointer chain
 *     falls through to the desktop
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
 * The same set lid_test.c uses, for the same reason: power.c is linked alone. */

static int rendered;

void synui_spawn(const char *cmd)         { (void)cmd; }
void synui_lock(syn_server_t *s)          { (void)s; }
void synui_render_power(syn_server_t *s)  { (void)s; rendered++; }
void logind_lid_update(syn_server_t *s)   { (void)s; }
void ctlpanel_child_closed(syn_server_t *s, const char *action)
{
    (void)s; (void)action;
}

bool logind_holds_lid(void) { return false; }

bool logind_lid_handler(bool docked, bool on_ac, char *buf, size_t n)
{
    (void)docked; (void)on_ac;
    snprintf(buf, n, "suspend");
    return true;
}

static char scratch[128];

bool syn_config_path(char *buf, size_t n, const char *name)
{
    snprintf(buf, n, "%s/%s", scratch, name);
    return true;
}
void syn_config_ensure_dir(void) { }

bool wlr_output_commit_state(struct wlr_output *output,
                             const struct wlr_output_state *state)
{
    (void)output; (void)state;
    return true;
}
void wlr_output_schedule_frame(struct wlr_output *output) { (void)output; }

/* ── Fixture ─────────────────────────────────────────────────
 *
 * The panel's geometry is normally written by synui_render_power(), which is
 * stubbed out here — so the test writes it itself, with the numbers render.c
 * uses. That is not a shortcut around the thing being tested: it is the
 * contract's own division of labour (render.c writes, the panel reads), and
 * writing it by hand is what lets the test place the panel somewhere awkward. */

#define PANEL_X   700
#define PANEL_Y   300
#define PANEL_W   520
#define PANEL_H   400
#define ROW_H      30
#define ROW_TOP    66      /* text baseline of row 0, panel-local */

static syn_server_t *srv;

/* Layout-space y of a point inside row i. */
static double row_y(int i)
{
    return PANEL_Y + ROW_TOP - 16 + i * ROW_H + ROW_H / 2.0;
}
static double row_x(void) { return PANEL_X + PANEL_W / 2.0; }

static void panel_open(void)
{
    srv->power.visible  = 1;
    srv->power.selected = 0;
    srv->power.dirty    = 0;
    srv->power.status[0] = '\0';

    hit_set_panel(&srv->power.hit, PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    hit_set_rows(&srv->power.hit, 12, ROW_TOP - 16, PANEL_W - 24, ROW_H,
                 POWER_ROW_COUNT);
}

/* ── Tests ───────────────────────────────────────────────── */

/* A shut panel must take nothing at all. This is the answer input.c's chain
 * relies on to move to the next panel and finally to the desktop; a handler
 * that claimed events while hidden would make the desktop unclickable. */
static void test_closed_takes_nothing(void)
{
    srv->power.visible = 0;
    hit_clear(&srv->power.hit);

    CHECK(power_motion(srv, row_x(), row_y(2)) == 0,
          "a hidden panel claimed a motion");
    CHECK(power_click(srv, row_x(), row_y(2), BTN_LEFT, 0) == 0,
          "a hidden panel claimed a click");
    CHECK(power_scroll(srv, row_x(), row_y(2), 1.0) == 0,
          "a hidden panel claimed a scroll");
}

/* Hover IS the cursor: the row under the pointer becomes the selected row, and
 * nothing fires. */
static void test_hover_selects(void)
{
    panel_open();

    CHECK(power_motion(srv, row_x(), row_y(3)) == 1, "an open panel refused a motion");
    CHECK(srv->power.selected == 3, "hover did not move the selection to row 3");
    CHECK(srv->power.dirty == 0, "hover changed a setting");

    CHECK(power_motion(srv, row_x(), row_y(6)) == 1, "an open panel refused a motion");
    CHECK(srv->power.selected == 6, "hover did not move the selection to row 6");
    CHECK(srv->power.dirty == 0, "hover changed a setting");

    /* Over the panel's chrome the selection stays put — the header and footer
     * are not "row 0" and "the last row". */
    srv->power.selected = 4;
    CHECK(power_motion(srv, row_x(), PANEL_Y + 10) == 1,
          "the panel refused a motion over its own header");
    CHECK(srv->power.selected == 4, "hovering the header moved the selection");
}

/* A left click is Right, a right click is Left. The lid rows are the clean
 * assertion: they step a five-long list of actions and they WRAP, so the value
 * after one of each is the value before. */
static void test_click_adjusts(void)
{
    panel_open();

    /* POWER_ROW_LID: on-battery lid action. */
    const double y = row_y(POWER_ROW_LID);
    srv->config.lid_close_action = SYN_LID_SUSPEND;

    CHECK(power_click(srv, row_x(), y, BTN_LEFT, 0) == 1, "left click not taken");
    CHECK(srv->power.selected == POWER_ROW_LID,
          "the click did not select the row it landed on");
    int forward = srv->config.lid_close_action;
    CHECK(forward != SYN_LID_SUSPEND, "a left click did not step the value");
    CHECK(srv->power.dirty == 1, "a left click did not mark the panel dirty");

    CHECK(power_click(srv, row_x(), y, BTN_RIGHT, 0) == 1, "right click not taken");
    CHECK(srv->config.lid_close_action == SYN_LID_SUSPEND,
          "a right click did not step the value back");

    /* A click on the chrome is swallowed — taken, so it cannot reach the window
     * under the panel, but it changes nothing. */
    srv->power.dirty = 0;
    int before = srv->config.lid_close_action;
    CHECK(power_click(srv, row_x(), PANEL_Y + 10, BTN_LEFT, 0) == 1,
          "a click on the panel's header was not swallowed");
    CHECK(srv->config.lid_close_action == before, "a click on the header changed a value");
    CHECK(srv->power.dirty == 0, "a click on the header marked the panel dirty");
    CHECK(srv->power.visible, "a click on the header closed the panel");
}

/* The one this whole change exists for: a click anywhere off the panel closes
 * it. Checked on all four sides, and with every button, because "click off to
 * close" that only works below the panel is not click off to close. */
static void test_click_off_closes(void)
{
    const struct { const char *where; double x, y; } outside[] = {
        { "above the panel",       PANEL_X + 100,      PANEL_Y - 1          },
        { "below the panel",       PANEL_X + 100,      PANEL_Y + PANEL_H    },
        { "left of the panel",     PANEL_X - 1,        PANEL_Y + 100        },
        { "right of the panel",    PANEL_X + PANEL_W,  PANEL_Y + 100        },
        { "the far corner",        0,                  0                    },
    };
    const uint32_t buttons[] = { BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };

    for (size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); i++) {
        for (size_t b = 0; b < sizeof(buttons) / sizeof(buttons[0]); b++) {
            panel_open();
            CHECK(power_click(srv, outside[i].x, outside[i].y,
                              buttons[b], 0) == 1,
                  "a click %s was not taken", outside[i].where);
            CHECK(!srv->power.visible,
                  "a click %s did not close the panel (button %u)",
                  outside[i].where, buttons[b]);
        }
    }
}

/* The wheel moves the selection and stops at the ends, which is what Up/Down
 * do in this panel — the contract says the two devices must agree. */
static void test_scroll_moves_selection(void)
{
    panel_open();
    srv->power.selected = 3;

    CHECK(power_scroll(srv, row_x(), row_y(3), 1.0) == 1, "scroll not taken");
    CHECK(srv->power.selected == 4, "scrolling down did not advance the selection");

    CHECK(power_scroll(srv, row_x(), row_y(4), -1.0) == 1, "scroll not taken");
    CHECK(srv->power.selected == 3, "scrolling up did not step the selection back");

    /* Ends. Stop, do not wrap: the keys do not wrap either. */
    srv->power.selected = 0;
    power_scroll(srv, row_x(), row_y(0), -1.0);
    CHECK(srv->power.selected == 0, "scrolling up off the top wrapped or ran away");

    srv->power.selected = POWER_ROW_COUNT - 1;
    power_scroll(srv, row_x(), row_y(POWER_ROW_COUNT - 1), 1.0);
    CHECK(srv->power.selected == POWER_ROW_COUNT - 1,
          "scrolling down off the bottom wrapped or ran away");

    /* Scrolling changes no setting. It is navigation, not adjustment — the
     * difference between "I am looking at this row" and "I have changed it". */
    CHECK(srv->power.dirty == 0, "scrolling marked the panel dirty");
}

/* A press with no motion before it — a tap, a tablet, a warped cursor — must
 * still act on the row it landed on rather than on wherever the selection
 * happened to be. Every _click in the tree calls its own _motion first for
 * this; the test exists so that a panel that stops doing it is caught. */
static void test_click_without_prior_motion(void)
{
    panel_open();
    srv->power.selected = 0;          /* the cursor is nowhere near row 6 */

    power_click(srv, row_x(), row_y(POWER_ROW_LID_DOCKED), BTN_LEFT, 0);
    CHECK(srv->power.selected == POWER_ROW_LID_DOCKED,
          "a click with no motion before it acted on the old selection");
}

int main(void)
{
    snprintf(scratch, sizeof(scratch), "%s", "/tmp");

    srv = calloc(1, sizeof(*srv));
    if (!srv) { fprintf(stderr, "out of memory\n"); return 1; }

    test_closed_takes_nothing();
    test_hover_selects();
    test_click_adjusts();
    test_click_off_closes();
    test_scroll_moves_selection();
    test_click_without_prior_motion();

    free(srv);

    if (failures) {
        fprintf(stderr, "panel_pointer_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("panel_pointer_test: ok\n");
    return 0;
}
