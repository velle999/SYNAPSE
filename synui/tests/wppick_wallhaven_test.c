/*
 * wppick_wallhaven_test.c — the [w] button in the wallpaper picker.
 *
 * Super+W lists what is on the disk; the picker's [w] button and `w` key are
 * where more of it comes from, and the browser's own `w` comes back here. So
 * one key flips between the two halves of picking a wallpaper, and this pins
 * the half that lives in the compositor.
 *
 * ⛔ WHY THE SPAWN IS COUNTED AND NOT JUST OBSERVED. The Wallhaven ROW shipped
 * dead in 590 for exactly this shape of reason — the apply path read perfectly
 * and Enter reached nothing — and the failure a button has is the mirror image:
 * a `w` that both spawns and falls through to something else spawns twice, and
 * two browsers racing to be the toggle across a process boundary cancel each
 * other out. One press, one spawn, one command.
 *
 * ⛔ AND `w` MUST NOT COMMIT. It is not a pick: a deferred Workshop row that
 * was waiting for Enter has to be abandoned on the way out, exactly as Esc
 * abandons it, or leaving the picker to go and browse would silently start a
 * GPU wallpaper engine.
 *
 * The button's rectangle is written by synui_render_wppick(), which is stubbed
 * here, so the test writes it with render.c's own numbers — the same division
 * of labour panel_pointer_test.c documents at length (render.c writes the
 * geometry, the panel reads it). What that cannot catch is the two drifting
 * apart, so the numbers below name the lines they come from.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

static int failures;
static int checks;

#define CHECK(cond, ...) do {                                    \
        checks++;                                                \
        if (cond) {                                              \
            printf("  ok   \xe2\x80\x94 "); printf(__VA_ARGS__);  \
            printf("\n");                                        \
        } else {                                                 \
            failures++;                                          \
            printf("  FAIL \xe2\x80\x94 "); printf(__VA_ARGS__);  \
            printf("\n");                                        \
        }                                                        \
    } while (0)

/* ── The compositor, stubbed ───────────────────────────────── */

static int  spawns;
static char last_spawn[256];

void synui_spawn(const char *cmd)
{
    spawns++;
    snprintf(last_spawn, sizeof(last_spawn), "%s", cmd ? cmd : "");
}

void synui_render_wppick(syn_server_t *s) { (void)s; }
void ctlpanel_child_closed(syn_server_t *s, const char *a) { (void)s; (void)a; }
void wallpaper_reload(syn_server_t *s) { (void)s; }
void wallpaper_state_save(syn_server_t *s) { (void)s; }
void wallpaper_effective(syn_config_t *c, const char *name,
                         syn_wallpaper_src_t *src, const char **path,
                         syn_wallpaper_mode_t *mode)
{
    (void)c; (void)name;
    if (src)  *src  = SYN_WP_SRC_IMAGE;
    if (path) *path = "";
    if (mode) *mode = SYN_WALLPAPER_FILL;
}
void wallpaper_output_apply(syn_config_t *c, const char *name, const char *tok,
                            int mode)
{
    (void)c; (void)name; (void)tok; (void)mode;
}
void wallpaper_output_clear(syn_config_t *c, const char *name)
{
    (void)c; (void)name;
}
syn_wp_output_t *wallpaper_output_entry(syn_config_t *c, const char *name,
                                        bool create)
{
    (void)c; (void)name; (void)create;
    return NULL;
}
void wlr_output_schedule_frame(struct wlr_output *o) { (void)o; }
struct wl_event_loop *wl_display_get_event_loop(struct wl_display *d)
{
    (void)d; return NULL;
}
struct wl_event_source *wl_event_loop_add_timer(struct wl_event_loop *loop,
                                                int (*fn)(void *), void *data)
{
    (void)loop; (void)fn; (void)data; return NULL;
}
int wl_event_source_timer_update(struct wl_event_source *src, int ms)
{
    (void)src; (void)ms; return 0;
}
void syn_utf8_copy(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "");
}

/* ── Fixture ─────────────────────────────────────────────────
 *
 * render.c's numbers, from synui_render_wppick(): the list is 520 wide with a
 * 300-wide preview pane beside it, rows are 48 tall starting at 58, and the
 * header button is a 24-tall strip at y=14 padded 6px either side of its text.
 */
#define PANEL_X    400
#define PANEL_Y    200
#define LIST_W     520
#define PANEL_W    (LIST_W + 300)
#define ROW_TOP     58
#define ROW_H       48
#define BTN_X      620          /* wherever the right-edge walk lands it */
#define BTN_W       96
#define BTN_Y       14
#define BTN_H       24

