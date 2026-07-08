/*
 * wppick.c — wallpaper selector panel
 *
 * A compositor-drawn modal picker (Super+W, or "wallpaper" bind) for
 * switching between the built-in wallpapers without editing synuirc:
 *
 *   Up/Down (j/k)     move the highlight (applies live for instant preview)
 *   Enter / Esc / q   close
 *
 * Selecting an entry applies it immediately (so you see the change while the
 * panel is still open) and persists it to ~/.config/synui/wallpaper.state, so
 * the choice survives a restart. The persisted choice overrides the synuirc
 * `wallpaper` line on the next load — delete the state file to hand control
 * back to synuirc.
 *
 * The panel itself follows dispcfg.c's modal pattern: state in the server
 * struct, a wlr_scene tree drawn by synui_render_wppick() (render.c), and a
 * key handler that swallows input while open.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <string.h>

#include <wlr/types/wlr_output.h>

#include "synui.h"

/* Built-in wallpapers offered by the picker. Order is the on-screen order. */
const struct wppick_option wppick_options[] = {
    { "Synapse", "Default image wallpaper",       "default" },
    { "Matrix",  "Animated kanji rain (GPU)",     "matrix"  },
    { "None",    "Solid background color",        "none"    },
};
const int wppick_option_count =
    (int)(sizeof(wppick_options) / sizeof(wppick_options[0]));

/* Which option currently matches the live config, so the panel opens with the
 * active wallpaper highlighted. */
static int current_index(syn_server_t *s)
{
    if (s->config.wallpaper_src == SYN_WP_SRC_MATRIX)
        return 1;   /* "matrix" */
    if (s->config.wallpaper[0] == '\0')
        return 2;   /* "none" */
    return 0;       /* any image path shows as "Synapse" */
}

/* Apply an option token to the live config and repaint. Mirrors the synuirc
 * `wallpaper` key semantics for the built-in keywords. */
static void wppick_apply(syn_server_t *s, int idx)
{
    if (idx < 0 || idx >= wppick_option_count) return;
    const char *tok = wppick_options[idx].token;

    if (strcmp(tok, "matrix") == 0) {
        s->config.wallpaper_src = SYN_WP_SRC_MATRIX;
    } else if (strcmp(tok, "default") == 0) {
        s->config.wallpaper_src = SYN_WP_SRC_IMAGE;
        strncpy(s->config.wallpaper, SYNUI_DATADIR "/wallpaper.png",
                sizeof(s->config.wallpaper) - 1);
        s->config.wallpaper[sizeof(s->config.wallpaper) - 1] = '\0';
    } else { /* "none" */
        s->config.wallpaper_src = SYN_WP_SRC_IMAGE;
        s->config.wallpaper[0] = '\0';
    }

    /* Repaint the static backend (decodes/clears wallpaper_buf). The matrix
     * backend picks up / tears down on the next frame via matrix_active(). */
    wallpaper_reload(s);

    /* Kick a frame on every output so the change is visible at once: the
     * matrix path renders its first frame (and self-sustains), and a switch
     * away from matrix runs matrix_output_frame() once to drop its buffer. */
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link)
        wlr_output_schedule_frame(o->wlr_output);

    wallpaper_state_save(s);
}

void wppick_show(syn_server_t *s)
{
    s->wppick.visible = 1;
    s->wppick.selected = current_index(s);
    synui_render_wppick(s);
}

void wppick_hide(syn_server_t *s)
{
    s->wppick.visible = 0;
    synui_render_wppick(s);
}

void wppick_toggle(syn_server_t *s)
{
    if (s->wppick.visible) wppick_hide(s);
    else                   wppick_show(s);
}

int wppick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->wppick.visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        wppick_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->wppick.selected > 0) {
            s->wppick.selected--;
            wppick_apply(s, s->wppick.selected);   /* live preview */
            synui_render_wppick(s);
        }
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->wppick.selected < wppick_option_count - 1) {
            s->wppick.selected++;
            wppick_apply(s, s->wppick.selected);   /* live preview */
            synui_render_wppick(s);
        }
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
