/*
 * theme.c — the theme manager (Super+Shift+A) and the presets behind it.
 *
 * synui grew every colour as a synuirc key (border_color_*, titlebar_color_*).
 * That is the right primitive, but nobody hand-writes eight hex triples to get a
 * coherent look. A *theme* is a named bundle of them plus a default translucency
 * and an app colour-scheme, so one keypress reskins the whole desktop.
 *
 * What a theme owns, and what it does not:
 *   - Window chrome (border + titlebar colours) and the active/inactive opacity
 *     levels: these it writes straight into syn_config_t. It does NOT flip the
 *     `transparency` master switch — that stays the user's call (control panel /
 *     synuirc), the theme only says how translucent things get *if* it is on.
 *   - The look of GTK apps, Dolphin (Qt/KDE) and Firefox: synui cannot recolour
 *     someone else's toolkit from in here, so it shells out to `synui-apply-theme`
 *     (a fire-and-forget spawn, never blocking the event loop) which drives
 *     kwriteconfig/gsettings/the GTK ini files. Missing tools = a no-op, so a box
 *     without KDE simply gets the GTK half. Firefox *transparency* needs nothing
 *     from that script — the compositor's opacity applies to it like any window.
 *
 * The presets are data, not code (theme_presets[]). Adding one is a row plus a
 * name — the panel, the config parse and the persistence all read the table.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* Short tokens: what synuirc `theme =` and theme.state store. */
const char *const syn_theme_names[SYN_THEME_COUNT] = {
    [SYN_THEME_SYNAPSE] = "synapse",
    [SYN_THEME_DARK]    = "dark",
    [SYN_THEME_WINXP]   = "winxp",
    [SYN_THEME_WIN95]   = "win95",
};

/* What the panel shows a human. */
const char *theme_name(syn_theme_t t)
{
    switch (t) {
    case SYN_THEME_SYNAPSE: return "SYNAPSE (neon)";
    case SYN_THEME_DARK:    return "Dark";
    case SYN_THEME_WINXP:   return "Windows XP";
    case SYN_THEME_WIN95:   return "Windows 95";
    default:                return "?";
    }
}

/* ── Presets ─────────────────────────────────────────────── */
/* Colours are RGBA 0..1. `scheme` is what synui-apply-theme is told: "dark" or
 * "light" picks the toolkit palette; `accent_*` (0..255) is the selection colour
 * it hands KDE/GTK so Dolphin's highlight matches the desktop's focus border. */
typedef struct {
    float border_norm[4], border_focus[4], border_ai[4], border_warn[4];
    float tb_norm[4], tb_focus[4], tb_text[4], tb_text_focus[4];
    float active_opacity, inactive_opacity;
    /* panel_accent: the colour synui's OWN panels (menu, control panel, every
     * overlay) draw with — headers, selections, rules. Tuned to read on the
     * dark panel chrome, so it is NOT the window border colour: Win95's navy
     * would vanish on a dark panel, so its accent is a legible periwinkle. */
    float panel_accent[4];
    const char *scheme;              /* "dark" | "light" */
    int   accent_r, accent_g, accent_b;
} syn_theme_preset_t;

