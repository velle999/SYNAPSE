/*
 * ctlpanel.c — the control panel (Super+C, and the first entry of the waybar
 * start menu).
 *
 * synui grew its settings one panel at a time — displays on Super+D, filters on
 * Super+E, power on Super+P — each reachable only by already knowing its key.
 * This is the front door, and since the categories landed it is the *whole*
 * front door rather than an index to twenty other ones: a category list down the
 * left, that category's rows on the right, and the panels that own the details
 * opening from it and handing control back to it when they close.
 *
 * Two things make it one system rather than a launcher:
 *
 *   - One item table (ctl_items[] below) is the only place a setting is
 *     declared. The sidebar, the row pane, the cursor and the key handler all
 *     walk it, so a row cannot appear in one and be missing from another.
 *
 *   - A row that opens a panel arms syn_ctlpanel_t::child, and that panel's
 *     hide path calls ctlpanel_child_closed(). Esc in Displays therefore lands
 *     back on Display ▸ Display settings, not on the desktop. Opened by their
 *     own keybind the same panels close to the desktop as they always did —
 *     child is empty then, and the call is a no-op.
 *
 * The shortcuts category is *generated from the live bind table* rather than
 * written out here. That is the whole point of it: a hand-maintained list is a
 * list that drifts, and this project has already shipped that bug once, in the
 * waybar start menu, where a stale entry list mapped menu items to the wrong
 * commands. Read the binds and there is nothing to keep in step.
 *
 * Rows are bind *actions*, executed through synui_binding_execute() — the same
 * path a keypress takes. A panel that reimplemented "open the power panel"
 * would be a second definition of it, free to disagree with the first.
 *
 * Keys follow filters.c/power.c (Up/Down select, Enter/Space activate, Esc
 * close), because a third panel that worked a third way would be its own bug.
 * Left/Right and Tab are the addition categories needed: they move between the
 * two columns, and Esc in the right column steps back to the left before it
 * closes anything — the same "back out one level" every menu tree has.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wlr/types/wlr_damage_ring.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* How long to keep repainting the panel after an AI-backend switch, waiting for
 * the helper to restart synapd and write the backend state file. Generous: it is a
 * service restart, and the poll stops early the moment the value changes. */
#define CTL_BACKEND_POLL_SECS  8.0

/* How long the AI-model row waits after the last Left/Right before asking
 * synapd for the model the cursor landed on.
 *
 * The choices are multi-gigabyte files, so "load what the key selected" cannot
 * mean "load it on the keypress": stepping from the first entry to the third
 * would load the second on the way past, and holding the key would work through
 * the whole directory. Long enough to cross a row you did not want, short enough
 * that a deliberate pick does not feel like it was ignored. */
#define CTL_MODEL_SETTLE_SECS  0.7

static double ctl_now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ── The item table ──────────────────────────────────────────
 *
 * Every row in the panel, once. `action` is the bind action a PANEL/LAUNCH/
 * ACTION row fires; the in-place toggles have none, because there is nothing to
 * hand to — they are handled by id in ctlpanel_activate().
 *
 * Rows are listed in display order and grouped by category. Nothing enforces
 * that grouping (the walk filters on .cat), but keeping the literal order and
 * the drawn order the same means one read of this table tells you what the
 * panel looks like.
 */
/* Enum row option names. Kept next to the table rather than shared with the
 * config parser's own name tables: these are what the PANEL shows, and a value
 * a user reads in a menu ("Bottom") is not always what the config file spells
 * ("bottom"). ctl_enum_write() lowercases on the way out, which is what keeps
 * the two in step without needing two tables that can drift. */
static const char *const ctl_names_dock_edge[] = { "Bottom", "Top", "Left", "Right" };
static const char *const ctl_names_arrange[]   = { "Name", "Type", "Size", "Date" };
static const char *const ctl_names_phosphor[]  = { "Off", "Green", "Amber", "Blue" };
/* Order matches syn_focus_mode_t, and these ARE the synuirc spellings — the
 * panel writes an enum as its display name folded to lower case (ctl_format),
 * precisely so there is no second table to drift. So they have to be single
 * words that read as config values, which is why the row leans on its help
 * line to say what "sloppy" means rather than spelling it in the value. */
static const char *const ctl_names_focus_mode[] = { "Click", "Sloppy", "Strict" };
/* Order matches cat_breed_t in synui.h; the lower-cased spellings synuirc takes
 * live in cat_breed_names[] beside the coats themselves, so a new breed needs
 * its display name added HERE and nowhere else. */
static const char *const ctl_names_cat_breed[] = {
    "Neon", "Tabby", "Ginger", "Tuxedo", "Siamese",
    "Calico", "Tortie", "Russian Blue", "Black",
};
/* Order matches syn_bar_shell_t. The lower-cased spellings are what config.c's
 * `bar_shell` case parses back and what synui-bar.sh matches on. */
static const char *const ctl_names_bar_shell[]   = { "SYNAPSE", "Antiquity" };
/* Order matches syn_panel_close_t. Folded to lower case these ARE the synuirc
 * spellings, which is what lets the row and `panel_close = clickoff` mean the
 * same thing. Single words for the reason the whole enum table is: ctl_format
 * lower-cases the display name and writes THAT. */
static const char *const ctl_names_panel_close[] = { "Clickoff", "Button", "Window" };
/* Order matches syn_bar_edge_t. Same two words as the first two dock edges
 * above, and folded to lower case they ARE the synuirc spellings — which is
 * what lets "put it at the bottom" mean one thing across both. */
static const char *const ctl_names_bar_edge[]    = { "Top", "Bottom" };
/* Order matches the GAME_OUT_* enum in synui.h, and folded to lower case these
 * ARE the synuirc spellings config.c's `game_output` case parses back. Single
 * words for the reason the whole table is — "Main screen" would be written to
 * settings.state as `main screen`. The help line carries the meaning. */
static const char *const ctl_names_game_output[] = { "Primary", "Focused", "Ask" };

/*
 * Which file a row is STORED in.
 *
 * settings.state is the default and covers nearly everything. The exceptions
 * are the rows whose field is also owned by a panel with its own state file —
 * the Super+E pages: filters.state (the CRT strengths) and uifx.state (corners,
 * shadow, blur). Both of those are read by synui_config_load() AFTER
 * settings.state, so for those fields a settings.state entry is a value that is
 * written, read, and then overwritten by the owning file at every load. That is
 * not theoretical: a phosphor tint picked in this panel read "amber" all session
 * and was back to off at the next login, because filters.state said off.
 *
 * So a row names its owner and this panel writes THAT file, which keeps one
 * field to one store. The alternative — teaching the load order to prefer
 * whichever file is newer — makes the answer depend on two mtimes, and the
 * panels would still be describing the same setting in two vocabularies.
 */
typedef enum {
    CTL_STORE_SETTINGS = 0,   /* settings.state, via `key` */
    CTL_STORE_FILTERS,        /* filters.state — the CRT page of Super+E */
    CTL_STORE_UIFX,           /* uifx.state — its window-effects page */
} syn_ctl_store_t;

struct ctl_item {
    int             row;
    syn_ctl_cat_t   cat;
    syn_ctl_kind_t  kind;
    const char     *label;
    const char     *action;

    /* ── The data-driven half ────────────────────────────────
     *
     * A row that names `key` and `off` needs no code anywhere: the value is
     * read from the offset, adjusted within [min,max], written to
     * settings.state under `key`, reset from the defaults snapshot, and applied
     * by re-running whatever `apply` names. Rows that leave these zeroed are
     * the old bespoke ones, and they still go through the switches below.
     *
     * `key` must be the synuirc spelling exactly. It is what gets written to
     * settings.state and read back by config_parse_kv(), so a typo here is a
     * setting that works all session and is gone at the next login.
     */
    const char     *section;   /* heading this row opens; NULL continues */
    const char     *key;       /* synuirc key, and the settings.state key */
    size_t          off;       /* offsetof(syn_config_t, field) */
    syn_ctl_val_t   vtype;
    float           vmin, vmax, vstep;
    const char     *unit;      /* "px", "ms", "%" — drawn after the number */
    const char *const *names;  /* CTL_VAL_ENUM options */
    int             nnames;
    syn_ctl_apply_t apply;
    syn_ctl_store_t store;     /* which file holds it; 0 = settings.state */
    const char     *help;      /* one line, drawn in the footer */
};

/* Shorthands. The table is wide enough that spelling every field per row would
 * bury the two things worth reading — the label and the range. */
#define CFG(field)  offsetof(syn_config_t, field)
/* Both designators at once, so an option list and its length cannot be given
 * separately and disagree. */
#define NAMES(a)    .names = (a), .nnames = (int)(sizeof(a) / sizeof((a)[0]))

