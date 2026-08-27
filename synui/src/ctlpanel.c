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

/* The same idea for the font size / text scale rows. Shorter than the model
 * row's because what is deferred is a handful of config rewrites rather than a
 * multi-gigabyte load: long enough that a held arrow key runs the script once
 * instead of once per repeat, short enough that a single press does not feel
 * ignored. */
#define CTL_FONT_SETTLE_SECS   0.45

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
 * Rows are listed in display order and grouped by category, then by SECTION
 * within it. Nothing enforces either (the walk filters on .cat and a section
 * runs until the next `.section`), but keeping the literal order and the drawn
 * order the same means one read of this table tells you what the panel looks
 * like — and the sections are what a reader is actually navigating by, since
 * `.section` is what the breadcrumb names and what the pane rules between.
 *
 * ── The layout, at a glance ──────────────────────────────────
 *
 *   Appearance  Look · Text · Glass · CRT effects · Phosphor
 *   Windows     Frame · Layout · Shadow · Blur · Animation · Behaviour ·
 *               Alt+Tab · Panels
 *   Desktop     Desktop · Dock · Dock look · Dock buttons · Clock ·
 *               Start menu · Bar · Login · Desktop cat
 *   Input       Keyboard · Pointer
 *   Display     Screens · Night light
 *   Sound       Notifications · Audio · Recording
 *   Network     Network · Printers
 *   Power       Power · Game mode
 *   System      AI · Tools · About
 *
 * ⚠ THE RULE THIS TABLE IS ORDERED BY: a row goes with the thing it is ABOUT,
 * not with the thing that implements it. That is the whole of the 2026-08-25
 * regroup, and it is worth stating because every one of the splits it undid
 * looked locally reasonable at the time:
 *
 *   - The clock had FOUR homes — the format panel in Display, two switches near
 *     the top of Dock, the position row three groups below them, the widget's
 *     dial at the bottom. They are one §Clock now, and the format panel changed
 *     CATEGORY to get there (Display keeps the screens and the night light).
 *   - Dock was one 22-row heading holding the dock, the start menu, a widget
 *     setting and the clock. It is §Dock (is there one, and how does it behave)
 *     · §Dock look (style, opacity, corners) · §Dock buttons (each button with
 *     ITS OWN position row, which is where the three position rows went).
 *   - §Shell held exactly one row while the other start-menu row sat in Dock;
 *     they are §Start menu together.
 *   - §Bar ended with four login rows that are not bar settings (§Login,
 *     §Desktop cat).
 *   - Appearance's §Look was seventeen rows of picker, font, glass and window
 *     opacity: §Look · §Text · §Glass now, with §Phosphor split off §CRT
 *     effects.
 *   - "Crop client shadows" was filed under Animation. It is a shadow row.
 *   - System declared §Tools TWICE, which drew a second rule and a second
 *     identical breadcrumb inside one group.
 *
 * Adding a row: put it in the section it BELONGS to, not at the end of the
 * category. Only the first row of a section carries `.section`.
 */
/* Enum row option names. Kept next to the table rather than shared with the
 * config parser's own name tables: these are what the PANEL shows, and a value
 * a user reads in a menu ("Bottom") is not always what the config file spells
 * ("bottom"). ctl_enum_write() lowercases on the way out, which is what keeps
 * the two in step without needing two tables that can drift. */
static const char *const ctl_names_dock_edge[] = { "Bottom", "Top", "Left", "Right" };
/* Order matches syn_dock_style_t / syn_widget_glass_t, and folded to lower case
 * these ARE the synuirc spellings config.c parses back — same contract as the
 * edge names above. "Auto" is first because it is the default and because it is
 * the only one of the three that is not an override. */
static const char *const ctl_names_dock_style[]   = { "Auto", "Solid", "Glass" };
static const char *const ctl_names_widget_glass[] = { "Auto", "Off", "On" };
/* syn_clock_face_t, in its order. ⚠ ctl_format() persists an enum as its option
 * name FOLDED TO LOWER CASE, and config.c parses exactly those four words back —
 * so renaming one of these renames a config key's legal value. */
static const char *const ctl_names_clock_face[] =
    { "Minimal", "Classic", "Roman", "Neon" };
/* syn_wp_accent_t, same three positions and the same spellings — a separate
 * array rather than sharing the one above because the two enums are free to
 * grow apart, and a shared table is how a fourth position on one of them
 * silently renames the other. */
static const char *const ctl_names_wp_accent[]   = { "Auto", "Off", "On" };
static const char *const ctl_names_arrange[]   = { "Name", "Type", "Size", "Date" };
static const char *const ctl_names_phosphor[]  = { "Off", "Green", "Amber", "Blue" };
/* Order matches syn_focus_mode_t, and these ARE the synuirc spellings — the
 * panel writes an enum as its display name folded to lower case (ctl_format),
 * precisely so there is no second table to drift. So they have to be single
 * words that read as config values, which is why the row leans on its help
 * line to say what "sloppy" means rather than spelling it in the value. */
static const char *const ctl_names_focus_mode[] = { "Click", "Sloppy", "Strict" };
/* Not a local list: these are syn_accel_profile_names title-cased, and the two
 * must stay in step because ctl_persist writes the display name lower-cased and
 * config.c reads it back. Spelled here anyway rather than derived, so the panel
 * shows "Adaptive" and not "adaptive" — the same arrangement every other enum
 * row uses. tests/ctlpanel_table_test.c walks all three and would catch a
 * drift. */
static const char *const ctl_names_accel_profile[] = {
    "Default", "Flat", "Adaptive"
};
/* Order matches syn_anim_window_t / syn_anim_ws_t / syn_anim_curve_t. The
 * lower-cased spellings synuirc takes live in config.c beside the parser, so a
 * new style needs its display name added HERE and its word THERE. */
static const char *const ctl_names_anim_window[] = { "Off", "Fade", "Rise" };
static const char *const ctl_names_anim_ws[]     = { "Off", "Fade", "Slide" };
/* Hyphenated, not spaced: ctl_persist writes an enum by lower-casing the name
 * it shows, so "Ease out" would persist as "ease out" and config.c spells it
 * "ease-out". Same reason cat_breed's "Russian-Blue" carries its hyphen. */
static const char *const ctl_names_anim_curve[]  = {
    "Ease-out", "Linear", "Ease-in-out", "Ease-in",
};
/* Order matches cat_breed_t in synui.h; the lower-cased spellings synuirc takes
 * live in cat_breed_names[] beside the coats themselves, so a new breed needs
 * its display name added HERE and nowhere else. */
static const char *const ctl_names_cat_breed[] = {
    "Neon", "Tabby", "Ginger", "Tuxedo", "Siamese",
    /* "Russian-Blue" with the hyphen cat_draw.c spells it with: this row
     * persists by lower-casing the name below, and "russian blue" is a word
     * config.c does not know — picking that coat used to survive the session
     * and be back to Neon at the next login. Found by the every-option walk in
     * tests/ctlpanel_table_test.c. */
    "Calico", "Tortie", "Russian-Blue", "Black",
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
/* Order matches syn_bar_shape_t. Hyphenated for the reason anim_curve's are:
 * ctl_format lower-cases the DISPLAYED name to persist it, so "Floating pill"
 * would be written as "floating pill" and syn_bar_shape_names[] spells it
 * "floating-pill". The hyphen is what keeps the one table honest. */
static const char *const ctl_names_bar_shape[]   = {
    "Full-width", "Rounded-ends", "Floating-pill",
};
/* Order matches the GAME_OUT_* enum in synui.h, and folded to lower case these
 * ARE the synuirc spellings config.c's `game_output` case parses back. Single
 * words for the reason the whole table is — "Main screen" would be written to
 * settings.state as `main screen`. The help line carries the meaning. */
static const char *const ctl_names_game_output[] = { "Primary", "Focused", "Ask" };
/* Order matches syn_start_menu_t, and folded to lower case these ARE the
 * synuirc spellings config.c's `start_menu_style` case parses back.
 * "App-overlay" carries its hyphen for the reason anim_curve's "Ease-out" does:
 * ctl_format() persists the DISPLAYED name lower-cased, and "app overlay" is a
 * word config.c would not know. */
static const char *const ctl_names_start_menu[] = { "Menu", "App-overlay", "Rofi" };

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
    /*
     * One rung BELOW vmin meaning "synui has no opinion; something else
     * decides". NULL on almost every row — a number row's minimum is usually a
     * real setting (opacity 0.20, a 1px reveal strip) and there is nothing else
     * to defer to.
     *
     * Where it is set, this string is what the row DRAWS there; the file always
     * gets the word `auto`, so the parser has one spelling to know. Held as the
     * label rather than a flag because the useful thing to read on the row is
     * what it defers TO ("Follow the theme"), not that a sentinel exists.
     *
     * It is a rung and not a separate row for the same reason CTL_VAL_TRI is
     * one row: "off" and "nobody has chosen" are answers to the same question,
     * and splitting them across two controls is how a panel ends up with a
     * switch whose slider is ignored.
     *
     * The rung IS the value CTL_AUTO, and a row that has one must have that as
     * its compiled default — otherwise "no opinion" would be written into
     * settings.state as a choice and pin the row against a future default (see
     * ctl_persist). It also requires vmin >= 0, since "below the minimum" is how
     * the rung is recognised.
     */
    const char     *vauto;
    /*
     * What the row calls ZERO, where zero is a mode rather than an amount.
     *
     * A row can have both this and `vauto` — the Glass row does, and needs to:
     * its bottom two rungs are "let the theme decide" and "no glass at all",
     * which are opposite instructions that a bare "0 %" one step under a bare
     * "-1 %" gave a user no way to tell apart. Named, the ladder reads
     * Auto / Off / 5% … 100%, and the two modes are visibly modes.
     *
     * Display only: `vauto` needs a config spelling because its value is a
     * sentinel the file cannot carry, and zero does not — it is just zero.
     */
    const char     *vzero;
    const char *const *names;  /* CTL_VAL_ENUM options */
    int             nnames;
    syn_ctl_apply_t apply;
    syn_ctl_store_t store;     /* which file holds it; 0 = settings.state */
    /*
     * The value is NOT in syn_config_t, so `off` means nothing and must never
     * be dereferenced. Set on the font size and text scale rows, which live in
     * font.state — a file the rest of the suite writes too.
     *
     * A flag rather than "off == 0", because 0 is a legitimate offset: it is
     * whatever field syn_config_t happens to declare first, and a row naming
     * that field would be indistinguishable. Getting this wrong reads the top
     * of the config struct as an int and formats it as the row's value, which
     * is a plausible-looking number and therefore the worst kind of wrong.
     *
     * vtype/vmin/vmax/vstep/unit stay meaningful — they are what the row is
     * FORMATTED and stepped by. Only the storage is elsewhere.
     */
    bool            external;
    const char     *help;      /* one line, drawn in the footer */
};

/* Shorthands. The table is wide enough that spelling every field per row would
 * bury the two things worth reading — the label and the range. */
#define CFG(field)  offsetof(syn_config_t, field)
/* The value a `.vauto` row holds while it is deferring. -1 for the same reason
 * CTL_VAL_TRI's "device default" is -1: it is out of every range this panel
 * shows, so it can never collide with a value somebody chose. */
#define CTL_AUTO    (-1.0f)
/* Both designators at once, so an option list and its length cannot be given
 * separately and disagree. */
#define NAMES(a)    .names = (a), .nnames = (int)(sizeof(a) / sizeof((a)[0]))