static const syn_theme_preset_t theme_presets[SYN_THEME_COUNT] = {
    /* SYNAPSE — byte-identical to the historical defaults (COLOR_BORDER_* etc.),
     * so picking it is a true "back to stock". Inactive windows go faintly glassy
     * when transparency is on; that neon-over-wallpaper look is the house style. */
    [SYN_THEME_SYNAPSE] = {
        .border_norm  = { 0.16f, 0.16f, 0.25f, 1.0f },
        .border_focus = { 1.00f, 0.16f, 0.43f, 1.0f },
        .border_ai    = { 0.02f, 0.85f, 0.91f, 1.0f },
        .border_warn  = { 1.00f, 0.21f, 0.14f, 1.0f },
        .tb_norm       = { 0.07f, 0.07f, 0.11f, 1.0f },
        .tb_focus      = { 0.12f, 0.12f, 0.19f, 1.0f },
        .tb_text       = { 0.45f, 0.45f, 0.55f, 1.0f },
        .tb_text_focus = { 0.90f, 0.90f, 0.95f, 1.0f },
        .active_opacity = 1.0f, .inactive_opacity = 0.92f,
        .panel_accent  = { 0.00f, 0.85f, 0.75f, 1.0f },  /* house neon cyan */
        .scheme = "dark", .accent_r = 255, .accent_g = 41, .accent_b = 109,
    },
    /* DARK — the "just a tasteful dark mode": flat greys, one restrained blue
     * accent, no neon. This is the theme whose whole point is the app-side dark:
     * Dolphin and GTK and Firefox all go dark with it. */
    [SYN_THEME_DARK] = {
        .border_norm  = { 0.18f, 0.18f, 0.19f, 1.0f },  /* #2e2e30 */
        .border_focus = { 0.24f, 0.49f, 1.00f, 1.0f },  /* #3d7dff */
        .border_ai    = { 0.30f, 0.69f, 0.97f, 1.0f },  /* #4db0f7 */
        .border_warn  = { 0.97f, 0.26f, 0.36f, 1.0f },  /* #f7435c */
        .tb_norm       = { 0.10f, 0.10f, 0.11f, 1.0f },  /* #1a1a1c */
        .tb_focus      = { 0.15f, 0.15f, 0.17f, 1.0f },  /* #26262b */
        .tb_text       = { 0.50f, 0.50f, 0.52f, 1.0f },
        .tb_text_focus = { 0.90f, 0.90f, 0.92f, 1.0f },
        .active_opacity = 1.0f, .inactive_opacity = 0.94f,
        .panel_accent  = { 0.24f, 0.49f, 1.00f, 1.0f },  /* restrained blue */
        .scheme = "dark", .accent_r = 61, .accent_g = 125, .accent_b = 255,
    },
    /* WINDOWS XP (Luna, "Blue") — a light theme with that blue title chrome.
     * Scene rects can't gradient, so the titlebar is Luna's mid blue flat; white
     * caption text sells it more than the gradient does. Apps go light. */
    [SYN_THEME_WINXP] = {
        .border_norm  = { 0.50f, 0.62f, 0.84f, 1.0f },  /* #7f9ed6 inactive frame */
        .border_focus = { 0.04f, 0.37f, 0.84f, 1.0f },  /* #0a5fd6 Luna blue      */
        .border_ai    = { 0.16f, 0.50f, 1.00f, 1.0f },  /* #2a7fff */
        .border_warn  = { 0.84f, 0.31f, 0.16f, 1.0f },  /* #d64f2a */
        .tb_norm       = { 0.50f, 0.62f, 0.84f, 1.0f },  /* inactive title */
        .tb_focus      = { 0.04f, 0.37f, 0.84f, 1.0f },  /* active title   */
        .tb_text       = { 0.88f, 0.92f, 1.00f, 1.0f },
        .tb_text_focus = { 1.00f, 1.00f, 1.00f, 1.0f },
        .active_opacity = 1.0f, .inactive_opacity = 1.0f,   /* XP was never glassy */
        .panel_accent  = { 0.16f, 0.55f, 1.00f, 1.0f },  /* Luna blue, brightened */
        .scheme = "light", .accent_r = 10, .accent_g = 95, .accent_b = 214,
    },
    /* WINDOWS 95 — navy active title, grey inactive, silver frame, on a light
     * (grey) palette. The bevels are gone (flat rects), the colours are the tell. */
    [SYN_THEME_WIN95] = {
        .border_norm  = { 0.50f, 0.50f, 0.50f, 1.0f },  /* #808080 */
        .border_focus = { 0.00f, 0.00f, 0.50f, 1.0f },  /* #000080 navy */
        .border_ai    = { 0.00f, 0.00f, 0.50f, 1.0f },
        .border_warn  = { 0.50f, 0.00f, 0.00f, 1.0f },  /* #800000 */
        .tb_norm       = { 0.50f, 0.50f, 0.50f, 1.0f },  /* grey inactive title */
        .tb_focus      = { 0.00f, 0.00f, 0.50f, 1.0f },  /* navy active title   */
        .tb_text       = { 0.83f, 0.82f, 0.78f, 1.0f },  /* #d4d0c8 */
        .tb_text_focus = { 1.00f, 1.00f, 1.00f, 1.0f },
        .active_opacity = 1.0f, .inactive_opacity = 1.0f,
        .panel_accent  = { 0.45f, 0.60f, 0.95f, 1.0f },  /* navy, legible on dark */
        .scheme = "light", .accent_r = 0, .accent_g = 0, .accent_b = 128,
    },
};

/* The colour the theme picker draws as a swatch — the focused-title colour, the
 * one that most says which theme this is (Luna blue, 95 navy, neon magenta). */
void theme_preview_color(syn_theme_t t, float out[4])
{
    if (t < 0 || t >= SYN_THEME_COUNT) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    memcpy(out, theme_presets[t].tb_focus, sizeof(float) * 4);
}

/* ── Applying a theme ────────────────────────────────────── */

static void theme_repaint(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (o->scene_output)
            wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
        wlr_output_schedule_frame(o->wlr_output);
    }
}