static const struct ctl_item ctl_items[] = {
    /* Appearance */
    { CTL_ROW_THEME,        CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "Theme",            "theme",
      .section = "Look", .help = "Colour preset for window chrome and synui's own panels" },
    { CTL_ROW_WALLPAPER,    CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "Wallpaper",        "wallpaper" },
    { CTL_ROW_CURSOR,       CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "Cursor theme",     "cursor"    },
    { CTL_ROW_UI_FONT,      CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "UI font",          "font",
      .help = "The family every synui panel draws in. Previews live; Esc puts it back" },
    { CTL_ROW_TRANSPARENCY, CTL_CAT_APPEARANCE, CTL_KIND_SLIDER, "Transparency",     NULL,
      .help = "Focused-window opacity. Left/Right adjust; Enter switches it off" },
    { CTL_ROW_INACTIVE_OPACITY, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Unfocused opacity", NULL,
      .key = "inactive_opacity", .off = CFG(inactive_opacity), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.30f, .vmax = 1.0f, .vstep = 0.02f, .apply = CTL_APPLY_GLASS,
      .help = "How far windows you are not using fade back" },
    { CTL_ROW_FOOT_ALPHA,   CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Terminal glass", NULL,
      .key = "foot_alpha", .off = CFG(foot_alpha), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.02f, .apply = CTL_APPLY_GLASS,
      .help = "foot draws its own background alpha, so it needs its own level" },

    { CTL_ROW_EFFECTS,      CTL_CAT_APPEARANCE, CTL_KIND_TOGGLE, "CRT effects",      NULL,
      .section = "CRT effects",
      .help = "The GLES post-process pass. Off on renderers that have none" },
    { CTL_ROW_FILTERS,      CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "All filters",      "filters"   },
    { CTL_ROW_EFFECT_SCANLINE, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Scanlines", NULL,
      .key = "effect_scanline", .off = CFG(effect_scanline), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.05f, .apply = CTL_APPLY_REPAINT, .store = CTL_STORE_FILTERS },
    { CTL_ROW_EFFECT_CURVATURE, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Screen curve", NULL,
      .key = "effect_curvature", .off = CFG(effect_curvature), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.05f, .apply = CTL_APPLY_REPAINT, .store = CTL_STORE_FILTERS },
    { CTL_ROW_EFFECT_ABERRATION, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Chromatic aberration", NULL,
      .key = "effect_aberration", .off = CFG(effect_aberration), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.05f, .apply = CTL_APPLY_REPAINT, .store = CTL_STORE_FILTERS },
    { CTL_ROW_EFFECT_GLITCH, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Glitch on alert", NULL,
      .key = "effect_glitch", .off = CFG(effect_glitch), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.05f, .apply = CTL_APPLY_REPAINT, .store = CTL_STORE_FILTERS },
    { CTL_ROW_EFFECT_PHOSPHOR, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Phosphor tint", NULL,
      .key = "effect_phosphor", .off = CFG(effect_phosphor), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_phosphor), .apply = CTL_APPLY_REPAINT, .store = CTL_STORE_FILTERS,
      .help = "Off leaves the picture in colour; the blend below is what applies it" },
    { CTL_ROW_EFFECT_MONO, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Phosphor blend", NULL,
      .key = "effect_mono", .off = CFG(effect_mono), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.05f, .apply = CTL_APPLY_REPAINT, .store = CTL_STORE_FILTERS,
      .help = "Blend toward the phosphor tint. Bloom only bites once this is up" },
    { CTL_ROW_EFFECT_BLOOM, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Phosphor glow", NULL,
      .key = "effect_bloom", .off = CFG(effect_bloom), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.05f, .apply = CTL_APPLY_REPAINT, .store = CTL_STORE_FILTERS },

    /* ── Windows ─────────────────────────────────────────────
     *
     * Everything about how a window is FRAMED. Split out of Appearance because
     * Appearance was where the theme lives and these are not about colour: a
     * border width and a shadow sigma belong with snapping and tiling, not with
     * a wallpaper picker. */
    /* One row per panel, not one for all three: velle asked for "a switch for
     * each of them in settings not all or nothing", and they genuinely differ —
     * a control panel you want gone the moment you look away and a calculator
     * you park in the corner are different answers. */
    { CTL_ROW_CALC_CLOSE,     CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Calculator", NULL,
      .section = "Panels",
      .key = "calc_close", .off = CFG(calc_close), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_panel_close), .apply = CTL_APPLY_NONE,
      .help = "Window: drag it by the header, click elsewhere freely. "
              "Clickoff: closes when you click away. Esc always closes" },
    { CTL_ROW_CTLPANEL_CLOSE, CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Control panel", NULL,
      .key = "ctlpanel_close", .off = CFG(ctlpanel_close), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_panel_close), .apply = CTL_APPLY_NONE,
      .help = "How this panel itself behaves" },
    { CTL_ROW_TASKMGR_CLOSE,  CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Task manager", NULL,
      .key = "taskmgr_close", .off = CFG(taskmgr_close), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_panel_close), .apply = CTL_APPLY_NONE,
      .help = "How the task manager behaves" },
    { CTL_ROW_TITLEBARS,      CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Titlebars", NULL,
      .section = "Frame", .help = "Server-side titlebars, on every window at once" },
    { CTL_ROW_TITLEBAR_HEIGHT, CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Titlebar height", NULL,
      .key = "titlebar_height", .off = CFG(titlebar_height), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 64, .vstep = 2, .unit = "px", .apply = CTL_APPLY_DECO,
      .help = "0 removes the titlebar; below 14 there is no room for a button" },
    { CTL_ROW_BORDER_WIDTH,   CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Border width", NULL,
      .key = "border_width", .off = CFG(border_width), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 32, .vstep = 1, .unit = "px", .apply = CTL_APPLY_DECO },
    { CTL_ROW_CORNER_RADIUS,  CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Corner radius", NULL,
      .key = "corner_radius", .off = CFG(corner_radius), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 48, .vstep = 1, .unit = "px", .apply = CTL_APPLY_GLASS, .store = CTL_STORE_UIFX,
      .help = "Forced square while maximized, so nothing pokes past the output" },
    { CTL_ROW_GAP,            CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Tiling gap", NULL,
      .key = "gap", .off = CFG(gap), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 128, .vstep = 2, .unit = "px", .apply = CTL_APPLY_RELAYOUT },
    { CTL_ROW_MASTER_FACTOR,  CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Master area", NULL,
      .key = "master_factor", .off = CFG(master_factor), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.1f, .vmax = 0.9f, .vstep = 0.05f, .apply = CTL_APPLY_RELAYOUT,
      .help = "Share of the screen the master window takes when tiling" },
    { CTL_ROW_CASCADE_STACK,  CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Cascade pile size", NULL,
      .key = "cascade_stack_max", .off = CFG(cascade_stack_max), .vtype = CTL_VAL_INT,
      .vmin = CASCADE_STACK_MIN, .vmax = CASCADE_STACK_MAX, .vstep = 1,
      .apply = CTL_APPLY_RELAYOUT,
      .help = "How deep a cascade pile may get once the grid of piles is full" },
    { CTL_ROW_ANIMATION_MS,   CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Animation length", NULL,
      .key = "animation_ms", .off = CFG(animation_ms), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 1000, .vstep = 10, .unit = "ms", .apply = CTL_APPLY_NONE,
      .help = "0 turns fades off — every one jumps straight to its end state" },
    { CTL_ROW_CLIP_CSD_MARGIN, CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Crop client shadows", NULL,
      .key = "clip_csd_margin", .off = CFG(clip_csd_margin), .vtype = CTL_VAL_BOOL,
      .apply = CTL_APPLY_DECO,
      .help = "Hides the invisible margin apps like Firefox draw their own shadow in" },

    { CTL_ROW_SHADOW,         CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Drop shadow", NULL,
      .section = "Shadow", .key = "shadow", .off = CFG(shadow), .vtype = CTL_VAL_BOOL,
      .apply = CTL_APPLY_SHADOW, .store = CTL_STORE_UIFX },
    { CTL_ROW_SHADOW_SIGMA,   CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Shadow softness", NULL,
      .key = "shadow_blur_sigma", .off = CFG(shadow_blur_sigma), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 80.0f, .vstep = 1.0f, .unit = "px", .apply = CTL_APPLY_SHADOW, .store = CTL_STORE_UIFX },
    { CTL_ROW_SHADOW_SPREAD,  CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Shadow spread", NULL,
      .key = "shadow_spread", .off = CFG(shadow_spread), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 64.0f, .vstep = 1.0f, .unit = "px", .apply = CTL_APPLY_SHADOW, .store = CTL_STORE_UIFX,
      .help = "Solid shadow before the falloff starts — what gives it weight" },
    { CTL_ROW_SHADOW_OFFSET_X, CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Shadow offset X", NULL,
      .key = "shadow_offset_x", .off = CFG(shadow_offset_x), .vtype = CTL_VAL_INT,
      .vmin = -64, .vmax = 64, .vstep = 1, .unit = "px", .apply = CTL_APPLY_SHADOW },
    { CTL_ROW_SHADOW_OFFSET_Y, CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Shadow offset Y", NULL,
      .key = "shadow_offset_y", .off = CFG(shadow_offset_y), .vtype = CTL_VAL_INT,
      .vmin = -64, .vmax = 64, .vstep = 1, .unit = "px", .apply = CTL_APPLY_SHADOW, .store = CTL_STORE_UIFX },

    { CTL_ROW_BLUR,           CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Backdrop blur", NULL,
      .section = "Blur", .key = "blur", .off = CFG(blur), .vtype = CTL_VAL_BOOL,
      .apply = CTL_APPLY_BLURDATA, .store = CTL_STORE_UIFX,
      .help = "Frosts what is behind a translucent window. Opaque ones cost nothing" },
    { CTL_ROW_BLUR_PASSES,    CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Blur passes", NULL,
      .key = "blur_passes", .off = CFG(blur_passes), .vtype = CTL_VAL_INT,
      .vmin = 1, .vmax = 5, .vstep = 1, .apply = CTL_APPLY_BLURDATA, .store = CTL_STORE_UIFX },
    { CTL_ROW_BLUR_RADIUS,    CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Blur radius", NULL,
      .key = "blur_radius", .off = CFG(blur_radius), .vtype = CTL_VAL_INT,
      .vmin = 1, .vmax = 20, .vstep = 1, .unit = "px", .apply = CTL_APPLY_BLURDATA, .store = CTL_STORE_UIFX },
    { CTL_ROW_BLUR_NOISE,     CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Blur noise", NULL,
      .key = "blur_noise", .off = CFG(blur_noise), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.01f, .apply = CTL_APPLY_BLURDATA },
    { CTL_ROW_BLUR_BRIGHTNESS, CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Blur brightness", NULL,
      .key = "blur_brightness", .off = CFG(blur_brightness), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 2.0f, .vstep = 0.05f, .apply = CTL_APPLY_BLURDATA },
    { CTL_ROW_BLUR_CONTRAST,  CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Blur contrast", NULL,
      .key = "blur_contrast", .off = CFG(blur_contrast), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 2.0f, .vstep = 0.05f, .apply = CTL_APPLY_BLURDATA },
    { CTL_ROW_BLUR_SATURATION, CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Blur saturation", NULL,
      .key = "blur_saturation", .off = CFG(blur_saturation), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 2.0f, .vstep = 0.05f, .apply = CTL_APPLY_BLURDATA },
    { CTL_ROW_GLASS_HALO,     CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Blur halo", NULL,
      .key = "glass_halo", .off = CFG(glass_halo), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 64, .vstep = 1, .unit = "px", .apply = CTL_APPLY_GLASS, .store = CTL_STORE_UIFX,
      .help = "How far the blur reaches past the window. 0 keeps it inside the frame" },

    /* Window behaviour, which is what KDE calls this and what most people come
     * looking for. Focus leads: it is the one row here that changes what the
     * keyboard does rather than what the mouse can do. */
    { CTL_ROW_FOCUS_MODE,     CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Focus follows", NULL,
      .section = "Behaviour", .key = "focus_mode", .off = CFG(focus_mode),
      .vtype = CTL_VAL_ENUM, NAMES(ctl_names_focus_mode),
      .help = "Click: only a click focuses. Sloppy and Strict follow the "
              "pointer; over the desktop, Strict drops focus, Sloppy keeps it" },
    { CTL_ROW_FOCUS_DELAY,    CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Focus delay", NULL,
      .key = "focus_delay_ms", .off = CFG(focus_delay_ms), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 1000, .vstep = 25, .unit = "ms",
      .help = "How long the pointer rests before focus follows it. 0 is "
              "instant, which also focuses windows you only crossed over" },
    /* Next to the focus rows because it is the same question asked about the
     * compositor's own windows: what does the pointer moving somewhere else
     * change? Nothing, now, unless you turn this on. */
    { CTL_ROW_PANEL_FOLLOW,   CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Panels follow the pointer", NULL,
      .key = "panel_follow_pointer", .off = CFG(panel_follow_pointer),
      .vtype = CTL_VAL_BOOL,
      .help = "On, an open panel moves to whichever monitor the pointer is on. "
              "Off, it stays on the monitor you opened it on" },

    { CTL_ROW_SNAP,         CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Edge snapping", NULL,
      .key = "snap", .off = CFG(snap), .vtype = CTL_VAL_BOOL,
      .help = "Drag a window to an edge to fill that half or quarter" },
    { CTL_ROW_SNAP_ZONE,      CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Snap zone", NULL,
      .key = "snap_zone", .off = CFG(snap_zone), .vtype = CTL_VAL_INT,
      .vmin = 2, .vmax = 200, .vstep = 2, .unit = "px",
      .help = "How close to the edge a drag arms the snap. Raise it on a "
              "high-DPI panel, or if a fast pointer crosses the band" },
    { CTL_ROW_REMEMBER_GEOMETRY, CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Remember window size", NULL,
      .key = "remember_geometry", .off = CFG(remember_geometry), .vtype = CTL_VAL_BOOL8,
      .help = "Reopen each app where and how big it was when it closed" },
    { CTL_ROW_ALT_TAB_STYLE, CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Alt+Tab is mission control", NULL,
      .key = "alt_tab_style", .off = CFG(alt_tab_overview), .vtype = CTL_VAL_BOOL,
      .help = "On, Alt+Tab opens the whole desk. Off, the MRU thumbnail strip "
              "— and the three rows below describe that strip" },
    { CTL_ROW_ALT_TAB_PREVIEW, CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Alt+Tab previews", NULL,
      .key = "alt_tab_preview", .off = CFG(alt_tab_preview), .vtype = CTL_VAL_BOOL,
      .help = "The thumbnail grid. Off, Alt+Tab still cycles — silently" },
    { CTL_ROW_ALT_TAB_ALL_DESKTOPS, CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Alt+Tab across desktops", NULL,
      .key = "alt_tab_all_desktops", .off = CFG(alt_tab_all_desktops), .vtype = CTL_VAL_BOOL },
    { CTL_ROW_ALT_TAB_MINIMIZED, CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Alt+Tab reaches minimized", NULL,
      .key = "alt_tab_minimized", .off = CFG(alt_tab_minimized), .vtype = CTL_VAL_BOOL },

    /* Desktop. Layout leads: it is the one row here that changes where your
     * windows go rather than what the shell furniture looks like. */
    { CTL_ROW_LAYOUT,        CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Layout",           NULL,
      .section = "Desktop",
      .help = "Of the desktop you are on — layout is per-desktop, not global" },
    { CTL_ROW_OVERVIEW,      CTL_CAT_DESKTOP, CTL_KIND_PANEL,  "Mission control",  "overview",
      .help = "Every window on this desktop at once, and the desktops themselves" },
    { CTL_ROW_WIDGETS,       CTL_CAT_DESKTOP, CTL_KIND_PANEL,  "Desktop widgets",  "widgets" },
    { CTL_ROW_DESKTOP_ICONS, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Desktop icons", NULL,
      .key = "desktop_icons", .off = CFG(desktop_icons), .vtype = CTL_VAL_BOOL8,
      .apply = CTL_APPLY_DESKICONS, .help = "Draw ~/Desktop on the wallpaper" },
    { CTL_ROW_DESKTOP_ICON_ARRANGE, CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Icon order", NULL,
      .key = "desktop_icon_arrange", .off = CFG(desktop_icon_arrange), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_arrange), .apply = CTL_APPLY_DESKICONS },

    { CTL_ROW_DOCK,          CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Dock",             NULL,
      .section = "Dock" },
    { CTL_ROW_DOCK_AUTOHIDE, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Dock auto-hide",   NULL      },
    { CTL_ROW_DOCK_EDGE,     CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock edge", NULL,
      .key = "dock_edge", .off = CFG(dock_edge), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_dock_edge), .apply = CTL_APPLY_DOCK },
    { CTL_ROW_DOCK_HEIGHT,   CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock size", NULL,
      .key = "dock_height", .off = CFG(dock_height), .vtype = CTL_VAL_INT,
      .vmin = 32, .vmax = 200, .vstep = 4, .unit = "px", .apply = CTL_APPLY_DOCK },
    { CTL_ROW_DOCK_HOVER_MARGIN, CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock reveal strip", NULL,
      .key = "dock_hover_margin", .off = CFG(dock_hover_margin), .vtype = CTL_VAL_INT,
      .vmin = 1, .vmax = 32, .vstep = 1, .unit = "px", .apply = CTL_APPLY_DOCK,
      .help = "How close to the edge the pointer must get to bring it back" },

    { CTL_ROW_LAUNCHER,      CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Start button",     NULL,
      .section = "Shell" },
    /* There was a "Super+Space opens" row here (launcher ⇄ command bar). It was
     * a SECOND way to declare a keybinding, and the Shortcuts category's rebind
     * (F2) is the first — so the two fought: a chord moved in the palette was
     * put back by the swap, which re-ran at the end of every config load, after
     * binds.state. One list of shortcuts, one owner. Rebind Super+Space and
     * Super+= from Control panel ▸ Shortcuts (or the Super+/ palette) instead. */
    /* Is there a bar at all — the row the Dock switch above has always had and
     * this side of the desktop never did. Bespoke rather than table-driven
     * (.key/.off left zeroed) because flipping the flag is the easy half: the
     * bar is a separate process, so the row also has to go and stop or start
     * it. See CTL_ROW_BAR in ctlpanel_activate. */
    { CTL_ROW_BAR,           CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Bar",              NULL,
      .section = "Bar",
      .help = "The top bar. Off stops it now; a waybar desktop needs "
              "bar_start_cmd in synuirc to put it back" },
    /* The bar is a SEPARATE PROCESS, and this is the one row on the panel whose
     * value the compositor does not act on — synui-bar reads it at startup.
     * CTL_APPLY_NONE is therefore literally right, and the help line has to say
     * so, or the row reads as broken: you flip it, and nothing happens until the
     * bar is restarted. */
    { CTL_ROW_BAR_SHELL,     CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Bar shell",        NULL,
      .key = "bar_shell", .off = CFG(bar_shell), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_bar_shell), .apply = CTL_APPLY_NONE,
      .help = "Antiquity is the diinki port; takes effect at the next login" },
    /* The bar's answer to Dock edge above, and the one row on this panel whose
     * value neither the compositor NOR a restart applies: the bar watches
     * settings.state itself, so it moves while you are looking at it. Two
     * options rather than the dock's four — the bar is a horizontal row and has
     * no vertical form (see syn_bar_edge_t). */
    { CTL_ROW_BAR_EDGE,      CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Bar edge",         NULL,
      .key = "bar_edge", .off = CFG(bar_edge), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_bar_edge), .apply = CTL_APPLY_NONE,
      .help = "Which edge the bar sits on. The bar picks this up live" },
    { CTL_ROW_WELCOME_AT_STARTUP, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Welcome menu at login", NULL,
      .key = "welcome_at_startup", .off = CFG(welcome_at_startup), .vtype = CTL_VAL_BOOL },
    { CTL_ROW_START_OVERLAY, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Neural overlay at login", NULL,
      .key = "start_overlay", .off = CFG(start_overlay), .vtype = CTL_VAL_BOOL },
    { CTL_ROW_CAT_START,     CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Desktop cat at login", NULL,
      .key = "cat", .off = CFG(cat_start), .vtype = CTL_VAL_BOOL,
      .help = "Super+Shift+C toggles it any time; this is only the login state" },
    { CTL_ROW_CAT_BREED,     CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Desktop cat breed", NULL,
      .key = "cat_breed", .off = CFG(cat_breed), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_cat_breed), .apply = CTL_APPLY_NONE,
      .help = "Coat and markings only; every breed walks the same" },

    /* ── Input ───────────────────────────────────────────────
     *
     * Nothing here was reachable from the panel at all: keyboard repeat, tap to
     * click and pointer acceleration were synuirc-only, which meant a laptop
     * with tapping off had no way to turn it on without a text editor and a
     * logout. */
    { CTL_ROW_REPEAT_RATE,  CTL_CAT_INPUT, CTL_KIND_VALUE, "Key repeat rate", NULL,
      .section = "Keyboard", .key = "repeat_rate", .off = CFG(repeat_rate),
      .vtype = CTL_VAL_INT, .vmin = 1, .vmax = 100, .vstep = 1, .unit = "/s",
      .apply = CTL_APPLY_INPUT },
    { CTL_ROW_REPEAT_DELAY, CTL_CAT_INPUT, CTL_KIND_VALUE, "Key repeat delay", NULL,
      .key = "repeat_delay", .off = CFG(repeat_delay), .vtype = CTL_VAL_INT,
      .vmin = 100, .vmax = 2000, .vstep = 25, .unit = "ms", .apply = CTL_APPLY_INPUT },
    /* The palette, which is also the rebind editor — the reason there is a row
     * here at all. As documentation it was already reachable (the Shortcuts
     * category below), but nothing on this panel led to the place where a
     * shortcut can be CHANGED, and Super+/ is only discoverable once you have
     * read the list it opens. */
    { CTL_ROW_KEYBINDS,     CTL_CAT_INPUT, CTL_KIND_PANEL, "Keyboard shortcuts", "keys",
      .help = "Searchable. F2 moves a shortcut to another key, F3 onto the tap" },
    { CTL_ROW_NUMLOCK,      CTL_CAT_INPUT, CTL_KIND_TOGGLE, "NumLock on at login", NULL,
      .key = "numlock", .off = CFG(numlock), .vtype = CTL_VAL_BOOL,
      .apply = CTL_APPLY_INPUT,
      .help = "A fresh xkb state has it off, which leaves the numpad on arrows" },

    { CTL_ROW_TAP_TO_CLICK, CTL_CAT_INPUT, CTL_KIND_VALUE, "Tap to click", NULL,
      .section = "Pointer", .key = "tap", .off = CFG(tap_to_click),
      .vtype = CTL_VAL_TRI, .apply = CTL_APPLY_INPUT },
    { CTL_ROW_NATURAL_SCROLL, CTL_CAT_INPUT, CTL_KIND_VALUE, "Natural scrolling", NULL,
      .key = "natural_scroll", .off = CFG(natural_scroll), .vtype = CTL_VAL_TRI,
      .apply = CTL_APPLY_INPUT },
    { CTL_ROW_LEFT_HANDED,  CTL_CAT_INPUT, CTL_KIND_VALUE, "Left-handed buttons", NULL,
      .key = "left_handed", .off = CFG(left_handed), .vtype = CTL_VAL_TRI,
      .apply = CTL_APPLY_INPUT },
    { CTL_ROW_ACCEL_SPEED,  CTL_CAT_INPUT, CTL_KIND_VALUE, "Pointer speed", NULL,
      .key = "accel_speed", .off = CFG(accel_speed), .vtype = CTL_VAL_FLOAT,
      .vmin = -1.0f, .vmax = 1.0f, .vstep = 0.1f, .apply = CTL_APPLY_INPUT },
    { CTL_ROW_CURSOR_SIZE,  CTL_CAT_INPUT, CTL_KIND_VALUE, "Cursor size", NULL,
      .key = "cursor_size", .off = CFG(cursor_size), .vtype = CTL_VAL_INT,
      .vmin = 8, .vmax = 256, .vstep = 4, .unit = "px", .apply = CTL_APPLY_CURSOR },

    /* Display */
    { CTL_ROW_DISPLAYS,   CTL_CAT_DISPLAY, CTL_KIND_PANEL,  "Display settings", "displays",
      .section = "Screens" },
    { CTL_ROW_CLOCK,      CTL_CAT_DISPLAY, CTL_KIND_PANEL,  "Date & time",      "clock"    },
    { CTL_ROW_NIGHTLIGHT, CTL_CAT_DISPLAY, CTL_KIND_TOGGLE, "Night light",      NULL,
      .section = "Night light" },
    { CTL_ROW_NIGHTLIGHT_TEMP, CTL_CAT_DISPLAY, CTL_KIND_VALUE, "Colour temperature", NULL,
      .key = "night_light_temp", .off = CFG(night_light_temp), .vtype = CTL_VAL_INT,
      .vmin = 1000, .vmax = 6500, .vstep = 100, .unit = "K",
      .apply = CTL_APPLY_NIGHTLIGHT,
      .help = "6500K is daylight — the identity ramp. Lower is warmer" },

    /* Sound. Recording audio lives here rather than under Display: what the
     * row decides is which SOUND goes into the file — the screen it captures is
     * settled by the focus, not by a setting. */
    { CTL_ROW_SOUNDS,       CTL_CAT_SOUND, CTL_KIND_PANEL,  "Event sounds", "sounds" },
    { CTL_ROW_EQUALIZER,    CTL_CAT_SOUND, CTL_KIND_PANEL,  "Equalizer", "equalizer",
      .help = "10-band system equalizer. Adds an output device while it is on" },
    { CTL_ROW_RECORD_AUDIO, CTL_CAT_SOUND, CTL_KIND_TOGGLE, "Record audio", NULL     },
    { CTL_ROW_RECORD_EDIT,  CTL_CAT_SOUND, CTL_KIND_TOGGLE, "Record for editing", NULL,
      .help = "DNxHR .mov that video editors read directly. About 1.1 GB/min" },

    /* Network. Two of the three hand off to something synui does not own —
     * nmtui in a terminal, cups in a browser — so they close the panel rather
     * than arming a return to it. */
    { CTL_ROW_NETWORK,   CTL_CAT_NETWORK, CTL_KIND_LAUNCH, "Network / Wi-Fi", "network"   },
    { CTL_ROW_BLUETOOTH, CTL_CAT_NETWORK, CTL_KIND_PANEL,  "Bluetooth",       "bluetooth" },
    { CTL_ROW_PRINTERS,  CTL_CAT_NETWORK, CTL_KIND_LAUNCH, "Printers",        "printers"  },

    /* Power */
    { CTL_ROW_POWER, CTL_CAT_POWER, CTL_KIND_PANEL,  "Power saving", "power",
      .section = "Power", .help = "Idle timeouts for dim, blank, lock and suspend" },
    { CTL_ROW_LOCK,  CTL_CAT_POWER, CTL_KIND_ACTION, "Lock screen",  "lock"  },
    /* No .apply: nothing to re-run, because the reader is started by the NEXT
     * lock. Turning it off while a lock screen is up is not a case that exists —
     * the panel is behind the lock. */
    { CTL_ROW_LOCK_FPRINT, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Unlock with fingerprint", NULL,
      .key = "lock_fingerprint", .off = CFG(lock_fingerprint), .vtype = CTL_VAL_BOOL,
      /* Names the command rather than the requirement. "Needs an enrolled
       * finger" left the one thing you have to DO off the only screen that
       * mentions the feature: the packaged synuirc that carries the enroll
       * line is a repo reference nothing installs, and an existing
       * ~/.config/synui/synuirc never gains it on upgrade. The footer draws
       * with cairo_show_text and does not clip, so this stays inside the
       * ~70 chars the other help lines hold to. */
      .help = "Install fprintd, run fprintd-enroll; your password always works too" },

    /* Enter cycles auto → always on → always off → auto, through game_toggle()
     * so this row and Super+G are the same control. The VALUE has to name the
     * override and not just s->game.active, which is what it used to show:
     * "auto, nothing running" and "forced off" both drew as plain `off`, so
     * two of the three positions were indistinguishable and pressing Enter
     * looked like it did nothing. */
    { CTL_ROW_GAME,  CTL_CAT_POWER, CTL_KIND_TOGGLE, "Game mode",    NULL,
      .section = "Game mode",
      .help = "Enter cycles auto / always on / always off — same as Super+G" },
    { CTL_ROW_GAME_MODE, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Detect games", NULL,
      .key = "game_mode", .off = CFG(game_mode), .vtype = CTL_VAL_BOOL,
      .help = "Treat a fullscreen game window as a game unless it is excluded" },
    { CTL_ROW_GAME_OUTPUT, CTL_CAT_POWER, CTL_KIND_VALUE, "Open games on", NULL,
      .key = "game_output", .off = CFG(game_output), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_game_output), .apply = CTL_APPLY_NONE,
      .help = "Primary = the monitor marked PRIMARY in Super+D; Ask obeys the game" },
    { CTL_ROW_GAME_SUSPEND_AI, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Stop the AI while gaming", NULL,
      .key = "game_suspend_ai", .off = CFG(game_suspend_ai), .vtype = CTL_VAL_BOOL },
    { CTL_ROW_GAME_INHIBIT_IDLE, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Hold off idle while gaming", NULL,
      .key = "game_inhibit_idle", .off = CFG(game_inhibit_idle), .vtype = CTL_VAL_BOOL,
      .help = "A gamepad is not input as far as the idle timer is concerned" },
    /* The help lines say what each one BUYS, because the four are worth wildly
     * different amounts and two of them look more attractive than they are.
     * Measured rather than guessed — see game.c. */
    { CTL_ROW_GAME_DROP_EFFECTS, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Drop screen effects while gaming", NULL,
      .key = "game_drop_effects", .off = CFG(game_drop_effects), .vtype = CTL_VAL_BOOL,
      .help = "The biggest win: lets the game draw straight to the display" },
    { CTL_ROW_GAME_PAUSE_WALLPAPER, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Pause the wallpaper while gaming", NULL,
      .key = "game_pause_wallpaper", .off = CFG(game_pause_wallpaper), .vtype = CTL_VAL_BOOL,
      .help = "Animated wallpapers keep rendering behind a fullscreen game" },
    { CTL_ROW_GAME_STOP_BAR, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Stop the bar while gaming", NULL,
      .key = "game_stop_bar", .off = CFG(game_stop_bar), .vtype = CTL_VAL_BOOL,
      .help = "Frees memory, but the bar takes a moment to come back" },
    { CTL_ROW_GAME_QUIET_KMOD, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Quiet the kernel monitor while gaming", NULL,
      .key = "game_quiet_kmod", .off = CFG(game_quiet_kmod), .vtype = CTL_VAL_BOOL,
      .help = "Saves very little, and security monitoring pauses with it" },

    /* System */
    { CTL_ROW_AI_BACKEND, CTL_CAT_SYSTEM, CTL_KIND_TOGGLE, "AI backend",        NULL,
      .section = "AI", .help = "Which device synapd runs inference on" },
    { CTL_ROW_AI_MODEL,   CTL_CAT_SYSTEM, CTL_KIND_CHOICE, "AI model",          "aimodel"   },
    { CTL_ROW_AI_LAYOUT,  CTL_CAT_SYSTEM, CTL_KIND_TOGGLE, "AI window placement", NULL,
      .key = "ai_layout", .off = CFG(ai_layout), .vtype = CTL_VAL_BOOL,
      .help = "Let the AI layout decide where a new window goes" },
    { CTL_ROW_AI_CTX_DECOR, CTL_CAT_SYSTEM, CTL_KIND_TOGGLE, "AI context in borders", NULL,
      .key = "ai_ctx_decor", .off = CFG(ai_ctx_decor), .vtype = CTL_VAL_BOOL,
      .help = "Tint a window's border when the AI is holding context for it" },

    { CTL_ROW_TASKMGR,    CTL_CAT_SYSTEM, CTL_KIND_PANEL,  "Task manager",      "taskmgr",
      .section = "Tools" },
    { CTL_ROW_CLIPBOARD,  CTL_CAT_SYSTEM, CTL_KIND_PANEL,  "Clipboard history", "clipboard" },
    { CTL_ROW_NEWS,       CTL_CAT_SYSTEM, CTL_KIND_PANEL,  "News",              "news"      },
    { CTL_ROW_NEWS_REFRESH, CTL_CAT_SYSTEM, CTL_KIND_VALUE, "News refresh", NULL,
      .key = "news_refresh", .off = CFG(news_refresh_min), .vtype = CTL_VAL_INT,
      .vmin = 1, .vmax = 240, .vstep = 5, .unit = "min",
      .help = "How often a feed may be re-fetched at most" },

    /* CTL_KIND_LAUNCH, like Network and Printers: it hands off to a terminal
     * synui does not own, so the panel closes rather than arming a return to
     * itself. There is nothing to come back to — the About box is the window,
     * and it closes on a keypress. */
    { CTL_ROW_ABOUT, CTL_CAT_SYSTEM, CTL_KIND_LAUNCH, "About OS", "about",
      .section = "About",
      .help = "The mark, the machine, and what this desktop is currently set to" },
};