static const struct ctl_item ctl_items[] = {
    /* Appearance */
    { CTL_ROW_THEME,        CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "Theme",            "theme",
      .section = "Look",
      .help = "Colour preset for window chrome and synui's own panels" },
    { CTL_ROW_WALLPAPER,    CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "Wallpaper",        "wallpaper" },
    /* ⚠ CTL_APPLY_WPACCENT AND NOT CTL_APPLY_REPAINT. Turning this on does not
     * redraw anything with a colour it already has — it CHANGES the colour, and
     * the colour lives in two places: synui's own panel fields (theme.c) and
     * palette.state, which the bar and the widgets watch. One apply reaches
     * both, because it goes through the export that publishes the decision. */
    { CTL_ROW_WP_ACCENT,    CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Wallpaper accent", NULL,
      .key = "wallpaper_accent", .off = CFG(wallpaper_accent), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_wp_accent), .apply = CTL_APPLY_WPACCENT,
      .help = "Take the accent off the wallpaper instead of the theme. "
              "Auto is Prism (light or dark), which is built on it, and "
              "nothing else" },
    /* The same accent, on the hardware that has lights in it.
     *
     * ⚠ EXTERNAL, like the font rows: the answer lives in
     * ~/.config/synui/rgb.state, which syn-rgb(1) owns and writes — and which
     * survives a logout, which a syn_config_t field would not. Giving it a
     * config key would create the second source of truth the state file exists
     * to avoid: `syn-rgb on` from a terminal has to move this row.
     *
     * ⛔ The row is drawn wherever OpenRGB is installed or not. A toggle that
     * disappears when its optdepend is missing is a feature nobody can find
     * out about; syn-rgb says which package to install, which is the answer
     * somebody can act on. */
    { CTL_ROW_RGB_LIGHTS,   CTL_CAT_APPEARANCE, CTL_KIND_TOGGLE, "RGB lights", NULL,
      .vtype = CTL_VAL_BOOL, .external = true,
      .help = "Put the wallpaper's accent on RGB hardware (needs openrgb)" },
    { CTL_ROW_CURSOR,       CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "Cursor theme",     "cursor"    },

    { CTL_ROW_UI_FONT,      CTL_CAT_APPEARANCE, CTL_KIND_PANEL,  "UI font",          "font",
      .section = "Text",
      .help = "The family every synui panel draws in. Previews live; Esc puts it back" },
    /* No .key and no .off: these two are NOT synui config keys. They live in
     * ~/.config/synui/font.state, which synfiles, syn-settings, syn-disks and
     * the bar all read and which synfiles also writes — so the value is read
     * off the file on every repaint and written through synui-apply-font(1).
     * Giving them a config field would create the second source of truth the
     * scale was moved into font.state to remove. vmin/vmax/vstep are still
     * honoured, by ctlpanel_adjust_font() rather than by ctl_adjust(); the
     * script clamps to the same range and is the authority.
     *
     * Two rows and not one because they are two settings: a point size means
     * nothing to a window that lays itself out in pixels, which is every
     * quickshell window in the suite. */
    { CTL_ROW_UI_FONT_SIZE, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Font size",         NULL,
      .vtype = CTL_VAL_INT, .vmin = 6, .vmax = 24, .vstep = 1, .unit = "pt",
      .external = true,
      .help = "Applications: GTK, Qt and the terminal. Applies when you stop" },
    { CTL_ROW_UI_TEXT_SCALE, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Text scale",       NULL,
      .vtype = CTL_VAL_INT, .vmin = 75, .vmax = 175, .vstep = 5, .unit = "%",
      .external = true,
      .help = "The bar and the SYNAPSE apps — Files, Settings, Disks, Software" },

    { CTL_ROW_TRANSPARENCY, CTL_CAT_APPEARANCE, CTL_KIND_SLIDER, "Transparency",     NULL,
      .section = "Glass",
      .help = "Focused-window opacity. Left/Right adjust; Enter switches it off" },
    /* ONE slider for the whole desktop's glass, above the per-surface rows it
     * drives. It is an integer 0..100 rather than an alpha because it is not
     * one: the windows, the panels and the bar get different numbers out of it
     * (syn_glass_* in synui.h), since the same alpha that is pleasant on a
     * 1200px window makes a dense panel row unreadable.
     *
     * `vauto` is what "nobody has chosen" looks like on the row, and it is the
     * compiled default — so the thirteen themes that are not a Prism keep the
     * opacities they were tuned with, and turning this on is an explicit act
     * whose result the per-surface rows further down this group then show. */
    { CTL_ROW_GLASS_LEVEL,  CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Glass",            NULL,
      /* ⚠ vmin STAYS 0 even though 0 is now a named mode rather than an amount.
       * The auto rung is defined as "one step below the minimum" (ctl_step), so
       * lifting vmin to 5 would put Auto where Off is and drop Off off the
       * bottom of the row entirely. Three rungs, in this order: Auto, Off, then
       * 5% upward. */
      .key = "glass_level", .off = CFG(glass_level), .vtype = CTL_VAL_INT,
      .vmin = 0.0f, .vmax = 100.0f, .vstep = 5.0f, .unit = "%",
      .vauto = "Auto", .vzero = "Off", .apply = CTL_APPLY_GLASS,
      .help = "Auto follows the theme \xc2\xb7 Off is never glass \xc2\xb7 or set how much you see through" },
    /* What makes the row above a MASTER rather than a fourth opinion. On, the
     * five rows it drives — the two window opacities, the terminal, the bar and
     * the dock — are recomputed from it and marked "synced". Dragging one of
     * those by hand pins it and it stops following; switching this back on
     * releases every pin at once. See glass_sync in synui.h. */
    { CTL_ROW_GLASS_SYNC,   CTL_CAT_APPEARANCE, CTL_KIND_TOGGLE, "Sync all glass", NULL,
      .key = "glass_sync", .off = CFG(glass_sync), .vtype = CTL_VAL_BOOL,
      .apply = CTL_APPLY_GLASS,
      .help = "Every surface follows Glass above. Change one and it keeps its "
              "own until you switch this back on" },
    { CTL_ROW_GLASS_LEGIBILITY, CTL_CAT_APPEARANCE, CTL_KIND_TOGGLE, "Legibility correction", NULL,
      .key = "glass_legibility", .off = CFG(glass_legibility), .vtype = CTL_VAL_BOOL,
      .apply = CTL_APPLY_GLASS,
      .help = "Surfaces go less see-through where their text would not read. "
              "Off draws exactly what you asked for" },
    /* WHAT the row above measures against, which was the wallpaper and only the
     * wallpaper until barscan.c. Its own row rather than a mode of Legibility
     * because it is a different kind of setting: legibility is "may a surface
     * overrule itself", this is "what is it looking at", and the two are worth
     * having independently — a desktop can want honest measurements and no
     * correction, which is precisely where the start menu ends up reading its
     * ink straight off the window it opened over.
     *
     * ⚠ CTL_APPLY_NONE IS LITERALLY RIGHT, AND THE HELP LINE HAS TO SAY SO:
     * barscan.c reads this at the top of every scan, so the row lands on the
     * next tick and nothing here has to push it. It must NOT push a repaint —
     * the scan clears both grids before it fills them, so switching this off
     * publishes -1 across the board and every surface is back on the wallpaper
     * by itself, with no second path to keep in step. */
    { CTL_ROW_SCENE_INK,    CTL_CAT_APPEARANCE, CTL_KIND_TOGGLE, "Live backdrop", NULL,
      .key = "scene_ink", .off = CFG(scene_ink), .vtype = CTL_VAL_BOOL,
      .apply = CTL_APPLY_NONE,
      .help = "Menus and panels ink themselves off the window behind them "
              "rather than the wallpaper it covers. Lands within a second" },
    { CTL_ROW_INACTIVE_OPACITY, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Unfocused opacity", NULL,
      .key = "inactive_opacity", .off = CFG(inactive_opacity), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.30f, .vmax = 1.0f, .vstep = 0.02f, .apply = CTL_APPLY_GLASS,
      .help = "How far windows you are not using fade back" },
    { CTL_ROW_FOOT_ALPHA,   CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Terminal glass", NULL,
      .key = "foot_alpha", .off = CFG(foot_alpha), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.02f, .apply = CTL_APPLY_GLASS,
      .help = "foot draws its own background alpha, so it needs its own level" },
    /* The one guard on the whole scheme, and the one row that says so out loud:
     * a surface measures the wallpaper under it and raises its own alpha until
     * its text clears AA. Off is "I said clear, I meant clear". */
    /* The way out, one press, sitting under the rows it undoes. Somebody who
     * does not want any of this had to find three controls — Transparency,
     * Glass, and Sync all glass to release pins they never knew they set — and
     * the third is invisible until a pinned dock stays glassy after the master
     * says Off. See synui_effects_solid(). One-way on purpose; the rows above
     * put any of it back. */
    { CTL_ROW_SOLID,        CTL_CAT_APPEARANCE, CTL_KIND_ACTION, "Make it all solid", "solid",
      .help = "Glass off and windows opaque, in one press. The rows above put it back" },
    /*
     * …and the other end of the same argument.
     *
     * The glass presets stopped asking for a bar and dock with NO background
     * (SYN_BAR_ALPHA_FROSTED): a surface that thin still frosts, still has edges
     * and still gives the legibility walk something to walk from, and the clear
     * bar over a photograph was the failure that prompted it. But a clear bar
     * over the right wallpaper is the best-looking thing this desktop does, and
     * taking it away as a DEFAULT is not the same as taking it away — so it
     * becomes a thing you ask for, in one press, beside its opposite.
     *
     * ⚠ IT IS NOT "SOLID BACKWARDS". Solid is a retreat to somewhere safe and
     * can be blunt about it; this is the opposite and has to leave the guards
     * standing — the legibility correction, the wallpaper ink, the scrim are
     * exactly what make a clear bar readable, and switching them off here would
     * hand somebody an unreadable desktop from a row labelled with a look.
     * See synui_effects_clear().
     */
    { CTL_ROW_CLEAR,        CTL_CAT_APPEARANCE, CTL_KIND_ACTION, "Make it all clear", "clear",
      .help = "Bar, dock and menus lose their background entirely, inked off "
              "the wallpaper. The rows above put it back" },

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
      .section = "Phosphor",
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
    { CTL_ROW_EFFECT_LIFT, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Phosphor lift", NULL,
      .key = "effect_lift", .off = CFG(effect_lift), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.05f, .apply = CTL_APPLY_REPAINT, .store = CTL_STORE_FILTERS,
      .help = "How far the unlit field glows: 0 keeps it black, up lights the raster" },
    { CTL_ROW_EFFECT_HUE, CTL_CAT_APPEARANCE, CTL_KIND_VALUE, "Phosphor hue", NULL,
      .key = "effect_hue", .off = CFG(effect_hue), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.0f, .vmax = 1.0f, .vstep = 0.05f, .apply = CTL_APPLY_REPAINT, .store = CTL_STORE_FILTERS,
      .help = "Turns the tint's colour: down is redder (amber to orange), up yellower. 0.50 is the preset" },

    /* ── Windows ─────────────────────────────────────────────
     *
     * Everything about how a window is FRAMED. Split out of Appearance because
     * Appearance was where the theme lives and these are not about colour: a
     * border width and a shadow sigma belong with snapping and tiling, not with
     * a wallpaper picker. */
    { CTL_ROW_TITLEBARS,      CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Titlebars", NULL,
      .section = "Frame",
      .help = "Server-side titlebars, on every window at once" },
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
      .section = "Layout",
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

    { CTL_ROW_SHADOW,         CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Drop shadow", NULL,
      .section = "Shadow",
      .key = "shadow", .off = CFG(shadow), .vtype = CTL_VAL_BOOL,
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
    { CTL_ROW_CLIP_CSD_MARGIN, CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Crop client shadows", NULL,
      .key = "clip_csd_margin", .off = CFG(clip_csd_margin), .vtype = CTL_VAL_BOOL,
      .apply = CTL_APPLY_DECO,
      .help = "Hides the invisible margin apps like Firefox draw their own shadow in" },

    { CTL_ROW_BLUR,           CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Backdrop blur", NULL,
      .section = "Blur",
      .key = "blur", .off = CFG(blur), .vtype = CTL_VAL_BOOL,
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

    /* ── Animation ────────────────────────────────────────────
     * Two events, two sets of rows, because they are two tastes: people who
     * want windows to appear instantly often still want the desk to move, and
     * the other way round. They shared one length until this section existed.
     * The curve is shared on purpose — one desktop, one way of decaying. */
    { CTL_ROW_ANIM_WINDOW,    CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Window open", NULL,
      .section = "Animation",
      .key = "anim_window", .off = CFG(anim_window), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_anim_window), .apply = CTL_APPLY_NONE,
      .help = "How a window arrives. Closing is not animated: the client's "
              "buffer is gone the moment it unmaps" },
    { CTL_ROW_ANIMATION_MS,   CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Window length", NULL,
      .key = "anim_window_ms", .off = CFG(anim_window_ms), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 1000, .vstep = 10, .unit = "ms", .apply = CTL_APPLY_NONE,
      .help = "0 jumps straight to the end state. Also times the niri strip slide" },
    { CTL_ROW_ANIM_RISE_PX,   CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Rise distance", NULL,
      .key = "anim_rise_px", .off = CFG(anim_rise_px), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 200, .vstep = 2, .unit = "px", .apply = CTL_APPLY_NONE,
      .help = "How far a Rise window travels up into place. Ignored by the other styles" },
    { CTL_ROW_ANIM_WORKSPACE, CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Desktop switch", NULL,
      .key = "anim_workspace", .off = CFG(anim_workspace), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_anim_ws), .apply = CTL_APPLY_NONE,
      .help = "Fade cross-fades the two desks; Slide sends them off the way you switched" },
    { CTL_ROW_ANIM_WORKSPACE_MS, CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Desktop length", NULL,
      .key = "anim_workspace_ms", .off = CFG(anim_workspace_ms), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 1000, .vstep = 10, .unit = "ms", .apply = CTL_APPLY_NONE,
      .help = "A slide wants longer than a fade — the eye has to follow it somewhere" },
    { CTL_ROW_ANIM_CURVE,     CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Easing", NULL,
      .key = "anim_curve", .off = CFG(anim_curve), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_anim_curve), .apply = CTL_APPLY_NONE,
      .help = "Shared by both, and by the strip slide: two easings read as two desktops" },

    /* Window behaviour, which is what KDE calls this and what most people come
     * looking for. Focus leads: it is the one row here that changes what the
     * keyboard does rather than what the mouse can do. */
    { CTL_ROW_FOCUS_MODE,     CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Focus follows", NULL,
      .section = "Behaviour",
      .key = "focus_mode", .off = CFG(focus_mode),
      .vtype = CTL_VAL_ENUM, NAMES(ctl_names_focus_mode),
      .help = "Click: only a click focuses. Sloppy and Strict follow the "
              "pointer; over the desktop, Strict drops focus, Sloppy keeps it" },
    { CTL_ROW_FOCUS_DELAY,    CTL_CAT_WINDOWS, CTL_KIND_VALUE, "Focus delay", NULL,
      .key = "focus_delay_ms", .off = CFG(focus_delay_ms), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 1000, .vstep = 25, .unit = "ms",
      .help = "How long the pointer rests before focus follows it. 0 is "
              "instant, which also focuses windows you only crossed over" },
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
      .section = "Alt+Tab",
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

    /* ── Panels ──────────────────────────────────────────────
     *
     * synui's own windows: where they open, and what closes them. It opens
     * with WHERE, because that is the question the three rows under it then
     * answer the other half of — the same question the focus rows ask about
     * ordinary windows ("what does the pointer moving somewhere else do?"),
     * asked about the compositor's panels instead. */
    { CTL_ROW_PANEL_FOLLOW,   CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Panels follow the pointer", NULL,
      .section = "Panels",
      .key = "panel_follow_pointer", .off = CFG(panel_follow_pointer),
      .vtype = CTL_VAL_BOOL,
      .help = "On, an open panel moves to whichever monitor the pointer is on. "
              "Off, it stays on the monitor you opened it on" },
    /* One row per panel, not one for all three: velle asked for "a switch for
     * each of them in settings not all or nothing", and they genuinely differ —
     * a control panel you want gone the moment you look away and a calculator
     * you park in the corner are different answers. */
    { CTL_ROW_CALC_CLOSE,     CTL_CAT_WINDOWS, CTL_KIND_TOGGLE, "Calculator", NULL,
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

    /* Desktop. Layout leads: it is the one row here that changes where your
     * windows go rather than what the shell furniture looks like. */
    { CTL_ROW_LAYOUT,        CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Layout",           NULL,
      .section = "Desktop",
      .help = "Of the desktop you are on — layout is per-desktop, not global" },
    { CTL_ROW_OVERVIEW,      CTL_CAT_DESKTOP, CTL_KIND_PANEL,  "Mission control",  "overview",
      .help = "Every window on this desktop at once, and the desktops themselves" },
    { CTL_ROW_DESKTOP_ICONS, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Desktop icons", NULL,
      .key = "desktop_icons", .off = CFG(desktop_icons), .vtype = CTL_VAL_BOOL8,
      .apply = CTL_APPLY_DESKICONS, .help = "Draw ~/Desktop on the wallpaper" },
    { CTL_ROW_DESKTOP_ICON_ARRANGE, CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Icon order", NULL,
      .key = "desktop_icon_arrange", .off = CFG(desktop_icon_arrange), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_arrange), .apply = CTL_APPLY_DESKICONS },
    { CTL_ROW_WIDGETS,       CTL_CAT_DESKTOP, CTL_KIND_PANEL,  "Desktop widgets",  "widgets" },
    /* The widgets are quickshell's and read this out of settings.state
     * themselves (WidgetFrame.qml), so the compositor has nothing to apply —
     * like Bar edge, it moves while you are looking at it.
     *
     * It sat in the Dock section for a while, on the argument that the row
     * exists to make the widgets MATCH the dock and should therefore be next to
     * what it copies. That is backwards for somebody looking for it: this is a
     * setting OF the widgets, and the place to look for a widget setting is
     * beside Desktop widgets. The link to the dock is what the help line is
     * for, and it survives the move. */
    { CTL_ROW_WIDGET_GLASS,  CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Widget glass", NULL,
      .key = "widget_glass", .off = CFG(widget_glass), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_widget_glass), .apply = CTL_APPLY_NONE,
      .help = "Desktop widgets take the dock's glass instead of the HUD panel" },

    { CTL_ROW_DOCK,          CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Dock",             NULL,
      .section = "Dock" },
    { CTL_ROW_DOCK_AUTOHIDE, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Dock auto-hide",   NULL      },
    /* Bespoke like the row above it (.key/.off left zeroed): each of these has
     * to persist to dock.state and wake the mirrors, which the table-driven path
     * writes to settings.state and cannot do. See ctlpanel_activate. */
    { CTL_ROW_DOCK_ON_TOP,   CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Dock above windows", NULL,
      .help = "Off, windows cover a pinned dock. Auto-hide always arrives on "
              "top — it is summoned over whatever is there" },
    { CTL_ROW_DOCK_EDGE,     CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock edge", NULL,
      .key = "dock_edge", .off = CFG(dock_edge), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_dock_edge), .apply = CTL_APPLY_DOCK },
    { CTL_ROW_DOCK_HEIGHT,   CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock size", NULL,
      .key = "dock_height", .off = CFG(dock_height), .vtype = CTL_VAL_INT,
      .vmin = 32, .vmax = 200, .vstep = 4, .unit = "px", .apply = CTL_APPLY_DOCK,
      /* Worth spelling out, because it did NOT until now: the number moved the
       * slab and left the icons at 48, so the row read as broken past about
       * 80px — a wall of glass with the same small pictures adrift in it. */
      .help = "The slab AND the icons in it — the icons are this minus 16" },
    { CTL_ROW_DOCK_HOVER_MARGIN, CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock reveal strip", NULL,
      .key = "dock_hover_margin", .off = CFG(dock_hover_margin), .vtype = CTL_VAL_INT,
      .vmin = 1, .vmax = 32, .vstep = 1, .unit = "px", .apply = CTL_APPLY_DOCK,
      .help = "How close to the edge the pointer must get to bring it back" },
    { CTL_ROW_DOCK_MAGNIFY,  CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Dock magnify",     NULL,
      .help = "The icons under the pointer swell and the row slides apart to "
              "make room" },
    /* Table-driven where the switch above it is bespoke, and the split is not an
     * inconsistency: the SWITCH has to reach dock.state (where the dock's own
     * on/off settings live, beside the edge and the pins), and the AMOUNT is a
     * number like Dock size and Dock corners, which settings.state already
     * holds. One value, one home — a copy in the other file is a value that is
     * quietly discarded at the next load. */
    { CTL_ROW_DOCK_MAGNIFY_SCALE, CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock magnify amount", NULL,
      .key = "dock_magnify_scale", .off = CFG(dock_magnify_scale),
      .vtype = CTL_VAL_FLOAT,
      .vmin = 1.00f, .vmax = 2.50f, .vstep = 0.05f, .apply = CTL_APPLY_DOCK,
      /* Worth saying that the bar gets taller: the canvas grows its transparent
       * headroom to match (dock_headroom), so a big number is not free — it is
       * a taller strip of screen the dock can be summoned into. */
      .help = "How big the icon under the pointer gets. The dock makes room for "
              "it, so a big swell is a taller dock" },

    /* ── Dock look ────────────────────────────────────────────
     *
     * The surface, as opposed to the dock's behaviour above it — in the order
     * you would reach for them, and the same order the Bar group uses for the
     * same three questions: what kind of surface it is, then how much of the
     * wallpaper it lets through, then its shape. */
    { CTL_ROW_DOCK_STYLE,    CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock style", NULL,
      .section = "Dock look",
      .key = "dock_style", .off = CFG(dock_style), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_dock_style), .apply = CTL_APPLY_DOCK,
      .help = "Glass frosts the wallpaper behind the bar. Auto follows the theme" },
    { CTL_ROW_DOCK_OPACITY,  CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock opacity", NULL,
      .key = "dock_opacity", .off = CFG(dock_opacity), .vtype = CTL_VAL_FLOAT,
      /* `Auto` is the theme's own answer — frosted on the two Prisms, the
       * 0.72 slab elsewhere — and it is the DEFAULT, so a desktop that has
       * never touched this row follows whatever theme is on it. Same rung and
       * same word Bar opacity has, because they are the same question about
       * two strips. */
      .vauto = "Auto",
      /* Down to a real 0.00: the icons are painted over the body at full
       * opacity, so the bottom of this range is a row of icons floating on the
       * wallpaper rather than a dock nobody can find. The 0.20 that used to be
       * here was one of five separate floors on the same setting — see the
       * dock_opacity note in config.c. */
      .vmin = 0.00f, .vmax = 1.00f, .vstep = 0.05f, .apply = CTL_APPLY_DOCK,
      .help = "0.00 leaves the icons alone on the wallpaper; 1.00 hides it "
              "completely" },
    { CTL_ROW_DOCK_RADIUS,   CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Dock corners", NULL,
      .key = "dock_radius", .off = CFG(dock_radius), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 64, .vstep = 2, .unit = "px", .apply = CTL_APPLY_DOCK,
      /* The clamp is worth saying out loud: past half the dock's thickness the
       * number stops doing anything, and a slider that visibly moves while the
       * screen does not reads as broken. */
      .help = "Its own, not the window radius. Caps at half the dock's size" },

    { CTL_ROW_DOCK_APPS,     CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Show all apps button", NULL,
      .section = "Dock buttons",
      .help = "A grid of dots at the end of the dock. Always opens the "
              "application overlay" },
    /*
     * A cell position, and there are three of them: this one, the power
     * button's below, and the clock's over in the Clock section. Each sits
     * directly under the switch that puts its cell on screen, which is the
     * whole reason the clock's is not here — the three used to be listed
     * together, three groups away from two of the switches they belong to.
     *
     * CTL_KIND_VALUE with no .key: they are not one config field with a name
     * and a range, they are a gap index with two sentinels, so the value is
     * formatted and stepped by id like the AI-model row is.
     *
     * ⚠ ALL THREE ARE ALSO A DRAG on the dock itself, and the drag can leave a
     * cell in a gap none of these three words names. The row says so ("after 3
     * icons") rather than rounding — see dock_slot_label().
     */
    { CTL_ROW_DOCK_APPS_POS, CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Apps button position", NULL,
      .help = "Where the all-apps button sits along the run. Dragging the "
              "button on the dock sets the same thing" },
    { CTL_ROW_DOCK_POWER,    CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Show power button", NULL,
      .help = "A power mark past the apps button. Clicking it opens a menu — "
              "Lock, Log Out, Suspend, Restart, Shut Down" },
    { CTL_ROW_DOCK_POWER_POS, CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Power button position", NULL,
      .help = "Where the power button sits along the run. Dragging the "
              "button on the dock sets the same thing" },

    /* ── Clock ────────────────────────────────────────────────
     *
     * Every clock row the desktop has, in one place. They were spread over
     * three: the format panel sat in Display (beside the monitor arrangement,
     * which it has nothing to do with — Display is where it landed, not where
     * anybody looks for it), the two dock-clock switches sat near the top of
     * the Dock section, the position row three groups below them, and the
     * widget's dial at the very bottom. Four answers to "how do I change the
     * clock", none of them next to each other.
     *
     * The format panel leads because it is the one that decides what the other
     * four DRAW: 12/24-hour and the date format come from here, and the dock
     * clock and the widget both follow it.
     *
     * ⚠ This row is CTL_CAT_DESKTOP now, not CTL_CAT_DISPLAY. Nothing else
     * moved category with it — the row is unchanged otherwise, it opens the
     * same `clock` panel, and Display keeps the screens and the night light. */
    { CTL_ROW_CLOCK,      CTL_CAT_DESKTOP, CTL_KIND_PANEL,  "Date & time",      "clock",
      .section = "Clock",
      .help = "12/24-hour and the date format, for the dock clock and the widget" },
    { CTL_ROW_DOCK_CLOCK,    CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Dock clock",       NULL,
      .help = "Time and date in a cell of its own — drag the cell to move it "
              "anywhere in the row. 12/24-hour follows Clock & Time" },
    { CTL_ROW_DOCK_CLOCK_ANALOG, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Analog dock clock", NULL,
      .help = "Draw the dock clock as a dial. The one clock that fits a "
              "dock on the left or right edge — a column cannot widen for a "
              "time string" },
    /* The last of the three cell positions, and the one the other two are
     * documented on — see CTL_ROW_DOCK_APPS_POS in Dock buttons. It is here
     * rather than beside them because it is a CLOCK row: somebody moving the
     * clock is in this group, not in the button group. */
    { CTL_ROW_DOCK_CLOCK_POS, CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Clock position", NULL,
      .help = "Where the dock clock sits along the run. Dragging the clock "
              "on the dock sets the same thing" },
    { CTL_ROW_CLOCK_FACE,    CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Analog clock face", NULL,
      .key = "widget_clock_face", .off = CFG(widget_clock_face),
      .vtype = CTL_VAL_ENUM, NAMES(ctl_names_clock_face),
      .apply = CTL_APPLY_NONE,
      .help = "Which dial the analog clock WIDGET draws — the desktop one, "
              "from Widgets. The dock's own analog clock has one design" },

    /* ── Start menu ───────────────────────────────────────────
     *
     * The two rows about the start menu, which were in two different places:
     * this one buried among the dock rows (it is not a dock setting and never
     * was), and Start button alone under a heading called "Shell" that held
     * nothing else. What the key opens, then what the button looks like.
     *
     * ⚠ This row does NOT govern the dock's apps button. That button draws a
     * grid of dots and opens the overlay those dots are a picture of; this is
     * the START KEY opens, which has no picture and so is free to be chosen.
     * CTL_APPLY_NONE because nothing is drawn from this — synui_start_menu_open()
     * reads it at the moment a key is pressed, which is also what makes the
     * change take effect with no reload. */
    { CTL_ROW_START_MENU_STYLE, CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Start menu", NULL,
      .section = "Start menu",
      .key = "start_menu_style", .off = CFG(start_menu_style),
      .vtype = CTL_VAL_ENUM, NAMES(ctl_names_start_menu),
      .apply = CTL_APPLY_NONE,
      .help = "What the Super tap and Super+Escape open. The keyboard shortcut "
              "list follows this row; the dock's apps button does not" },
    { CTL_ROW_LAUNCHER,      CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Start button",     NULL },
    /* There was a "Super+Space opens" row here (launcher ⇄ command bar). It was
     * a SECOND way to declare a keybinding, and the Shortcuts category's rebind
     * (F2) is the first — so the two fought: a chord moved in the palette was
     * put back by the swap, which re-ran at the end of every config load, after
     * binds.state. One list of shortcuts, one owner. Rebind Super+Space and
     * Super+= from Control panel ▸ Shortcuts (or the Super+/ palette) instead. */

    /* Is there a bar at all — the row the Dock switch has always had and this
     * side of the desktop never did. Bespoke rather than table-driven
     * (.key/.off left zeroed) because the bar is a separate process: the flag
     * has to reach settings.state, which is the file the bar watches, and a
     * foreign bar still needs the optional command pair run.
     * See CTL_ROW_BAR in ctlpanel_activate. */
    { CTL_ROW_BAR,           CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Bar",              NULL,
      .section = "Bar",
      .help = "The top bar, and only it — the widgets, notes and start menu "
              "stay. A waybar desktop needs bar_stop_cmd/bar_start_cmd" },
    /* The bar is a SEPARATE PROCESS, and this is the one row on the panel whose
     * value the compositor does not act on — synui-bar reads it at startup.
     * CTL_APPLY_NONE is therefore literally right, and the help line has to say
     * so, or the row reads as broken: you flip it, and nothing happens until the
     * bar is restarted. */
    { CTL_ROW_BAR_SHELL,     CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Bar shell",        NULL,
      .key = "bar_shell", .off = CFG(bar_shell), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_bar_shell), .apply = CTL_APPLY_NONE,
      .help = "Antiquity is the diinki port; takes effect at the next login" },
    /* The bar's answer to Dock edge, and the one row on this panel whose
     * value neither the compositor NOR a restart applies: the bar watches
     * settings.state itself, so it moves while you are looking at it. Two
     * options rather than the dock's four — the bar is a horizontal row and has
     * no vertical form (see syn_bar_edge_t). */
    /* …with one thing on THIS side to do, which is why it is not APPLY_NONE like
     * its neighbours: a bar with no background of its own takes its ink from the
     * wallpaper strip it covers (backdrop.state), and moving the bar moves which
     * strip that is. Without this the ink stays picked from the top of the
     * screen while the bar sits at the bottom — a wrong answer that only shows
     * up on the one theme that draws a clear bar, and only on some wallpapers. */
    { CTL_ROW_BAR_EDGE,      CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Bar edge",         NULL,
      .key = "bar_edge", .off = CFG(bar_edge), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_bar_edge), .apply = CTL_APPLY_WALLPAPER,
      .help = "Which edge the bar sits on. The bar picks this up live" },
    /* The bar's half of Dock auto-hide, and the one row on this panel that
     * neither reads nor writes a setting the compositor owns. See
     * ctl_bar_autohide_label() for why it reads bar.json and asks the bar to
     * write it rather than writing the file itself. */
    { CTL_ROW_BAR_AUTOHIDE,  CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Bar auto-hide",    NULL,
      .apply = CTL_APPLY_NONE,
      .help = "Every monitor's bar at once. Per-monitor lives on the bar's own "
              "right-click menu, and \"mixed\" means they disagree" },
    /* The bar's half of Dock opacity, in the same place in the same order — what
     * kind of surface, then how much of the wallpaper it lets through, then its
     * shape. Watched live by the bar like Bar edge, hence APPLY_NONE.
     *
     * It starts on the auto rung rather than at a number, and that is the whole
     * design of the row: the theme already has an opinion (macOS 26 asks for a
     * clear bar; nothing else asks for anything), so a numeric default here
     * would silently overrule the one preset with a view about it.
     *
     * 0.00 is a real position and the reason the row is worth having — a bar
     * with NO background, its ink taken off the wallpaper. That has a failure
     * mode the help line has to name: where the wallpaper offers no legible ink
     * the bar keeps its background, so the row can be set to 0 and the bar can
     * still, correctly, look solid. */
    { CTL_ROW_BAR_OPACITY,   CTL_CAT_DESKTOP, CTL_KIND_VALUE, "Bar opacity", NULL,
      .key = "bar_opacity", .off = CFG(bar_opacity), .vtype = CTL_VAL_FLOAT,
      .vmin = 0.00f, .vmax = 1.00f, .vstep = 0.05f, .vauto = "Follow the theme",
      .apply = CTL_APPLY_NONE,
      .help = "0.00 is a clear bar, inked off the wallpaper; 1.00 hides it "
              "completely" },
    /* Watched live by the bar like Bar edge above, and like it applied by
     * neither the compositor nor a restart. The help line has to say the row
     * does nothing on its own: it is the bar's share of Window effects ▸ Corner
     * radius, so on a desktop with the corners off every option here looks
     * identical, and a row that appears to be ignored is worse than one that
     * says what it is waiting for. */
    { CTL_ROW_BAR_SHAPE,     CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Bar shape",        NULL,
      .key = "bar_shape", .off = CFG(bar_shape), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_bar_shape), .apply = CTL_APPLY_NONE,
      .help = "Shape when corners are on; needs Window effects \xe2\x96\xb8 Corner radius" },

    { CTL_ROW_WELCOME_AT_STARTUP, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Welcome menu at login", NULL,
      .section = "Login",
      .key = "welcome_at_startup", .off = CFG(welcome_at_startup), .vtype = CTL_VAL_BOOL },
    { CTL_ROW_START_OVERLAY, CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Neural overlay at login", NULL,
      .key = "start_overlay", .off = CFG(start_overlay), .vtype = CTL_VAL_BOOL },

    { CTL_ROW_CAT_START,     CTL_CAT_DESKTOP, CTL_KIND_TOGGLE, "Desktop cat at login", NULL,
      .section = "Desktop cat",
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
      .section = "Keyboard",
      .key = "repeat_rate", .off = CFG(repeat_rate),
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
      .section = "Pointer",
      .key = "tap", .off = CFG(tap_to_click),
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
    /* The curve, which is the row somebody wanting "acceleration on" is
     * actually after — Pointer speed above scales whichever curve the device is
     * already on and can neither turn acceleration on nor off. */
    { CTL_ROW_ACCEL_PROFILE, CTL_CAT_INPUT, CTL_KIND_VALUE, "Pointer acceleration", NULL,
      .key = "accel_profile", .off = CFG(accel_profile), .vtype = CTL_VAL_ENUM,
      NAMES(ctl_names_accel_profile), .apply = CTL_APPLY_INPUT,
      .help = "Adaptive moves further the faster you move; Flat is 1:1. "
              "Default is libinput's own pick for the device" },
    { CTL_ROW_POINTER_SMOOTHING, CTL_CAT_INPUT, CTL_KIND_VALUE, "Pointer smoothing", NULL,
      .key = "pointer_smoothing", .off = CFG(pointer_smoothing), .vtype = CTL_VAL_INT,
      .vmin = 0, .vmax = 10, .vstep = 1, .apply = CTL_APPLY_INPUT,
      .help = "Steadies a shaky or noisy pointer by averaging its path. "
              "Costs a little lag; 0 is off. Games reading raw motion are "
              "unaffected" },
    { CTL_ROW_CURSOR_SIZE,  CTL_CAT_INPUT, CTL_KIND_VALUE, "Cursor size", NULL,
      .key = "cursor_size", .off = CFG(cursor_size), .vtype = CTL_VAL_INT,
      .vmin = 8, .vmax = 256, .vstep = 4, .unit = "px", .apply = CTL_APPLY_CURSOR },

    /* Display */
    { CTL_ROW_DISPLAYS,   CTL_CAT_DISPLAY, CTL_KIND_PANEL,  "Display settings", "displays",
      .section = "Screens" },
    /* Handled by id in ctlpanel_activate()/ctlpanel_adjust_choice() rather than
     * by the table, because setting it is not a field write: dispcfg has to
     * re-flow the layout, change modes and move windows off a screen it is
     * about to switch off. CHOICE so Left/Right step it and the row shows which
     * of the three is on — the same shape as the AI-model row, without the
     * settle, since none of these costs anything to enter and leave. */
    { CTL_ROW_DISPLAY_MODE, CTL_CAT_DISPLAY, CTL_KIND_CHOICE, "Screens", "displays",
      .help = "Extend, Duplicate, or built-in off (closed lid). Also m in Super+D" },
    /* ⚠ THE ONE THAT MAKES EVERYTHING BIGGER, and it is not the Text scale row
     * under Appearance. That one sizes text inside the suite's own QML windows
     * and can reach neither a panel synui draws in cairo nor Firefox; this
     * scales the DESKTOP, so the compositor's panels, every application and
     * the cursor all grow together and stay sharp. Somebody who wants a larger
     * desktop wants this one, so it says so in the row and in the help.
     * Handled by id like the row above it — setting it re-flows the layout and
     * touches every output, which is not a field write. */
    { CTL_ROW_DISPLAY_SCALE, CTL_CAT_DISPLAY, CTL_KIND_CHOICE,
      "Scale everything", "displays",
      .help = "Every screen, every app, the cursor. Super+Ctrl+= / - , "
              "Super+Ctrl+0 resets" },
    /* Where a MONITOR is configured, as opposed to arranged. The Displays panel
     * above owns the arrangement, scale and the mode synui drives; syn-settings'
     * display pane reads the connectors' KERNEL state beside it — EDID, the
     * modes the hardware offers, what is actually plugged in — which a
     * compositor panel showing its own view of the world cannot tell you when
     * the two disagree, and that disagreement is the whole class of "the screen
     * is there and nothing comes out of it". */
    { CTL_ROW_MONITORS, CTL_CAT_DISPLAY, CTL_KIND_LAUNCH, "Monitor settings", "settings display",
      .help = "Connectors, EDID and the modes the hardware offers (syn-settings)" },

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
    /* Do Not Disturb sits under Sound, not Desktop, because this is the
     * category somebody opens when they want the machine to stop making noise.
     * It is FIRST in it for the same reason. */
    { CTL_ROW_DND,          CTL_CAT_SOUND, CTL_KIND_TOGGLE, "Do Not Disturb", NULL,
      .section = "Notifications",
      .help = "Super+Shift+M anywhere. Hides toasts and mutes the chime; "
              "critical alerts still come through" },
    { CTL_ROW_SOUNDS,       CTL_CAT_SOUND, CTL_KIND_PANEL,  "Event sounds", "sounds" },

    /* Tri-state, and `auto` is the useful position rather than a hedge: it
     * resolves to on where following a screen is what you want (a laptop) and
     * off where it is a nuisance (a desk whose monitors are always plugged in
     * and whose HDMI pins are live all day). CTL_APPLY_NONE — this is read
     * when a screen is plugged in, so the store IS the change. */
    { CTL_ROW_HDMI_AUDIO,   CTL_CAT_SOUND, CTL_KIND_VALUE, "Screen audio", NULL,
      .section = "Audio",
      .key = "hdmi_audio", .off = CFG(hdmi_audio), .vtype = CTL_VAL_TRI,
      .apply = CTL_APPLY_NONE,
      .help = "Move sound to a TV or monitor when you plug it in. Auto = laptops" },
    { CTL_ROW_EQUALIZER,    CTL_CAT_SOUND, CTL_KIND_PANEL,  "Equalizer", "equalizer",
      .help = "10-band system equalizer. Adds an output device while it is on" },

    { CTL_ROW_RECORD_AUDIO, CTL_CAT_SOUND, CTL_KIND_TOGGLE, "Record audio", NULL,
      .section = "Recording" },
    { CTL_ROW_RECORD_EDIT,  CTL_CAT_SOUND, CTL_KIND_TOGGLE, "Record for editing", NULL,
      .help = "DNxHR .mov that video editors read directly. About 1.1 GB/min" },

    /* Network. Two of the three hand off to something synui does not own —
     * nmtui in a terminal, cups in a browser — so they close the panel rather
     * than arming a return to it. */
    { CTL_ROW_NETWORK,   CTL_CAT_NETWORK, CTL_KIND_LAUNCH, "Network / Wi-Fi", "network",
      .section = "Network" },
    { CTL_ROW_BLUETOOTH, CTL_CAT_NETWORK, CTL_KIND_PANEL,  "Bluetooth",       "bluetooth" },

    { CTL_ROW_PRINTERS,  CTL_CAT_NETWORK, CTL_KIND_LAUNCH, "Printers",        "printers",
      .section = "Printers" },
    /* The row that comes BEFORE opening an admin page. CUPS's web UI can do
     * everything and starts by asking which discovery protocol to use and which
     * driver to install — two questions whose answer, for any network printer
     * sold this decade, is "ask the printer". This finds them and sets them up
     * driverless, and reports by toast because the panel is gone by then. */
    { CTL_ROW_PRINTERS_SCAN, CTL_CAT_NETWORK, CTL_KIND_LAUNCH, "Find printers",
      "printers_scan",
      .help = "Add every network printer that is not set up yet, driverless" },

    /* Power */
    { CTL_ROW_POWER, CTL_CAT_POWER, CTL_KIND_PANEL,  "Power saving", "power",
      .section = "Power",
      .help = "Idle timeouts for dim, blank, lock and suspend" },
    { CTL_ROW_SAVER, CTL_CAT_POWER, CTL_KIND_PANEL,  "Screensaver",  "saver",
      .help = "What the screen shows when idle, and how the lock screen looks" },
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
    { CTL_ROW_GAME_CONFINE_POINTER, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Keep the mouse on the game's screen", NULL,
      .key = "game_confine_pointer", .off = CFG(game_confine_pointer), .vtype = CTL_VAL_BOOL,
      .help = "Games rarely ask for it themselves; Alt-Tab still frees it" },
    { CTL_ROW_GAME_QUIET_KMOD, CTL_CAT_POWER, CTL_KIND_TOGGLE, "Quiet the kernel monitor while gaming", NULL,
      .key = "game_quiet_kmod", .off = CFG(game_quiet_kmod), .vtype = CTL_VAL_BOOL,
      .help = "Saves very little, and security monitoring pauses with it" },

    /* System */
    { CTL_ROW_AI_BACKEND, CTL_CAT_SYSTEM, CTL_KIND_TOGGLE, "AI backend",        NULL,
      .section = "AI",
      .help = "Which device synapd runs inference on" },
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
    /* syn-settings, the settings APP. The two are not rivals and the split is
     * not arbitrary: THIS panel configures the desktop that is drawing it, and
     * cannot exist where synui is not running; syn-settings configures the
     * SYSTEM — clock, locale, kernel, default applications, the AI backend —
     * and runs anywhere. Neither was findable from the other, so somebody
     * looking for the timezone in the obvious place (Super+C) found nothing
     * and had no way to learn where it actually lives. */
    { CTL_ROW_SETTINGS, CTL_CAT_SYSTEM, CTL_KIND_LAUNCH, "System settings", "settings",
      .help = "Clock, locale, kernel, default apps — what the SYSTEM is set to" },

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

    /*
     * ⚠ THE TWO NAMED RUNGS ARE CHECKED FOR BOTH NUMERIC TYPES, and that is a
     * fix rather than a tidy-up. The vauto test used to live inside the FLOAT
     * case only, so an INT row with a `.vauto` — which is the Appearance ▸ Glass
     * row, the one row that has one — drew its deferring rung as the raw
     * sentinel: the panel said "-1 %" where it meant "Auto", and wrote "-1"
     * where it meant "auto".
     *
     * That is most of why the row was impossible to read. Its bottom end went
     * "-1 %", "0 %", "5 %" — two of those three being modes rather than
     * amounts, and neither of them saying so — and the middle one, a real and
     * deliberate "no glass at all", was a plausible-looking number sitting
     * exactly where a user aiming for "let the theme decide" would land.
     *
     * `-1` also does not survive the round trip: config.c clamps a negative
     * glass_level to 0, so a row left on Auto and written as "-1" comes back as
     * Off. One spelling in the file whatever the row calls the rung on screen,
     * so config_parse_kv has a single token to recognise.
     */
    case CTL_VAL_INT:
    case CTL_VAL_FLOAT: {
        if (it->vauto && v < it->vmin) {
            snprintf(buf, n, "%s", for_config ? "auto" : it->vauto);
            break;
        }
        /* And the other end of the same confusion: a row whose zero is a MODE
         * rather than a quantity says so. Only on screen — the config file
         * keeps the number, because 0 is a perfectly good value for it and
         * inventing a token would be a second spelling to parse. */
        if (it->vzero && !for_config && v == 0.0f) {
            snprintf(buf, n, "%s", it->vzero);
            break;
        }
        if (it->vtype == CTL_VAL_INT) {
            if (!for_config && it->unit) snprintf(buf, n, "%d %s", (int)v, it->unit);
            else                          snprintf(buf, n, "%d", (int)v);
        } else {
            /* Two decimals is enough for every float here (opacities, blur
             * weights, a shadow sigma) and reads better than the six %g would
             * give. */
            if (!for_config && it->unit) snprintf(buf, n, "%.2f %s", v, it->unit);
            else                          snprintf(buf, n, "%.2f", v);
        }
        break;
    }

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
        /*
         * ⚠ THE SYNC HAS TO RE-RUN HERE, NOT ONLY AT LOAD, and until it did the
         * Glass row was a slider you had to log out to see the effect of.
         * synui_config_apply_glass_sync() was called from exactly one place —
         * the tail of synui_config_load — so moving the row wrote a new level
         * into the config and nothing recomputed the five alphas that level is
         * FOR. The panels changed, because syn_glass_resolve() reads the level
         * live; the windows, the terminal, the bar and the dock did not.
         *
         * Then theme_glass_refresh(), which re-saves theme.state — where the
         * bar and the widgets read the resolved bar_opacity and dock_opacity.
         * That is the only path across the process boundary, so a slider move
         * that skipped it would move the compositor's half of the desktop and
         * leave quickshell's half behind.
         *
         * And the dock, which paints its body from dock_opacity and has to be
         * rebuilt to pick a new one up — it is not part of the buffer walk
         * uifx_apply does.
         */
        synui_config_apply_glass_sync(&s->config);
        uifx_apply(s);
        theme_glass_refresh(s);
        dock_rebuild(s);
        dock_relayout(s);
        ctlpanel_repaint(s);
        break;

    case CTL_APPLY_SHADOW:
    case CTL_APPLY_BLURDATA:
        /* One hook for both: uifx_apply() pushes the global blur data,
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

    case CTL_APPLY_WALLPAPER:
        wallpaper_relayout(s);
        ctlpanel_repaint(s);
        break;

    /* No wallpaper_relayout() with it: the per-output measurement is cached and
     * the picture on screen is not what changed. This re-publishes the answer
     * and re-resolves the desktop's colours from it. */
    case CTL_APPLY_WPACCENT:
        wallpaper_accent_refresh(s);
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
 * ── Which glass rows have been taken off the slider ──────────────────────────
 *
 * One writer for the pin set, because the bitmask and its settings.state line
 * have to move together: a pin that is set in memory and not written comes back
 * released at the next login, which is a row that quietly starts following the
 * slider again some days after you took it off.
 */
static void ctl_glass_pins_set(syn_server_t *s, int pins)
{
    /* The body moved to settings.c so the transparency slider — which is not
     * this panel and edits the same driven row — writes the pin the same way.
     * See synui_glass_pins_store(). */
    synui_glass_pins_store(&s->config, pins);
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
    /* font.state is written by synui-apply-font(1) and by nothing else, this
     * panel included. Writing the number a second time into settings.state
     * would give it two homes that disagree the moment synfiles moves the
     * scale from its own dialog. */
    if (it->external) return;
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
    /* An external row has no field to read and no defaults snapshot to compare
     * against — synui-apply-font owns both. Answering "default" suppresses the
     * "· default" tag and the reset, which is right: the row cannot know, and
     * claiming otherwise would put a wrong tag on a true value. */
    if (it->external) return 1;

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
static int ctl_commit(syn_server_t *s, const struct ctl_item *it, float v);

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
        /* The auto rung sits one step below the minimum and is entered and left
         * only from that minimum, so the range keeps its two ends and neither
         * direction can get stuck. Stepping DOWN off vmin lands on it; stepping
         * UP from it lands back on vmin rather than on vmin+step, which would
         * skip the first real value on the way in. */
        if (it->vauto && v < it->vmin) {
            if (dir <= 0) return 0;            /* already at the bottom */
            v = it->vmin;
        } else {
            v += dir * step;
            if (v < it->vmin) v = it->vauto ? CTL_AUTO : it->vmin;
            if (v > it->vmax) v = it->vmax;
        }
        break;
    }

    default:
        return 0;
    }

    return ctl_commit(s, it, v);
}