static void theme_state_save(syn_server_t *s)
{
    char path[256];
    if (!syn_config_path(path, sizeof(path), "theme.state")) return;
    syn_config_ensure_dir();
    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: theme: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    fprintf(f, "theme=%s\n", syn_theme_names[s->config.theme]);
    /* Translucency lives here too so the sliders survive a restart, same file as
     * the theme name they sit beside in the appearance panels. */
    fprintf(f, "transparency=%s\n", s->config.transparency ? "on" : "off");
    fprintf(f, "active_opacity=%.2f\n", s->config.active_opacity);
    fclose(f);
}

/* The unfocused level always trails the focused one by a hair, so one slider
 * drives both. Clamped so a window can never go fully invisible. */
static float inactive_from_active(float active)
{
    float v = active - 0.06f;
    if (v < 0.50f) v = 0.50f;
    return v;
}

void transparency_set_opacity(syn_server_t *s, float active)
{
    if (active < 0.50f) active = 0.50f;
    if (active > 1.00f) active = 1.00f;
    s->config.active_opacity   = active;
    s->config.inactive_opacity = inactive_from_active(active);
    anim_apply_alpha_all(s);
    theme_repaint(s);
    theme_state_save(s);
}

void transparency_set_enabled(syn_server_t *s, int on)
{
    s->config.transparency = on;
    /* Enabling while the focused level is still 1.0 shows nothing — the window
     * you are looking at is the focused one. Drop to a clearly translucent
     * default so "on" actually looks on (Firefox included: it is just a window). */
    if (on && s->config.active_opacity > 0.98f) {
        s->config.active_opacity   = 0.90f;
        s->config.inactive_opacity = inactive_from_active(0.90f);
    }
    anim_apply_alpha_all(s);
    theme_repaint(s);
    theme_state_save(s);
}

/* Copy a preset's colours + opacity levels into a config, nothing more. This is
 * the half config.c needs at parse time (`theme =`), before there is a server to
 * repaint or apps to reskin — and doing it here means an explicit border_color_*
 * written AFTER `theme =` in synuirc still overrides, because it parses later. */
void theme_load_colors(syn_config_t *cfg, syn_theme_t theme)
{
    if (theme < 0 || theme >= SYN_THEME_COUNT) return;
    const syn_theme_preset_t *p = &theme_presets[theme];

    cfg->theme = theme;
    memcpy(cfg->border_color_norm,  p->border_norm,  sizeof(cfg->border_color_norm));
    memcpy(cfg->border_color_focus, p->border_focus, sizeof(cfg->border_color_focus));
    memcpy(cfg->border_color_ai,    p->border_ai,    sizeof(cfg->border_color_ai));
    memcpy(cfg->border_color_warn,  p->border_warn,  sizeof(cfg->border_color_warn));
    memcpy(cfg->titlebar_color,       p->tb_norm,       sizeof(cfg->titlebar_color));
    memcpy(cfg->titlebar_color_focus, p->tb_focus,      sizeof(cfg->titlebar_color_focus));
    memcpy(cfg->titlebar_text,        p->tb_text,       sizeof(cfg->titlebar_text));
    memcpy(cfg->titlebar_text_focus,  p->tb_text_focus, sizeof(cfg->titlebar_text_focus));
    cfg->active_opacity   = p->active_opacity;
    cfg->inactive_opacity = p->inactive_opacity;
    memcpy(cfg->panel_accent, p->panel_accent, sizeof(cfg->panel_accent));

    /* Push the accent into render.c's cache now, so every panel drawn after this
     * (including the first one, before any theme_apply) uses the theme's colour.
     * Safe with no server — it only writes a static. */
    render_set_panel_accent(p->panel_accent);
}

void theme_apply(syn_server_t *s, syn_theme_t theme, int save)
{
    if (theme < 0 || theme >= SYN_THEME_COUNT) return;
    const syn_theme_preset_t *p = &theme_presets[theme];

    theme_load_colors(&s->config, theme);

    /* Re-tint every window's chrome (the titlebar buffer is redrawn with the new
     * caption colours) and re-push opacity, since the inactive level may have
     * moved. Only touches mapped windows; unmapped ones re-read cfg when shown. */
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link)
            if (v->mapped)
                anim_apply_alpha(v);   /* calls view_update_decorations itself */
    }
    theme_repaint(s);

    /* Hand the app-side reskin to the helper (safe/merge-y, and a no-op where the
     * tools aren't installed). Firefox transparency is already covered by the
     * compositor's opacity — this only carries the light/dark scheme. */
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "synui-apply-theme %s %d %d %d",
             p->scheme, p->accent_r, p->accent_g, p->accent_b);
    synui_spawn(cmd);

    if (save) theme_state_save(s);
    wlr_log(WLR_INFO, "synui: theme applied: %s (scheme %s)",
            syn_theme_names[theme], p->scheme);
}