#define CTL_ITEM_COUNT ((int)(sizeof(ctl_items) / sizeof(ctl_items[0])))

static int ctl_item_index(int row)
{
    for (int i = 0; i < CTL_ITEM_COUNT; i++)
        if (ctl_items[i].row == row) return i;
    return -1;
}

static const struct ctl_item *ctl_item(int row)
{
    int i = ctl_item_index(row);
    return i < 0 ? NULL : &ctl_items[i];
}

/* Defined with the rest of the panel plumbing further down; the apply
 * dispatcher below needs it first. */
static void ctlpanel_repaint(syn_server_t *s);

/* ── The generic value path ──────────────────────────────────
 *
 * Everything below works off `off` and `vtype`, so a row that fills those in
 * needs no code of its own to be read, adjusted, drawn, persisted or reset.
 *
 * The one thing worth being careful about is WIDTH. syn_config_t holds these as
 * int, float and bool, and reading a one-byte bool through an int* picks up
 * three neighbouring fields and reports a number in the millions — quietly, and
 * only for the two rows that are bool. Hence CTL_VAL_BOOL8, and hence every
 * access going through these four functions rather than a cast at each site.
 */

static void *ctl_field(syn_server_t *s, const struct ctl_item *it)
{
    return (char *)&s->config + it->off;
}

static const void *ctl_field_of(const syn_config_t *cfg, const struct ctl_item *it)
{
    return (const char *)cfg + it->off;
}

/* The row's value as a float, whatever it is stored as. */
static float ctl_get(const syn_config_t *cfg, const struct ctl_item *it)
{
    const void *p = ctl_field_of(cfg, it);
    switch (it->vtype) {
    case CTL_VAL_FLOAT:  return *(const float *)p;
    case CTL_VAL_BOOL8:  return *(const bool *)p ? 1.0f : 0.0f;
    case CTL_VAL_BOOL:
    case CTL_VAL_INT:
    case CTL_VAL_ENUM:
    case CTL_VAL_TRI:    return (float)*(const int *)p;
    default:             return 0.0f;
    }
}

static void ctl_put(syn_server_t *s, const struct ctl_item *it, float v)
{
    void *p = ctl_field(s, it);
    switch (it->vtype) {
    case CTL_VAL_FLOAT:  *(float *)p = v;                     break;
    case CTL_VAL_BOOL8:  *(bool *)p  = (v != 0.0f);           break;
    case CTL_VAL_BOOL:   *(int *)p   = (v != 0.0f);           break;
    case CTL_VAL_TRI:
    case CTL_VAL_ENUM:
    case CTL_VAL_INT:    *(int *)p   = (int)(v < 0 ? v - 0.5f : v + 0.5f); break;
    default: break;
    }

    /* accel_speed is the one field with a companion flag: libinput's default
     * acceleration and an explicitly-set 0.0 are different states, so input.c
     * only applies the value when accel_speed_set says someone asked for it.
     * Setting the speed IS asking. Without this the row would move and the
     * pointer would not, which is the exact failure the apply enum exists to
     * prevent — it just happens one level lower down than the rest. */
    if (it->off == offsetof(syn_config_t, accel_speed))
        s->config.accel_speed_set = 1;
}

/* What the row shows, and what goes into settings.state. One function for both,
 * so the file cannot say something the panel does not — except for the ENUM
 * case, where the panel shows "Bottom" and the config spells it "bottom". */
static void ctl_format(const struct ctl_item *it, float v, int for_config,
                       char *buf, size_t n)
{
    switch (it->vtype) {
    case CTL_VAL_BOOL:
    case CTL_VAL_BOOL8:
        snprintf(buf, n, "%s", v != 0.0f ? "on" : "off");
        break;

    case CTL_VAL_TRI:
        /* -1 is not "off": it means synui has no opinion and libinput's own
         * default for that device stands. A tri-state drawn as a checkbox is
         * the classic way to lose that distinction, so it is named. */
        if (v < 0)       snprintf(buf, n, "%s", for_config ? "default" : "device default");
        else if (v == 0) snprintf(buf, n, "off");
        else             snprintf(buf, n, "on");
        break;

    case CTL_VAL_ENUM: {
        int i = (int)v;
        if (!it->names || i < 0 || i >= it->nnames) { snprintf(buf, n, "?"); break; }
        snprintf(buf, n, "%s", it->names[i]);
        /* The config file spells its option names in lower case. Rather than
         * keep a second table that can drift from the first, fold the case on
         * the way out — every option name in this panel is ASCII. */
        if (for_config)
            for (char *c = buf; *c; c++)
                if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        break;
    }

    case CTL_VAL_INT:
        if (!for_config && it->unit) snprintf(buf, n, "%d %s", (int)v, it->unit);
        else                          snprintf(buf, n, "%d", (int)v);
        break;

    case CTL_VAL_FLOAT:
        /* Two decimals is enough for every float here (opacities, blur weights,
         * a shadow sigma) and reads better than the six %g would give. */
        if (!for_config && it->unit) snprintf(buf, n, "%.2f %s", v, it->unit);
        else                          snprintf(buf, n, "%.2f", v);
        break;

    default:
        buf[0] = '\0';
        break;
    }
}

/* Re-run whatever the change needs for the screen to agree with it. */
static void ctl_apply(syn_server_t *s, syn_ctl_apply_t what)
{
    switch (what) {
    case CTL_APPLY_NONE:
        break;
    case CTL_APPLY_REPAINT:
        ctlpanel_repaint(s);
        break;
    case CTL_APPLY_RELAYOUT:
        for (int w = 0; w < WORKSPACE_MAX; w++)
            layout_apply(s, &s->workspaces[w]);
        ctlpanel_repaint(s);
        break;

    case CTL_APPLY_DECO:
        /* The heavy one: it re-runs view_resize on every window, which sends a
         * configure to every client. Only for the settings that genuinely
         * change a window's METRICS (border width, titlebar height) — the
         * render-only ones below go through uifx_apply instead, which is the
         * distinction that keeps a held-down arrow key from becoming a
         * configure storm. */
        deco_refresh_all(s);
        ctlpanel_repaint(s);
        break;

    case CTL_APPLY_GLASS:
    case CTL_APPLY_SHADOW:
    case CTL_APPLY_BLURDATA:
        /* One hook for all three: uifx_apply() pushes the global blur data,
         * re-walks every buffer for opacity and corner radius, and rebuilds
         * each frame's shadow and halo nodes. The filters panel already drives
         * its rows through it, so these rows and that panel cannot end up
         * applying the same settings two different ways. */
        uifx_apply(s);
        ctlpanel_repaint(s);
        break;

    case CTL_APPLY_INPUT:
        /* The same call SIGHUP makes: keymap and repeat to every keyboard,
         * libinput options to every tracked pointer. */
        input_reload_config(s);
        break;

    case CTL_APPLY_DOCK:
        dock_rebuild(s);
        dock_relayout(s);
        ctlpanel_repaint(s);
        break;

    case CTL_APPLY_NIGHTLIGHT:
        /* Re-commits the gamma ramps at the new temperature. A no-op while the
         * night light is off, which is correct: the temperature is dormant
         * then, and the row says so. */
        nightlight_apply(s);
        ctlpanel_repaint(s);
        break;

    case CTL_APPLY_CURSOR:
        cursor_reload(s);
        break;

    case CTL_APPLY_DESKICONS:
        deskicons_reload(s);
        ctlpanel_repaint(s);
        break;

    }
}

/*
 * Rows owned by another panel's state file are written THERE, and their
 * settings.state key is dropped rather than kept in step: both files are read
 * by synui_config_load() and the owner is read later, so a copy in
 * settings.state is a value that is quietly discarded at the next load.
 *
 * One call stores the row that moved and every sibling in that file, because
 * both savers write their whole file from the live config. That is also why
 * this needs no value formatting of its own — the owner already spells its
 * fields the way it reads them back.
 *
 * Returns 1 if the row was stored here, so ctl_persist() can stop.
 */