/*
 * Put a row at an exact value, and do everything that follows from it.
 *
 * Split out of ctl_adjust so that something which sets a row outright — the
 * "Make it all solid" action below — travels the identical path a keypress
 * does. The pinning, the release and the persist are not decoration: a second
 * writer that set the field and called ctl_apply would leave a pinned row still
 * glassy, or a settings.state that disagrees with the screen, and it would do
 * it silently. There is one way to change a row.
 */
static int ctl_commit(syn_server_t *s, const struct ctl_item *it, float v)
{
    float before = ctl_get(&s->config, it);
    ctl_put(s, it, v);
    if (ctl_get(&s->config, it) == before) return 0;   /* already at the end */

    /*
     * ── Editing a synced row is how you pin it ───────────────────────────────
     *
     * ⚠ BEFORE ctl_apply, AND THAT ORDER IS THE WHOLE FEATURE. CTL_APPLY_GLASS
     * re-runs the sync, and the sync overwrites every row it still drives — so a
     * pin set afterwards would be set on a field that had already been put back
     * to the slider's number, and the drag would appear to do nothing at all.
     *
     * There is no separate "pin" control and there should not be: taking hold of
     * a row IS the act of claiming it, and a switch you had to find and flip
     * first would make the common case — nudge the dock, keep everything else
     * together — a two-step. Switching the sync off entirely is still there for
     * anyone who wants the old five-independent-rows desktop.
     *
     * ⚠ SET OR CLEARED, NEVER JUST SET, AND THE RULE IS THE ROW'S OWN DEFAULT:
     * a row is pinned exactly when settings.state records it.
     *
     * Pinning on every move alone leaves two states the panel cannot draw
     * honestly, both reached by an ordinary drag. Take the dock down and back up
     * to exactly 0.72 and the value IS the compiled default — so ctl_persist
     * drops the key, the modified dot goes out, and the pin survives with
     * nothing anywhere recording the number it pins. Worse on a row with an auto
     * rung: Right then Left puts Bar opacity back on "Follow the theme", which is
     * the row saying it has NO opinion, and it would have been pinned there —
     * the slider blocked by a row that is explicitly declining to answer.
     *
     * So the pin tracks the value: an opinion pins, and returning to the default
     * — by the arrow, exactly as Delete does it — releases. One rule, and it is
     * the same one settings.state already follows.
     */
    int pin = syn_glass_pin_by_name(it->key);
    if (pin && s->config.glass_sync)
        ctl_glass_pins_set(s, ctlpanel_row_is_default(s, it->row)
                              ? (s->config.glass_pins & ~pin)
                              : (s->config.glass_pins | pin));

    /* …and switching the sync back ON releases the lot. This is the "until
     * someone turns auto sync back on" half: one flip re-claims every row that
     * was ever taken, rather than making you hunt down which ones you moved. */
    if (it->row == CTL_ROW_GLASS_SYNC && s->config.glass_sync)
        ctl_glass_pins_set(s, 0);

    /*
     * ── …and the master rows handing the five back ───────────────────────────
     *
     * Auto, and Sync off, are both "the slider is no longer driving these". The
     * rows must go back to what the next login will give them, or the desktop on
     * screen and the desktop in settings.state stop being the same desktop — see
     * synui_config_glass_release(). Before ctl_apply for the reason the pin is.
     */
    if ((it->row == CTL_ROW_GLASS_LEVEL || it->row == CTL_ROW_GLASS_SYNC) &&
        !(s->config.glass_sync && syn_glass_set(&s->config)))
        synui_config_glass_release(&s->config);

    ctl_apply(s, it->apply);
    ctl_persist(s, it);
    return 1;
}