static syn_server_t srv;

static void fixture(int rows)
{
    memset(&srv, 0, sizeof(srv));
    /* ⚠ A ZEROED wl_list IS NOT AN EMPTY ONE — its `next` is NULL, and the
     * preview a row click fires walks the output list to schedule a frame. */
    wl_list_init(&srv.outputs);
    srv.wppick.visible = 1;
    srv.wppick.pending_we = -1;
    srv.wppick.selected = 0;
    /* An empty scan: the four built-ins are the whole list, which is what a
     * fresh install looks like anyway. */
    srv.wppick.found_count = rows;
    srv.wppick.we_count = 0;

    hit_set_panel(&srv.wppick.hit, PANEL_X, PANEL_Y, PANEL_W, 400);
    hit_set_rows(&srv.wppick.hit, 12, ROW_TOP, LIST_W - 24, ROW_H, 4);
    hit_set_first(&srv.wppick.hit, 0);
    hit_add_spot(&srv.wppick.hit, BTN_X, BTN_Y, BTN_W, BTN_H);

    spawns = 0;
    last_spawn[0] = '\0';
}

int main(void)
{
    printf("wppick_wallhaven_test\n");

    /* ── The row the button borrows its label from ──────────── */
    int wh = wppick_wallhaven_row();
    CHECK(wh >= 0 && wh < wppick_option_count,
          "the Wallhaven row is in the option table (%d)", wh);
    CHECK(wh >= 0 && strcmp(wppick_options[wh].token, "wallhaven") == 0,
          "and it is the row with the wallhaven token");

    /* ── `w` opens the browser ──────────────────────────────── */
    fixture(0);
    int handled = wppick_key(&srv, XKB_KEY_w, 0);
    CHECK(handled == 1, "w is answered by the picker");
    CHECK(spawns == 1, "…exactly one spawn (got %d)", spawns);
    CHECK(strcmp(last_spawn, "synui-wallhaven toggle") == 0,
          "…and it is the launcher, not quickshell: '%s'", last_spawn);
    CHECK(srv.wppick.visible == 0,
          "…and the picker is down, so the two do not both hold the keyboard");

    /* ── …and abandons a deferred row rather than applying it ── */
    fixture(0);
    srv.wppick.pending_we = 1;              /* a Workshop row awaiting Enter */
    wppick_key(&srv, XKB_KEY_w, 0);
    CHECK(srv.wppick.pending_we == -1,
          "a deferred row is abandoned by w, as it is by Esc");
    CHECK(spawns == 1, "…and w still spawned once (got %d)", spawns);

    /* ── A modified w is not ours ───────────────────────────── */
    fixture(0);
    handled = wppick_key(&srv, XKB_KEY_w, WLR_MODIFIER_LOGO);
    CHECK(handled == 0 && spawns == 0,
          "Super+W falls through to the bind table (toggling the picker)");
    CHECK(srv.wppick.visible == 1, "…and does not close the panel from here");

    /* ── Closed: w belongs to whoever has the keyboard ──────── */
    fixture(0);
    srv.wppick.visible = 0;
    handled = wppick_key(&srv, XKB_KEY_w, 0);
    CHECK(handled == 0 && spawns == 0,
          "with the picker closed, w reaches the focused window");

    /* ── The button answers a SINGLE click ──────────────────── */
    fixture(0);
    int taken = wppick_click(&srv, PANEL_X + BTN_X + 4, PANEL_Y + BTN_Y + 12,
                             BTN_LEFT, 1000);
    CHECK(taken == 1, "a click on [w] is the panel's");
    CHECK(spawns == 1, "…one click, one spawn (got %d)", spawns);
    CHECK(strcmp(last_spawn, "synui-wallhaven toggle") == 0,
          "…the same command the key sends");
    CHECK(srv.wppick.visible == 0, "…and the picker is down");

    /* ── A click on a ROW is still a row ────────────────────── */
    fixture(0);
    taken = wppick_click(&srv, PANEL_X + 40, PANEL_Y + ROW_TOP + ROW_H + 10,
                         BTN_LEFT, 2000);
    CHECK(taken == 1 && spawns == 0,
          "a click in the list selects and spawns nothing");
    CHECK(srv.wppick.visible == 1 && srv.wppick.selected == 1,
          "…it picked row 1 and left the panel up");

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