static int ctl_store_write(syn_server_t *s, const struct ctl_item *it)
{
    switch (it->store) {
    case CTL_STORE_FILTERS:
        if (it->key) settings_state_clear(it->key);
        filters_state_save(s);
        return 1;
    case CTL_STORE_UIFX:
        if (it->key) settings_state_clear(it->key);
        uifx_state_save(s);
        return 1;
    case CTL_STORE_SETTINGS:
        return 0;
    }
    return 0;
}

/*
 * Store the change so it is still there next login.
 *
 * A row at its default drops out of settings.state rather than being written as
 * its default. Storing it would pin the setting to today's value for good: a
 * later synui that improved the default would never reach a desktop whose owner
 * had once nudged that row and put it back. Absent means "follow the default",
 * which is the only version of that with a future in it.
 */
static void ctl_persist(syn_server_t *s, const struct ctl_item *it)
{
    if (ctl_store_write(s, it)) return;
    if (!it->key) return;

    char val[64];
    ctl_format(it, ctl_get(&s->config, it), 1, val, sizeof(val));

    if (ctlpanel_row_is_default(s, it->row)) settings_state_clear(it->key);
    else                                     settings_state_set(it->key, val);
}

int ctlpanel_row_is_default(syn_server_t *s, int row)
{
    const struct ctl_item *it = ctl_item(row);
    if (!it || it->vtype == CTL_VAL_NONE) return 1;   /* nothing to compare */

    float now = ctl_get(&s->config, it);
    float def = ctl_get(synui_config_defaults(), it);

    /* Floats are compared with a tolerance a good deal finer than the smallest
     * step any row uses, so stepping away and back reads as "default" again
     * rather than leaving a row permanently marked by accumulated error. */
    if (it->vtype == CTL_VAL_FLOAT) {
        float d = now - def;
        return (d < 0 ? -d : d) < 0.0005f;
    }
    return (int)now == (int)def;
}

/* Left/Right on a table-driven row. Returns 1 if the value actually moved. */
static int ctl_adjust(syn_server_t *s, const struct ctl_item *it, int dir)
{
    float v = ctl_get(&s->config, it);

    switch (it->vtype) {
    case CTL_VAL_BOOL:
    case CTL_VAL_BOOL8:
        v = (v != 0.0f) ? 0.0f : 1.0f;
        break;

    case CTL_VAL_TRI:
        /* Cycles device-default → off → on and wraps, so both directions can
         * reach every state without the row having an end to get stuck at. */
        v = (float)(((int)v + 1 + dir + 3) % 3 - 1);
        break;

    case CTL_VAL_ENUM:
        if (it->nnames <= 0) return 0;
        v = (float)(((int)v + dir + it->nnames) % it->nnames);
        break;

    case CTL_VAL_INT:
    case CTL_VAL_FLOAT: {
        float step = it->vstep > 0 ? it->vstep : 1.0f;
        v += dir * step;
        if (v < it->vmin) v = it->vmin;
        if (v > it->vmax) v = it->vmax;
        break;
    }

    default:
        return 0;
    }

    float before = ctl_get(&s->config, it);
    ctl_put(s, it, v);
    if (ctl_get(&s->config, it) == before) return 0;   /* already at the end */

    ctl_apply(s, it->apply);
    ctl_persist(s, it);
    return 1;
}

/*
 * Put a row back to the value synui ships with.
 *
 * Note this resets to the COMPILED default, not to whatever synuirc says — and
 * then drops the key from settings.state, at which point synuirc's line (if
 * there is one) takes over again at the next load. The two can therefore
 * disagree for the rest of the session, which is the honest outcome: synuirc is
 * parsed at startup and this panel cannot re-run it without discarding
 * everything else the session has changed.
 */
static int ctl_reset(syn_server_t *s, const struct ctl_item *it)
{
    if (!it || it->vtype == CTL_VAL_NONE) return 0;
    if (ctlpanel_row_is_default(s, it->row)) return 0;

    ctl_put(s, it, ctl_get(synui_config_defaults(), it));
    ctl_apply(s, it->apply);
    if (it->key) settings_state_clear(it->key);

    /* A row owned by filters.state or uifx.state gets its default WRITTEN
     * rather than dropped: those files are absolute records of what is on
     * screen, every key every time, so there is no "absent" for a synuirc line
     * to show through. Leaving the old value in the owner's file would make
     * this reset last exactly until the next config load. */
    ctl_store_write(s, it);
    return 1;
}

const char *ctlpanel_cat_name(int cat)
{
    switch (cat) {
    case CTL_CAT_APPEARANCE: return "Appearance";
    case CTL_CAT_WINDOWS:    return "Windows";
    case CTL_CAT_DESKTOP:    return "Desktop";
    case CTL_CAT_INPUT:      return "Input";
    case CTL_CAT_DISPLAY:    return "Display";
    case CTL_CAT_SOUND:      return "Sound";
    case CTL_CAT_NETWORK:    return "Network";
    case CTL_CAT_POWER:      return "Power";
    case CTL_CAT_SYSTEM:     return "System";
    case CTL_CAT_SHORTCUTS:  return "Shortcuts";
    default:                 return "?";
    }
}

int ctlpanel_cat_items(int cat, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < CTL_ITEM_COUNT && n < max; i++)
        if ((int)ctl_items[i].cat == cat) out[n++] = ctl_items[i].row;
    return n;
}

/* ── Search ──────────────────────────────────────────────────
 *
 * Case-insensitive substring, over the label, the section and the synuirc key.
 *
 * The key is in there deliberately. Someone who knows the setting as
 * `alt_tab_minimized` because they read it in synuirc should not have to guess
 * that the panel calls it "Alt+Tab reaches minimized" — and this is the panel
 * that just made every synuirc key reachable, so the file's vocabulary is
 * exactly what a user arrives holding.
 */
static int ctl_matches(const struct ctl_item *it, const char *needle)
{
    if (!needle || !*needle) return 1;

    const char *fields[3] = { it->label, it->section, it->key };
    for (int f = 0; f < 3; f++) {
        if (!fields[f]) continue;
        if (strcasestr(fields[f], needle)) return 1;
    }
    return 0;
}

int ctlpanel_visible_rows(syn_server_t *s, int *out, int max)
{
    /* Not searching: the selected category, exactly as before. */
    if (!s->ctlpanel.searching)
        return ctlpanel_cat_items(s->ctlpanel.cat, out, max);

    /* Searching: every category at once, in table order, so results stay
     * grouped the way the panel is rather than by how well they scored. A
     * relevance sort would move a row under the cursor as you typed. */
    int n = 0;
    for (int i = 0; i < CTL_ITEM_COUNT && n < max; i++) {
        if (ctl_items[i].cat == CTL_CAT_SHORTCUTS) continue;   /* not settings */
        if (ctl_matches(&ctl_items[i], s->ctlpanel.search))
            out[n++] = ctl_items[i].row;
    }
    return n;
}

syn_ctl_kind_t ctlpanel_row_kind(int row)
{
    int i = ctl_item_index(row);
    return i < 0 ? CTL_KIND_TOGGLE : ctl_items[i].kind;
}

static const char *ctl_row_action(int row)
{
    int i = ctl_item_index(row);
    return i < 0 ? NULL : ctl_items[i].action;
}

const char *ctlpanel_row_label(int row)
{
    int i = ctl_item_index(row);
    return i < 0 ? "?" : ctl_items[i].label;
}

/* The shortcuts category has no rows of its own — it is one scrolling list, and
 * the cursor there drives the scroll instead. -1 says so, and every caller that
 * would act on a row has to check it. */
int ctlpanel_selected_row(syn_server_t *s)
{
    int rows[CTL_CAT_ITEMS_MAX];
    int n = ctlpanel_visible_rows(s, rows, CTL_CAT_ITEMS_MAX);
    if (n == 0) return -1;

    int i = s->ctlpanel.item;
    if (i < 0)  i = 0;
    if (i >= n) i = n - 1;
    return rows[i];
}

/* synapd's current inference device, as recorded by synui-ai-backend. Absent
 * file means nothing has toggled it yet, so synapd's own auto-detect stands. */
static const char *ai_backend_label(void)
{
    FILE *f = fopen(SYNAPD_BACKEND_STATE, "r");
    if (!f) f = fopen(SYNAPD_BACKEND_STATE_LEGACY, "r");
    if (!f) return "auto";
    char b[16] = {0};
    size_t n = fread(b, 1, sizeof(b) - 1, f);
    fclose(f);
    while (n > 0 && (b[n - 1] == '\n' || b[n - 1] == ' ')) b[--n] = 0;
    if (strcmp(b, "gpu") == 0) return "GPU";
    if (strcmp(b, "cpu") == 0) return "CPU";
    if (strcmp(b, "off") == 0) return "off";
    return "auto";
}

/* Read the desktop-widget toggles the same way the bar does — straight out of
 * widgets.state. synui does not own that state (synui-widgets writes it and
 * quickshell watches it), so there is nothing in syn_server_t to read; the file
 * IS the state, and a missing file means nothing has been switched on yet.
 *
 * "partial" is a real answer, not a fudge: the widgets toggle independently
 * from the CLI, so "on"/"off" alone would misreport a desktop with only the
 * clock up. */
static const char *widgets_label(void)
{
    char path[256];
    if (!syn_config_path(path, sizeof(path), "widgets.state")) return "off";

    FILE *f = fopen(path, "r");
    if (!f) return "off";

    int on = 0, total = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        const char *eq = strchr(line, '=');
        if (!eq) continue;
        total++;
        /* Skip the spaces the writer puts either side of the '='. */
        const char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        if (strncmp(v, "on", 2) == 0) on++;
    }
    fclose(f);

    if (total == 0 || on == 0) return "off";
    return on == total ? "on" : "partial";
}

/* Event sounds, summarised the same way. Unlike the widgets this state IS
 * mirrored in syn_server_t (sound.c caches it to skip a fork per event), so the
 * summary is read from the cache — refreshed first, because the panel is one of
 * the few places that must show what is on disk right now. "off" covers both a
 * silent master switch and every event being off: from this row's height they
 * are the same desktop. */
static const char *sounds_label(syn_server_t *s)
{
    sound_state_refresh(s);
    if (!s->sound.enabled) return "off";

    int on = 0;
    for (int i = 0; i < SOUND_EVT_COUNT; i++)
        if (s->sound.on[i]) on++;

    if (on == 0)               return "off";
    if (s->sound.volume <= 0)  return "muted";
    return on == SOUND_EVT_COUNT ? "on" : "partial";
}

void ctlpanel_row_value(syn_server_t *s, int row, char *buf, size_t n)
{
    switch (row) {
    case CTL_ROW_EFFECTS:
        /* Two ways for the effects to be off, and they are not the same thing:
         * the master switch, or no GLES pass to switch on (pixman/VM). */
        if (!s->effects)      snprintf(buf, n, "n/a");
        else                  snprintf(buf, n, "%s", s->config.effects ? "on" : "off");
        break;
    case CTL_ROW_GAME:
        /* Two independent facts, and the row used to show only the second:
         * which of the three override positions is selected, and whether a
         * game is running right now. Auto is the only one where the second is
         * not implied by the first, so only auto spells it out. */
        if      (s->game.forced > 0) snprintf(buf, n, "always on");
        else if (s->game.forced < 0) snprintf(buf, n, "always off");
        else snprintf(buf, n, "auto (%s)", s->game.active ? "on" : "off");
        break;
    case CTL_ROW_AI_BACKEND:
        snprintf(buf, n, "%s", ai_backend_label());
        break;
    case CTL_ROW_LAYOUT: {
        /* Layout is per-DESKTOP, not global, so this row is about the desktop
         * you are on — the same one Super+Tab would cycle. Naming it keeps that
         * honest: on desktop 3 the row would otherwise look like a global
         * setting that mysteriously differs from desktop 2. */
        syn_workspace_t *ws = server_active_workspace(s);
        if (!ws) { snprintf(buf, n, "n/a"); break; }
        snprintf(buf, n, "%s \xc2\xb7 desktop %d",
                 layout_label(ws->layout), ws->index + 1);
        break;
    }
    case CTL_ROW_DOCK:
        snprintf(buf, n, "%s", s->config.dock_enabled ? "on" : "off");
        break;
    case CTL_ROW_BAR:
        snprintf(buf, n, "%s", s->config.bar_enabled ? "on" : "off");
        break;
    case CTL_ROW_DOCK_AUTOHIDE:
        /* "n/a" when the dock is off entirely: hide-behaviour is moot. */
        if (!s->config.dock_enabled) snprintf(buf, n, "n/a");
        else snprintf(buf, n, "%s", s->config.dock_autohide ? "on" : "off");
        break;
    case CTL_ROW_TITLEBARS:
        /* The row is the titlebars, not the hiding of them: "on" means shown. */
        snprintf(buf, n, "%s", s->titlebars_hidden ? "off" : "on");
        break;
    case CTL_ROW_LAUNCHER:
        snprintf(buf, n, "%s",
                 s->config.launcher_style == SYN_LAUNCHER_LOGO ? "logo" : "text");
        break;
    case CTL_ROW_TRANSPARENCY:
        /* Show the slider position too, so the row reads "on · 90%" — Left/Right
         * adjust it. Off hides the number: the levels are dormant then. */
        if (s->config.transparency)
            snprintf(buf, n, "on \xc2\xb7 %d%%",
                     (int)(s->config.active_opacity * 100 + 0.5f));
        else
            snprintf(buf, n, "off");
        break;
    case CTL_ROW_NIGHTLIGHT:
        /* The temperature only means anything while it is on — off is the
         * identity ramp, and "off · 4000K" would read as a warm screen. */
        if (s->config.night_light)
            snprintf(buf, n, "on \xc2\xb7 %dK", s->config.night_light_temp);
        else
            snprintf(buf, n, "off");
        break;
    case CTL_ROW_WIDGETS:
        snprintf(buf, n, "%s", widgets_label());
        break;
    case CTL_ROW_SOUNDS:
        snprintf(buf, n, "%s", sounds_label(s));
        break;
    case CTL_ROW_RECORD_AUDIO:
        /* Name the source, not just on/off: "on" alone is exactly the ambiguity
         * this setting exists to remove — desktop sound, not the microphone. */
        snprintf(buf, n, "%s",
                 s->config.record_audio ? "desktop sound" : "off");
        break;
    case CTL_ROW_RECORD_EDIT:
        /* Name the FORMAT, and name the cost. "on" would hide both the reason
         * to want it and the reason not to leave it on — a mezzanine fills a
         * disk in under an hour, and this row is where that is still cheap to
         * notice. */
        snprintf(buf, n, "%s",
                 s->config.record_edit ? "DNxHR ~1.1 GB/min" : "off (H.264 mp4)");
        break;
    case CTL_ROW_THEME:
        /* A jump-off, but showing the active theme here saves opening the panel
         * just to read which one is on. */
        snprintf(buf, n, "%s", theme_name(s->config.theme));
        break;
    case CTL_ROW_AI_MODEL:
        /* The one row whose choices are not synui's: they are the GGUFs in
         * synapd's models directory, so the list, the cursor and the loaded
         * marker all live in aimodel.c and this only asks. */
        aimodel_row_value(s, buf, n);
        break;
    default: {
        /* The table-driven rows, which is now most of them: read the field the
         * item names and format it by its type. A row with no `off` at all —
         * every jump-off — formats to nothing, which is what leaves the value
         * column empty for it. */
        const struct ctl_item *it = ctl_item(row);
        if (it && it->vtype != CTL_VAL_NONE) ctl_format(it, ctl_get(&s->config, it), 0, buf, n);
        else                                 buf[0] = '\0';
        break;
    }
    }
}

/*
 * The section a row is IN — not the one it starts.
 *
 * Only the first row of a section carries the name in the table, which is what
 * makes adding a row to an existing section a one-line change. Everything that
 * wants to say where a row lives (the breadcrumb, the search results) has to
 * walk back to find it, so that walk lives here rather than in each caller.
 */
const char *ctlpanel_row_section(int row)
{
    int i = ctl_item_index(row);
    if (i < 0) return NULL;

    syn_ctl_cat_t cat = ctl_items[i].cat;
    for (; i >= 0; i--) {
        if (ctl_items[i].cat != cat) break;   /* ran off the top of the category */
        if (ctl_items[i].section) return ctl_items[i].section;
    }
    return NULL;
}

/* Does this row OPEN a section? What the pane draws its dividing rule above. */
int ctlpanel_row_starts_section(int row)
{
    const struct ctl_item *it = ctl_item(row);
    return it && it->section ? 1 : 0;
}