/*
 * ── One press, and nothing is see-through ────────────────────────────────────
 *
 * The glass and the window translucency are the two things somebody who does
 * not want any of this has to switch off, and they were four rows apart with a
 * pin mechanism in between. Turning them off by hand meant Transparency, then
 * Glass to Off, and then — only if you knew pins existed — Sync all glass on
 * again to release whichever rows you had ever nudged, because a pinned bar
 * keeps its own alpha and stays glassy while the master says Off. Three
 * controls, one of them invisible until it bites.
 *
 * So this is the escape hatch: Appearance ▸ Make it all solid, `synctl dispatch
 * solid`, or a bind on `solid`.
 *
 * ⚠ IT IS ONE-WAY, deliberately. Restoring would mean remembering what the four
 * numbers were, somewhere that survives a logout, and a switch that half-
 * remembers is worse than one that does not pretend to: the rows above put any
 * of it back, and Delete on a row restores the shipped default. What this owes
 * the user is that it leaves NOTHING see-through — a leftover glassy dock after
 * pressing it is the whole failure — so it clears the pins outright rather than
 * relying on the sync toggle to do it, which it only does when the toggle
 * actually changes.
 */
void synui_effects_solid(syn_server_t *s)
{
    if (!s) return;

    /* Windows first: it is the one the user can see change immediately, and the
     * helper both re-pushes alpha to every surface and persists on its own. */
    if (s->config.transparency)
        transparency_set_enabled(s, false);

    /* ⚠ UNCONDITIONAL, and before the rows. A pin means "this surface stopped
     * following the master", so a bar or dock pinned at 0.55 would sit there
     * looking exactly like the thing that was just switched off. The sync row
     * below releases pins too, but only on a change — and it is already on for
     * most people, so relying on that leaves the common case broken. */
    ctl_glass_pins_set(s, 0);

    /* Then the master, then the level. The other order sets a level nothing is
     * listening to yet. */
    const struct ctl_item *sync  = ctl_item(CTL_ROW_GLASS_SYNC);
    const struct ctl_item *level = ctl_item(CTL_ROW_GLASS_LEVEL);
    if (sync)  ctl_commit(s, sync, 1.0f);
    if (level) ctl_commit(s, level, 0.0f);   /* 0 is the row's own "Off" rung */

    /*
     * ⚠ AND THE BAR AND DOCK STILL NEED SAYING OUTRIGHT — Glass at Off does not
     * make them opaque. syn_glass_bar_alpha() is `0.95 - 0.95t`, so the bottom
     * of the slider hands both strips 0.95 rather than 1.00: the curve was
     * fitted to the bar's historical default at the clear end, and it carried
     * the last 5% all the way down with it. Small, and exactly the sort of
     * small that has somebody turning every switch off and still seeing the
     * wallpaper through their dock.
     *
     * Set as rows, so each one pins itself. That is the honest record: solid is
     * an opinion this desktop now holds about those two surfaces, it shows as
     * modified, it survives a login, and Delete on either row hands it back to
     * the slider. Leaving them unpinned would mean the next nudge of Glass
     * silently made them see-through again.
     */
    const struct ctl_item *bar  = ctl_item(CTL_ROW_BAR_OPACITY);
    const struct ctl_item *dock = ctl_item(CTL_ROW_DOCK_OPACITY);
    if (bar)  ctl_commit(s, bar,  1.0f);
    if (dock) ctl_commit(s, dock, 1.0f);

    /*
     * ⚠ AND THE DESKTOP WIDGETS, WHICH NONE OF THE ABOVE REACHES.
     *
     * `widget_glass` defaults to AUTO — "follow the theme" — and on a glass
     * preset that resolves to glass whatever the two masters say, because the
     * widgets are quickshell's and the only thing they can ask is theme.state.
     * Every other see-through surface on the desktop went solid and the clock,
     * the sysmon and the notes stayed frosted, which reads as the row not having
     * worked rather than as a setting it does not cover.
     *
     * OFF and not AUTO: auto is the thing being overruled. It is set as a ROW so
     * it persists to settings.state and shows as modified, exactly like the two
     * above — this is now an opinion the desktop holds, and Delete on the row is
     * how it is given back.
     */
    const struct ctl_item *wg = ctl_item(CTL_ROW_WIDGET_GLASS);
    if (wg) ctl_commit(s, wg, (float)SYN_WIDGET_GLASS_OFF);

    snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
             "glass off \xc2\xb7 bar, dock and widgets solid \xc2\xb7 windows opaque");
    ctlpanel_repaint(s);
}