/* Lay theme.state over whatever synuirc left in cfg->theme, then apply it. Called
 * once at startup (save=0 — it is re-applying what it just read, not a new pick). */
void theme_state_load(syn_server_t *s)
{
    int   have_tr = 0, tr = 0, have_op = 0;
    float op = 0.0f;

    char path[256];
    if (syn_config_path(path, sizeof(path), "theme.state")) {
        FILE *f = fopen(path, "r");
        if (f) {
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = '\0';
                if (strncmp(line, "theme=", 6) == 0) {
                    for (int t = 0; t < SYN_THEME_COUNT; t++)
                        if (strcmp(line + 6, syn_theme_names[t]) == 0)
                            s->config.theme = t;
                } else if (strncmp(line, "transparency=", 13) == 0) {
                    tr = strcmp(line + 13, "on") == 0; have_tr = 1;
                } else if (strncmp(line, "active_opacity=", 15) == 0) {
                    op = (float)atof(line + 15); have_op = 1;
                }
            }
            fclose(f);
        }
    }

    /* Apply the theme first — it resets the opacity levels from the preset — then
     * lay the persisted translucency back over them, so a user's slider position
     * wins over the theme's default rather than being clobbered every boot. */
    theme_apply(s, s->config.theme, 0);
    if (have_op && op >= 0.50f && op <= 1.00f) {
        s->config.active_opacity   = op;
        s->config.inactive_opacity = inactive_from_active(op);
    }
    if (have_tr) s->config.transparency = tr;
    if (have_tr || have_op) anim_apply_alpha_all(s);
}

/* ── The panel ───────────────────────────────────────────── */

void theme_show(syn_server_t *s)
{
    s->thememgr.visible   = 1;
    s->thememgr.selected  = s->config.theme;   /* start on the current theme */
    s->thememgr.status[0] = '\0';
    wlr_log(WLR_INFO, "synui: theme manager shown");
    synui_render_thememgr(s);
}

void theme_hide(syn_server_t *s)
{
    s->thememgr.visible = 0;
    synui_render_thememgr(s);
}

void theme_toggle(syn_server_t *s)
{
    if (s->thememgr.visible) theme_hide(s);
    else                     theme_show(s);
}

static void theme_move(syn_server_t *s, int dir)
{
    int t = s->thememgr.selected + dir;
    if (t < 0) t = 0;
    if (t >= SYN_THEME_COUNT) t = SYN_THEME_COUNT - 1;
    s->thememgr.selected = t;
}

int theme_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->thememgr.visible) return 0;

    /* Let modified combos through to the global binds, so Super+Shift+A closes
     * the panel it opened (same idiom as ctlpanel_key). */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        theme_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        theme_move(s, -1);
        synui_render_thememgr(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        theme_move(s, +1);
        synui_render_thememgr(s);
        return 1;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        theme_apply(s, s->thememgr.selected, 1);
        snprintf(s->thememgr.status, sizeof(s->thememgr.status),
                 "applied: %s", theme_name(s->thememgr.selected));
        synui_render_thememgr(s);
        return 1;
    case XKB_KEY_t:
    case XKB_KEY_T:
        /* Toggle the transparency master switch right here — the panel that owns
         * the look owns its glassiness too. */
        transparency_set_enabled(s, !s->config.transparency);
        snprintf(s->thememgr.status, sizeof(s->thememgr.status),
                 "transparency %s", s->config.transparency ? "on" : "off");
        synui_render_thememgr(s);
        return 1;
    case XKB_KEY_Left:
    case XKB_KEY_h:
    case XKB_KEY_minus:
        /* Left/Right are the opacity slider; nudging it turns transparency on. */
        if (!s->config.transparency) transparency_set_enabled(s, 1);
        transparency_set_opacity(s, s->config.active_opacity - 0.05f);
        snprintf(s->thememgr.status, sizeof(s->thememgr.status),
                 "opacity %d%%", (int)(s->config.active_opacity * 100 + 0.5f));
        synui_render_thememgr(s);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
    case XKB_KEY_plus:
    case XKB_KEY_equal:
        if (!s->config.transparency) transparency_set_enabled(s, 1);
        transparency_set_opacity(s, s->config.active_opacity + 0.05f);
        snprintf(s->thememgr.status, sizeof(s->thememgr.status),
                 "opacity %d%%", (int)(s->config.active_opacity * 100 + 0.5f));
        synui_render_thememgr(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