const char *ctlpanel_row_help(int row)
{
    const struct ctl_item *it = ctl_item(row);
    return it ? it->help : NULL;
}

int ctlpanel_row_cat(int row)
{
    const struct ctl_item *it = ctl_item(row);
    return it ? (int)it->cat : -1;
}

/*
 * The synuirc key a row drives, or NULL for the rows that drive none.
 *
 * That NULL is the honest dividing line through this panel: a row with a key is
 * table-driven — read, adjusted, persisted and reset generically — and a row
 * without one is either a jump-off to another panel or a toggle whose state is
 * not a syn_config_t field at all (the AI backend is a file synapd writes, the
 * layout belongs to the desktop you are on). Callers that want to act on "the
 * settings, generically" have to be able to ask which is which.
 */
const char *ctlpanel_row_key(int row)
{
    const struct ctl_item *it = ctl_item(row);
    return (it && it->vtype != CTL_VAL_NONE) ? it->key : NULL;
}

/* ── Shortcuts column ────────────────────────────────────── */

/* What a bind action does, in words. An action with no entry here still lists —
 * it falls back to the action name — so a bind added to input.c and forgotten
 * here degrades to "slightly terse", not "missing from the panel". */
static const char *action_desc(const char *action, const char *arg)
{
    static const struct { const char *action, *desc; } tbl[] = {
        { "term",              "Terminal" },
        { "cmdbar",            "AI command bar" },
        { "ai_ask",            "Ask the AI about the window" },
        { "overlay",           "Neural overlay" },
        { "menu",              "Welcome menu" },
        { "control",           "Control panel" },
        { "keys",              "Keyboard shortcuts (this list)" },
        { "bluetooth",         "Bluetooth" },
        { "printers",          "Printers" },
        { "about",             "About OS" },
        { "overview",          "Mission control (all windows)" },   /* unbound: Alt+Tab */
        { "keybinds",          "Rebind a shortcut" },
        { "night_light",       "Night light" },
        { "record",            "Record screen" },
        { "clipboard",         "Clipboard history" },
        { "brightness_up",     "Brightness up" },
        { "brightness_down",   "Brightness down" },
        { "start_menu",        "Start menu" },
        { "launcher_style",    "Start button: text/logo" },
        { "close",             "Close window" },
        { "quit",              "Quit synui" },
        { "layout_cycle",      "Cycle layout" },
        { "retile",            "Tile this desktop" },
        { "cascade",           "Cascade this desktop (overlapping piles)" },
        { "float_arrange",     "Arrange floating windows" },
        { "master_shrink",     "Shrink master area" },
        { "master_grow",       "Grow master area" },
        { "column_consume",    "niri: pull window into the left column" },
        { "column_expel",      "niri: push window out to its own column" },
        { "focus_next",        "Focus next window" },
        { "focus_prev",        "Focus previous window" },
        { "alt_tab",           "Switch window (Alt+Tab)" },
        { "alt_tab_prev",      "Switch window, backwards" },
        { "stack_next",        "Move window down the stack" },
        { "stack_prev",        "Move window up the stack" },
        { "float_toggle",      "Float window" },
        { "fullscreen_toggle", "Fullscreen window" },
        { "maximize_toggle",   "Maximize window" },
        { "minimize_toggle",   "Minimize window" },
        { "minimize_restore",  "Restore minimized window" },
        { "decorations_toggle","Titlebars on/off" },
        { "displays",          "Display settings" },
        { "wallpaper",         "Wallpaper picker" },
        { "wallpaper_reload",  "Reload wallpaper / config" },
        { "filters",           "Visual effects (CRT + window)" },
        { "widgets",           "Desktop widget manager" },
        { "sounds",            "Event sounds" },
        { "effects_toggle",    "CRT effects on/off" },
        { "power",             "Power saving panel" },
        { "taskmgr",           "Task manager" },
        { "aimodel",           "AI model" },
        { "network",           "Network / Wi-Fi" },
        { "game",              "Game mode" },
        { "lock",              "Lock screen" },
        { "ai_backend",        "AI backend (GPU/CPU/off)" },
        { "move_output",       "Move window to next output" },
    };

    /* A spawn bind is only meaningful as the thing it spawns — for either
     * spelling of it. spawn_toggle rows read as the command too rather than as
     * "<command> (toggle)": the palette is a list of what the keys OPEN, and
     * the difference between the two is what the key does the second time. */
    if ((strcmp(action, "spawn") == 0 || strcmp(action, "spawn_toggle") == 0)
        && arg && *arg)
        return arg;

    for (unsigned i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++)
        if (strcmp(action, tbl[i].action) == 0) {
            /* move_output takes a direction; "prev" is a different line. */
            if (strcmp(action, "move_output") == 0 && arg && strcmp(arg, "prev") == 0)
                return "Move window to previous output";
            return tbl[i].desc;
        }
    return action;
}

/* The same table, for callers outside this file. The rebind helper needs it to
 * name the shortcut a chord is ALREADY taken by, and deriving that from the
 * shortcut list would mean searching a list of strings for the row that happens
 * to hold the same action — with the answer depending on which of the three
 * `spawn` rows it found first. */
const char *ctlpanel_action_desc(const char *action, const char *arg)
{
    return action_desc(action, arg);
}

/* xkbcommon spells keys for machines ("Return", "space", "e"). Spell them the
 * way a keycap does, or the column reads like a config file. */
static void key_name(xkb_keysym_t sym, char *out, size_t n)
{
    switch (sym) {
    case XKB_KEY_Return:    snprintf(out, n, "Enter");     return;
    case XKB_KEY_KP_Enter:  snprintf(out, n, "KP Enter");  return;
    case XKB_KEY_space:     snprintf(out, n, "Space");     return;
    case XKB_KEY_Escape:    snprintf(out, n, "Esc");       return;
    case XKB_KEY_Delete:    snprintf(out, n, "Del");       return;
    case XKB_KEY_BackSpace: snprintf(out, n, "Backspace"); return;
    case XKB_KEY_Tab:       snprintf(out, n, "Tab");       return;
    /* The cmdbar's key. xkbcommon spells it "equal", which is how it has to be
     * WRITTEN in a bind (the combo is split on '+', so a literal '=' cannot be
     * the key name) — but the keycap says '='. */
    case XKB_KEY_equal:     snprintf(out, n, "=");         return;
    default: break;
    }
    char raw[64] = {0};
    if (xkb_keysym_get_name(sym, raw, sizeof(raw)) <= 0) {
        snprintf(out, n, "?");
        return;
    }
    /* Single letters read as keycaps: "e" -> "E". */
    if (raw[1] == '\0' && raw[0] >= 'a' && raw[0] <= 'z')
        raw[0] = (char)(raw[0] - 'a' + 'A');
    snprintf(out, n, "%s", raw);
}

/* Modifier order matches how the binds are written in synuirc (super+shift+q),
 * so the panel and the config spell the same combo the same way. */
static void combo_str(uint32_t mods, xkb_keysym_t sym, char *out, size_t n)
{
    char key[64];
    key_name(sym, key, sizeof(key));
    snprintf(out, n, "%s%s%s%s%s",
             (mods & WLR_MODIFIER_LOGO)  ? "Super+" : "",
             (mods & WLR_MODIFIER_CTRL)  ? "Ctrl+"  : "",
             (mods & WLR_MODIFIER_ALT)   ? "Alt+"   : "",
             (mods & WLR_MODIFIER_SHIFT) ? "Shift+" : "",
             key);
}

/* The keycap word for a tap modifier, and "Off" for no tap at all. Here rather
 * than in config.c beside syn_tap_mod_name() for the reason combo_str() is here
 * beside syn_bind_format_combo(): that one spells what synuirc takes ("super"),
 * this one spells what a keyboard says ("Super"). Exported because keys.c's
 * status line names the new tap key and must not spell it a third way. */
const char *ctlpanel_tap_key_name(uint32_t mod)
{
    switch (mod) {
    case WLR_MODIFIER_LOGO:  return "Super";
    case WLR_MODIFIER_CTRL:  return "Ctrl";
    case WLR_MODIFIER_ALT:   return "Alt";
    case WLR_MODIFIER_SHIFT: return "Shift";
    default:                 return "Off";
    }
}

int ctlpanel_shortcuts(syn_server_t *s, syn_ctl_shortcut_t *out, int max)
{
    int n = 0;
    int saw_ws = 0, saw_movews = 0;

    /* The tap is the one shortcut that is not a bind — it is defined by the
     * absence of a chord (see syn_server::tap_armed), so it appears in no bind
     * table and would otherwise be the one feature this panel hid.
     *
     * Which modifier it is comes from the live config, not from the word
     * "Super": the palette can move it, and a row that always said Super would
     * be a list disagreeing with the keyboard. */
    if (n < max) {
        memset(&out[n], 0, sizeof(out[n]));
        if (s->config.tap_mod)
            snprintf(out[n].combo, sizeof(out[n].combo), "%s (tap)",
                     ctlpanel_tap_key_name(s->config.tap_mod));
        else
            snprintf(out[n].combo, sizeof(out[n].combo), "Off");
        /* What it opens comes from the live config too, for the same reason the
         * modifier does: `tap_action` can point the tap at rofi or the command
         * bar, and a row that always said "Start menu" would be a list
         * disagreeing with the keyboard on the other axis. */
        snprintf(out[n].desc,  sizeof(out[n].desc),  "%s",
                 action_desc(s->config.tap_action, s->config.tap_arg));
        /* No bind, but there IS an action behind it — the palette can run this
         * one even though no combo in the table produces it. */
        snprintf(out[n].action, sizeof(out[n].action), "%s",
                 s->config.tap_action);
        snprintf(out[n].arg,    sizeof(out[n].arg),    "%s", s->config.tap_arg);
        /* Rebindable, but to a bare modifier: `tap` is what tells the capture
         * loops that a Super press is the answer here and not a chord half. */
        out[n].rebindable = 1;
        out[n].tap        = 1;
        out[n].mods       = s->config.tap_mod;
        n++;
    }

    for (int i = 0; i < s->config.bind_count && n < max; i++) {
        const syn_bind_t *b = &s->config.binds[i];

        /* The nine workspace binds and the nine move-to-workspace binds are
         * collapsed into one row each below: listed one per line they are 18
         * of ~40 rows, and they bury everything else in the column. */
        if (strcmp(b->action, "ws") == 0)     { saw_ws = 1;     continue; }
        if (strcmp(b->action, "movews") == 0) { saw_movews = 1; continue; }

        memset(&out[n], 0, sizeof(out[n]));
        combo_str(b->mods, b->sym, out[n].combo, sizeof(out[n].combo));
        snprintf(out[n].desc, sizeof(out[n].desc), "%s",
                 action_desc(b->action, b->arg));
        snprintf(out[n].action, sizeof(out[n].action), "%s", b->action);
        snprintf(out[n].arg,    sizeof(out[n].arg),    "%s", b->arg);
        /* One bind, one chord — the only shape the rebind helper can move. */
        out[n].rebindable = 1;
        out[n].mods       = b->mods;
        out[n].sym        = b->sym;
        n++;
    }

    /* The two collapsed rows carry no action: "Super+1–9" is nine binds, and
     * running one of them would mean picking a workspace the row does not name.
     * The palette greys them out rather than guessing. */
    if (saw_ws && n < max) {
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].combo, sizeof(out[n].combo), "Super+1\xe2\x80\x93""9");
        snprintf(out[n].desc,  sizeof(out[n].desc),  "Switch to workspace");
        n++;
    }
    if (saw_movews && n < max) {
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].combo, sizeof(out[n].combo), "Super+Shift+1\xe2\x80\x93""9");
        snprintf(out[n].desc,  sizeof(out[n].desc),  "Move window to workspace");
        n++;
    }
    return n;
}

/* ── Panel ───────────────────────────────────────────────── */

/* Toggling a config float changes no scene node, so nothing would repaint on
 * its own. Force it, or the panel shows a state the screen disagrees with. */
static void ctlpanel_repaint(syn_server_t *s)
{
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (o->scene_output)
            wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
        wlr_output_schedule_frame(o->wlr_output);
    }
}

/* Redraw the panel if it happens to be up; a no-op otherwise.
 *
 * For state that changes from OUTSIDE the panel. ctlpanel_key() passes modified
 * combos straight through to the bind table on purpose — Super+C has to be able
 * to close the panel it opened — so Super+Tab cycles the layout while the
 * Layout row is on screen still reading the old value. The row is not wrong,
 * it is just painted; nothing in a bind's path knows to redraw a panel.
 *
 * synui_render_ctlpanel(), not ctlpanel_repaint(): the repaint only damages the
 * output so the existing panel buffer is re-composited, which is why the rows'
 * in-place toggles pair it with the render the key handler runs straight after.
 * A row's TEXT only changes when the panel is drawn again. */
void ctlpanel_refresh(syn_server_t *s)
{
    if (s && s->ctlpanel.visible) synui_render_ctlpanel(s);
}

/*
 * The AI-model row's settle timer has run down: ask synapd for the pick.
 *
 * Deliberately not the same machinery as the backend poll below. That one
 * watches a file for a value to change; this one waits for the USER to stop,
 * and what confirms it afterwards is a status poll from the daemon, not another
 * look at the row.
 */
static void ctlpanel_model_commit(syn_server_t *s)
{
    s->ctlpanel.model_commit_at = 0.0;

    if (aimodel_row_commit(s)) {
        /* Said here rather than left to the row's own "· loading": a model
         * swap stops the AI answering for as long as it takes, and that is
         * worth a sentence the first time someone flips this row. */
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "loading \xc2\xb7 the AI pauses until it is up");
    } else {
        /* Refused, or already loaded. aimodel.c has written the reason —
         * synapd's own words when it refused — and a blank one means there was
         * nothing to report (the cursor came back to the loaded model). */
        const char *why = aimodel_status_text(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status), "%s", why);
    }
    synui_render_ctlpanel(s);
}

/*
 * Called once per frame from output_frame. Returns 1 while it wants more frames.
 *
 * Two rows need it, and for different reasons. The AI-model row is waiting for
 * the cursor to settle before it commits a pick. The AI-backend row is waiting
 * for a value it does not own: it is read back from a file the synui-ai-backend
 * helper writes after restarting synapd, which lands whenever it lands, so the
 * panel has to look again rather than be told. Everything else on the panel is
 * state synui owns and changes synchronously, so the keypress that changed it
 * also repaints.
 *
 * Both stop the moment they are done, so an idle panel costs nothing.
 */
int ctlpanel_tick(syn_server_t *s)
{
    /* Closing the panel abandons both: there is nothing left to repaint, and a
     * pick the user walked away from is not one to act on. */
    if (!s->ctlpanel.visible) {
        s->ctlpanel.backend_poll_until = 0.0;
        s->ctlpanel.model_commit_at    = 0.0;
        return 0;
    }

    if (s->ctlpanel.model_commit_at != 0.0) {
        if (ctl_now_secs() >= s->ctlpanel.model_commit_at)
            ctlpanel_model_commit(s);
        else
            return 1;   /* still settling — keep the frames coming */
    }

    if (s->ctlpanel.backend_poll_until == 0.0)
        return 0;

    /* Only the AI backend polls now: it is the one row whose value is set by a
     * helper this panel cannot wait on. The desktop-widget row used to, back
     * when it flipped the widgets in place; it opens the manager instead. */
    const char *poll_name = "AI backend";

    char now_val[16];
    ctlpanel_row_value(s, s->ctlpanel.poll_row, now_val, sizeof(now_val));

    if (strcmp(now_val, s->ctlpanel.backend_before) != 0) {
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "%s: %s", poll_name, now_val);
        s->ctlpanel.backend_poll_until = 0.0;   /* landed — stop polling */
        synui_render_ctlpanel(s);
        return 0;
    }

    if (ctl_now_secs() >= s->ctlpanel.backend_poll_until) {
        /* Timed out. Say so rather than leaving "switching …" on screen for
         * good — a helper that failed (no polkit, synapd wedged) is exactly the
         * case where a stuck spinner reads as "it worked". */
        s->ctlpanel.backend_poll_until = 0.0;
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "%s: still %s \xc2\xb7 switch did not land", poll_name, now_val);
        synui_render_ctlpanel(s);
        return 0;
    }

    return 1;   /* keep the frames coming */
}