/*
 * The other one-press: every chrome surface loses its background outright.
 *
 * The mirror of synui_effects_solid() and deliberately not its inverse. Solid
 * can be blunt — it is a retreat to a desktop nobody can fail to read. This one
 * hands over the look that the frost floor stopped being a DEFAULT precisely
 * because it can fail, so the machinery that rescues it has to be left standing:
 *
 *   * `glass_legibility` IS NOT TOUCHED. It is what picks the ink off the
 *     wallpaper when the theme's own stops reading, and what puts the scrim
 *     behind a bar over a wallpaper where neither ink survives. Clearing the
 *     background and disabling the thing that makes a cleared background legible
 *     would be a row that hands somebody an unreadable desktop.
 *   * `transparency` IS turned ON. A surface with no background on a desktop
 *     with the master transparency off is a contradiction, and the row's label
 *     promises a look rather than a setting.
 *
 * ⚠ 0.00 IS SET AS A ROW ON BOTH STRIPS, so each pins itself — the same reason
 * solid does it. syn_glass_bar_alpha() bottoms out at SYN_BAR_ALPHA_FROSTED now,
 * so the Glass slider alone can no longer reach nothing however far it is
 * dragged; leaving these unpinned would mean the next nudge of Glass silently
 * put the frost back.
 *
 * One-way, like solid: no stored "before". The rows above put any of it back.
 */
void synui_effects_clear(syn_server_t *s)
{
    if (!s) return;

    if (!s->config.transparency)
        transparency_set_enabled(s, true);

    /* Unconditional and before the rows, exactly as in solid(): a bar pinned at
     * 0.95 would sit there looking like the thing that was just switched off. */
    ctl_glass_pins_set(s, 0);

    const struct ctl_item *sync  = ctl_item(CTL_ROW_GLASS_SYNC);
    const struct ctl_item *level = ctl_item(CTL_ROW_GLASS_LEVEL);
    if (sync)  ctl_commit(s, sync,  1.0f);
    if (level) ctl_commit(s, level, 100.0f);

    const struct ctl_item *bar  = ctl_item(CTL_ROW_BAR_OPACITY);
    const struct ctl_item *dock = ctl_item(CTL_ROW_DOCK_OPACITY);
    if (bar)  ctl_commit(s, bar,  0.0f);
    if (dock) ctl_commit(s, dock, 0.0f);

    /* ON and not AUTO, for the reason solid() sets it OFF: auto asks the theme,
     * and the answer being overruled here is the theme's. */
    const struct ctl_item *wg = ctl_item(CTL_ROW_WIDGET_GLASS);
    if (wg) ctl_commit(s, wg, (float)SYN_WIDGET_GLASS_ON);

    snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
             "glass full \xc2\xb7 bar and dock clear \xc2\xb7 ink off the wallpaper");
    ctlpanel_repaint(s);
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
    /* Belt and braces: ctlpanel_row_is_default() already answers 1 for these,
     * so the line below returns first. Stated anyway, because ctl_put() on a
     * row with no field would write through a bogus offset — a silent memory
     * corruption of whatever syn_config_t declares first. */
    if (it->external) return 0;
    if (ctlpanel_row_is_default(s, it->row)) return 0;

    /* Resetting a glass row RELEASES it back to the slider, which is the honest
     * reading of Delete on this panel: the key is dropped from settings.state, so
     * the row no longer records an opinion, and a pin left behind would be an
     * opinion recorded in the other file. Before the apply, for the reason the
     * pin in ctl_adjust is — the sync runs inside it. */
    ctl_glass_pins_set(s, s->config.glass_pins & ~syn_glass_pin_by_name(it->key));

    ctl_put(s, it, ctl_get(synui_config_defaults(), it));

    /* Delete on the sync row puts it back ON, which is the same release-the-lot
     * that switching it on by hand is. Delete on the Glass row puts the level
     * back to Auto, which is the same hand-back ctl_adjust does above. */
    if (it->row == CTL_ROW_GLASS_SYNC && s->config.glass_sync)
        ctl_glass_pins_set(s, 0);
    if ((it->row == CTL_ROW_GLASS_LEVEL || it->row == CTL_ROW_GLASS_SYNC) &&
        !(s->config.glass_sync && syn_glass_set(&s->config)))
        synui_config_glass_release(&s->config);

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