void ctlpanel_show(syn_server_t *s)
{
    /* Opened from the keyboard, so it answers to the keyboard — a windowed
     * panel you had to click before it would respond would be worse than the
     * modal one it replaced. */
    panel_take_kbd(s, SYN_PDRAG_CTLPANEL);
    s->ctlpanel.visible    = 1;
    s->ctlpanel.cat        = CTL_CAT_APPEARANCE;
    s->ctlpanel.item       = 0;
    s->ctlpanel.focus      = CTL_FOCUS_CATS;
    s->ctlpanel.scroll     = 0;
    s->ctlpanel.sc_sel     = 0;
    s->ctlpanel.sc_capturing = 0;
    s->ctlpanel.row_scroll = 0;
    s->ctlpanel.searching  = 0;
    s->ctlpanel.search[0]  = '\0';
    s->ctlpanel.search_len = 0;
    s->ctlpanel.status[0]  = '\0';
    s->ctlpanel.child[0]  = '\0';
    s->ctlpanel.backend_poll_until = 0.0;
    s->ctlpanel.model_commit_at    = 0.0;
    s->ctlpanel.poll_row  = CTL_ROW_AI_BACKEND;
    /* The AI-model row shows what synapd is running, which only arrives on a
     * status poll — ask for one, and put the row's cursor on the loaded model
     * so it opens on the truth rather than on the last thing the picker was
     * left on. */
    synmon_want_refresh(s);
    aimodel_row_sync(s);
    wlr_log(WLR_INFO, "synui: control panel shown");
    synui_render_ctlpanel(s);
}

void ctlpanel_hide(syn_server_t *s)
{
    s->ctlpanel.visible  = 0;
    /* Dismissing the panel abandons any return it was holding: the sub-panel may
     * still be up, and it closing later must not resurrect a panel the user has
     * already put away. */
    s->ctlpanel.child[0] = '\0';
    /* …and any pick still settling. Esc is how you back out of a cycle you did
     * not mean to start, so it must not fire the load on the way out. A switch
     * ALREADY sent keeps running: that one is synapd's now, not the panel's. */
    s->ctlpanel.model_commit_at = 0.0;
    /* …and any armed rebind. A capture that survived the close would make the
     * next chord typed at the desktop rebind a shortcut, which is the worst
     * shape of "it did something I did not ask for": silent, persistent, and
     * attributed to whatever you happened to press. */
    s->ctlpanel.sc_capturing = 0;
    synmon_want_refresh(s);
    synui_render_ctlpanel(s);
}

void ctlpanel_toggle(syn_server_t *s)
{
    if (s->ctlpanel.visible) ctlpanel_hide(s);
    else                     ctlpanel_show(s);
}

int ctlpanel_cat_from_name(const char *name)
{
    if (!name || !*name) return -1;
    for (int c = 0; c < CTL_CAT_COUNT; c++) {
        if (strcasecmp(name, ctlpanel_cat_name(c)) == 0) return c;
    }
    return -1;
}

/*
 * Open straight onto a category — what `synctl dispatch control display` does,
 * and with it the start menu's Settings submenu.
 *
 * That submenu used to be its own hand-written list of thirteen bind actions in
 * StartMenu.qml, which is the failure this file's header warns about in the
 * other direction: it had drifted to missing Theme, Printers, Task manager and
 * six more, and nothing could have told you. Naming a *category* leaves the
 * contents to the one item table, so the two menus cannot disagree about what
 * settings exist.
 *
 * Shows rather than toggles: a menu row that closed the panel when it happened
 * to be open already would be a row that does the opposite of what it says.
 */
void ctlpanel_show_cat(syn_server_t *s, const char *name)
{
    int cat = ctlpanel_cat_from_name(name);
    if (cat < 0) {                 /* unknown name: the plain front door */
        ctlpanel_toggle(s);
        return;
    }

    if (!s->ctlpanel.visible) ctlpanel_show(s);
    s->ctlpanel.cat    = cat;
    s->ctlpanel.item   = 0;
    s->ctlpanel.scroll = 0;
    s->ctlpanel.sc_sel = 0;
    s->ctlpanel.sc_capturing = 0;
    /* Focus lands in the rows: the caller already chose the category, and
     * putting the cursor back on the sidebar would make them choose it again. */
    s->ctlpanel.focus  = CTL_FOCUS_ITEMS;
    synui_render_ctlpanel(s);
}

/* Hide the panel *without* forgetting where the cursor was or which child is
 * out — the difference between "the user closed it" and "it stepped aside for
 * the panel it just opened". */
static void ctlpanel_conceal(syn_server_t *s)
{
    s->ctlpanel.visible = 0;
    /* The panel it is stepping aside for may want the synapd poll itself (the
     * model picker does); refresh rather than assume either way. */
    synmon_want_refresh(s);
    synui_render_ctlpanel(s);
}

/* Bring it back on the category and row it was left on, rather than resetting to
 * the top the way ctlpanel_show() does. */
static void ctlpanel_resume(syn_server_t *s, int cat, int item)
{
    s->ctlpanel.visible = 1;
    s->ctlpanel.cat     = cat;
    s->ctlpanel.item    = item;
    s->ctlpanel.focus   = CTL_FOCUS_ITEMS;
    synmon_want_refresh(s);
    /* Re-read the model list on the way back: the picker may have loaded one,
     * and its R key may have found models that appeared while it was open. The
     * row lands on whatever is LOADED, so browsing the picker without pressing
     * Enter leaves the row where it was — a look is not a choice. */
    aimodel_row_sync(s);
    synui_render_ctlpanel(s);
}

/* Is the panel `action` opens actually on screen? Asked immediately after firing
 * the action, to tell "it opened" from "it was already open and the toggle just
 * closed it" — only the first arms a return, and the second has to reopen this
 * panel or the keypress would look like it did nothing.
 *
 * A row whose panel is not listed here simply never arms a return. That is the
 * safe direction to fail: a missed hook costs one Esc, a wrong one pops the
 * control panel open at some unrelated moment. */
static int ctl_child_is_up(syn_server_t *s, const char *action)
{
    if (strcmp(action, "theme") == 0)     return s->thememgr.visible;
    if (strcmp(action, "wallpaper") == 0) return s->wppick.visible;
    if (strcmp(action, "cursor") == 0)    return s->curpick.visible;
    if (strcmp(action, "font") == 0)      return s->fontpick.visible;
    if (strcmp(action, "emoji") == 0)     return s->emoji.visible;
    if (strcmp(action, "equalizer") == 0) return s->eq.visible;
    if (strcmp(action, "filters") == 0)   return s->filters.visible;
    if (strcmp(action, "widgets") == 0)   return s->widgets.visible;
    if (strcmp(action, "displays") == 0)  return s->dispcfg.visible;
    if (strcmp(action, "clock") == 0)     return s->clock.visible;
    if (strcmp(action, "sounds") == 0)    return s->sound.visible;
    if (strcmp(action, "bluetooth") == 0) return s->bt.visible;
    if (strcmp(action, "power") == 0)     return s->power.visible;
    if (strcmp(action, "taskmgr") == 0)   return s->taskmgr.visible;
    if (strcmp(action, "aimodel") == 0)   return s->aimodel.visible;
    if (strcmp(action, "news") == 0)      return s->news.visible;
    if (strcmp(action, "clipboard") == 0) return s->clipboard.visible;
    if (strcmp(action, "keys") == 0)      return s->keys.visible;
    if (strcmp(action, "overview") == 0)  return s->overview.visible;
    return 0;
}

/* Step aside for the panel that owns this setting, and arrange to come back. */
static void ctlpanel_open_child(syn_server_t *s, const char *action)
{
    int cat = s->ctlpanel.cat, item = s->ctlpanel.item;

    s->ctlpanel.child[0] = '\0';   /* nothing armed while the action runs */
    ctlpanel_conceal(s);
    synui_binding_execute(s, action, NULL);

    if (!ctl_child_is_up(s, action)) {
        /* The action toggled a panel that was already open, so it is now shut
         * and there is nothing to come back from. Come back immediately. */
        ctlpanel_resume(s, cat, item);
        return;
    }

    snprintf(s->ctlpanel.child, sizeof(s->ctlpanel.child), "%s", action);
    s->ctlpanel.child_cat  = cat;
    s->ctlpanel.child_item = item;
}

void ctlpanel_child_closed(syn_server_t *s, const char *action)
{
    if (!action || s->ctlpanel.child[0] == '\0') return;
    if (strcmp(s->ctlpanel.child, action) != 0) return;

    s->ctlpanel.child[0] = '\0';

    /* A lock that came down while a sub-panel was open closes it as part of
     * clearing the screen. Popping the control panel up behind the lock screen
     * would leave it there for whoever unlocks. */
    if (s->locked) return;

    ctlpanel_resume(s, s->ctlpanel.child_cat, s->ctlpanel.child_item);
}

/* ── Navigation ──────────────────────────────────────────── */

static int ctlpanel_item_count(syn_server_t *s)
{
    int rows[CTL_CAT_ITEMS_MAX];
    return ctlpanel_visible_rows(s, rows, CTL_CAT_ITEMS_MAX);
}

/*
 * Keep the cursor inside the drawn window.
 *
 * Categories used to fit on screen whole, so there was nothing to scroll and
 * the cursor was always visible by construction. Windows has forty rows now.
 * Called after anything that moves the cursor or changes the list under it.
 */
static void ctlpanel_scroll_to_cursor(syn_server_t *s)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;
    int n = ctlpanel_item_count(s);

    int max_scroll = n - CTL_ROW_ROWS;
    if (max_scroll < 0) max_scroll = 0;

    if (cp->item < cp->row_scroll)                    cp->row_scroll = cp->item;
    if (cp->item > cp->row_scroll + CTL_ROW_ROWS - 1) cp->row_scroll = cp->item - CTL_ROW_ROWS + 1;

    if (cp->row_scroll > max_scroll) cp->row_scroll = max_scroll;
    if (cp->row_scroll < 0)          cp->row_scroll = 0;
}

/* Moving to another category resets the row cursor and the shortcuts scroll:
 * carrying row 4 into a category with two rows, or a scroll offset into a list
 * that is not the one it was measured against, are the two ways this drifts. */
/*
 * Moving off the AI-model row drops whatever pick was settling on it.
 *
 * The settle timer is the only thing on this panel that acts a moment after the
 * key that armed it, so it is the only one that can fire at a row you have
 * already left. Cycling to a model, thinking better of it and pressing Down
 * must not load it from three rows away — leaving is how you say no.
 *
 * A switch already SENT is not affected: that one belongs to synapd now.
 */
static void ctlpanel_cancel_pending(syn_server_t *s)
{
    if (s->ctlpanel.model_commit_at == 0.0) return;
    s->ctlpanel.model_commit_at = 0.0;
    s->ctlpanel.status[0] = '\0';
    /* Put the row back on the model that is actually loaded, so an abandoned
     * cycle does not leave the panel naming one that never got asked for. */
    aimodel_row_sync(s);
}

static void ctlpanel_set_cat(syn_server_t *s, int cat)
{
    if (cat < 0 || cat >= CTL_CAT_COUNT || cat == s->ctlpanel.cat) return;
    ctlpanel_cancel_pending(s);
    s->ctlpanel.cat        = cat;
    s->ctlpanel.item       = 0;
    s->ctlpanel.scroll     = 0;
    s->ctlpanel.sc_sel     = 0;
    /* An armed capture belongs to a row in the category being left. Leaving it
     * armed would make the next chord rebind a shortcut that is no longer on
     * screen — the same reason ctlpanel_hide() drops it. */
    s->ctlpanel.sc_capturing = 0;
    s->ctlpanel.row_scroll = 0;
    /* Moving to a category is the other way of saying "not that search". The
     * results list spans every category, so leaving it open while the sidebar
     * moved would show a highlighted category whose rows are not on screen. */
    s->ctlpanel.searching  = 0;
    s->ctlpanel.search[0]  = '\0';
    s->ctlpanel.search_len = 0;
}

static void ctlpanel_move(syn_server_t *s, int dir)
{
    if (s->ctlpanel.focus == CTL_FOCUS_CATS) {
        ctlpanel_set_cat(s, s->ctlpanel.cat + dir);
        return;
    }

    int n = ctlpanel_item_count(s);
    if (n == 0) return;                    /* shortcuts: Up/Down scroll instead */

    int next = s->ctlpanel.item + dir;
    if (next < 0 || next >= n) return;     /* stop at the ends, as before */
    ctlpanel_cancel_pending(s);
    s->ctlpanel.item = next;
    ctlpanel_scroll_to_cursor(s);
}

/* Enter the row pane. Refused for a category with nothing in the pane at all, so
 * focus can never sit in a column with nothing to drive.
 *
 * The shortcuts category has no *rows* but is not empty — it is one scrolling
 * list, and focus there is what puts Up/Down on the scroll. Gating purely on the
 * row count locked that category out of the pane entirely. */
static void ctlpanel_focus_items(syn_server_t *s)
{
    if (ctlpanel_item_count(s) == 0 && s->ctlpanel.cat != CTL_CAT_SHORTCUTS)
        return;
    s->ctlpanel.focus = CTL_FOCUS_ITEMS;
}

static void ctlpanel_activate(syn_server_t *s)
{
    int row = ctlpanel_selected_row(s);
    if (row < 0) return;

    /* Every row that hands off does it the same way, by kind, so a new panel row
     * is one table line rather than another case here. Only the in-place toggles
     * below need to know which row they are. */
    switch (ctlpanel_row_kind(row)) {
    case CTL_KIND_CHOICE:
        /* Enter is the way through to the panel that owns the setting, not a
         * second way to commit the pick — Left/Right already did that. A pick
         * still settling is dropped rather than fired on the way out: the panel
         * being opened is where you go to see what a model IS before loading
         * it, so loading it first would answer the question by doing the thing.
         */
        s->ctlpanel.model_commit_at = 0.0;
        ctlpanel_open_child(s, ctl_row_action(row));
        return;
    case CTL_KIND_PANEL:
        ctlpanel_open_child(s, ctl_row_action(row));
        return;
    case CTL_KIND_LAUNCH:
    case CTL_KIND_ACTION:
        ctlpanel_hide(s);
        synui_binding_execute(s, ctl_row_action(row), NULL);
        return;
    case CTL_KIND_VALUE: {
        /* Enter on a number steps it forward, the same as Right. Not "nothing":
         * every other row on the panel does something on Enter, and a row that
         * ignored the key people press first would read as broken. Left/Right
         * remain the way to move it in both directions. */
        const struct ctl_item *it = ctl_item(row);
        if (it && ctl_adjust(s, it, +1)) {
            char v[64];
            ctlpanel_row_value(s, row, v, sizeof(v));
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "%s: %s", it->label, v);
        }
        return;
    }
    default:
        break;
    }

    /* A table-driven TOGGLE: flip it generically. Checked before the bespoke
     * switch below so that a row which names a config field never needs a case
     * there — the switch is now only for the toggles whose state is NOT a plain
     * syn_config_t field (game mode, the AI backend, the per-desktop layout). */
    {
        const struct ctl_item *it = ctl_item(row);
        if (it && it->vtype != CTL_VAL_NONE) {
            if (ctl_adjust(s, it, +1)) {
                char v[64];
                ctlpanel_row_value(s, row, v, sizeof(v));
                snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                         "%s: %s", it->label, v);
            }
            return;
        }
    }

    switch (row) {
    case CTL_ROW_EFFECTS:
        if (!s->effects) {   /* no GLES pass — say so rather than lie */
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "no GLES renderer \xc2\xb7 effects unavailable here");
            return;
        }
        s->config.effects = !s->config.effects;
        /* The one bespoke row of the CRT set, so it needs the store call the
         * table-driven ones get from ctl_persist(). Without it this toggle was
         * session-only — and worse than that once filters.state is read on
         * reload, because then the stale `enabled=` in that file put the shader
         * back on the next time anything reloaded the config. */
        filters_state_save(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "CRT effects %s", s->config.effects ? "on" : "off");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_GAME:
        game_toggle(s);
        /* Say what was SELECTED, not what happened to result: cycling onto
         * "always off" with no game running changed nothing observable, and a
         * status line reading "game mode off" made that look like the row had
         * refused rather than moved. */
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 s->game.forced > 0 ? "game mode: always on" :
                 s->game.forced < 0 ? "game mode: always off" :
                 s->game.active     ? "game mode: auto \xc2\xb7 a game is running"
                                    : "game mode: auto \xc2\xb7 no game running");
        return;

    case CTL_ROW_LAYOUT: {
        /* Enter cycles, exactly as Super+Tab does — and through the bind action,
         * not a private copy of it, so this row inherits the reclaim on
         * selecting tiling/AI and cannot drift from the key. Forward-only for
         * the same reason the AI backend row is: Left/Right already mean "move
         * columns" here, and only the slider row may take them.
         *
         * The desktop under the panel really does re-tile while you watch,
         * which is the point of putting it here rather than only on a key. */
        synui_binding_execute(s, "layout_cycle", NULL);
        syn_workspace_t *ws = server_active_workspace(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "layout: %s", ws ? layout_label(ws->layout) : "?");
        ctlpanel_repaint(s);
        return;
    }

    /*
     * The bar, which unlike every other row on this panel is not ours to draw.
     *
     * So the flag is only the bookkeeping — the actual work is running one of
     * the two commands, and the flag exists so that reopening the panel says
     * what the desktop looks like rather than always "on".
     *
     * Fire-and-forget through synui_spawn, like every other shell-out here. A
     * start command that is wrong for this desktop (the default names the
     * shipped bar, and this box may run waybar) therefore fails silently — the
     * help line on the row is where that is said, because there is nothing to
     * wait for and nothing to report.
     */
    case CTL_ROW_BAR: {
        s->config.bar_enabled = !s->config.bar_enabled;

        const char *cmd = s->config.bar_enabled ? s->config.bar_start_cmd
                                                : s->config.bar_stop_cmd;
        if (cmd && *cmd) synui_spawn(cmd);

        settings_state_set("bar_enabled", s->config.bar_enabled ? "on" : "off");
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "bar %s", s->config.bar_enabled ? "on" : "off");
        ctlpanel_repaint(s);
        return;
    }
    case CTL_ROW_DOCK:
        s->config.dock_enabled = !s->config.dock_enabled;
        dock_rebuild(s);
        dock_relayout(s);
        /* PERSISTED, which it was not until now: the row turned the dock off,
         * the dock went away, and the next login brought it straight back —
         * which reads as the setting not working rather than as the setting
         * not being saved. The table-driven rows get this for free (see
         * ctl_adjust); the bespoke ones have to ask, and this one never did.
         * "on"/"off" is the spelling config_parse_kv reads back. */
        settings_state_set("dock_enabled", s->config.dock_enabled ? "on" : "off");
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "dock %s", s->config.dock_enabled ? "on" : "off");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_DOCK_AUTOHIDE:
        /* No-op while the dock is off — the row reads "n/a" then, and toggling
         * a hidden dock's hide-behaviour would only confuse. */
        if (!s->config.dock_enabled) {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "dock is off");
            ctlpanel_repaint(s);
            return;
        }
        s->config.dock_autohide = !s->config.dock_autohide;
        dock_state_save(s);   /* persist to dock.state, like edge/pins */
        dock_wake(s);         /* pin or release the bar on the next frame */
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "dock auto-hide %s", s->config.dock_autohide ? "on" : "off");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_RECORD_AUDIO:
        /* Takes effect on the NEXT Super+Shift+R: the flag is read where the
         * action spawns synui-record, and wf-recorder cannot grow a track
         * mid-take. Say so, so a flip during a recording is not mistaken for
         * one that applied to it. */
        record_audio_toggle(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 s->config.record_audio
                     ? "recordings capture desktop sound"
                     : "recordings are silent");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_RECORD_EDIT:
        /* Same next-take-only contract as the audio row. The status names the
         * editor, because "DNxHR" answers a question nobody asked unless they
         * already know why the mp4 would not import. */
        record_edit_toggle(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 s->config.record_edit
                     ? "recordings open in a video editor · ~1.1 GB/min"
                     : "recordings are H.264 mp4 · small, share anywhere");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_TITLEBARS:
        /* Same call the Super+Shift+D bind makes — it re-runs the layout for
         * every view, which a bare repaint here would not: the clients have to
         * be resized for the titlebar that just came or went. */
        deco_toggle_titlebars(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "titlebars %s", s->titlebars_hidden ? "off" : "on");
        return;

    case CTL_ROW_LAUNCHER:
        /* Same call the start-menu Settings row and any bind make: it flips the
         * style, redraws the button on every output, and persists to
         * launcher.state. Repaint after so this row's value reads the new style. */
        launcher_toggle_style(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "start button: %s",
                 s->config.launcher_style == SYN_LAUNCHER_LOGO ? "logo" : "text");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_TRANSPARENCY:
        /* Enter flips the master switch; Left/Right (below) drive the level. The
         * helper re-pushes alpha to every window and persists, and turns a still-
         * opaque desktop visibly translucent so "on" is not a no-op. */
        transparency_set_enabled(s, !s->config.transparency);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 s->config.transparency ? "transparency on \xc2\xb7 %d%%" : "transparency off",
                 (int)(s->config.active_opacity * 100 + 0.5f));
        return;

    case CTL_ROW_NIGHTLIGHT:
        /* Same call the bind makes: it re-commits the colour transform on every
         * output, so there is nothing to repaint by hand here. */
        nightlight_toggle(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "night light %s", s->config.night_light ? "on" : "off");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_AI_BACKEND:
        /* The helper owns the work (systemd drop-in, restart synapd) and it is
         * not instant, so the row still reads the old device when this returns.
         * Poll the panel for a few seconds so the new value appears on its own
         * — see backend_poll_until. Without this the row only refreshed when
         * some other keypress happened to repaint the panel. */
        ctlpanel_row_value(s, CTL_ROW_AI_BACKEND, s->ctlpanel.backend_before,
                           sizeof(s->ctlpanel.backend_before));
        synui_binding_execute(s, "ai_backend", NULL);
        s->ctlpanel.poll_row = CTL_ROW_AI_BACKEND;
        s->ctlpanel.backend_poll_until = ctl_now_secs() + CTL_BACKEND_POLL_SECS;
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "switching AI backend \xe2\x80\xa6");
        return;

    default:
        return;
    }
}

/* How many shortcuts the pane is listing. */
int ctlpanel_shortcut_count(syn_server_t *s)
{
    syn_ctl_shortcut_t sc[CTL_SHORTCUTS_MAX];
    return ctlpanel_shortcuts(s, sc, CTL_SHORTCUTS_MAX);
}

/* Copy out the selected shortcut. Returns 0 when the list is empty or the
 * cursor is somehow off the end of it.
 *
 * By value, not by pointer: every caller is about to rewrite the bind table
 * this row was derived from, and the pane rebuilds the list from that table on
 * every render. A pointer would be stale before it was used. */
int ctlpanel_shortcut_selected(syn_server_t *s, syn_ctl_shortcut_t *out)
{
    syn_ctl_shortcut_t sc[CTL_SHORTCUTS_MAX];
    int n = ctlpanel_shortcuts(s, sc, CTL_SHORTCUTS_MAX);
    int i = s->ctlpanel.sc_sel;
    if (n <= 0 || i < 0 || i >= n) return 0;
    *out = sc[i];
    return 1;
}

/* Keep the shortcuts cursor on screen — the pane draws CTL_SHORTCUT_ROWS at a
 * time, so a cursor outside that window is a highlight you cannot see. */
static void ctlpanel_shortcut_scroll_to_sel(syn_server_t *s)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;
    int n = ctlpanel_shortcut_count(s);

    if (cp->sc_sel >= n) cp->sc_sel = n > 0 ? n - 1 : 0;
    if (cp->sc_sel < 0)  cp->sc_sel = 0;

    if (cp->sc_sel < cp->scroll) cp->scroll = cp->sc_sel;
    if (cp->sc_sel >= cp->scroll + CTL_SHORTCUT_ROWS)
        cp->scroll = cp->sc_sel - CTL_SHORTCUT_ROWS + 1;

    int max_scroll = n - CTL_SHORTCUT_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    if (cp->scroll > max_scroll) cp->scroll = max_scroll;
    if (cp->scroll < 0) cp->scroll = 0;
}

/* Move the shortcuts cursor, dragging the scroll behind it.
 *
 * This used to move `scroll` directly — the pane was read-only, so there was
 * nothing to point at and scrolling was the only thing Up/Down could mean.
 * Rebinding needs a row, and a cursor that the rest of the panel already has on
 * every other pane is the least surprising place to put one. */
static void ctlpanel_scroll_by(syn_server_t *s, int dir)
{
    s->ctlpanel.sc_sel += dir;
    ctlpanel_shortcut_scroll_to_sel(s);
}

/* ── Rebinding from this pane ────────────────────────────────
 *
 * The same three keys as the palette (F2 or Ctrl+R to arm, Ctrl+Shift+R to put
 * everything back), driving the same rules in keys.c. What lives here is only
 * which row is armed and what the footer says about it — see syn_rebind_apply().
 *
 * Rebinding was reachable only from Super+/ for one pkgrel, which put the panel
 * that has a whole category called "Shortcuts" in the position of showing you
 * every binding and letting you change none of them.
 */
static void ctlpanel_rebind_begin(syn_server_t *s)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;

    syn_ctl_shortcut_t sc;
    if (!ctlpanel_shortcut_selected(s, &sc)) return;

    const char *refusal = syn_rebind_refusal(&sc);
    if (refusal) {
        snprintf(cp->status, sizeof(cp->status), "%s", refusal);
        return;
    }

    cp->sc_capturing = 1;
    cp->sc_capture   = sc;
    cp->status[0]    = '\0';
}

/* The captured chord. The cursor is left where it was: the row it is on is the
 * one that just changed, and moving it would hide the result of the edit. */
static void ctlpanel_rebind_finish(syn_server_t *s, xkb_keysym_t sym,
                                   uint32_t mods)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;

    cp->sc_capturing = 0;
    syn_rebind_apply(s, &cp->sc_capture, sym, mods,
                     cp->status, sizeof(cp->status));

    /* The list is rebuilt by the next render either way, so nothing to
     * re-snapshot — but the cursor can now be off the end of a shorter list if
     * the rebind collapsed two rows into one. */
    ctlpanel_shortcut_scroll_to_sel(s);
}

/* F3: put the selected row's action on the modifier tap. No capture state to
 * keep — the row is the answer — so this is the whole of it here, and the rules
 * are keys.c's like the rest of the rebind path. */
static void ctlpanel_tap_action_set(syn_server_t *s)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;

    syn_ctl_shortcut_t sc;
    if (!ctlpanel_shortcut_selected(s, &sc)) return;

    syn_rebind_set_tap_action(s, &sc, cp->status, sizeof(cp->status));
}

static void ctlpanel_rebind_reset_all(syn_server_t *s)
{
    syn_rebind_reset_all(s, s->ctlpanel.status, sizeof(s->ctlpanel.status));
    /* The default table is a different length from the one that was on screen. */
    ctlpanel_shortcut_scroll_to_sel(s);
}

/* Left/Right on the Transparency row are a slider: nudge the focused-window
 * opacity in 5% steps, turning transparency on first if it is off (you would not
 * be adjusting it otherwise). Everywhere else Left/Right change column, and Tab
 * does that from here too — a slider row is not a dead end. */
static void ctlpanel_adjust_opacity(syn_server_t *s, int dir)
{
    if (!s->config.transparency) transparency_set_enabled(s, 1);
    transparency_set_opacity(s, s->config.active_opacity + dir * 0.05f);
    snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
             "transparency %d%%", (int)(s->config.active_opacity * 100 + 0.5f));
}

/* Left/Right on a CHOICE row step through its options. Only the AI-model row is
 * one, and it is the only setting on the panel whose options are not synui's to
 * enumerate — so the move goes to the code that owns the list, and this arms the
 * settle timer that turns "the cursor stopped here" into "load this". */
/*
 * Left/Right on a table-driven row, and the status line that goes with it.
 *
 * The value is echoed into the footer rather than left to the row's own text
 * because the row may be off the top or bottom of a scrolled pane by the time
 * a key repeat has been held — and because "Border width: 7 px" says which
 * setting moved, which a number changing somewhere in a list of forty does not.
 */
static int ctlpanel_adjust_value(syn_server_t *s, int row, int dir)
{
    const struct ctl_item *it = ctl_item(row);
    if (!it || it->vtype == CTL_VAL_NONE) return 0;

    if (!ctl_adjust(s, it, dir)) {
        /* Already at the end of the range. Say so once rather than leaving the
         * previous message up, which reads as the key having done something. */
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "%s is at its %s", it->label, dir < 0 ? "minimum" : "maximum");
        return 1;
    }

    char v[64];
    ctlpanel_row_value(s, row, v, sizeof(v));
    if (ctlpanel_row_is_default(s, row))
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "%s: %s \xc2\xb7 default", it->label, v);
    else
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "%s: %s", it->label, v);
    return 1;
}

static void ctlpanel_adjust_choice(syn_server_t *s, int row, int dir)
{
    if (row != CTL_ROW_AI_MODEL) return;

    if (!aimodel_row_cycle(s, dir)) {
        /* Nothing to cycle, or a switch already in flight. aimodel.c wrote why
         * in its own status; an empty one means the directory is empty, which
         * it does not consider worth a sentence but this row does. */
        const char *why = aimodel_status_text(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status), "%s",
                 why[0] ? why : "no models in /var/lib/synapd/models");
        s->ctlpanel.model_commit_at = 0.0;
        return;
    }

    s->ctlpanel.model_commit_at = ctl_now_secs() + CTL_MODEL_SETTLE_SECS;
    /* Says what is about to happen, because it is about to happen on its own —
     * a row that changed under the cursor and then loaded several GB with no
     * warning is the one thing this affordance must not feel like. */
    snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
             "loads when you stop \xc2\xb7 Esc cancels");
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * The panel a user is most likely to open first was the one with no pointer at
 * all: every row here says "Enter activate" and none of them could be clicked.
 *
 * The contract is the one every panel in synui now follows, so that knowing one
 * of them is knowing all of them:
 *
 *   - Moving over a row selects it, and over the sidebar selects the category
 *     *and* moves focus there — hovering the categories while focus is stuck in
 *     the rows would highlight a column that the keys are not driving, which is
 *     precisely the confusion Tab exists to avoid.
 *   - A left click does what Enter does on that row. Not "select, then press
 *     Enter": a settings row that needs two gestures to flip is the reason
 *     people say a panel is keyboard-only.
 *   - A click anywhere off the panel closes it, exactly like Esc from the
 *     category column.
 *   - The wheel scrolls the shortcuts list and moves the selection everywhere
 *     else, which is the same split Up/Down already have.
 *
 * Everything routes through ctlpanel_activate()/ctlpanel_move(), the functions
 * the keys call. A pointer path with its own copy of "what this row does" is a
 * second definition of the item table, and the whole point of this panel is
 * that there is only one.
 */

int ctlpanel_motion(syn_server_t *s, double lx, double ly)
{
    /* A windowed panel does not own the pointer: off it, the event belongs
     * to whatever is under the cursor, so take nothing. This is what stops
     * an open panel freezing the rest of the desktop. */
    if (s->ctlpanel.visible && panel_is_windowed(s, SYN_PDRAG_CTLPANEL) &&
        !hit_in_panel(&s->ctlpanel.hit, lx, ly))
        return 0;

    syn_ctlpanel_t *cp = &s->ctlpanel;
    if (!cp->visible) return 0;

    int cat = hit_row_at(&cp->hit, lx, ly);
    if (cat >= 0 && cat < CTL_CAT_COUNT) {
        if (cat == cp->cat && cp->focus == CTL_FOCUS_CATS) return 1;
        ctlpanel_set_cat(s, cat);
        cp->focus = CTL_FOCUS_CATS;
        synui_render_ctlpanel(s);
        return 1;
    }

    /* The grid is the DRAWN rows, so its index is an offset into the visible
     * window, not into the category. Adding the scroll is what makes a click
     * land on the row under the pointer once a long category has been scrolled
     * — without it every click acts on the row that many places above. */
    int i = hit_row_at(&cp->hit_items, lx, ly);
    if (i >= 0) {
        i += cp->row_scroll;
        if (i >= ctlpanel_item_count(s)) return 1;   /* past the last row */
        if (i == cp->item && cp->focus == CTL_FOCUS_ITEMS) return 1;
        /* Same rule as the keys: the pointer leaving the row drops a pick that
         * was settling on it. This sets cp->item directly rather than going
         * through ctlpanel_move(), so it has to say so itself. */
        if (i != cp->item) ctlpanel_cancel_pending(s);
        cp->item  = i;
        cp->focus = CTL_FOCUS_ITEMS;
        synui_render_ctlpanel(s);
    }
    return 1;
}