/* ── The bar's auto-hide, read out of the bar's own file ──────────────────
 *
 * ⚠ THIS ONE SETTING IS PER MONITOR, and that is why the row looks different
 * from every other one on this panel.
 *
 * bar.json is the bar's own file: quickshell's right-click menu writes it and
 * nothing else does, which is exactly what lets the bar write it back without a
 * second writer to race (see the header of BarConfig.qml). A control-panel row
 * that wrote it would introduce that second writer for one switch.
 *
 * So the row READS the file and ASKS the bar to change it — synui_bar_ipc_arg()
 * over quickshell's IPC, the same direction the start menu and the mixer already
 * go. The bar stays the only writer.
 *
 * And because the setting is per monitor while the row is not, it is a MASTER:
 * it reports on/off/mixed across the live outputs and sets them all at once.
 * "mixed" is a real answer, not a fudge — the same one widgets_label() gives for
 * the same reason. Per-monitor control stays where it already is, on the bar's
 * right-click menu.
 */

/* Does bar.json say this output auto-hides? Absent output, absent file and
 * absent key all mean the default, which is off — a monitor nobody has touched
 * has the bar a fresh install has. Deliberately a scan and not a parser: the
 * file is two levels of flat objects written by JSON.stringify, and a JSON
 * parser in the compositor for one boolean is a dependency with a CVE feed. */
static bool bar_json_autohide(const char *json, const char *output)
{
    if (!json || !output || !*output) return false;

    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\"", output);
    const char *p = strstr(json, needle);
    if (!p) return false;

    p = strchr(p + strlen(needle), '{');
    if (!p) return false;

    /* The output's object, brace-counted so a key that happened to contain a
     * brace cannot run the search off into the next monitor's settings. */
    int depth = 0;
    const char *end = p;
    for (; *end; end++) {
        if (*end == '{') depth++;
        else if (*end == '}' && --depth == 0) break;
    }
    if (!*end) return false;

    for (const char *k = strstr(p, "\"autohide\""); k && k < end;
         k = strstr(k + 1, "\"autohide\"")) {
        const char *c = strchr(k, ':');
        if (!c || c > end) return false;
        c++;
        while (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') c++;
        return strncmp(c, "true", 4) == 0;
    }
    return false;
}

/* "on" when every live output's bar auto-hides, "off" when none does, "mixed"
 * when they disagree — and "n/a" with no bar to hide. */
static const char *bar_autohide_label(syn_server_t *s)
{
    if (!s->config.bar_enabled) return "n/a";

    char path[256];
    if (!syn_config_path(path, sizeof(path), "bar.json")) return "off";

    /* Small file — a few hundred bytes per monitor — so one read, no streaming.
     * A missing file is the normal case: nobody has changed anything yet. */
    char json[8192] = {0};
    FILE *f = fopen(path, "re");
    if (f) {
        size_t got = fread(json, 1, sizeof(json) - 1, f);
        json[got] = '\0';
        fclose(f);
    }

    int on = 0, total = 0;
    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (!o->wlr_output || !o->wlr_output->name) continue;
        total++;
        if (bar_json_autohide(json, o->wlr_output->name)) on++;
    }
    if (total == 0) return "off";
    if (on == 0)     return "off";
    return on == total ? "on" : "mixed";
}

/* ---- the RGB bridge, as a row -------------------------------------------
 *
 * syn-rgb(1) owns the state and the hardware; this reads its file to draw the
 * row and runs the command to change it. Nothing here knows what OpenRGB is,
 * which is the point: the row is a switch on a script, and the script is
 * where the colour, the brightness and the device quirks live.
 */
static int synrgb_is_on(void)
{
    char path[256];
    syn_config_path(path, sizeof(path), "rgb.state");
    if (!path[0]) return 0;

    FILE *f = fopen(path, "re");
    if (!f) return 0;   /* no file is the shipped state: off */

    char line[256];
    int on = 0;
    while (fgets(line, sizeof(line), f)) {
        char v[32];
        /* ⚠ `on=` and not `on =`: syn-rgb writes the same key=value shape
         * every other state file in this directory uses. */
        if (sscanf(line, "on=%31s", v) == 1)
            on = (strcmp(v, "yes") == 0);
    }
    fclose(f);
    return on;
}