int ctlpanel_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    syn_ctlpanel_t *cp = &s->ctlpanel;
    if (!cp->visible) return 0;

    if (hit_in_close(&cp->hit, lx, ly)) {
        ctlpanel_hide(s);
        return 1;
    }

    /* The header is the grab handle in window mode. Before the row hit-tests,
     * since the header is chrome and owns the press outright. */
    if (button == BTN_LEFT && hit_in_drag(&cp->hit, lx, ly)) {
        panel_take_kbd(s, SYN_PDRAG_CTLPANEL);
        panel_drag_begin(s, SYN_PDRAG_CTLPANEL, lx, ly);
        return 1;
    }

    if (!hit_in_panel(&cp->hit, lx, ly)) {
        switch (panel_mode(s, SYN_PDRAG_CTLPANEL)) {
        case SYN_PANEL_CLOSE_CLICKOFF: ctlpanel_hide(s); return 1;
        case SYN_PANEL_CLOSE_WINDOW:
            panel_drop_kbd(s);
            synui_render_ctlpanel(s);
            return 0;          /* the click belongs to whatever is under it */
        default: return 1;
        }
    }

    if (panel_is_windowed(s, SYN_PDRAG_CTLPANEL) && !cp->win.kbd)
        panel_take_kbd(s, SYN_PDRAG_CTLPANEL);

    /* Put the cursor where the pointer is first, so the activation below acts on
     * the row that was pointed at even if no motion event preceded this press
     * (a tap, a tablet, a warped cursor). */
    ctlpanel_motion(s, lx, ly);

    if (button != BTN_LEFT) return 1;   /* swallowed: the panel is modal */

    /* In the sidebar a click opens the category — the same "step into the
     * submenu" Enter and Right mean there. Only in the row pane does it fire. */
    if (hit_row_at(&cp->hit, lx, ly) >= 0) {
        ctlpanel_focus_items(s);
        synui_render_ctlpanel(s);
        return 1;
    }

    if (hit_row_at(&cp->hit_items, lx, ly) >= 0) {
        ctlpanel_activate(s);
        /* A row that handed off has already hidden this panel and opened
         * another; re-rendering would draw this one back over the top of it. */
        if (cp->visible) synui_render_ctlpanel(s);
    }
    return 1;
}

int ctlpanel_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;
    if (!cp->visible) return 0;
    if (delta == 0) return 1;

    int dir = delta > 0 ? 1 : -1;

    /* Which column the wheel drives is decided by where the POINTER is, not by
     * which column has keyboard focus. On a two-column panel those are routinely
     * different — focus follows Tab, the pointer follows the hand — and a wheel
     * that scrolled the far column while sitting over this one would be the
     * single most confusing thing in the panel. Off the panel entirely (the
     * chrome, the footer) the focused column is the only sensible answer. */
    int over_cats = hit_row_at(&cp->hit, lx, ly) >= 0;
    int over_rows = hit_in_panel(&cp->hit_items, lx, ly) && !over_cats;

    if (over_cats) {
        ctlpanel_set_cat(s, cp->cat + dir);
        cp->focus = CTL_FOCUS_CATS;
    } else if (cp->cat == CTL_CAT_SHORTCUTS && over_rows) {
        /* The shortcuts pane is one long list with no rows to step through, so
         * there the wheel is the scroll — the same thing Up/Down do in it. */
        ctlpanel_scroll_by(s, dir * 3);
    } else if (over_rows) {
        cp->focus = CTL_FOCUS_ITEMS;
        /* Over a category too long to fit, the wheel SCROLLS rather than moving
         * the selection: dragging the cursor down forty rows to read the bottom
         * of Windows is not what a wheel means anywhere else. The selection
         * still follows the pointer, because motion events land on the rows the
         * scroll brought under it. */
        if (ctlpanel_item_count(s) > CTL_ROW_ROWS) {
            int max_scroll = ctlpanel_item_count(s) - CTL_ROW_ROWS;
            cp->row_scroll += dir * 3;
            if (cp->row_scroll > max_scroll) cp->row_scroll = max_scroll;
            if (cp->row_scroll < 0)          cp->row_scroll = 0;
        } else {
            ctlpanel_move(s, dir);
        }
    } else if (cp->focus == CTL_FOCUS_ITEMS && ctlpanel_item_count(s) == 0) {
        ctlpanel_scroll_by(s, dir * 3);
    } else {
        ctlpanel_move(s, dir);
    }

    synui_render_ctlpanel(s);
    return 1;
}

/* Drop the search and go back to showing a category. */
static void ctlpanel_search_close(syn_server_t *s)
{
    s->ctlpanel.searching  = 0;
    s->ctlpanel.search[0]  = '\0';
    s->ctlpanel.search_len = 0;
    s->ctlpanel.item       = 0;
    s->ctlpanel.row_scroll = 0;
}

/*
 * Typing, while the search box is open.
 *
 * Runs BEFORE the modifier check below, which is why Shift+letter reaches it:
 * that check exists so Super+C can close the panel it opened, and it would
 * otherwise send every capital letter to the global bind table mid-word.
 *
 * Returns 1 if the key was consumed.
 */
static int ctlpanel_search_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_ctlpanel_t *cp = &s->ctlpanel;
    if (!cp->searching) return 0;

    /* Super and Ctrl still belong to the compositor even mid-search: those are
     * how you leave, and a search box that swallowed Super+C would trap you in
     * the panel. Shift is ours (capitals); Alt is nobody's here. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL)) return 0;

    if (sym == XKB_KEY_BackSpace) {
        if (cp->search_len > 0) cp->search[--cp->search_len] = '\0';
        else                    ctlpanel_search_close(s);
        cp->item = 0;
        cp->row_scroll = 0;
        synui_render_ctlpanel(s);
        return 1;
    }

    /* Printable ASCII only. The labels and keys being searched are ASCII, so
     * anything wider cannot match anything and would only make the box lie
     * about what it is filtering on. */
    if (sym >= 0x20 && sym <= 0x7e) {
        if (cp->search_len < (int)sizeof(cp->search) - 1) {
            cp->search[cp->search_len++] = (char)sym;
            cp->search[cp->search_len]   = '\0';
        }
        /* Any edit puts the cursor back at the top of the results: it indexes
         * a list that just changed under it, and leaving it at row 9 of a list
         * that now has two entries is how a cursor ends up off screen. */
        cp->item = 0;
        cp->row_scroll = 0;
        synui_render_ctlpanel(s);
        return 1;
    }

    return 0;   /* arrows, Tab, Enter, Esc: the normal handler below */
}

int ctlpanel_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!panel_wants_keys(s, SYN_PDRAG_CTLPANEL)) return 0;
    if (!s->ctlpanel.visible) return 0;

    /* ── Capture mode ────────────────────────────────────────
     *
     * FIRST — above the search box, and above the modified-combo passthrough
     * below. While a capture is armed EVERY chord is the answer, including the
     * compositor's own: moving a shortcut onto Super+K has to be possible, and
     * letting Super through here would run whatever Super+K does today instead
     * of capturing it. That is also why Esc is the only way out — with every
     * other key taken as input, a capture that could not be cancelled would be
     * a trap. Same shape as keys.c's, for the same reasons. */
    if (s->ctlpanel.sc_capturing) {
        /* A held modifier arrives as a press of its own while you reach for the
         * other half of the chord. Ignore them, or every capture comes out as
         * "Super" — except on the tap row, where the modifier is the answer. */
        if (syn_rebind_capture_ignores(&s->ctlpanel.sc_capture, sym)) return 1;

        if (sym == XKB_KEY_Escape) {
            s->ctlpanel.sc_capturing = 0;
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "Rebind cancelled");
        } else {
            ctlpanel_rebind_finish(s, sym, mods);
        }
        synui_render_ctlpanel(s);
        return 1;
    }

    if (ctlpanel_search_key(s, sym, mods)) return 1;

    /* Ctrl+R arms a rebind and Ctrl+Shift+R puts every shortcut back — tested
     * here, ahead of the passthrough below, because Ctrl combos otherwise go
     * straight to the global bind table. The pair is on Ctrl as well as F2
     * because F2 is a long reach and half the keyboards this runs on need Fn to
     * get to it; both spellings are the palette's, so the two panels do not
     * disagree about which key rebinds.
     *
     * Shift is tested on the SYMBOL, not the mask: xkb reports Ctrl+Shift+r as
     * XKB_KEY_R, and matching on WLR_MODIFIER_SHIFT alone would make every
     * Ctrl+R a reset on a keyboard with caps lock on. */
    if ((mods & WLR_MODIFIER_CTRL) &&
        !(mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT)) &&
        s->ctlpanel.focus == CTL_FOCUS_ITEMS &&
        s->ctlpanel.cat == CTL_CAT_SHORTCUTS && !s->ctlpanel.searching) {
        if (sym == XKB_KEY_r) {
            ctlpanel_rebind_begin(s);
            synui_render_ctlpanel(s);
            return 1;
        }
        if (sym == XKB_KEY_R) {
            ctlpanel_rebind_reset_all(s);
            synui_render_ctlpanel(s);
            return 1;
        }
    }

    /* Modified combos (Super+…) still reach the global bind table, so Super+C
     * closes the panel it opened and Super+P still opens the power panel. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    /* '/' opens the search box, from either column.
     *
     * Only '/', not "any letter". The row pane has had vim keys since it was
     * written — h/j/k/l move, q closes — and quietly turning those into text
     * entry would break the navigation of every user who has them in their
     * fingers, to save one keystroke. */
    if (sym == XKB_KEY_slash && !s->ctlpanel.searching) {
        s->ctlpanel.searching  = 1;
        s->ctlpanel.search[0]  = '\0';
        s->ctlpanel.search_len = 0;
        s->ctlpanel.item       = 0;
        s->ctlpanel.row_scroll = 0;
        s->ctlpanel.focus      = CTL_FOCUS_ITEMS;
        ctlpanel_cancel_pending(s);
        synui_render_ctlpanel(s);
        return 1;
    }

    /* Delete puts the selected row back to what synui ships with. Only ever a
     * row's own value: there is no "reset everything" here on purpose, because
     * a single keystroke that discards a desktop's entire configuration is not
     * an affordance, it is a trap. */
    if (sym == XKB_KEY_Delete && s->ctlpanel.focus == CTL_FOCUS_ITEMS) {
        int r = ctlpanel_selected_row(s);
        const struct ctl_item *it = r >= 0 ? ctl_item(r) : NULL;
        if (!it || it->vtype == CTL_VAL_NONE) {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "nothing to reset on this row");
        } else if (ctl_reset(s, it)) {
            char v[64];
            ctlpanel_row_value(s, r, v, sizeof(v));
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "%s reset to %s", it->label, v);
        } else {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "%s is already at its default", it->label);
        }
        synui_render_ctlpanel(s);
        return 1;
    }

    int row = ctlpanel_selected_row(s);
    int in_items = (s->ctlpanel.focus == CTL_FOCUS_ITEMS);
    /* The shortcuts pane has no ctl_items[] rows — its list is generated from
     * the live bind table — so every "step the row cursor" path below has to
     * move its own cursor instead. */
    int list_only = in_items && ctlpanel_item_count(s) == 0;
    /* Named separately from list_only: that one is "this pane has no table
     * rows", this one is "this pane is the shortcuts list", and only the second
     * is a reason to rebind something. They coincide today and a category with
     * no rows for some other reason would make them differ silently. */
    int in_shortcuts = in_items && s->ctlpanel.cat == CTL_CAT_SHORTCUTS;

    switch (sym) {
    /* The rebind key everything else in the world uses for "rename this". */
    case XKB_KEY_F2:
        if (in_shortcuts) {
            ctlpanel_rebind_begin(s);
            synui_render_ctlpanel(s);
        } else {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "Rebinding lives in the Shortcuts category");
            synui_render_ctlpanel(s);
        }
        return 1;

    /* F3: point the modifier tap at the selected row. The palette's key, so the
     * two panels do not mean different things by it. */
    case XKB_KEY_F3:
        if (in_shortcuts) {
            ctlpanel_tap_action_set(s);
        } else {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "The tap is set from the Shortcuts category");
        }
        synui_render_ctlpanel(s);
        return 1;

    case XKB_KEY_Escape:
    case XKB_KEY_q:
        /* Back out one level before closing: from a row pane to the category
         * list, and only from the category list to the desktop. Closing outright
         * from anywhere would make Esc mean two different things depending on
         * where you happened to be. */
        /* One more level to back out of than there used to be: a search is the
         * innermost thing you can be inside, so Esc leaves it first and only
         * then starts walking back out of the columns. */
        if (s->ctlpanel.searching) {
            ctlpanel_cancel_pending(s);
            ctlpanel_search_close(s);
            synui_render_ctlpanel(s);
        } else if (in_items) {
            /* Backing out of the row pane also drops a pick that was settling —
             * Esc means "not that" everywhere else on this panel, and it would
             * be a poor place to start meaning "load it anyway". */
            ctlpanel_cancel_pending(s);
            s->ctlpanel.focus = CTL_FOCUS_CATS;
            synui_render_ctlpanel(s);
        } else {
            ctlpanel_hide(s);
        }
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (list_only) ctlpanel_scroll_by(s, -1);
        else           ctlpanel_move(s, -1);
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (list_only) ctlpanel_scroll_by(s, +1);
        else           ctlpanel_move(s, +1);
        synui_render_ctlpanel(s);
        return 1;

    case XKB_KEY_Tab:
        /* The one key that always swaps columns, whatever the row is doing with
         * Left/Right. */
        if (in_items) s->ctlpanel.focus = CTL_FOCUS_CATS;
        else          ctlpanel_focus_items(s);
        synui_render_ctlpanel(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        /* In the sidebar, Enter opens the category — the submenu step. In the
         * row pane it activates the row. */
        if (!in_items) ctlpanel_focus_items(s);
        else           ctlpanel_activate(s);
        /* A row that handed off already hid the panel and opened another one;
         * re-rendering here would draw this panel back over the top of it. */
        if (s->ctlpanel.visible) synui_render_ctlpanel(s);
        return 1;

    case XKB_KEY_Left:
    case XKB_KEY_h:
        /* On a slider row Left/Right are the slider, and on a choice row they
         * are the choice; everywhere else they are the column move, which is
         * what makes the two panes feel like one menu and its submenu. Tab
         * still changes column from any of them, so neither is a dead end. */
        if (in_items && row >= 0 && ctlpanel_row_kind(row) == CTL_KIND_SLIDER)
            ctlpanel_adjust_opacity(s, -1);
        else if (in_items && row >= 0 && ctlpanel_row_kind(row) == CTL_KIND_CHOICE)
            ctlpanel_adjust_choice(s, row, -1);
        else if (in_items && row >= 0 && ctlpanel_adjust_value(s, row, -1))
            ; /* a table-driven row: Left is its decrement, not a column move */
        else
            s->ctlpanel.focus = CTL_FOCUS_CATS;
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Right:
    case XKB_KEY_l:
        if (in_items && row >= 0 && ctlpanel_row_kind(row) == CTL_KIND_SLIDER)
            ctlpanel_adjust_opacity(s, +1);
        else if (in_items && row >= 0 && ctlpanel_row_kind(row) == CTL_KIND_CHOICE)
            ctlpanel_adjust_choice(s, row, +1);
        else if (in_items && row >= 0 && ctlpanel_adjust_value(s, row, +1))
            ;
        else
            ctlpanel_focus_items(s);
        synui_render_ctlpanel(s);
        return 1;

    /* Page keys move by half a screen. Which list they move depends on which one
     * is under you: the shortcuts pane has no rows and scrolls, everything else
     * has a cursor and pages it (which drags the scroll along behind it). Before
     * the categories got long enough to scroll, these only ever meant the
     * shortcuts list, and in a forty-row category that read as a dead key. */
    case XKB_KEY_Prior:     /* Page Up */
        if (list_only) ctlpanel_scroll_by(s, -CTL_SHORTCUT_ROWS / 2);
        else for (int i = 0; i < CTL_ROW_ROWS / 2; i++) ctlpanel_move(s, -1);
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_Next:      /* Page Down */
        if (list_only) ctlpanel_scroll_by(s, +CTL_SHORTCUT_ROWS / 2);
        else for (int i = 0; i < CTL_ROW_ROWS / 2; i++) ctlpanel_move(s, +1);
        synui_render_ctlpanel(s);
        return 1;

    /* Home/End, which a hundred-row category is the first thing here to need. */
    case XKB_KEY_Home:
        if (list_only) { s->ctlpanel.sc_sel = 0; ctlpanel_shortcut_scroll_to_sel(s); }
        else if (in_items) { s->ctlpanel.item = 0; ctlpanel_scroll_to_cursor(s); }
        synui_render_ctlpanel(s);
        return 1;
    case XKB_KEY_End:
        if (list_only) {
            int n = ctlpanel_shortcut_count(s);
            if (n > 0) { s->ctlpanel.sc_sel = n - 1; ctlpanel_shortcut_scroll_to_sel(s); }
        } else if (in_items) {
            int n = ctlpanel_item_count(s);
            if (n > 0) { s->ctlpanel.item = n - 1; ctlpanel_scroll_to_cursor(s); }
        }
        synui_render_ctlpanel(s);
        return 1;

    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }
}