static void synrgb_toggle(syn_server_t *s)
{
    int on = synrgb_is_on();

    /* ⚠ Through the COMMAND, never by writing the file. `syn-rgb on` also
     * enables the systemd path unit and pushes the colour immediately; a row
     * that only flipped a key would leave the lights unchanged until the next
     * wallpaper, which reads as the switch not working. */
    synui_spawn(on ? "syn-rgb off" : "syn-rgb on");
    snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
             on ? "RGB lights: off"
                : "RGB lights: following the accent");
    ctlpanel_repaint(s);
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
        /* "n/a" when the dock is off entirely: hide-behaviour is moot. The
         * three rows below take the same line for the same reason. */
        if (!s->config.dock_enabled) snprintf(buf, n, "n/a");
        else snprintf(buf, n, "%s", s->config.dock_autohide ? "on" : "off");
        break;
    case CTL_ROW_DOCK_ON_TOP:
        /* Also n/a while the dock auto-hides: a summoned dock is always on top,
         * so the switch would appear to be set and be ignored. */
        if (!s->config.dock_enabled)      snprintf(buf, n, "n/a");
        else if (s->config.dock_autohide) snprintf(buf, n, "always");
        else snprintf(buf, n, "%s", s->config.dock_on_top ? "on" : "off");
        break;
    case CTL_ROW_DOCK_MAGNIFY:
        if (!s->config.dock_enabled) snprintf(buf, n, "n/a");
        else snprintf(buf, n, "%s", s->config.dock_magnify ? "on" : "off");
        break;
    case CTL_ROW_DOCK_CLOCK:
        if (!s->config.dock_enabled) snprintf(buf, n, "n/a");
        else snprintf(buf, n, "%s", s->config.dock_clock ? "on" : "off");
        break;
    case CTL_ROW_DOCK_CLOCK_ANALOG:
        /* "n/a" for a dock with no clock as well as for no dock: this row is a
         * style, and there is nothing to style. */
        if (!s->config.dock_enabled || !s->config.dock_clock)
            snprintf(buf, n, "n/a");
        else snprintf(buf, n, "%s", s->config.dock_clock_analog ? "on" : "off");
        break;
    case CTL_ROW_DOCK_APPS:
        if (!s->config.dock_enabled) snprintf(buf, n, "n/a");
        else snprintf(buf, n, "%s", s->config.dock_apps_button ? "on" : "off");
        break;
    case CTL_ROW_DOCK_POWER:
        if (!s->config.dock_enabled) snprintf(buf, n, "n/a");
        else snprintf(buf, n, "%s", s->config.dock_power_button ? "on" : "off");
        break;
    /* "n/a" for a cell that is switched off as well as for no dock, exactly as
     * the analog-face row does: a position for something not drawn is a value
     * with nothing to be about. */
    case CTL_ROW_DOCK_CLOCK_POS:
    case CTL_ROW_DOCK_APPS_POS:
    case CTL_ROW_DOCK_POWER_POS: {
        dock_cell_t c = row == CTL_ROW_DOCK_CLOCK_POS ? DOCK_CELL_CLOCK
                      : row == CTL_ROW_DOCK_APPS_POS  ? DOCK_CELL_APPS
                                                      : DOCK_CELL_POWER;
        int on = c == DOCK_CELL_CLOCK ? s->config.dock_clock
               : c == DOCK_CELL_APPS  ? s->config.dock_apps_button
                                      : s->config.dock_power_button;
        if (!s->config.dock_enabled || !on) snprintf(buf, n, "n/a");
        else snprintf(buf, n, "%s", dock_slot_label(s, c));
        break;
    }
    case CTL_ROW_BAR_AUTOHIDE:
        snprintf(buf, n, "%s", bar_autohide_label(s));
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
    case CTL_ROW_DND:
        /* Say what it is doing to the machine, not just "on". The count is the
         * part nobody can get from anywhere else — the whole point of the mode
         * is that those toasts were never drawn. */
        if (s->config.notif_dnd && s->notifs.missed > 0)
            snprintf(buf, n, "on \xe2\x80\x94 %d missed", s->notifs.missed);
        else
            snprintf(buf, n, "%s", s->config.notif_dnd ? "on" : "off");
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
    case CTL_ROW_HDMI_AUDIO:
        /* Auto is resolved on screen, the same way the game-mode row spells out
         * what "auto" currently amounts to. "auto" alone would leave the one
         * question this row is asked — will it move my sound or not? —
         * unanswered on the only screen that mentions the setting, and the
         * answer differs per machine by design. */
        if (s->config.hdmi_audio > 0)       snprintf(buf, n, "on");
        else if (s->config.hdmi_audio == 0) snprintf(buf, n, "off");
        else snprintf(buf, n, "auto (%s)",
                      power_has_battery() ? "on \xc2\xb7 laptop" : "off \xc2\xb7 desktop");
        break;
    case CTL_ROW_DISPLAY_MODE:
        /* Words, not the config spellings: "external" is what the file says and
         * "Built-in off" is what the setting DOES, which is the thing somebody
         * reading this row wants to know. The two tables are allowed to differ
         * here precisely because this row does not persist by lower-casing its
         * display name the way the enum rows do — dispcfg_set_mode_cfg() writes
         * syn_display_mode_names[] itself. */
        switch (s->config.display_mode) {
        case SYN_DISPLAY_MIRROR:   snprintf(buf, n, "Duplicate");    break;
        case SYN_DISPLAY_EXTERNAL: snprintf(buf, n, "Built-in off"); break;
        default:                   snprintf(buf, n, "Extend");       break;
        }
        break;
    case CTL_ROW_DISPLAY_SCALE: {
        /* A percentage, because that is what every other desktop's scale row
         * says and what somebody comparing them expects to read. The stored
         * value is wlroots' float; 1.25 is "125 %". */
        double v = (double)dispcfg_scale_now(s) * 100.0;
        snprintf(buf, n, "%.0f %%", v);
        break;
    }
    case CTL_ROW_RGB_LIGHTS:
        /* Read off rgb.state every time rather than cached, so the row is
         * right when `syn-rgb on` was typed in a terminal a moment ago. One
         * short file read on a repaint of a panel that is open. */
        snprintf(buf, n, "%s", synrgb_is_on() ? "On" : "Off");
        break;
    case CTL_ROW_UI_FONT_SIZE:
    case CTL_ROW_UI_TEXT_SCALE: {
        /* Read off font.state every time rather than cached, so the row is
         * right when synfiles' own text-size slider moved the scale a moment
         * ago. It is a four-line file read on a repaint of a panel that is
         * open, which is nowhere near a hot path.
         *
         * While a change is settling the PENDING value is shown instead: the
         * file still holds the old one, and a row that snapped back to it
         * between the keypress and the apply would read as the key not having
         * worked. */
        int size = 0, scale = 0;
        fontpick_state_read(&size, &scale);
        int pend = (row == CTL_ROW_UI_FONT_SIZE) ? s->ctlpanel.font_pending_size
                                                 : s->ctlpanel.font_pending_scale;
        int v = pend > 0 ? pend
                         : (row == CTL_ROW_UI_FONT_SIZE ? size : scale);
        snprintf(buf, n, "%d %s", v,
                 row == CTL_ROW_UI_FONT_SIZE ? "pt" : "%");
        break;
    }
    default: {
        /* The table-driven rows, which is now most of them: read the field the
         * item names and format it by its type. A row with no `off` at all —
         * every jump-off — formats to nothing, which is what leaves the value
         * column empty for it. */
        const struct ctl_item *it = ctl_item(row);
        if (it && it->vtype != CTL_VAL_NONE) ctl_format(it, ctl_get(&s->config, it), 0, buf, n);
        else                                 buf[0] = '\0';

        /*
         * A row the Glass slider is currently driving says so, and shows the
         * number as well as the word.
         *
         * Both halves matter. Without "synced" the five rows look like five
         * independent settings that mysteriously move on their own, which is
         * exactly the complaint the sync exists to answer; without the number
         * you cannot see WHAT the slider decided, and the row stops being a
         * readout of the desktop. The tag disappears the moment you drag the
         * row, because dragging it is what pins it — so the row's own state is
         * the whole explanation of why it did or did not move.
         */
        if (it && buf[0] && syn_glass_drives(&s->config,
                                (syn_glass_pin_t)syn_glass_pin_by_name(it->key))) {
            char v[64];
            snprintf(v, sizeof(v), "%s", buf);
            snprintf(buf, n, "synced \xc2\xb7 %s", v);
        }
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

int ctlpanel_row_options(int row)
{
    const struct ctl_item *it = ctl_item(row);
    return (it && it->vtype == CTL_VAL_ENUM) ? it->nnames : 0;
}

/* ── Shortcuts column ────────────────────────────────────── */

/* What a bind action does, in words. An action with no entry here still lists —
 * it falls back to the action name — so a bind added to input.c and forgotten
 * here degrades to "slightly terse", not "missing from the panel". */
/*
 * ⚠ FILE SCOPE, NOT A FUNCTION STATIC, AND THAT IS THE POINT NOW.
 *
 * It was a lookup table: hand it an action, get a description. It is also the
 * ROSTER — the complete list of things this desktop can put a key on — and the
 * palette needs to walk it, because "every action with no chord on it" is what
 * makes unbinding a shortcut reversible. Deriving that list from anywhere else
 * would be a second roster, and a second roster is one that goes stale.
 */
static const struct { const char *action, *desc; } action_tbl[] = {
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
        { "settings",          "System settings (syn-settings)" },
        { "printers_scan",     "Find and add network printers" },
        { "overview",          "Mission control (all windows)" },   /* unbound: Alt+Tab */
        { "keybinds",          "Rebind a shortcut" },
        { "night_light",       "Night light" },
        { "dnd",               "Do Not Disturb (mute notifications)" },
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
        { "move_left",         "Move window left (or earlier in the layout)" },
        { "move_right",        "Move window right (or later in the layout)" },
        { "move_up",           "Move window up (or earlier in the layout)" },
        { "move_down",         "Move window down (or later in the layout)" },
        { "float_toggle",      "Float window" },
        { "fullscreen_toggle", "Fullscreen window" },
        { "maximize_toggle",   "Maximize window" },
        { "expand_v_toggle",   "Fill screen height (or put it back)" },
        { "expand_h_toggle",   "Fill screen width (or put it back)" },
        { "minimize_toggle",   "Minimize window" },
        { "minimize_restore",  "Restore minimized window" },
        { "decorations_toggle","Titlebars on/off" },
        { "displays",          "Display settings" },
        { "display_mode",      "Screens: extend / duplicate / built-in off" },
        { "display_scale",     "Scale the whole desktop (accessibility)" },
        { "wallpaper",         "Wallpaper picker" },
        { "wallpaper_reload",  "Reload wallpaper / config" },
        { "filters",           "Visual effects (CRT + window)" },
        { "widgets",           "Desktop widget manager" },
        { "sounds",            "Event sounds" },
        { "effects_toggle",    "CRT effects on/off" },
        { "power",             "Power saving panel" },
        { "saver",             "Screensaver + lock screen" },
        { "taskmgr",           "Task manager" },
        { "aimodel",           "AI model" },
        { "network",           "Network / Wi-Fi" },
        { "game",              "Game mode" },
        { "lock",              "Lock screen" },
        { "ai_backend",        "AI backend (GPU/CPU/off)" },
    { "move_output",       "Move window to next output" },
};
#define ACTION_TBL_N ((int)(sizeof action_tbl / sizeof action_tbl[0]))

/*
 * The roster, for the palette. `i` walks 0..ctlpanel_action_count(); each is a
 * built-in action, and `desc` is the same string action_desc() would return.
 *
 * `spawn` and `spawn_toggle` are deliberately NOT in it and must not be: they
 * are only meaningful as the thing they spawn, and a bare "spawn" row would be
 * a shortcut you could bind a key to that runs nothing. Those two arrive
 * through the APP and COMMAND rows instead, which carry the command with them.
 */
int ctlpanel_action_count(void) { return ACTION_TBL_N; }

const char *ctlpanel_action_at(int i, const char **desc)
{
    if (i < 0 || i >= ACTION_TBL_N) return NULL;
    if (desc) *desc = action_tbl[i].desc;
    return action_tbl[i].action;
}

static const char *action_desc(syn_server_t *s, const char *action,
                               const char *arg)
{
    /* A spawn bind is only meaningful as the thing it spawns — for either
     * spelling of it. spawn_toggle rows read as the command too rather than as
     * "<command> (toggle)": the palette is a list of what the keys OPEN, and
     * the difference between the two is what the key does the second time. */
    if ((strcmp(action, "spawn") == 0 || strcmp(action, "spawn_toggle") == 0)
        && arg && *arg)
        return arg;

    /* The start menu is three different things depending on start_menu_style,
     * and this list exists to say what the keys actually DO. A row that always
     * read "Start menu" would be the list disagreeing with the keyboard —
     * exactly what the tap row's comment in ctlpanel_shortcuts() refuses to do
     * about which modifier the tap is on. Both halves of the tap row now come
     * from the live config, not from a word compiled in here. */
    if (s && strcmp(action, "start_menu") == 0) {
        switch (s->config.start_menu_style) {
        case SYN_START_MENU_APPGRID: return "Start menu (application page)";
        case SYN_START_MENU_ROFI:    return "Start menu (rofi)";
        case SYN_START_MENU_BAR:
        default:                     return "Start menu (bar menu)";
        }
    }

    for (int i = 0; i < ACTION_TBL_N; i++)
        if (strcmp(action, action_tbl[i].action) == 0) {
            /* move_output takes a direction; "prev" is a different line. */
            if (strcmp(action, "move_output") == 0 && arg && strcmp(arg, "prev") == 0)
                return "Move window to previous output";
            return action_tbl[i].desc;
        }
    return action;
}

/* The same table, for callers outside this file. The rebind helper needs it to
 * name the shortcut a chord is ALREADY taken by, and deriving that from the
 * shortcut list would mean searching a list of strings for the row that happens
 * to hold the same action — with the answer depending on which of the three
 * `spawn` rows it found first. */
const char *ctlpanel_action_desc(syn_server_t *s, const char *action,
                                 const char *arg)
{
    return action_desc(s, action, arg);
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
    /* And the rest of the punctuation this desktop binds, for the same reason:
     * the shortcuts column, the palette and `synctl binds` all read "Super+/"
     * on a keycap and "super+slash" in a config file, and only one of those is
     * a list of keys to press. */
    case XKB_KEY_slash:     snprintf(out, n, "/");         return;
    case XKB_KEY_question:  snprintf(out, n, "?");         return;
    case XKB_KEY_comma:     snprintf(out, n, ",");         return;
    case XKB_KEY_period:    snprintf(out, n, ".");         return;
    case XKB_KEY_semicolon: snprintf(out, n, ";");         return;
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
 * so the panel and the config spell the same combo the same way.
 *
 * Exported (as ctlpanel_combo_str) for ctlpanel_tap_key_name()'s reason: the
 * shortcut palette names the keys too, and so does `synctl binds` — which is
 * how the welcome guide gets them from outside the process. A second spelling
 * of "Super+Shift+C" is a second one to keep in step. */
void ctlpanel_combo_str(uint32_t mods, xkb_keysym_t sym, char *out, size_t n)
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

/* Is any chord in the live table bound to this action? The tap counts: it is a
 * key too, and an action the tap opens is not one you have lost. */
static bool action_is_bound(syn_server_t *s, const char *action)
{
    if (strcmp(s->config.tap_action, action) == 0) return true;
    for (int i = 0; i < s->config.bind_count; i++)
        if (strcmp(s->config.binds[i].action, action) == 0) return true;
    return false;
}

int ctlpanel_shortcuts(syn_server_t *s, syn_ctl_shortcut_t *out, int max)
{
    return ctlpanel_shortcuts_ex(s, out, max, false);
}

/*
 * ⚠ `include_unbound` IS A PARAMETER AND NOT A SECOND FUNCTION, which is the
 * whole reason this file has one shortcut list rather than two. The control
 * panel's Shortcuts column is a READ-ONLY list of what the keys do and wants
 * only the chords that exist; the palette is where keys are assigned and needs
 * the actions that have none, or removing a shortcut is a one-way door. Same
 * walk, same strings, one flag — a copy of this function with thirty extra rows
 * in it is exactly the drift ctlpanel_shortcuts() was written to end.
 */
int ctlpanel_shortcuts_ex(syn_server_t *s, syn_ctl_shortcut_t *out, int max,
                          bool include_unbound)
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
                 action_desc(s, s->config.tap_action, s->config.tap_arg));
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
        ctlpanel_combo_str(b->mods, b->sym, out[n].combo, sizeof(out[n].combo));
        snprintf(out[n].desc, sizeof(out[n].desc), "%s",
                 action_desc(s, b->action, b->arg));
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

    /*
     * ── Everything with no key on it ────────────────────────────────────────
     *
     * The rows that make unbinding reversible, and the reason this list is
     * "what a key could go on" rather than "what the bind table holds".
     *
     * They come off the SAME roster action_desc() answers from, so an action
     * added there is bindable from the palette with no second edit — and one
     * that is never given a default chord in config.c is reachable for the
     * first time, which several of them (`printers_scan`, `cascade`,
     * `launcher_style`) never were.
     *
     * Bound-ness is asked per ACTION and not per chord: two chords on one
     * action is fine and neither makes it unbound, and an action the tap opens
     * is not one you have lost either.
     */
    if (!include_unbound) return n;

    for (int i = 0; i < ctlpanel_action_count() && n < max; i++) {
        const char *desc = NULL;
        const char *action = ctlpanel_action_at(i, &desc);
        if (!action || action_is_bound(s, action)) continue;

        memset(&out[n], 0, sizeof(out[n]));
        /* No chord, and the column says so rather than being blank: an empty
         * cell in a list of keys reads as a row that failed to draw. */
        snprintf(out[n].combo, sizeof(out[n].combo), "\xe2\x80\x94");
        snprintf(out[n].desc,  sizeof(out[n].desc),  "%s", desc ? desc : action);
        snprintf(out[n].action, sizeof(out[n].action), "%s", action);
        out[n].rebindable = 1;
        out[n].kind       = SYN_SC_UNBOUND;
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

/* Defined with the rest of the pending-change handling, below the panel's own
 * show/hide — the tick is the earliest caller, not the natural home. */
static void ctlpanel_font_commit(syn_server_t *s);

/*
 * Called once per frame from output_frame. Returns 1 while it wants more frames.
 *
 * Three rows need it now, and for different reasons. The AI-model row is
 * waiting for the cursor to settle before it commits a pick; the two font.state
 * rows are waiting for the same thing, to keep a held arrow key from running
 * synui-apply-font once per repeat. The AI-backend row is waiting for a value
 * it does not own: it is read back from a file the synui-ai-backend helper
 * writes after restarting synapd, which lands whenever it lands, so the panel
 * has to look again rather than be told. Everything else on the panel is state
 * synui owns and changes synchronously, so the keypress that changed it also
 * repaints.
 *
 * All three stop the moment they are done, so an idle panel costs nothing.
 */
int ctlpanel_tick(syn_server_t *s)
{
    /* Closing the panel abandons the model pick and the backend poll: there is
     * nothing left to repaint, and a pick the user walked away from is not one
     * to act on. */
    if (!s->ctlpanel.visible) {
        s->ctlpanel.backend_poll_until = 0.0;
        s->ctlpanel.model_commit_at    = 0.0;
        /* …but the font rows COMMIT rather than drop. ctlpanel_hide() already
         * does this; the call here covers the panel being taken down by some
         * other path (a lock, a session end) between the keypress and the
         * settle. Committing twice is a no-op — the deadline is cleared. */
        ctlpanel_font_commit(s);
        return 0;
    }

    if (s->ctlpanel.font_commit_at != 0.0) {
        if (ctl_now_secs() >= s->ctlpanel.font_commit_at) {
            ctlpanel_font_commit(s);
            /* The row draws the pending value while one is set and the file's
             * value once it is cleared, so the panel has to repaint here or it
             * keeps showing a number that is now merely the truth by accident. */
            synui_render_ctlpanel(s);
        } else {
            return 1;   /* still settling — keep the frames coming */
        }
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
    /* Nothing is pending on a panel that has only just opened. Cleared rather
     * than assumed: ctlpanel_hide() commits and zeroes these, but the panel can
     * also go down without it (a lock), and a stale pending value would draw a
     * row at a number the desktop is not at. */
    s->ctlpanel.font_commit_at     = 0.0;
    s->ctlpanel.font_pending_size  = 0;
    s->ctlpanel.font_pending_scale = 0;
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
    /* The font rows go the OTHER way: a size still settling is APPLIED on the
     * way out, not dropped. Closing the panel is not "I changed my mind", it is
     * "I am done" — and unlike a model load there is nothing expensive or
     * irreversible about honouring it. Dropping it here is what would make a
     * quick change-and-close silently do nothing. */
    ctlpanel_font_commit(s);
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
    if (strcmp(action, "saver") == 0)     return s->saver.visible;
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
    /* The font rows resolve here too, and they resolve the other way: leaving
     * one APPLIES it. Every "the cursor left" path in this file — arrow keys,
     * the pointer moving to another row, switching category, opening the
     * search, Esc backing out of the pane — funnels through this function, so
     * putting the commit here is what makes all of them agree.
     *
     * Esc included, deliberately. It means "not that" for a model load because
     * that is expensive and one-way; a font size is neither, and in practice
     * the settle has already fired by the time anyone reaches for Esc. A key
     * that usually applied the change and occasionally did not, depending on
     * how fast you pressed it, would be worse than one that always does.
     *
     * Before the early return below, which only concerns the model row. */
    ctlpanel_font_commit(s);

    if (s->ctlpanel.model_commit_at == 0.0) return;
    s->ctlpanel.model_commit_at = 0.0;
    s->ctlpanel.status[0] = '\0';
    /* Put the row back on the model that is actually loaded, so an abandoned
     * cycle does not leave the panel naming one that never got asked for. */
    aimodel_row_sync(s);
}

/*
 * Fire whatever the font rows have been sitting on.
 *
 * ⚠ This is the OPPOSITE of ctlpanel_cancel_pending() above, and deliberately
 * so. Leaving the AI-model row abandons the pick, because loading a model is
 * expensive and "I moved away" is how you say no. Leaving a font row must
 * APPLY it: the number on screen is what the user asked for, the change is
 * cheap and reversible, and a size that quietly reverted because the panel was
 * closed too quickly is the sort of thing that reads as the setting being
 * broken. So every exit path commits — moving off the row, closing the panel,
 * Esc.
 *
 * Both can be pending at once (change the size, then the scale, inside one
 * settle window). They are spawned back to back and the script serialises
 * itself with flock — without that, whichever finished second would write the
 * other's stale value back over it. See the lock at the top of
 * synui-apply-font.sh.
 */
static void ctlpanel_font_commit(syn_server_t *s)
{
    if (s->ctlpanel.font_commit_at == 0.0) return;
    s->ctlpanel.font_commit_at = 0.0;

    if (s->ctlpanel.font_pending_size > 0) {
        fontpick_push_size(s, s->ctlpanel.font_pending_size);
        s->ctlpanel.font_pending_size = 0;
    }
    if (s->ctlpanel.font_pending_scale > 0) {
        fontpick_push_scale(s, s->ctlpanel.font_pending_scale);
        s->ctlpanel.font_pending_scale = 0;
    }
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

/*
 * Step one of the three dock-cell position rows; 0 if `row` is not one of them.
 *
 * They have no .key/.off/.vtype to drive ctl_adjust() with, because the thing
 * being stepped is a gap index with two sentinels rather than a number in a
 * range. Handled by id rather than made CTL_KIND_CHOICE: Enter on a CHOICE row
 * opens the panel that owns the setting, and there is no such panel — the
 * dock's own right-click menu is the other route, and it is a menu.
 *
 * Shared by Left/Right and by Enter (and so by a click, which is Enter) so all
 * three routes step the same way and say the same thing.
 */
static int ctlpanel_dock_pos_step(syn_server_t *s, int row, int dir)
{
    dock_cell_t c;
    int on;
    switch (row) {
    case CTL_ROW_DOCK_CLOCK_POS:
        c = DOCK_CELL_CLOCK; on = s->config.dock_clock;        break;
    case CTL_ROW_DOCK_APPS_POS:
        c = DOCK_CELL_APPS;  on = s->config.dock_apps_button;  break;
    case CTL_ROW_DOCK_POWER_POS:
        c = DOCK_CELL_POWER; on = s->config.dock_power_button; break;
    default:
        return 0;
    }

    const struct ctl_item *it = ctl_item(row);
    const char *label = it ? it->label : "position";

    if (!s->config.dock_enabled || !on) {
        /* Says WHICH thing is off. "n/a" in the value column with nothing in
         * the status line is a key that appears to be dead. */
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status), "%s",
                 s->config.dock_enabled ? "turn the cell on first"
                                        : "dock is off");
        return 1;
    }

    dock_slot_cycle(s, c, dir);
    snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
             "%s: %s", label, dock_slot_label(s, c));
    return 1;
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
    case CTL_KIND_ACTION: {
        /* Split the action from its argument on the first space, exactly as the
         * bind parser does (config.c) — a row's action string IS a bind line,
         * and "settings display" has to mean the same thing on a row as it does
         * on a key. Passed whole it would be compared as one action name,
         * match nothing, and the row would be a dead button that logs nothing.
         *
         * Only here. A PANEL row's action is also the token ctlpanel_child_closed()
         * is armed with, so splitting it there would break the return path for
         * a gain nothing wants: a panel is opened, not parameterised. */
        const char *a = ctl_row_action(row);
        char verb[64] = "", argbuf[128] = "";
        if (a) {
            const char *sp = strchr(a, ' ');
            if (sp) {
                size_t n = (size_t)(sp - a);
                if (n >= sizeof(verb)) n = sizeof(verb) - 1;
                memcpy(verb, a, n);
                verb[n] = '\0';
                while (*sp == ' ') sp++;
                snprintf(argbuf, sizeof(argbuf), "%s", sp);
            } else {
                snprintf(verb, sizeof(verb), "%s", a);
            }
        }
        ctlpanel_hide(s);
        if (verb[0]) synui_binding_execute(s, verb, argbuf[0] ? argbuf : NULL);
        return;
    }
    case CTL_KIND_VALUE: {
        /* Enter on a number steps it forward, the same as Right. Not "nothing":
         * every other row on the panel does something on Enter, and a row that
         * ignored the key people press first would read as broken. Left/Right
         * remain the way to move it in both directions. */
        /* The dock-cell rows first: ctl_adjust() cannot move them (no vtype),
         * so without this Enter — and therefore a CLICK, which is Enter — was
         * dead on exactly the three rows added because the mouse could not
         * reach the setting any other way. */
        if (ctlpanel_dock_pos_step(s, row, +1)) return;
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

    /* ⛔ An EXTERNAL row first. The generic flip below reads and writes the
     * config field the item names, and an external row names none — so
     * ctl_get/ctl_put would read the top of syn_config_t as this row's value
     * and write a bool over whatever field is declared first. A plausible
     * number and a silent corruption, which is the pair the `external` flag
     * exists to prevent. */
    if (row == CTL_ROW_RGB_LIGHTS) { synrgb_toggle(s); return; }

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
     * So THE WRITE IS THE WORK: settings.state is the file the bar watches
     * (BarConfig.qml), and Bar.qml maps or unmaps its window off the key. That
     * is a change of shape, not a tidy-up. This row used to do its job by
     * running `bar_stop_cmd`, whose default was `pkill -x quickshell` — and the
     * bar's process is also the desktop widgets, the OSD, the start menu, the
     * mixer and the post-it notes, all mapped from one shell.qml. So switching
     * the bar off cleared the whole desktop: velle asked for the strip and lost
     * the visualiser, the big clock, the notes and Tux, with no message anywhere
     * connecting the two. Unmapping one window cannot do that.
     *
     * The flag is still kept so that reopening the panel says what the desktop
     * looks like rather than always "on".
     *
     * The command pair is now an ESCAPE HATCH for a bar that cannot be asked —
     * waybar — and is empty by default, so the usual desktop shells out to
     * nothing. When it is set it is fire-and-forget through synui_spawn like
     * every other shell-out here, so a command that is wrong for this desktop
     * fails silently; the help line on the row is where that is said, because
     * there is nothing to wait for and nothing to report.
     */
    case CTL_ROW_BAR: {
        s->config.bar_enabled = !s->config.bar_enabled;

        /* The key FIRST. It is what the shipped bar acts on, and a foreign
         * bar's command should not get a head start on the file that decides
         * what the panel and the bar both believe. */
        settings_state_set("bar_enabled", s->config.bar_enabled ? "on" : "off");

        const char *cmd = s->config.bar_enabled ? s->config.bar_start_cmd
                                                : s->config.bar_stop_cmd;
        if (cmd && *cmd) synui_spawn(cmd);

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
        dock_relayout(s);     /* the canvas changed shape; repaint every mirror */
        dock_wake(s);         /* pin or release the bar on the next frame */
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "dock auto-hide %s", s->config.dock_autohide ? "on" : "off");
        ctlpanel_repaint(s);
        return;

    /* The three below share the autohide row's shape: flip, persist to
     * dock.state (NOT settings.state — dock.state is where the dock's own
     * switches live, beside the edge and the pins), repaint every mirror and
     * wake them. */
    case CTL_ROW_DOCK_ON_TOP:
        if (!s->config.dock_enabled) {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "dock is off");
            ctlpanel_repaint(s);
            return;
        }
        if (s->config.dock_autohide) {
            /* Not a silent no-op: the row reads "always" in this state, and a
             * key that appeared to do nothing would read as a broken row. */
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "an auto-hiding dock is always on top");
            ctlpanel_repaint(s);
            return;
        }
        s->config.dock_on_top = !s->config.dock_on_top;
        dock_state_save(s);
        dock_relayout(s);
        dock_wake(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "dock %s windows",
                 s->config.dock_on_top ? "above" : "below");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_DOCK_CLOCK_ANALOG:
        if (!s->config.dock_enabled || !s->config.dock_clock) {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     s->config.dock_enabled ? "the dock clock is off"
                                            : "dock is off");
            ctlpanel_repaint(s);
            return;
        }
        s->config.dock_clock_analog = !s->config.dock_clock_analog;
        dock_state_save(s);
        dock_relayout(s);
        dock_wake(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "dock clock %s",
                 s->config.dock_clock_analog ? "analog" : "digital");
        ctlpanel_repaint(s);
        return;

    case CTL_ROW_DOCK_MAGNIFY:
    case CTL_ROW_DOCK_CLOCK:
    case CTL_ROW_DOCK_APPS:
    case CTL_ROW_DOCK_POWER: {
        if (!s->config.dock_enabled) {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "dock is off");
            ctlpanel_repaint(s);
            return;
        }
        int *flag;
        const char *what;
        switch (row) {
        case CTL_ROW_DOCK_MAGNIFY:
            flag = &s->config.dock_magnify;     what = "magnify"; break;
        case CTL_ROW_DOCK_CLOCK:
            flag = &s->config.dock_clock;       what = "clock";   break;
        case CTL_ROW_DOCK_APPS:
            flag = &s->config.dock_apps_button; what = "all-apps button"; break;
        default:
            flag = &s->config.dock_power_button; what = "power button"; break;
        }
        *flag = !*flag;
        dock_state_save(s);
        dock_relayout(s);
        dock_wake(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "dock %s %s", what, *flag ? "on" : "off");
        ctlpanel_repaint(s);
        return;
    }

    case CTL_ROW_BAR_AUTOHIDE: {
        /* Asks the bar; does not write bar.json. See bar_autohide_label().
         * "mixed" resolves to ON, because the reason two monitors disagree is
         * almost always that one was set from its own right-click menu and the
         * master is being reached for to finish the job. */
        const char *now = bar_autohide_label(s);
        if (strcmp(now, "n/a") == 0) {
            snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                     "the bar is off");
            ctlpanel_repaint(s);
            return;
        }
        bool want = strcmp(now, "on") != 0;
        synui_bar_ipc_arg(s, "bar", "autohide", want ? "on" : "off");
        /* No local flag to flip and nothing to persist: the bar owns both. The
         * row re-reads bar.json on the next repaint, so it catches up on its
         * own once the bar has written — which is why the status line says what
         * was ASKED for rather than claiming it is done. */
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "bar auto-hide %s on every monitor", want ? "on" : "off");
        ctlpanel_repaint(s);
        return;
    }

    case CTL_ROW_DND:
        /* notif_dnd_toggle(), not a flip of the config field: turning it on
         * also has to clear the cards already on screen and write dnd.state,
         * and turning it off has to report what was missed. A row that only
         * set the flag would leave a toast sitting there and forget the choice
         * at the next login. */
        notif_dnd_toggle(s);
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 s->config.notif_dnd
                     ? "notifications are silenced"
                     : "notifications are back on");
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

    /* The row the cursor was on is GONE — F3 takes its chord, and this list is
     * the bind table. Same clamp the rebind path above owes, for the same
     * reason: the next render builds a list one row shorter than the cursor. */
    ctlpanel_shortcut_scroll_to_sel(s);
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
/*
 * Left/Right on the two font.state rows.
 *
 * Same shape as the AI-model row and for the same reason, one order of
 * magnitude cheaper: `synui-apply-font --size` rewrites kdeglobals, three GTK
 * files, rofi's theme and every terminal's config, then SIGUSR1s the running
 * terminals. Doing that on the keypress would put a held arrow key through the
 * whole sequence once per repeat, and the terminals would strobe.
 *
 * So the row moves instantly (the pending value is what it draws) and the
 * script is not run until the cursor has been still for CTL_FONT_SETTLE_SECS.
 * Only the LAST value in a run of keypresses is ever applied.
 */
static int ctlpanel_adjust_font(syn_server_t *s, int row, int dir)
{
    const struct ctl_item *it = ctl_item(row);
    if (!it) return 0;

    int size = 0, scale = 0;
    fontpick_state_read(&size, &scale);

    const bool is_size = (row == CTL_ROW_UI_FONT_SIZE);
    int *pend = is_size ? &s->ctlpanel.font_pending_size
                        : &s->ctlpanel.font_pending_scale;

    /* Step from the pending value once there is one, so a second press
     * continues from where the first left it rather than from the file, which
     * has not been written yet. */
    int cur = *pend > 0 ? *pend : (is_size ? size : scale);
    int v   = cur + dir * (int)it->vstep;

    if (v < (int)it->vmin) v = (int)it->vmin;
    if (v > (int)it->vmax) v = (int)it->vmax;
    if (v == cur) {
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "%s is at its %s", it->label, dir < 0 ? "minimum" : "maximum");
        return 1;
    }

    *pend = v;
    s->ctlpanel.font_commit_at = ctl_now_secs() + CTL_FONT_SETTLE_SECS;

    /* Says that it has not happened yet, because it has not. The alternative —
     * a row showing 14pt while the desktop is still at 10 and nothing saying
     * why — is the same confusion the AI-model row's message exists to avoid. */
    snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
             "%s: %d %s \xc2\xb7 applies when you stop",
             it->label, v, is_size ? "pt" : "%");
    return 1;
}

static int ctlpanel_adjust_value(syn_server_t *s, int row, int dir)
{
    /* The three dock-cell positions, before the table lookup — see
     * ctlpanel_dock_pos_step(). */
    if (ctlpanel_dock_pos_step(s, row, dir)) return 1;

    const struct ctl_item *it = ctl_item(row);
    if (!it || it->vtype == CTL_VAL_NONE) return 0;

    /* ⚠ Two external rows now, and they are not the same one. Dispatched by
     * ROW rather than by the flag: `external` says only "the value is not in
     * the config struct", and sending a lighting toggle to the font stepper
     * would move the desktop's text size. */
    if (row == CTL_ROW_RGB_LIGHTS) { synrgb_toggle(s); return 1; }
    if (it->external) return ctlpanel_adjust_font(s, row, dir);

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
    if (row == CTL_ROW_DISPLAY_SCALE) {
        dispcfg_scale_step_all(s, dir);
        /* dispcfg wrote the outcome — including a refusal, which this row must
         * repeat rather than replace: "everything at 1.50x" and "1.50x would
         * leave less than 800x500 to work with" are the two things a reader of
         * this row needs, and only one of them is the value. */
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "%s", s->dispcfg.status);
        return;
    }
    if (row == CTL_ROW_DISPLAY_MODE) {
        /* Acts on the keypress, unlike the model row below. Every one of the
         * three is entered and left in a few milliseconds and none of them
         * loses anything, so there is nothing a settle timer would protect. */
        int m = (s->config.display_mode + dir + SYN_DISPLAY_MODE_COUNT)
                % SYN_DISPLAY_MODE_COUNT;
        dispcfg_set_mode_cfg(s, m);
        char v[64];
        ctlpanel_row_value(s, row, v, sizeof(v));
        snprintf(s->ctlpanel.status, sizeof(s->ctlpanel.status),
                 "Screens: %s", v);
        return;
    }

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
    /* A windowed panel does not own the pointer: off it, the wheel belongs to
     * whatever is under the cursor, so take nothing. The same guard
     * ctlpanel_motion() opens with, and it has to be here too — the modal panel
     * below deliberately answers the wheel from ANYWHERE on the desktop (see
     * the `else` at the bottom, which moves the focused column), and a windowed
     * panel doing that would eat every client's scroll for as long as it was
     * open. */
    if (s->ctlpanel.visible && panel_is_windowed(s, SYN_PDRAG_CTLPANEL) &&
        !hit_in_panel(&s->ctlpanel.hit, lx, ly))
        return 0;

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
