/*
 * theme.c — the theme manager (Super+T) and the presets behind it.
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
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/types/wlr_damage_ring.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* Short tokens: what synuirc `theme =` and theme.state store. */
const char *const syn_theme_names[SYN_THEME_COUNT] = {
    [SYN_THEME_SYNAPSE]    = "synapse",
    [SYN_THEME_DARK]       = "dark",
    [SYN_THEME_WINXP]      = "winxp",
    [SYN_THEME_WIN95]      = "win95",
    [SYN_THEME_CATPPUCCIN] = "catppuccin",
    [SYN_THEME_GRUVBOX]    = "gruvbox",
    [SYN_THEME_TOKYONIGHT] = "tokyonight",
    [SYN_THEME_NORD]       = "nord",
    [SYN_THEME_DRACULA]    = "dracula",
    [SYN_THEME_BUBBLEGUM]  = "bubblegum",
    [SYN_THEME_MACOS26]    = "macos26",
    [SYN_THEME_AQUA]       = "aqua",
    [SYN_THEME_PLATINUM]   = "platinum",
    [SYN_THEME_PRISM]      = "prism",
    [SYN_THEME_PRISM_LIGHT] = "prism-light",
};

/* What the panel shows a human. */
const char *theme_name(syn_theme_t t)
{
    switch (t) {
    case SYN_THEME_SYNAPSE:    return "SYNAPSE (neon)";
    case SYN_THEME_DARK:       return "Dark";
    case SYN_THEME_WINXP:      return "Windows XP";
    case SYN_THEME_WIN95:      return "Windows 95";
    case SYN_THEME_CATPPUCCIN: return "Catppuccin Mocha";
    case SYN_THEME_GRUVBOX:    return "Gruvbox Dark";
    case SYN_THEME_TOKYONIGHT: return "Tokyo Night";
    case SYN_THEME_NORD:       return "Nord";
    case SYN_THEME_DRACULA:    return "Dracula";
    case SYN_THEME_BUBBLEGUM:  return "Bubblegum";
    case SYN_THEME_MACOS26:    return "macOS 26 (Tahoe)";
    case SYN_THEME_AQUA:       return "Mac OS X 10.0 (Aqua)";
    case SYN_THEME_PLATINUM:   return "Mac OS 8.1 (Platinum)";
    case SYN_THEME_PRISM:      return "SYNAPSE Prism";
    case SYN_THEME_PRISM_LIGHT: return "SYNAPSE Prism Light";
    default:                   return "?";
    }
}

/* ── Presets ─────────────────────────────────────────────── */
/* Colours are RGBA 0..1. `scheme` is what synui-apply-theme is told: "dark" or
 * "light" picks the toolkit palette; `accent_*` (0..255) is the selection colour
 * it hands KDE/GTK so Dolphin's highlight matches the desktop's focus border. */
typedef struct {
    float border_norm[4], border_focus[4], border_ai[4], border_warn[4];
    float tb_norm[4], tb_focus[4], tb_text[4], tb_text_focus[4];
    /* Caption gradient ends + the 3D face colour. A flat theme leaves the two
     * gradient ends NULL-equivalent (all zero alpha) and theme_load_colors then
     * copies the caption colours into them, so deco.c never has to ask which
     * kind of theme it is drawing — see syn_chrome_t. */
    float tb_grad_norm[4], tb_grad_focus[4], face[4];
    syn_chrome_t chrome;
    float active_opacity, inactive_opacity;
    /* panel_accent: the colour synui's OWN panels (menu, control panel, every
     * overlay) draw with — headers, selections, rules. Tuned to read on the
     * dark panel chrome, so it is NOT the window border colour: Win95's navy
     * would vanish on a dark panel, so its accent is a legible periwinkle. */
    float panel_accent[4];
    /* panel_bg / panel_ink: the SURFACE synui's own panels are drawn on and the
     * colour their text is drawn in. Only the accent used to be theme data, so
     * every panel — control panel, desktop menu, calendar, task manager, all 19
     * of them — stayed the same near-black navy whatever theme was picked, with
     * one themed accent laid over it. On a light theme that is not a theme at
     * all: XP's beige desktop opened a black control panel.
     *
     * Alpha 0 means "derive from the base and text pair below", which every theme
     * wants — the panel is the same surface as an app window, so a rice's panels
     * are the rice's colours for free. SYNAPSE sets panel_bg explicitly because
     * its historical panel navy is DARKER than its window face, and stock has to
     * stay pixel-identical. See render_set_panel_surface(). */
    float panel_bg[4], panel_ink[4];
    const char *scheme;              /* "dark" | "light" */
    int   accent_r, accent_g, accent_b;
    /* glyph_*: the colour the BAR's module glyphs (cpu/mem/net/audio/gamemode)
     * are drawn in. Normally the same as accent_* — the bar tracks the desktop
     * accent on a theme switch, which is the point. SYNAPSE is the exception:
     * its accent is the neon magenta selection colour, but the launcher caret
     * beside those glyphs is hard-coded teal (LAUNCHER_R/G/B in launcher.c), so
     * taking the accent here split the bar into two clashing colours. Teal here
     * keeps the neon bar reading as one piece; every other theme still recolours. */
    int   glyph_r, glyph_g, glyph_b;
    /* base and text: the app window face and its foreground, handed to
     * synui-apply-theme so Dolphin/GTK get THIS theme's palette rather than a
     * generic Breeze light/dark. That is what makes a rice a rice — Gruvbox
     * windows have to be Gruvbox brown, not "some dark grey" — and it is also
     * how XP gets its #ECE9D8 beige and 95 its #C0C0C0 silver. */
    int   base_r, base_g, base_b;
    int   text_r, text_g, text_b;
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
        /* The historical panel navy, kept exactly. It is darker than this
         * theme's window face (30,30,36), so deriving would lighten every
         * panel on the one theme that must not change. */
        .panel_bg      = { 0.06f, 0.06f, 0.12f, 1.0f },
        .panel_ink     = { 0.95f, 0.95f, 1.00f, 1.0f },
        .scheme = "dark", .accent_r = 255, .accent_g = 41, .accent_b = 109,
        /* #05d9e8 — the launcher caret's teal, not the magenta accent. */
        .glyph_r = 0x05, .glyph_g = 0xd9, .glyph_b = 0xe8,
        .base_r = 30, .base_g = 30, .base_b = 36,        /* the historical pair */
        .text_r = 235, .text_g = 235, .text_b = 242,
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
        .glyph_r = 61, .glyph_g = 125, .glyph_b = 255,   /* follows the accent */
        .base_r = 30, .base_g = 30, .base_b = 36,
        .text_r = 235, .text_g = 235, .text_b = 242,
    },
    /* WINDOWS XP (Luna, "Blue"). Every colour here is a real Luna registry
     * value rather than one picked by eye: ActiveTitle #0054E3 with
     * GradientActiveTitle #3D95FF, InactiveTitle #7A96DF / #9DB9EB, the
     * unmistakable #ECE9D8 beige face, and Explorer's #316AC5 selection.
     * SYN_CHROME_LUNA is what actually sells it — the caption is a real vertical
     * gradient with rounded top corners and the red pill close button (deco.c);
     * a flat mid-blue bar never read as XP no matter how right the hex was.
     * The frame is the beige face, as in Luna: only the caption is blue. */
    [SYN_THEME_WINXP] = {
        .border_norm  = { 0.925f, 0.914f, 0.847f, 1.0f },  /* #ECE9D8 face */
        .border_focus = { 0.925f, 0.914f, 0.847f, 1.0f },
        .border_ai    = { 0.192f, 0.416f, 0.773f, 1.0f },  /* #316AC5 selection */
        .border_warn  = { 0.788f, 0.239f, 0.157f, 1.0f },  /* #C93D28 */
        .tb_norm       = { 0.478f, 0.588f, 0.875f, 1.0f },  /* #7A96DF */
        .tb_focus      = { 0.000f, 0.329f, 0.890f, 1.0f },  /* #0054E3 */
        .tb_grad_norm  = { 0.616f, 0.725f, 0.922f, 1.0f },  /* #9DB9EB */
        .tb_grad_focus = { 0.239f, 0.584f, 1.000f, 1.0f },  /* #3D95FF */
        .face          = { 0.925f, 0.914f, 0.847f, 1.0f },  /* #ECE9D8 */
        .chrome = SYN_CHROME_LUNA,
        .tb_text       = { 0.847f, 0.894f, 0.973f, 1.0f },  /* #D8E4F8 */
        .tb_text_focus = { 1.00f, 1.00f, 1.00f, 1.0f },
        .active_opacity = 1.0f, .inactive_opacity = 1.0f,   /* XP was never glassy */
        .panel_accent  = { 0.36f, 0.62f, 1.00f, 1.0f },  /* Luna blue, brightened */
        .scheme = "light", .accent_r = 49, .accent_g = 106, .accent_b = 197,
        .glyph_r = 49, .glyph_g = 106, .glyph_b = 197,   /* follows the accent */
        .base_r = 236, .base_g = 233, .base_b = 216,     /* #ECE9D8 */
        .text_r = 0, .text_g = 0, .text_b = 0,
    },
    /* WINDOWS 95 — the real VGA system colours (the 95 control-panel defaults):
     * ActiveTitle #000080 navy, InactiveTitle #808080, #C0C0C0 silver face. Two
     * accuracy fixes over the first pass: the frame is SILVER in both states (95
     * never coloured the border with the caption — the navy is the caption bar
     * only), and the inactive caption text is #C0C0C0, not the #D4D0C8 that
     * arrived with Win98/2000. 95 had no gradient at all (GradientActiveTitle is
     * a 98 feature) so both ends match; what makes it read as 95 is
     * SYN_CHROME_BEVEL — the raised 3D frame and square bevelled buttons. */
    [SYN_THEME_WIN95] = {
        .border_norm  = { 0.753f, 0.753f, 0.753f, 1.0f },  /* #C0C0C0 face */
        .border_focus = { 0.753f, 0.753f, 0.753f, 1.0f },
        .border_ai    = { 0.753f, 0.753f, 0.753f, 1.0f },
        .border_warn  = { 0.502f, 0.000f, 0.000f, 1.0f },  /* #800000 */
        .tb_norm       = { 0.502f, 0.502f, 0.502f, 1.0f },  /* #808080 */
        .tb_focus      = { 0.000f, 0.000f, 0.502f, 1.0f },  /* #000080 navy */
        .tb_grad_norm  = { 0.502f, 0.502f, 0.502f, 1.0f },  /* no gradient in 95 */
        .tb_grad_focus = { 0.000f, 0.000f, 0.502f, 1.0f },
        .face          = { 0.753f, 0.753f, 0.753f, 1.0f },  /* #C0C0C0 */
        .chrome = SYN_CHROME_BEVEL,
        .tb_text       = { 0.753f, 0.753f, 0.753f, 1.0f },  /* #C0C0C0 */
        .tb_text_focus = { 1.00f, 1.00f, 1.00f, 1.0f },
        .active_opacity = 1.0f, .inactive_opacity = 1.0f,
        .panel_accent  = { 0.45f, 0.60f, 0.95f, 1.0f },  /* navy, legible on dark */
        .scheme = "light", .accent_r = 0, .accent_g = 0, .accent_b = 128,
        .glyph_r = 0, .glyph_g = 0, .glyph_b = 128,      /* follows the accent */
        .base_r = 192, .base_g = 192, .base_b = 192,     /* #C0C0C0 */
        .text_r = 0, .text_g = 0, .text_b = 0,
    },
    /* ── The rices ───────────────────────────────────────────
     * Upstream palette hex, unmodified, so a synui desktop sits beside a
     * matching terminal/editor colourscheme without clashing. All six lean
     * faintly glassy by default (0.95/0.89) — translucency is half the aesthetic
     * — but that is still only a default: the master switch stays the user's. */
    /* CATPPUCCIN MOCHA — base #1E1E2E, mantle #181825, surface0 #313244,
     * text #CDD6F4, mauve #CBA6F7, sky #89DCEB, red #F38BA8. */
    [SYN_THEME_CATPPUCCIN] = {
        .border_norm  = { 0.192f, 0.196f, 0.267f, 1.0f },  /* #313244 surface0 */
        .border_focus = { 0.796f, 0.651f, 0.969f, 1.0f },  /* #CBA6F7 mauve */
        .border_ai    = { 0.537f, 0.863f, 0.922f, 1.0f },  /* #89DCEB sky */
        .border_warn  = { 0.953f, 0.545f, 0.659f, 1.0f },  /* #F38BA8 red */
        .tb_norm       = { 0.094f, 0.094f, 0.145f, 1.0f },  /* #181825 mantle */
        .tb_focus      = { 0.118f, 0.118f, 0.180f, 1.0f },  /* #1E1E2E base */
        .tb_text       = { 0.424f, 0.439f, 0.525f, 1.0f },  /* #6C7086 overlay0 */
        .tb_text_focus = { 0.804f, 0.839f, 0.957f, 1.0f },  /* #CDD6F4 text */
        .active_opacity = 0.95f, .inactive_opacity = 0.89f,
        .panel_accent  = { 0.796f, 0.651f, 0.969f, 1.0f },  /* mauve */
        .scheme = "dark", .accent_r = 203, .accent_g = 166, .accent_b = 247,
        .glyph_r = 203, .glyph_g = 166, .glyph_b = 247,
        .base_r = 30, .base_g = 30, .base_b = 46,           /* #1E1E2E */
        .text_r = 205, .text_g = 214, .text_b = 244,        /* #CDD6F4 */
    },
    /* GRUVBOX DARK (hard) — bg0_h #1D2021, bg0 #282828, bg1 #3C3836,
     * fg1 #EBDBB2, orange #FE8019, aqua #8EC07C, red #FB4934. */
    [SYN_THEME_GRUVBOX] = {
        .border_norm  = { 0.235f, 0.220f, 0.212f, 1.0f },  /* #3C3836 bg1 */
        .border_focus = { 0.996f, 0.502f, 0.098f, 1.0f },  /* #FE8019 orange */
        .border_ai    = { 0.557f, 0.753f, 0.486f, 1.0f },  /* #8EC07C aqua */
        .border_warn  = { 0.984f, 0.286f, 0.204f, 1.0f },  /* #FB4934 red */
        .tb_norm       = { 0.114f, 0.125f, 0.129f, 1.0f },  /* #1D2021 bg0_h */
        .tb_focus      = { 0.157f, 0.157f, 0.157f, 1.0f },  /* #282828 bg0 */
        .tb_text       = { 0.659f, 0.600f, 0.518f, 1.0f },  /* #A89984 gray */
        .tb_text_focus = { 0.922f, 0.859f, 0.698f, 1.0f },  /* #EBDBB2 fg1 */
        .active_opacity = 0.95f, .inactive_opacity = 0.89f,
        .panel_accent  = { 0.996f, 0.502f, 0.098f, 1.0f },  /* orange */
        .scheme = "dark", .accent_r = 254, .accent_g = 128, .accent_b = 25,
        .glyph_r = 254, .glyph_g = 128, .glyph_b = 25,
        .base_r = 40, .base_g = 40, .base_b = 40,           /* #282828 */
        .text_r = 235, .text_g = 219, .text_b = 178,        /* #EBDBB2 */
    },
    /* TOKYO NIGHT (storm) — bg #24283B, bg_dark #1F2335, fg #C0CAF5,
     * blue #7AA2F7, purple #BB9AF7, cyan #7DCFFF, red #F7768E. */
    [SYN_THEME_TOKYONIGHT] = {
        .border_norm  = { 0.239f, 0.263f, 0.373f, 1.0f },  /* #3D435F */
        .border_focus = { 0.478f, 0.635f, 0.969f, 1.0f },  /* #7AA2F7 blue */
        .border_ai    = { 0.490f, 0.812f, 1.000f, 1.0f },  /* #7DCFFF cyan */
        .border_warn  = { 0.969f, 0.463f, 0.557f, 1.0f },  /* #F7768E red */
        .tb_norm       = { 0.122f, 0.137f, 0.208f, 1.0f },  /* #1F2335 bg_dark */
        .tb_focus      = { 0.141f, 0.157f, 0.231f, 1.0f },  /* #24283B bg */
        .tb_text       = { 0.337f, 0.369f, 0.494f, 1.0f },  /* #565F89 comment */
        .tb_text_focus = { 0.753f, 0.792f, 0.961f, 1.0f },  /* #C0CAF5 fg */
        .active_opacity = 0.95f, .inactive_opacity = 0.89f,
        .panel_accent  = { 0.733f, 0.604f, 0.969f, 1.0f },  /* #BB9AF7 purple */
        .scheme = "dark", .accent_r = 122, .accent_g = 162, .accent_b = 247,
        .glyph_r = 122, .glyph_g = 162, .glyph_b = 247,
        .base_r = 36, .base_g = 40, .base_b = 59,           /* #24283B */
        .text_r = 192, .text_g = 202, .text_b = 245,        /* #C0CAF5 */
    },
    /* NORD — nord0 #2E3440 … nord3 #4C566A, nord4 #D8DEE9, frost #88C0D0 /
     * #81A1C1, aurora red #BF616A. */
    [SYN_THEME_NORD] = {
        .border_norm  = { 0.231f, 0.259f, 0.322f, 1.0f },  /* #3B4252 nord1 */
        .border_focus = { 0.533f, 0.753f, 0.816f, 1.0f },  /* #88C0D0 nord8 */
        .border_ai    = { 0.506f, 0.631f, 0.757f, 1.0f },  /* #81A1C1 nord9 */
        .border_warn  = { 0.749f, 0.380f, 0.416f, 1.0f },  /* #BF616A nord11 */
        .tb_norm       = { 0.180f, 0.204f, 0.251f, 1.0f },  /* #2E3440 nord0 */
        .tb_focus      = { 0.231f, 0.259f, 0.322f, 1.0f },  /* #3B4252 nord1 */
        .tb_text       = { 0.298f, 0.337f, 0.416f, 1.0f },  /* #4C566A nord3 */
        .tb_text_focus = { 0.847f, 0.871f, 0.914f, 1.0f },  /* #D8DEE9 nord4 */
        .active_opacity = 0.95f, .inactive_opacity = 0.89f,
        .panel_accent  = { 0.533f, 0.753f, 0.816f, 1.0f },  /* frost */
        .scheme = "dark", .accent_r = 136, .accent_g = 192, .accent_b = 208,
        .glyph_r = 136, .glyph_g = 192, .glyph_b = 208,
        .base_r = 46, .base_g = 52, .base_b = 64,           /* #2E3440 */
        .text_r = 216, .text_g = 222, .text_b = 233,        /* #D8DEE9 */
    },
    /* DRACULA — bg #282A36, current-line #44475A, fg #F8F8F2, purple #BD93F9,
     * pink #FF79C6, cyan #8BE9FD, red #FF5555. */
    [SYN_THEME_DRACULA] = {
        .border_norm  = { 0.267f, 0.278f, 0.353f, 1.0f },  /* #44475A */
        .border_focus = { 0.741f, 0.576f, 0.976f, 1.0f },  /* #BD93F9 purple */
        .border_ai    = { 0.545f, 0.914f, 0.992f, 1.0f },  /* #8BE9FD cyan */
        .border_warn  = { 1.000f, 0.333f, 0.333f, 1.0f },  /* #FF5555 red */
        .tb_norm       = { 0.129f, 0.137f, 0.180f, 1.0f },  /* #21232E */
        .tb_focus      = { 0.157f, 0.165f, 0.212f, 1.0f },  /* #282A36 bg */
        .tb_text       = { 0.384f, 0.447f, 0.643f, 1.0f },  /* #6272A4 comment */
        .tb_text_focus = { 0.973f, 0.973f, 0.949f, 1.0f },  /* #F8F8F2 fg */
        .active_opacity = 0.95f, .inactive_opacity = 0.89f,
        .panel_accent  = { 1.000f, 0.475f, 0.776f, 1.0f },  /* #FF79C6 pink */
        .scheme = "dark", .accent_r = 255, .accent_g = 121, .accent_b = 198,
        .glyph_r = 189, .glyph_g = 147, .glyph_b = 249,     /* purple beside pink */
        .base_r = 40, .base_g = 42, .base_b = 54,           /* #282A36 */
        .text_r = 248, .text_g = 248, .text_b = 242,        /* #F8F8F2 */
    },
    /* BUBBLEGUM — the one LIGHT rice: pastel pink shell, hot-pink caption, mint
     * for the AI accent so it is not pink-on-pink. Apps go light on a #FFE9F2
     * face, which is what stops Dolphin breaking the spell with plain white. The
     * bar glyphs go a deeper #D6337A: this is the only rice whose bar is light,
     * and the caption pink would wash out on it. */
    [SYN_THEME_BUBBLEGUM] = {
        .border_norm  = { 0.976f, 0.847f, 0.902f, 1.0f },  /* #F9D8E6 */
        .border_focus = { 1.000f, 0.416f, 0.667f, 1.0f },  /* #FF6AAA hot pink */
        .border_ai    = { 0.427f, 0.878f, 0.812f, 1.0f },  /* #6DE0CF mint */
        .border_warn  = { 1.000f, 0.400f, 0.400f, 1.0f },  /* #FF6666 coral */
        .tb_norm       = { 0.988f, 0.788f, 0.875f, 1.0f },  /* #FCC9DF */
        .tb_focus      = { 1.000f, 0.416f, 0.667f, 1.0f },  /* #FF6AAA */
        .tb_text       = { 0.639f, 0.325f, 0.451f, 1.0f },  /* #A35373 */
        .tb_text_focus = { 1.000f, 1.000f, 1.000f, 1.0f },
        .active_opacity = 0.96f, .inactive_opacity = 0.90f,
        .panel_accent  = { 1.000f, 0.518f, 0.741f, 1.0f },  /* #FF84BD on dark chrome */
        .scheme = "light", .accent_r = 255, .accent_g = 106, .accent_b = 170,
        .glyph_r = 214, .glyph_g = 51, .glyph_b = 122,      /* #D6337A */
        .base_r = 255, .base_g = 233, .base_b = 242,        /* #FFE9F2 */
        .text_r = 61, .text_g = 26, .text_b = 42,           /* #3D1A2A */
    },
    /* ── The Macs ────────────────────────────────────────────
     * Three eras of one desktop, the same way winxp/win95 are two of another.
     * What makes each read as a Mac is SHAPE before colour — controls on the
     * left, a centred caption, and per-era: Tahoe's big radius, Aqua's
     * pinstripes and traffic lights, Platinum's racing stripes and close box.
     * See syn_chrome_t and the three caption painters in deco.c.
     *
     * Provenance, because these are not registry values the way Luna's are:
     * the 8.1 and 10.0 numbers were SAMPLED off the screenshots velle supplied
     * (Platinum's #9B9CCE desktop, its #DEDEDE face and the white/grey title
     * stripes; Aqua's #356CBC menu highlight, its #345CA5 desktop and the
     * near-white pinstriped caption). macOS 26's are Apple's published system
     * colours — systemBlue #007AFF, systemRed #FF3B30, label #1D1D1F,
     * secondarySystemBackground #F2F2F7 — plus the traffic-light hexes in
     * deco.c. Anything not on that list was tuned by eye, not measured. */
    /* macOS 26 "TAHOE" — Liquid Glass: near-white translucent chrome, a hairline
     * instead of a frame, and a corner radius big enough to be the theme (see
     * CHROME_LIQUID_RADIUS_MIN). This is the one that leans on the compositor's
     * own glass: it ships translucent by default, because a flat opaque white
     * window is Tahoe with the point removed. */
    [SYN_THEME_MACOS26] = {
        .border_norm  = { 0.839f, 0.839f, 0.855f, 1.0f },  /* #D6D6DA hairline */
        .border_focus = { 0.000f, 0.478f, 1.000f, 1.0f },  /* #007AFF systemBlue */
        .border_ai    = { 0.369f, 0.361f, 0.902f, 1.0f },  /* #5E5CE6 systemIndigo */
        .border_warn  = { 1.000f, 0.231f, 0.188f, 1.0f },  /* #FF3B30 systemRed */
        .tb_norm       = { 0.929f, 0.929f, 0.941f, 1.0f },  /* #EDEDF0 */
        .tb_focus      = { 0.969f, 0.969f, 0.980f, 1.0f },  /* #F7F7FA */
        /* The bottom end of the toolbar's very shallow ramp. Tahoe's glass is
         * nearly flat — a strong gradient here reads as Aqua, one era early. */
        .tb_grad_norm  = { 0.910f, 0.910f, 0.925f, 1.0f },  /* #E8E8EC */
        .tb_grad_focus = { 0.937f, 0.937f, 0.957f, 1.0f },  /* #EFEFF4 */
        .face          = { 0.949f, 0.949f, 0.969f, 1.0f },  /* #F2F2F7 */
        .chrome = SYN_CHROME_LIQUID,
        .tb_text       = { 0.557f, 0.557f, 0.576f, 1.0f },  /* #8E8E93 secondary */
        .tb_text_focus = { 0.114f, 0.114f, 0.122f, 1.0f },  /* #1D1D1F label */
        .active_opacity = 0.94f, .inactive_opacity = 0.88f,
        .panel_accent  = { 0.000f, 0.478f, 1.000f, 1.0f },  /* systemBlue */
        .scheme = "light", .accent_r = 0, .accent_g = 122, .accent_b = 255,
        /* Deeper than the accent: this bar is light, and #007AFF on near-white
         * is a glyph you have to look for. Same reason bubblegum's glyphs are. */
        .glyph_r = 0, .glyph_g = 86, .glyph_b = 214,        /* #0056D6 */
        .base_r = 245, .base_g = 245, .base_b = 247,        /* #F5F5F7 */
        .text_r = 29, .text_g = 29, .text_b = 31,           /* #1D1D1F */
    },
    /* MAC OS X 10.0 "AQUA" — the pinstriped grey caption with the three glossy
     * traffic lights, black centred title, and the blue focus ring that came
     * with it. The frame is grey in both states, as it was: what tells a focused
     * Aqua window from an unfocused one is that the stripes and the lights go
     * out, which SYN_CHROME_AQUA draws. Opaque, like XP — the transparency Aqua
     * showed off was in its menus and its shadows, not its window bodies. */
    [SYN_THEME_AQUA] = {
        .border_norm  = { 0.725f, 0.725f, 0.725f, 1.0f },  /* #B9B9B9 */
        .border_focus = { 0.208f, 0.424f, 0.737f, 1.0f },  /* #356CBC sampled */
        .border_ai    = { 0.204f, 0.361f, 0.647f, 1.0f },  /* #345CA5 desktop blue */
        .border_warn  = { 0.757f, 0.153f, 0.176f, 1.0f },  /* #C1272D */
        /* The caption ramp's two ends: tb_* is the DARK end at the bottom of the
         * bar, tb_grad_* the light end at the top — the same convention Luna
         * uses, so deco.c mixes both styles with one pair of colours. */
        .tb_norm       = { 0.902f, 0.902f, 0.902f, 1.0f },  /* #E6E6E6 */
        .tb_focus      = { 0.812f, 0.812f, 0.812f, 1.0f },  /* #CFCFCF */
        .tb_grad_norm  = { 0.969f, 0.969f, 0.969f, 1.0f },  /* #F7F7F7 */
        .tb_grad_focus = { 0.988f, 0.988f, 0.988f, 1.0f },  /* #FCFCFC */
        .face          = { 0.929f, 0.929f, 0.929f, 1.0f },  /* #EDEDED */
        .chrome = SYN_CHROME_AQUA,
        .tb_text       = { 0.478f, 0.478f, 0.478f, 1.0f },  /* #7A7A7A */
        .tb_text_focus = { 0.102f, 0.102f, 0.102f, 1.0f },  /* #1A1A1A */
        .active_opacity = 1.0f, .inactive_opacity = 1.0f,
        .panel_accent  = { 0.208f, 0.424f, 0.737f, 1.0f },  /* the menu blue */
        .scheme = "light", .accent_r = 53, .accent_g = 108, .accent_b = 188,
        .glyph_r = 53, .glyph_g = 108, .glyph_b = 188,
        .base_r = 236, .base_g = 236, .base_b = 236,        /* #ECECEC */
        .text_r = 0, .text_g = 0, .text_b = 0,
    },
    /* MAC OS 8.1 "PLATINUM" — grey on grey with the racing stripes across the
     * title bar, a square close box on the LEFT and collapse/zoom on the right.
     * The frame is a hard outline with no shadow (chrome_shadow drops it): a
     * Platinum window's only depth cue is that outline and the bevels inside it.
     * The signature colour of the era is the desktop, #9B9CCE, which is a
     * wallpaper rather than chrome — Super+W's picker owns that, the same
     * arrangement XP's Bliss has. */
    [SYN_THEME_PLATINUM] = {
        /* Real Platinum outlined active and inactive windows identically in
         * black. The inactive one is #808080 here — a real Platinum system
         * colour (ButtonShadow's equivalent), borrowed so an unfocused window
         * is still told apart at a glance on a 3-window desktop. */
        .border_norm  = { 0.502f, 0.502f, 0.502f, 1.0f },  /* #808080 */
        .border_focus = { 0.000f, 0.000f, 0.000f, 1.0f },  /* #000000 outline */
        .border_ai    = { 0.239f, 0.239f, 0.561f, 1.0f },  /* #3D3D8F */
        .border_warn  = { 0.600f, 0.000f, 0.000f, 1.0f },  /* #990000 */
        .tb_norm       = { 0.867f, 0.867f, 0.867f, 1.0f },  /* #DDDDDD, no stripes */
        .tb_focus      = { 0.800f, 0.800f, 0.800f, 1.0f },  /* #CCCCCC under them */
        .tb_grad_norm  = { 0.867f, 0.867f, 0.867f, 1.0f },  /* Platinum is flat */
        .tb_grad_focus = { 0.800f, 0.800f, 0.800f, 1.0f },
        .face          = { 0.867f, 0.867f, 0.867f, 1.0f },  /* #DDDDDD sampled */
        .chrome = SYN_CHROME_PLATINUM,
        .tb_text       = { 0.502f, 0.502f, 0.502f, 1.0f },  /* #808080 */
        .tb_text_focus = { 0.000f, 0.000f, 0.000f, 1.0f },
        .active_opacity = 1.0f, .inactive_opacity = 1.0f,
        /* The desktop purple deepened until it is ink: #9B9CCE itself measures
         * 2.2:1 on this theme's #DDDDDD panels, which is a heading nobody can
         * read. Same hue, four stops down. */
        .panel_accent  = { 0.239f, 0.239f, 0.561f, 1.0f },  /* #3D3D8F */
        .scheme = "light", .accent_r = 61, .accent_g = 61, .accent_b = 143,
        .glyph_r = 61, .glyph_g = 61, .glyph_b = 143,
        .base_r = 221, .base_g = 221, .base_b = 221,        /* #DDDDDD */
        .text_r = 0, .text_g = 0, .text_b = 0,
    },
    /* SYNAPSE PRISM — the house theme, and what a fresh install boots into.
     *
     * ⚠ THE ACCENT HERE IS A FALLBACK, NOT THE THEME. Prism's colour comes off
     * the WALLPAPER (palette.c), live, and is substituted over the four accent
     * fields below whenever there is one to substitute. What is written here is
     * what a greyscale wallpaper gets, and what is on screen for the fraction
     * of a second before the first measurement lands. It is deliberately the
     * house cyan rather than a neutral: a fallback nobody notices is a fallback
     * nobody finds out is being used.
     *
     * Everything that is NOT the accent is fixed, and that is the design. A
     * theme whose chrome colour also came off the wallpaper would be a
     * different theme on every picture, and the glass would have nothing
     * constant to be glass AGAINST. So: one dark, desaturated, near-neutral
     * surface at low alpha, and the wallpaper supplies the colour through it.
     *
     * Dark rather than light, unlike Tahoe. Glass over an arbitrary photograph
     * is a contrast problem, and a dark surface is the one that survives a
     * bright wallpaper — a pale glass over a white beach is a panel with no
     * edges. syn_contrast_fix() then has room to work in, because the corrector
     * is a no-op on a dark surface and would otherwise be dragging every
     * measured accent around on the theme a fresh install ships with.
     *
     * That is an argument about what a DEFAULT should be, not a claim that a
     * light Prism cannot work — see SYN_THEME_PRISM_LIGHT below, which is this
     * entry with the surface inverted and the correctors running the other
     * way. This one stays what a fresh install boots into. */
    [SYN_THEME_PRISM] = {
        /* A hairline, like Tahoe's, but dark: the frame is the edge of a piece
         * of glass, not a border drawn round a window. */
        .border_norm  = { 0.180f, 0.196f, 0.235f, 1.0f },  /* #2E323C */
        .border_focus = { 0.000f, 0.839f, 0.898f, 1.0f },  /* #00D6E5 — the fallback accent */
        .border_ai    = { 0.545f, 0.451f, 0.984f, 1.0f },  /* #8B73FB */
        .border_warn  = { 1.000f, 0.353f, 0.404f, 1.0f },  /* #FF5A67 */
        /* The glass itself. Near-neutral and only faintly blue — a tint with a
         * hue of its own fights whatever the wallpaper supplies, and the two
         * together are what make a "themed" desktop look muddy. */
        .tb_norm       = { 0.106f, 0.118f, 0.145f, 1.0f },  /* #1B1E25 */
        .tb_focus      = { 0.145f, 0.161f, 0.196f, 1.0f },  /* #252932 */
        .tb_grad_norm  = { 0.086f, 0.098f, 0.122f, 1.0f },  /* #16191F */
        .tb_grad_focus = { 0.118f, 0.133f, 0.165f, 1.0f },  /* #1E222A */
        .face          = { 0.098f, 0.110f, 0.137f, 1.0f },  /* #191C23 */
        .chrome = SYN_CHROME_LIQUID,
        .tb_text       = { 0.545f, 0.573f, 0.627f, 1.0f },  /* #8B92A0 secondary */
        .tb_text_focus = { 0.902f, 0.918f, 0.945f, 1.0f },  /* #E6EAF1 */
        /* Ships translucent, because a theme built on glass with the glass
         * turned off is the theme with the point removed — the same argument
         * macOS 26's entry makes. `glass_level` moves both of these together
         * and can take them to fully clear. */
        .active_opacity = 0.90f, .inactive_opacity = 0.84f,
        .panel_accent  = { 0.000f, 0.839f, 0.898f, 1.0f },  /* the fallback again */
        .scheme = "dark", .accent_r = 0, .accent_g = 214, .accent_b = 229,
        .glyph_r = 0, .glyph_g = 214, .glyph_b = 229,
        .base_r = 25, .base_g = 28, .base_b = 35,           /* #191C23 */
        .text_r = 230, .text_g = 234, .text_b = 241,        /* #E6EAF1 */
    },
    /* SYNAPSE PRISM LIGHT — Prism in daylight.
     *
     * The same theme with the surface inverted, and deliberately nothing else:
     * CHROME_LIQUID, the same 0.90/0.84 glass, the same accent off the
     * wallpaper. Everything gated on "is this Prism" (theme_is_glass,
     * theme_bar_alpha, wp_accent_on in synui.h) names this one too, because a
     * light Prism that was not glass and did not follow the picture would be a
     * different theme wearing the name.
     *
     * ⚠ AND IT IS A TRADE, NOT A FREE VARIANT. Prism's own entry above says why
     * the dark surface is the safe one: glass over an arbitrary photograph is a
     * contrast problem, and a pale panel over a white beach is a panel with no
     * edges. What makes the light one workable is that the correctors run the
     * OTHER WAY on a pale surface and actually do something — syn_contrast_fix()
     * darkens (it is a no-op on dark), palette.c's UI_V_MIN_ON_LIGHT gives a
     * measured accent room to be darkened into, and glass_legibility is there
     * for the wallpapers where neither is enough. So the shipped surface is
     * near-white rather than white: #EEF1F6 keeps a little headroom above the
     * chrome and below the ink.
     *
     * ⚠ THE FALLBACK ACCENT IS NOT THE HOUSE CYAN. #00D6E5 measures 1.7:1 on
     * this surface — the same hex that is the whole look on dark Prism is an
     * accent that is not there on light Prism. Same hue (186°), four stops
     * down, exactly the move Platinum's panel_accent documents. */
    [SYN_THEME_PRISM_LIGHT] = {
        /* A hairline again, and light: the edge of a piece of glass, not a
         * border. It is the only colour here that is allowed to be low
         * contrast — a 1.5:1 rim is what a frame drawn IN the surface looks
         * like, and raising it turns the theme back into bordered windows. */
        .border_norm  = { 0.765f, 0.788f, 0.839f, 1.0f },  /* #C3C9D6 */
        .border_focus = { 0.000f, 0.447f, 0.494f, 1.0f },  /* #00727E — the fallback accent */
        .border_ai    = { 0.357f, 0.271f, 0.839f, 1.0f },  /* #5B45D6 */
        .border_warn  = { 0.776f, 0.176f, 0.227f, 1.0f },  /* #C62D3A */
        /* The glass. Near-neutral and only faintly blue, for the reason the
         * dark one is: a tint with a hue of its own fights whatever the
         * wallpaper supplies. */
        .tb_norm       = { 0.886f, 0.902f, 0.933f, 1.0f },  /* #E2E6EE */
        .tb_focus      = { 0.957f, 0.965f, 0.980f, 1.0f },  /* #F4F6FA */
        .tb_grad_norm  = { 0.847f, 0.867f, 0.906f, 1.0f },  /* #D8DDE7 */
        .tb_grad_focus = { 0.918f, 0.933f, 0.961f, 1.0f },  /* #EAEEF5 */
        .face          = { 0.933f, 0.945f, 0.965f, 1.0f },  /* #EEF1F6 */
        .chrome = SYN_CHROME_LIQUID,
        /* 4.6:1 and 15.6:1 on the captions they land on — the same pair of
         * jobs dark Prism's #8B92A0 (4.7:1) and #E6EAF1 (12.1:1) do. */
        .tb_text       = { 0.369f, 0.400f, 0.459f, 1.0f },  /* #5E6675 secondary */
        .tb_text_focus = { 0.102f, 0.114f, 0.141f, 1.0f },  /* #1A1D24 */
        .active_opacity = 0.90f, .inactive_opacity = 0.84f,
        .panel_accent  = { 0.000f, 0.447f, 0.494f, 1.0f },  /* the fallback again */
        .scheme = "light", .accent_r = 0, .accent_g = 114, .accent_b = 126,
        .glyph_r = 0, .glyph_g = 114, .glyph_b = 126,
        .base_r = 238, .base_g = 241, .base_b = 246,        /* #EEF1F6 */
        .text_r = 26, .text_g = 29, .text_b = 36,           /* #1A1D24 */
    },
};

/* The theme picker's swatch. It takes TWO colours, because one cannot tell the
 * themes apart: the caption alone makes every rice an identical dark grey square
 * (they all sit on a near-black bg), and the accent alone would have said nothing
 * about 95 vs XP once both frames became their real face colour. Caption on the
 * left, focus accent on the right — navy+silver reads as 95, blue+beige as XP,
 * near-black+mauve as Catppuccin. */
void theme_preview_colors(syn_theme_t t, float caption[4], float accent[4])
{
    if (t < 0 || t >= SYN_THEME_COUNT) {
        memset(caption, 0, sizeof(float) * 4);
        memset(accent,  0, sizeof(float) * 4);
        return;
    }
    memcpy(caption, theme_presets[t].tb_focus,     sizeof(float) * 4);
    memcpy(accent,  theme_presets[t].border_focus, sizeof(float) * 4);
}

/* ── Colour maths, for palettes that are not presets ─────── */
/* A preset is a designer's dozen colours. A palette pushed in from the bar is
 * three, and the other nine have to be derived — so these are the tools that
 * derive them, and the contrast checks that stop the derivation producing a
 * caption whose text cannot be read. Nothing here is used by the presets. */

static int q255(float v)
{
    int q = (int)(v * 255.0f + 0.5f);
    return q < 0 ? 0 : (q > 255 ? 255 : q);
}

static void col_mix(float out[4], const float a[4], const float b[4], float t)
{
    for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * t;
    out[3] = 1.0f;
}

/* WCAG relative luminance — the sRGB transfer curve undone, not a naive 601
 * weighting. It matters here: the whole point of these helpers is to answer
 * "can this be read", and 601 luma over-rates dark colours enough to pass a
 * pairing that measures under 2:1 in practice. */
static float col_lum(const float c[4])
{
    float l[3];
    for (int i = 0; i < 3; i++) {
        float v = c[i] < 0.0f ? 0.0f : (c[i] > 1.0f ? 1.0f : c[i]);
        l[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    return 0.2126f * l[0] + 0.7152f * l[1] + 0.0722f * l[2];
}

static float col_contrast(const float a[4], const float b[4])
{
    float la = col_lum(a), lb = col_lum(b);
    float hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

/* The ink to write on `bg`: the one that was asked for when it can be read, and
 * otherwise near-white or near-black, whichever side of the background it is on.
 * The bar hands over one ink for its own surfaces, and synui's captions are not
 * those surfaces — a caption tinted toward the accent can land close enough to
 * the ink to erase it. Not pure #fff/#000: a caption is a small surface and the
 * full-range pair reads as harsh next to everything else on screen. */
static void col_ink_for(float out[4], const float bg[4], const float want[4])
{
    if (col_contrast(bg, want) >= 4.5f) {
        memcpy(out, want, sizeof(float) * 4);
        out[3] = 1.0f;
        return;
    }
    int dark_bg = col_lum(bg) < 0.18f;
    out[0] = out[1] = out[2] = dark_bg ? 0.94f : 0.08f;
    out[3] = 1.0f;
}

/* ── Applying a theme ────────────────────────────────────── */

static void theme_repaint(syn_server_t *s)
{
    /* The shell's layer surfaces — the start menu, the widgets, the OSD — do
     * not commit just because the theme changed, so nothing else would tell
     * their blur that glass has just been turned on or off. Here rather than in
     * each caller because this is the tail every one of them already shares
     * (theme_apply, transparency_set_enabled, the blur toggle, a config reload),
     * and it is idempotent: on a change that was only a colour it walks a few
     * surfaces and finds every setter already holding the value it wants. */
    layer_glass_all(s);

    /* And the compositor's own panels, for the ordering reason spelt out in
     * uifx_apply(): the resolved glass is a pushed cache that panel_chrome_sync()
     * refreshes on the next FRAME, while a panel bakes its alpha into its rect
     * at RENDER time — and every caller of this renders before that frame runs.
     * Pushing it here is what stops a theme switch, a transparency toggle or a
     * config reload leaving a panel drawn at the previous desktop's alpha over
     * this one's blur. */
    panel_chrome_sync(s);

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
    /* The custom palette, when one is in force. Written as the three colours it
     * was given rather than the dozen derived from them: the derivation is code
     * and may improve, and a state file full of results would pin every future
     * session to today's version of it. Absent = following the preset above. */
    if (s->config.theme_custom) {
        const float *a = s->config.theme_custom_accent;
        const float *b = s->config.theme_custom_base;
        const float *i = s->config.theme_custom_ink;
        fprintf(f, "custom=%02x%02x%02x,%02x%02x%02x,%02x%02x%02x\n",
                q255(a[0]), q255(a[1]), q255(a[2]),
                q255(b[0]), q255(b[1]), q255(b[2]),
                q255(i[0]), q255(i[1]), q255(i[2]));
    }
    /* Translucency lives here too so the sliders survive a restart, same file as
     * the theme name they sit beside in the appearance panels. */
    fprintf(f, "transparency=%s\n", s->config.transparency ? "on" : "off");
    /*
     * ⚠ NOT WRITTEN FOR A ROW THE USER HAS PINNED, and that guard is new because
     * the Glass sync made an old hazard reachable.
     *
     * theme.state is read AFTER settings.state (see synui_config_load), so these
     * two keys have always beaten the control panel's own file — which was
     * harmless while they only ever held a value the theme or the transparency
     * slider had put there. The sync changed that: it resolves foot_alpha and
     * active_opacity from the level, and they land here on the way past. Pin the
     * Terminal glass row, log out, and theme.state's synced number would have
     * overwritten the one you pinned it to, in the one file the panel does not
     * write. A pin exists only because the control panel set it, and the panel
     * wrote settings.state at the same moment — so a pinned row is settings.state's
     * outright and this file must stay quiet about it.
     */
    if (!(s->config.glass_pins & SYN_GLASS_PIN_ACTIVE))
        fprintf(f, "active_opacity=%.2f\n", s->config.active_opacity);
    /* Only written once set, so an untouched foot_alpha stays absent from the
     * file and keeps following the slider rather than being frozen at a value
     * the user never chose. */
    if (s->config.foot_alpha >= 0.0f &&
        !(s->config.glass_pins & SYN_GLASS_PIN_FOOT))
        fprintf(f, "foot_alpha=%.2f\n", s->config.foot_alpha);
    /*
     * An EXPORT, not a setting: nothing reads this back into the config, and
     * theme_state_load_config() ignores it. It is here for the bar.
     *
     * The bar rounds its own panels — the right-click menu, the start menu, the
     * mixer, the tooltips — on the desktop's `corner_radius`, so that turning
     * corners on moves the whole desktop rather than everything except the one
     * strip across the top of the screen. That setting is only half the answer:
     * chrome_corner_radius() forces 0 for the retro chromes, because a Windows
     * 95 desktop with a 14px-rounded start menu is neither one thing nor the
     * other. The bar cannot work that half out for itself without a copy of the
     * LUNA/BEVEL table in QML, which is a table that would be wrong the first
     * time a preset here changed — as it would have been the moment macOS 26
     * arrived, a non-FLAT chrome that is rounder than the default.
     *
     * So the DERIVED fact travels instead of the enum, and the bar's whole share
     * of the rule is one ternary. It lives in theme.state rather than in
     * theme.json because it changes exactly when the theme does, and theme.json
     * is written by synui-apply-theme — which is handed a palette and never
     * learns which chrome drew it (the Antiquity bar calls that helper directly
     * with nothing but colours).
     */
    fprintf(f, "square_chrome=%s\n",
            chrome_square(&s->config) ? "on" : "off");
    /*
     * The same kind of export, for the same kind of reader, one question over:
     * does this desktop's chrome do GLASS — frosted translucent surfaces over a
     * blur — rather than a tinted slab?
     *
     * The dock resolves `dock_style = auto` against theme_is_glass() in-process.
     * The desktop widgets are quickshell's and have no way to ask: theme.json
     * carries a palette and a scheme, and neither says "macOS 26" — a light
     * scheme is XP and Win95 as well, and both are emphatically not glass. So
     * the derived fact travels here beside square_chrome, and WidgetFrame.qml's
     * whole share of the rule is reading one line.
     *
     * It inherits square_chrome's upgrade path, which is the reason it sits in
     * this function rather than anywhere else: a desktop that picked its theme
     * under an older synui has a theme.state with no such key, and the startup
     * re-save at the bottom of theme_state_load() writes it on the first login
     * instead of leaving the widgets wrong until somebody next visits the theme
     * manager.
     *
     * It inherits the GAP too, and that is worth saying out loud: that re-save
     * refuses to CREATE the file (see its comment — creating it would hand
     * theme.state precedence over settings.state's opacity keys on a desktop
     * that never asked for it). So a box that has never picked a theme and names
     * a glass one in synuirc gets a glass dock, which the compositor resolves
     * in-process, and HUD widgets, which cannot ask. `widget_glass = on` says it
     * explicitly and is the answer for that case.
     */
    fprintf(f, "glass_chrome=%s\n",
            theme_is_glass(&s->config) ? "on" : "off");
    /*
     * Whether the desktop is ACTUALLY drawing glass right now, which is not the
     * same question as the line above and is the one the shell's popups need.
     *
     * glass_chrome is a fact about the PRESET. This is syn_glass_active() — the
     * preset AND transparency AND blur — and it is exported rather than
     * recomputed in QML because two of those three are not in this file at all:
     * `blur` is a synuirc key the bar has never read, and a popup that dropped
     * to 0.86 because the theme is Prism, on a machine where blur is off, would
     * be a see-through menu with a sharp wallpaper behind it. That is the one
     * outcome worse than a slab.
     *
     * Same upgrade path as glass_chrome above: absent means a synui too old to
     * export it, and the reader's honest answer for that is "not glass", which
     * is the desktop those machines already have.
     */
    fprintf(f, "glass_surfaces=%s\n",
            syn_glass_active(&s->config) ? "on" : "off");

    /*
     * ── The Glass slider's answer, for the two surfaces that are not ours ────
     *
     * ⚠ WITHOUT THIS THE SLIDER DID NOT REACH THE BAR OR THE WIDGETS AT ALL, and
     * the reason is a process boundary that nothing else in the chain crosses.
     *
     * synui_config_apply_glass_sync() resolves the level onto bar_opacity and
     * dock_opacity in the compositor's own syn_config_t, which is everything the
     * compositor draws. The bar and the desktop widgets are quickshell's, and
     * BarConfig reads those two keys out of settings.state — a file the slider
     * never writes, because a synced value is not a value anybody chose. So the
     * level moved, the panels and the dock body followed, and the strip across
     * the top of the screen stayed exactly where it was: "one desktop, one
     * amount of glass" with the most visible surface on the desktop opted out.
     *
     * So the RESOLVED numbers travel, in the file the shell already watches for
     * square_chrome and glass_surfaces, and BarConfig gives them precedence over
     * settings.state. Written only for the rows the sync currently owns: a
     * pinned row is absent, and absent is what hands the key back to
     * settings.state — so the shell needs no notion of pinning, and the one
     * place that decides which surfaces the slider drives stays syn_glass_
     * drives(). `glass_sync=off` empties the block for the same reason.
     */
    fprintf(f, "glass_legibility=%s\n",
            s->config.glass_legibility ? "on" : "off");
    if (syn_glass_drives(&s->config, SYN_GLASS_PIN_BAR))
        fprintf(f, "bar_opacity=%.2f\n", syn_glass_bar_alpha(&s->config));
    if (syn_glass_drives(&s->config, SYN_GLASS_PIN_DOCK))
        fprintf(f, "dock_opacity=%.2f\n", syn_glass_dock_alpha(&s->config));
    fclose(f);
}

/*
 * The UI FX page's "Backdrop blur" switch, which is the one input to
 * syn_glass_active() that is NOT a theme change and NOT a transparency change.
 *
 * Both of those already come through this file and pick up the tail they share
 * (theme_repaint's layer walk, theme_state_save's export). The blur switch does
 * not: it writes straight through the int pointer in uifx.c's row table and
 * calls uifx_apply(), which knows about scene blur data, window alpha and
 * decorations — and nothing about glass. So it needs both halves handed to it.
 *
 * ⚠ THE EXPORT IS THE HALF THAT MATTERS, and it is the one nothing else would
 * catch. The compositor's own panels are safe without it, because
 * panel_chrome_sync() re-pushes the factor every frame — but the shell reads
 * `glass_surfaces` out of theme.state, and nothing rewrites that file when blur
 * is switched off. The start menu and the widgets would keep the 0.86 alpha
 * they took for a glass desktop while the blur behind them was being torn down,
 * which is a see-through menu with a SHARP wallpaper through it: the one
 * outcome theme_state_save's own comment calls worse than a slab.
 *
 * The layer walk is here for the reason it is in theme_repaint — a widget
 * sitting idle does not commit, so layer_glass_apply() never runs for it and
 * its blur companion would outlive the switch that turned it off.
 *
 * Not folded into theme_repaint(): that is a repaint, uifx_apply() already
 * damages every output for its own reasons, and this has to SAVE as well.
 */
void theme_glass_refresh(syn_server_t *s)
{
    theme_state_save(s);
    layer_glass_all(s);
}

/* The unfocused level always trails the focused one by a hair, so one slider
 * drives both. Clamped so a window can never go fully invisible. */
static float inactive_from_active(float active)
{
    float v = active - 0.06f;
    if (v < 0.50f) v = 0.50f;
    return v;
}

/* Terminals (foot) draw their own glass, so they are excluded from the
 * compositor's uniform fade (anim.c) — instead their real background alpha is
 * driven here so a translucent terminal keeps opaque text. Off = solid (1.0);
 * on = the slider value. Fire-and-forget, a no-op when foot isn't installed. */
static void glass_push(syn_server_t *s)
{
    /* ~/.config/foot/foot.ini belongs to the seat's desktop, not to a headless
     * rig that happens to be running the same binary. See synui_owns_seat(). */
    if (!synui_owns_seat(s)) return;

    /* foot gets its own level when one is set. The slider is a poor proxy for it:
     * alpha over foot's near-black background reads much more solid than the same
     * alpha over a light toolkit window, so a comfortable 0.86 desktop left the
     * terminal barely see-through. Unset (-1) keeps the old 1:1 coupling. */
    float a = s->config.foot_alpha >= 0.0f ? s->config.foot_alpha
                                           : s->config.active_opacity;
    if (!s->config.transparency) a = 1.0f;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "synui-glass %.2f", a);
    synui_spawn(cmd);
}

/*
 * The transparency slider edits a row the Glass sync DRIVES — so it has to
 * claim it, exactly as dragging the row in the control panel does.
 *
 * The sync was specified to drive every glass row until somebody edits one, and
 * to hand that row back only when auto sync is switched on again. ctlpanel.c
 * implemented that and nothing else did: transparency_set_opacity() is the
 * funnel for BOTH Super+E's Window opacity row (uifx.c, UIFX_ROW_OPACITY) and
 * the theme manager's -/= keys, and neither touched glass_pins. So the value
 * applied, theme_state_save() wrote it to theme.state — which it is only
 * willing to do BECAUSE the row reads as unpinned — and then the next login
 * read theme.state and, one line later in synui_config_load(),
 * synui_config_apply_glass_sync() overwrote it with syn_glass_window_alpha().
 * At glass_level = 100 that is 1.00 - 0.38 = 0.62, every single time: the
 * window opacity going back to 62% at every login was this, and 62% is not a
 * default anywhere — it is the floor of the curve at the top of the slider.
 *
 * Pinning also moves where the value LIVES. theme_state_save() stays quiet
 * about a pinned row so settings.state can show through (theme.state is read
 * last and would otherwise win), so the pin and the settings.state value have
 * to be written in the same breath or the number is simply lost at the next
 * login — the pin without the value is worse than neither.
 *
 * ⚠ Must run BEFORE theme_state_save(), which asks the pin whether to write.
 */
static void transparency_claim_active(syn_server_t *s)
{
    syn_config_t *cfg = &s->config;

    /* Nothing is driving this row — no slider set, or the sync switched off —
     * so there is no pin to claim and theme.state keeps the value as it always
     * has. Claiming here would strand it in settings.state for no reason. */
    if (!syn_glass_set(cfg) || !cfg->glass_sync) return;

    /* Compared at the precision it is STORED at (%.2f), not float epsilon:
     * a row dialled back onto the slider's own answer has no opinion again and
     * must RELEASE, or it sits pinned to a value nothing recorded a choice of —
     * the invariant ctlpanel_row_is_default() keeps for the panel's rows. */
    float d = cfg->active_opacity - syn_glass_window_alpha(cfg);
    if (d < 0) d = -d;
    if (d < 0.005f) {
        synui_glass_pins_store(cfg, cfg->glass_pins & ~SYN_GLASS_PIN_ACTIVE);
        settings_state_clear("active_opacity");
        return;
    }

    synui_glass_pins_store(cfg, cfg->glass_pins | SYN_GLASS_PIN_ACTIVE);
    char val[32];
    snprintf(val, sizeof(val), "%.2f", cfg->active_opacity);
    settings_state_set("active_opacity", val);
}

void transparency_set_opacity(syn_server_t *s, float active)
{
    if (active < 0.50f) active = 0.50f;
    if (active > 1.00f) active = 1.00f;
    s->config.active_opacity   = active;
    s->config.inactive_opacity = inactive_from_active(active);
    anim_apply_alpha_all(s);
    glass_push(s);
    /* No dock_relayout() here on purpose: the dock's cached buffer is filled
     * from panel_bg and outlined in panel_accent, and an opacity change touches
     * neither (the dock's own 0.80 is a literal in dock.c, not the window
     * opacity slider). Rebuilding it on every tick of that slider would be pure
     * work. The theme switch that DOES change its colours rebuilds it in
     * theme_apply(). */
    theme_repaint(s);
    transparency_claim_active(s);
    theme_state_save(s);
}

void transparency_set_enabled(syn_server_t *s, int on)
{
    s->config.transparency = on;
    /* Enabling while the focused level is still 1.0 shows nothing — the window
     * you are looking at is the focused one. Drop to a clearly translucent
     * default so "on" actually looks on (Firefox included: it is just a window). */
    int bumped = 0;
    if (on && s->config.active_opacity > 0.98f) {
        s->config.active_opacity   = 0.90f;
        s->config.inactive_opacity = inactive_from_active(0.90f);
        bumped = 1;
    }
    anim_apply_alpha_all(s);
    glass_push(s);
    theme_repaint(s);
    /* Only when the switch actually MOVED the opacity. Flipping translucency off
     * and on again must not quietly pin a row nobody dragged. */
    if (bumped) transparency_claim_active(s);
    theme_state_save(s);
}

/*
 * Push a config's resolved panel colours into render.c's file-scope cache.
 *
 * Separate from theme_load_colors() below, which used to do it inline. That
 * cache is PROCESS-GLOBAL, and theme_load_colors() is reachable from
 * config_parse_kv() on a `theme =` line — which config.c documents as safe to
 * run against a SCRATCH config. It was not: a scratch parse reached past the
 * struct it was handed and recoloured the live desktop's panels. Exactly the
 * shape of the ui_font bug fixed in 293, one file over, and the reason that fix
 * moved syn_text_set_ui_font() to the end of synui_config_load(). Same remedy:
 * theme_load_colors() is pure now, and the push happens where there IS a server
 * — theme_apply(), theme_apply_custom() and theme_apply_from_config().
 */
static void theme_push_panel_colors(const syn_config_t *cfg)
{
    render_set_panel_accent(cfg->panel_accent);
    /* SynapseOS's own app icons are drawn in one violet family, which reads as
     * a house style on SYNAPSE and as an oversight on Gruvbox. They follow the
     * panel accent — the colour our own surfaces are already drawn in — so the
     * dock stops being the one part of the bar that ignored the theme. Third
     * party icons are untouched; see iconhue.c for what "ours" means. */
    icon_set_accent(cfg->panel_accent);
    render_set_panel_surface(cfg->panel_bg, cfg->panel_ink);
    /* Glass is a property of the theme too, so it travels with the colours:
     * switching to Prism has to reach the panels in the same push that recolours
     * them, or the first repaint after the switch draws the new surface at the
     * old solidity. panel_chrome_sync() re-pushes every frame and would catch it
     * eventually — "eventually" being one frame of the wrong picture on exactly
     * the action whose whole point is to change how the desktop looks. */
    render_set_panel_glass(syn_glass_resolve(cfg));
    render_set_panel_legibility(cfg->glass_legibility != 0);
}

/* Copy a preset's colours + opacity levels into a config, nothing more — no
 * render.c push, no repaint, no spawn (see theme_push_panel_colors above). This
 * is the half config.c needs at parse time (`theme =`), before there is a server
 * to repaint or apps to reskin — and doing it here means an explicit
 * border_color_* written AFTER `theme =` in synuirc still overrides, because it
 * parses later. */
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

    /* Chrome style + its extra colours. A flat preset leaves the gradient ends
     * and the face zeroed (they are not in its initialiser), so fill them from
     * the caption colours: deco.c can then always draw "top → bottom" and always
     * have a face to cut buttons from, with no theme-specific branching. The
     * alpha is the tell for "unset" — no real colour here is fully transparent. */
    cfg->chrome = p->chrome;
    memcpy(cfg->titlebar_grad,       p->tb_grad_norm[3]  > 0.0f ? p->tb_grad_norm
                                                                : p->tb_norm,
           sizeof(cfg->titlebar_grad));
    memcpy(cfg->titlebar_grad_focus, p->tb_grad_focus[3] > 0.0f ? p->tb_grad_focus
                                                                : p->tb_focus,
           sizeof(cfg->titlebar_grad_focus));
    memcpy(cfg->chrome_face,         p->face[3]          > 0.0f ? p->face
                                                                : p->border_norm,
           sizeof(cfg->chrome_face));

    /* The surface the accent is drawn ON. Alpha 0 = derive from the app
     * window pair, which is what every theme but SYNAPSE does: the panels are
     * the same surface as a window, so a rice's panels come out in the rice's
     * colours without a second set of numbers to keep in step. */
    float pbg[4], pink[4];
    if (p->panel_bg[3] > 0.0f) {
        memcpy(pbg, p->panel_bg, sizeof(pbg));
    } else {
        pbg[0] = (float)p->base_r / 255.0f;
        pbg[1] = (float)p->base_g / 255.0f;
        pbg[2] = (float)p->base_b / 255.0f;
        pbg[3] = 1.0f;
    }
    if (p->panel_ink[3] > 0.0f) {
        memcpy(pink, p->panel_ink, sizeof(pink));
    } else {
        pink[0] = (float)p->text_r / 255.0f;
        pink[1] = (float)p->text_g / 255.0f;
        pink[2] = (float)p->text_b / 255.0f;
        pink[3] = 1.0f;
    }
    memcpy(cfg->panel_bg,  pbg,  sizeof(cfg->panel_bg));
    memcpy(cfg->panel_ink, pink, sizeof(cfg->panel_ink));
}

/*
 * `push_apps` is whether to shell out to synui-apply-theme.
 *
 * Every PICK does (that is most of what picking a theme means). A re-apply of
 * the theme the desktop is already on does not: the toolkit side has not
 * changed, and the script is ~20 seconds of kwriteconfig/gsettings/dbus. That
 * distinction only exists because theme_apply_from_config() re-applies on every
 * config reload, and a SIGHUP should not cost a fifth of a minute of shelling
 * out to redraw the same colours.
 */
static void theme_apply_ex(syn_server_t *s, syn_theme_t theme, int save,
                           int push_apps)
{
    if (theme < 0 || theme >= SYN_THEME_COUNT) return;
    const syn_theme_preset_t *p = &theme_presets[theme];

    /* Picking a preset is how a custom palette is got rid of. There is no other
     * way out of one — the bar can push a palette but has no "revert", and a
     * user staring at colours they want gone will go to the theme manager. */
    s->config.theme_custom = 0;

    theme_load_colors(&s->config, theme);
    theme_push_panel_colors(&s->config);

    /* …and then the wallpaper's colour back over the top of it, where this
     * desktop takes one.
     *
     * ⚠ HERE, AND NOT AT THE END. The substitution writes panel_accent and
     * border_color_focus, which are exactly what the window loop below re-tints
     * the chrome with and what dock_relayout() rebuilds the dock's outline from.
     * Run after them and a switch TO Prism came up with the preset's cyan on
     * every border and dock until something else forced a repaint — which is
     * what happened before this call existed at all: the only thing that ever
     * resolved the accent was a WALLPAPER change, so picking the theme built on
     * the wallpaper's colour did not take it. */
    theme_refresh_wallpaper_accent(s);

    /* Re-tint every window's chrome (the titlebar buffer is redrawn with the new
     * caption colours) and re-push opacity, since the inactive level may have
     * moved. Only touches mapped windows; unmapped ones re-read cfg when shown. */
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link)
            if (v->mapped) {
                /* The titlebar surface is cached on size/focus/title, none of
                 * which a theme switch touches — drop it first or the window
                 * keeps its old caption until something else forces a repaint. */
                view_invalidate_titlebar(v);
                anim_apply_alpha(v);   /* calls view_update_decorations itself */
            }
    }

    /* The dock draws its body from panel_bg and its outline from panel_accent
     * into a CACHED cairo buffer, rebuilt only when its contents or geometry
     * change — none of which a theme switch touches. theme_repaint() below only
     * damages and schedules a frame, so without this the dock keeps the PREVIOUS
     * theme's colours until an app happens to open or close. Exactly the
     * titlebar cache problem handled a few lines up, one buffer over.
     *
     * (This call was written for pkgrel 158 but landed in
     * transparency_set_opacity(), where the dock's appearance does not depend on
     * anything that changed — so a theme switch left the outline stale, which is
     * the one case it exists for.) */
    dock_relayout(s);

    theme_repaint(s);

    /* Hand the app-side reskin to the helper (safe/merge-y, and a no-op where the
     * tools aren't installed). Firefox transparency is already covered by the
     * compositor's opacity — this only carries the light/dark scheme. */
    if (push_apps && !synui_owns_seat(s)) {
        /* A headless or nested synui shares the real desktop's $HOME and
         * session bus, so this push would land on the seat's Firefox, GTK and
         * terminal rather than on anything this instance draws. Said out loud
         * because it is a silent no-op otherwise, and the ONE thing anybody
         * debugging from a rig would want to know. */
        wlr_log(WLR_INFO, "synui: no seat (headless/nested) — not pushing the "
                          "theme to the desktop's apps");
        push_apps = 0;
    }

    if (push_apps) {
        char cmd[256];
        /* The chrome style travels too, because a window synui does NOT
         * decorate draws its own corners: Firefox never binds xdg-decoration,
         * so a Win95 desktop kept Adwaita's rounded corners on it no matter
         * what corner_radius said. The helper turns this into a GTK rule; the
         * same derived fact theme.state carries for the bar (square_chrome),
         * spelt the same way, so the two cannot disagree about what "retro" is. */
        /* The bar's alpha rides along for the same reason: it is the theme's,
         * not the scheme's, and the helper is the one thing that writes
         * theme.json. "-" is the no-opinion token, kept out of band from a real
         * 0.00 because a clear bar is a value this now has to be able to say. */
        char alpha[8] = "-";
        float ba = theme_bar_alpha(&s->config);
        if (ba >= 0.0f) snprintf(alpha, sizeof(alpha), "%.2f", ba);

        snprintf(cmd, sizeof(cmd),
                 "synui-apply-theme %s %d %d %d %d %d %d %d %d %d %d %d %d %s %s",
                 p->scheme, p->accent_r, p->accent_g, p->accent_b,
                 p->glyph_r, p->glyph_g, p->glyph_b,
                 p->base_r, p->base_g, p->base_b,
                 p->text_r, p->text_g, p->text_b,
                 chrome_square(&s->config) ? "on" : "off", alpha);
        synui_spawn(cmd);

        /* And the terminal's glass, because the alpha it should run at DEPENDS
         * on the scheme this just changed.
         *
         * synui-glass floors the alpha on a light scheme: a dark terminal over
         * a wallpaper stays dark, but a light one is dragged DOWN toward its own
         * dark text, so 95's silver at the shipped 0.70 composites to about
         * #878787 and every colour on it collapses. The floor was right and it
         * was never re-applied here — glass_push ran from the opacity slider,
         * the transparency toggle and startup, none of which is how a scheme
         * changes. So picking Win95 left the terminal on the DARK theme's 0.70
         * and its blues and greys measured 1.85–2.31:1.
         *
         * It is the same slider value either way; only the floor's answer to it
         * moves, which is exactly why the push has to happen on this path too. */
        glass_push(s);
    }

    if (save) theme_state_save(s);
    wlr_log(WLR_INFO, "synui: theme applied: %s (scheme %s)",
            syn_theme_names[theme], p->scheme);
}

/*
 * SYNAPSE Prism's accent, from the wallpaper.
 *
 * This is the whole difference between the two Prisms and every other preset:
 * the thirteen others carry their accent in theme_presets[], and Prism carries a
 * FALLBACK there and takes the real one off whatever is on the desktop.
 *
 * ── Why only the accents move ─────────────────────────────────────────────
 *
 * The chrome colours, the panel surface and the text stay exactly as the preset
 * declares them. A theme whose SURFACE also came off the wallpaper would be a
 * different theme on every picture, and the glass would have nothing constant
 * to be glass against — the point of glass is that you see the wallpaper
 * THROUGH something, and if the something is also the wallpaper there is no
 * theme left. So: one fixed dark surface, and the colour comes through it.
 *
 * ── Why this is not synui-apply-theme ─────────────────────────────────────
 *
 * ⚠ THIS RUNS ON EVERY WALLPAPER CHANGE, INCLUDING EVERY SLIDE OF A
 * SLIDESHOW. synui-apply-theme is ~20 seconds of shelling out to kwriteconfig
 * and gsettings; running it here would make changing wallpaper a twenty-second
 * operation and rewrite the toolkit palette dozens of times an hour. The
 * toolkit's accent therefore tracks the THEME, and synui's own panels track the
 * WALLPAPER — a split that is visible if you look for it (Dolphin's highlight
 * does not follow the picture) and is the right trade for not making the
 * desktop unusable.
 *
 * A no-op on every other theme, and on Prism with a greyscale wallpaper: the
 * fallback in the preset is what stands, which is why that fallback is the
 * house cyan and not a neutral nobody would notice being used.
 */
void theme_refresh_wallpaper_accent(syn_server_t *s)
{
    /* ⚠ THE GATE IS THE SETTING, NOT THE THEME, and it used to be
     * `theme != PRISM`. Same answer by default — wp_accent_on() resolves AUTO
     * to exactly that — but a desktop that wants its accent off the picture on
     * macOS 26, or Prism with the picture's colour switched off, can now say
     * so. Control panel ▸ Appearance ▸ Wallpaper accent. */
    const syn_palette_t *p = wp_accent_on(&s->config) ? wallpaper_palette(s) : NULL;

    if (!p || !p->ok) {
        /* Back to the theme's own colour — a wallpaper switched from a
         * photograph to a greyscale one, or the row moved to Off, must not
         * leave the last picture's colour on the panels. Cheap: it is the same
         * copy the theme switch does, minus the spawn.
         *
         * ⚠ A PUSHED PALETTE IS NOT THE PRESET'S. The bar's theme picker can
         * put three colours into the config (theme_apply_custom), and reloading
         * the preset over them would throw away a palette the user picked —
         * which nothing here would have said, and which no "revert" exists to
         * get back. That was unreachable while this ran on Prism alone; it is
         * one Off away on any theme now. */
        if (s->config.theme_custom)
            theme_apply_custom(s, s->config.theme_custom_accent,
                               s->config.theme_custom_base,
                               s->config.theme_custom_ink, 0);
        else
            theme_load_colors(&s->config, s->config.theme);
        theme_push_panel_colors(&s->config);
        dock_relayout(s);
        theme_repaint(s);
        return;
    }

    /* Four fields, and no others. panel_accent is what synui's own panels draw
     * their headers, selections and rules with; border_color_focus is the frame of
     * the window you are in. Both are "the thing you are pointing at", which is
     * exactly what an accent off the wallpaper should colour. */
    for (int i = 0; i < 3; i++) {
        s->config.panel_accent[i] = p->accent[i];
        s->config.border_color_focus[i] = p->accent[i];
    }
    s->config.panel_accent[3] = 1.0f;
    s->config.border_color_focus[3] = 1.0f;

    theme_push_panel_colors(&s->config);
    /* The dock's buffer is CACHED — dock_render_output() paints the icons into
     * it once and a whole-output damage does not refill it. theme_apply() has
     * always rebuilt it afterwards, but this path is reached from a WALLPAPER
     * change too (wallpaper.c), and that one never did: the dock kept the
     * previous accent's outline until some unrelated thing relaid it out. That
     * was a hairline nobody filed. It is not one now — the app icons are
     * tinted from this accent, so a stale buffer is a dock full of the old
     * colour on exactly the theme whose whole point is to follow the picture. */
    dock_relayout(s);
    theme_repaint(s);
}

void theme_apply(syn_server_t *s, syn_theme_t theme, int save)
{
    theme_apply_ex(s, theme, save, 1);
}

/* ── A palette from outside the preset table ─────────────── */
/*
 * WHY THIS EXISTS AT ALL.
 *
 * The bar ships its own theme picker with its own palettes (see
 * quickshell-antiquity/Config.qml). Picking one there recoloured the bar, and
 * — since 272 — Dolphin, GTK, kitty and Firefox through synui-apply-theme. It
 * could not touch the surfaces synui draws ITSELF: the control panel, the
 * desktop menu, the wallpaper picker, the task manager, the window borders. So
 * "apply theme" left a desktop in two halves, and the half that did not move
 * was the compositor's.
 *
 * The fix is not a sixth preset per bar palette. Those palettes live in a QML
 * file the user is explicitly invited to add to, and a C table that has to be
 * kept in step with it would be wrong the first time anybody accepted the
 * invitation. So the colours travel instead of the name, and the chrome a
 * preset would have specified is DERIVED here.
 *
 * Three colours, because three is what the bar can honestly supply: an accent,
 * the surface its own panels are drawn on, and the ink it writes on that
 * surface. Everything else — the caption pair, the inactive border, the frame
 * face — is a function of those, with a contrast check on each pairing so a
 * palette drawn for a bar cannot produce a titlebar whose text is not there.
 * (That is the 273 failure mode exactly, one process over: a colour that was
 * correct on the surface it was chosen for, reused on a surface nobody
 * measured it against.)
 *
 * What is NOT derived, and keeps coming from the preset in cfg->theme:
 *   - the WARN border. Red means the same thing whatever the palette is; a
 *     "themed" warning colour is a warning colour that has stopped working.
 *   - the chrome STYLE. A palette says nothing about bevels, and forcing FLAT
 *     would silently undo a deliberate Win95 pick. Derived colours are fed to
 *     the gradient ends and the face, so LUNA and BEVEL keep drawing whatever
 *     they draw, in the new colours.
 *   - the opacity levels. Those are the user's slider (theme.state persists
 *     them separately) and a colour push has no business moving it.
 */
void theme_apply_custom(syn_server_t *s, const float accent[4],
                        const float base[4], const float ink[4], int save)
{
    syn_config_t *cfg = &s->config;

    cfg->theme_custom = 1;
    memcpy(cfg->theme_custom_accent, accent, sizeof(float) * 4);
    memcpy(cfg->theme_custom_base,   base,   sizeof(float) * 4);
    memcpy(cfg->theme_custom_ink,    ink,    sizeof(float) * 4);

    /* Panels: the three colours as given, which is the whole point — these are
     * the surfaces the bar's palette was actually drawn for.
     *
     * The accent is checked against the surface anyway. It is the one pairing
     * the bar can get wrong without noticing, because in the bar the accent is
     * mostly drawn on its own glass rather than on `base`; here it carries every
     * selection fill and every header in nineteen panels. Under 3:1 it is pulled
     * toward the ink until it separates. */
    float acc[4];
    memcpy(acc, accent, sizeof(acc));
    for (int i = 0; i < 4 && col_contrast(acc, base) < 3.0f; i++)
        col_mix(acc, acc, ink, 0.25f);

    memcpy(cfg->panel_accent, acc, sizeof(cfg->panel_accent));
    memcpy(cfg->panel_bg, base, sizeof(cfg->panel_bg));
    cfg->panel_bg[3] = 1.0f;
    col_ink_for(cfg->panel_ink, base, ink);
    theme_push_panel_colors(cfg);

    /* Borders. Focus IS the accent (as given, not the panel-corrected one — a
     * border is drawn on the wallpaper, not on the panel surface, so the
     * correction above is about a different background). The inactive border is
     * the surface lifted a little toward the ink, which is what every preset's
     * pair amounts to; the AI border stays distinguishable by being the accent
     * lightened rather than a second hue nobody chose. */
    memcpy(cfg->border_color_focus, accent, sizeof(cfg->border_color_focus));
    cfg->border_color_focus[3] = 1.0f;
    col_mix(cfg->border_color_norm, base, ink, 0.20f);
    {
        const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        col_mix(cfg->border_color_ai, accent, white, 0.40f);
    }

    /* Captions. The inactive one is the panel surface, so an unfocused window
     * and an open panel are the same material; the focused one is that surface
     * pulled a third of the way to the accent, which reads as "lit" on a dark
     * palette and as "tinted" on a light one without either needing a branch.
     * Both texts are then measured against the caption they land on. */
    memcpy(cfg->titlebar_color, base, sizeof(cfg->titlebar_color));
    cfg->titlebar_color[3] = 1.0f;
    col_mix(cfg->titlebar_color_focus, base, accent, 0.32f);

    col_ink_for(cfg->titlebar_text_focus, cfg->titlebar_color_focus, ink);
    /* The inactive caption's text is deliberately quieter than the focused
     * one's — that difference is half of what tells the two apart at a glance —
     * but only down to 3:1, which is where "quiet" becomes "gone". */
    {
        float dim[4];
        col_ink_for(dim, cfg->titlebar_color, ink);
        col_mix(cfg->titlebar_text, dim, cfg->titlebar_color, 0.35f);
        if (col_contrast(cfg->titlebar_text, cfg->titlebar_color) < 3.0f)
            memcpy(cfg->titlebar_text, dim, sizeof(cfg->titlebar_text));
    }

    /* The gradient ends and the 3D face. A flat theme wants them equal to the
     * captions (deco.c always draws top → bottom); LUNA and BEVEL want a real
     * second colour, so give them one derived the same way rather than leaving
     * a Win95 frame in the previous palette's silver. */
    memcpy(cfg->titlebar_grad, cfg->titlebar_color, sizeof(cfg->titlebar_grad));
    if (cfg->chrome == SYN_CHROME_FLAT) {
        memcpy(cfg->titlebar_grad_focus, cfg->titlebar_color_focus,
               sizeof(cfg->titlebar_grad_focus));
    } else {
        col_mix(cfg->titlebar_grad_focus, cfg->titlebar_color_focus, base, 0.45f);
    }
    memcpy(cfg->chrome_face, cfg->border_color_norm, sizeof(cfg->chrome_face));

    /* The same re-decorate/rebuild work a preset switch does, and for the same
     * reasons — the titlebar and the dock are both CACHED buffers that nothing
     * here would otherwise invalidate. See theme_apply(). */
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link)
            if (v->mapped) {
                view_invalidate_titlebar(v);
                anim_apply_alpha(v);
            }
    }
    dock_relayout(s);
    theme_repaint(s);

    if (save) theme_state_save(s);
    wlr_log(WLR_INFO,
            "synui: custom palette applied: accent #%02x%02x%02x "
            "surface #%02x%02x%02x ink #%02x%02x%02x",
            q255(accent[0]), q255(accent[1]), q255(accent[2]),
            q255(base[0]), q255(base[1]), q255(base[2]),
            q255(ink[0]), q255(ink[1]), q255(ink[2]));
}

/* "#rrggbb" or "rrggbb" → RGBA. Nothing shorter: a 3-digit form would have to
 * be guessed at, and this is only ever fed by a program. */
static int theme_parse_hex(const char *s, float out[4])
{
    if (!s) return 0;
    if (*s == '#') s++;
    for (int i = 0; i < 6; i++)
        if (!isxdigit((unsigned char)s[i])) return 0;
    if (s[6] != '\0') return 0;

    unsigned v = (unsigned)strtoul(s, NULL, 16);
    out[0] = (float)((v >> 16) & 0xff) / 255.0f;
    out[1] = (float)((v >>  8) & 0xff) / 255.0f;
    out[2] = (float)( v        & 0xff) / 255.0f;
    out[3] = 1.0f;
    return 1;
}

/* `synctl dispatch theme <arg>`: a preset token, or three colours.
 *
 * Both spellings on one action rather than a second action name, for the reason
 * `wallpaper` takes an optional path — the bind action and the scriptable one
 * are the same idea, and splitting them means two entries in the bind table for
 * one concept, one of which can be bound to a key that then does something no
 * key should. Bare still opens the picker; input.c only calls this with an arg. */
int theme_dispatch(syn_server_t *s, const char *arg)
{
    char buf[128];
    if (!arg || !*arg || strlen(arg) >= sizeof(buf)) {
        wlr_log(WLR_ERROR, "synui: theme: bad argument");
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", arg);

    for (int t = 0; t < SYN_THEME_COUNT; t++) {
        if (strcmp(buf, syn_theme_names[t]) == 0) {
            theme_apply(s, (syn_theme_t)t, 1);
            return 1;
        }
    }

    float col[3][4];
    int n = 0;
    for (char *tok = strtok(buf, " \t"); tok && n < 3; tok = strtok(NULL, " \t"))
        if (!theme_parse_hex(tok, col[n++])) {
            wlr_log(WLR_ERROR, "synui: theme: '%s' is neither a theme name nor "
                               "#rrggbb", arg);
            return 0;
        }
    if (n != 3) {
        wlr_log(WLR_ERROR, "synui: theme: needs 3 colours "
                           "(accent, surface, ink), got %d", n);
        return 0;
    }

    theme_apply_custom(s, col[0], col[1], col[2], 1);
    return 1;
}

/*
 * theme.state, laid over whatever synuirc left in cfg — the CONFIG half.
 *
 * This used to be one function that read the file and applied it, called once
 * from synui_main() and from nowhere else. That made theme.state the only one
 * of the nine state files that synui_config_load() does not read, and
 * synui_config_reload() replaces s->config WHOLESALE:
 *
 *     syn_config_t fresh = {0};
 *     synui_config_load(&fresh);
 *     s->config = fresh;
 *
 * so every reload silently reset the theme to SYNAPSE, threw away a palette the
 * bar had pushed, and put transparency/opacity/foot_alpha back to the defaults —
 * with nothing re-reading theme.state until the next login. The desktop flipped
 * to stock neon and stayed there for the session (velle, 2026-08-07, twice).
 *
 * Worse, it did not even stay a session-only glitch: theme_state_save() writes
 * `theme=<s->config.theme>`, and the control panel's Transparency row, Super+E
 * and the theme manager's own +/- all call it. Touch any of them after a reload
 * and the clobbered theme is written to disk, so the next login comes up on it
 * too and the user's pick is gone for good. That is what made this one look
 * unrecoverable rather than merely wrong.
 *
 * Reading it here, with the other eight, makes the reload correct by
 * construction instead of by anyone remembering to add a call. Pure: it parses
 * into the config it is handed and touches neither the server nor render.c, so
 * it is safe on the scratch config a reload builds. The applying half is
 * theme_apply_from_config() below.
 *
 * Loaded LAST of the state files (see synui_config_load), because that is where
 * it effectively sat before: it ran after the whole config load, so its
 * active_opacity/foot_alpha won over settings.state's. Keeping that order keeps
 * the precedence a desktop already has.
 */
void theme_state_load_config(syn_config_t *cfg)
{
    char path[256];
    if (!syn_config_path(path, sizeof(path), "theme.state")) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no pick recorded — synuirc and the defaults stand */

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "theme=", 6) == 0) {
            for (int t = 0; t < SYN_THEME_COUNT; t++)
                if (strcmp(line + 6, syn_theme_names[t]) == 0)
                    theme_load_colors(cfg, (syn_theme_t)t);
        } else if (strncmp(line, "transparency=", 13) == 0) {
            cfg->transparency = strcmp(line + 13, "on") == 0;
        } else if (strncmp(line, "active_opacity=", 15) == 0) {
            float op = (float)atof(line + 15);
            /* The theme's own levels were just seeded by theme_load_colors; a
             * persisted slider position is the user's and wins over them. */
            if (op >= 0.50f && op <= 1.00f) {
                cfg->active_opacity   = op;
                cfg->inactive_opacity = inactive_from_active(op);
            }
        } else if (strncmp(line, "foot_alpha=", 11) == 0) {
            float fa = (float)atof(line + 11);
            if (fa >= 0.0f && fa <= 1.00f) cfg->foot_alpha = fa;
        } else if (strncmp(line, "custom=", 7) == 0) {
            /* Three comma-separated rrggbb. All or nothing: a half-read palette
             * would be applied as two of the user's colours and one
             * uninitialised one.
             *
             * Only recorded here, not derived into the chrome: turning three
             * colours into a dozen is theme_apply_custom()'s job and it needs a
             * server to re-decorate with. The flag is what carries it across
             * the reload, and theme_apply_from_config() does the derivation. */
            float cust[3][4];
            int n = 0;
            for (char *tok = strtok(line + 7, ","); tok && n < 3;
                 tok = strtok(NULL, ","))
                if (!theme_parse_hex(tok, cust[n++])) { n = -1; break; }
            if (n == 3) {
                cfg->theme_custom = 1;
                memcpy(cfg->theme_custom_accent, cust[0], sizeof(cust[0]));
                memcpy(cfg->theme_custom_base,   cust[1], sizeof(cust[1]));
                memcpy(cfg->theme_custom_ink,    cust[2], sizeof(cust[2]));
            } else {
                wlr_log(WLR_ERROR, "synui: theme.state: unreadable custom "
                                   "palette, ignoring it");
            }
        }
    }
    fclose(f);
}

/*
 * Put the desktop on the theme the config resolved — the SERVER half.
 *
 * Called at startup and at the end of every synui_config_reload(). save=0
 * throughout: this is re-applying what was already read, not a new pick, and
 * saving here is precisely how a reload used to make its own damage permanent.
 *
 * `push_apps` is passed to theme_apply_ex: true at login so Dolphin/GTK/Firefox
 * come up matching, false on a reload where nothing about the toolkit side has
 * changed.
 */
void theme_apply_from_config(syn_server_t *s, int push_apps)
{
    /* Snapshot what theme_apply() is about to overwrite. It resets the opacity
     * pair from the preset and clears theme_custom (picking a preset IS how a
     * pushed palette is dropped) — both correct for a pick from the manager,
     * both wrong here, where the config has already resolved all three. */
    int   custom = s->config.theme_custom;
    float cust[3][4];
    memcpy(cust[0], s->config.theme_custom_accent, sizeof(cust[0]));
    memcpy(cust[1], s->config.theme_custom_base,   sizeof(cust[1]));
    memcpy(cust[2], s->config.theme_custom_ink,    sizeof(cust[2]));
    int   tr  = s->config.transparency;
    float act = s->config.active_opacity;
    float ina = s->config.inactive_opacity;
    float fa  = s->config.foot_alpha;

    theme_apply_ex(s, s->config.theme, 0, push_apps);

    /* Then the pushed palette over the top of it, if one is in force. This is
     * the leg that makes the bar's picker stick: the bar only pushes when a
     * theme is PICKED, so without this every login came up on the preset's
     * colours while the bar came up on its own — the desktop back in two
     * halves, and only until the next pick, which is the hardest kind of bug to
     * be told about. */
    if (custom) theme_apply_custom(s, cust[0], cust[1], cust[2], 0);

    s->config.transparency     = tr;
    s->config.active_opacity   = act;
    s->config.inactive_opacity = ina;
    s->config.foot_alpha       = fa;
    anim_apply_alpha_all(s);
    /* Re-sync the terminal's own glass to the restored state: if the last
     * session left transparency off, foot.ini must go back to solid. */
    glass_push(s);

    /*
     * Bring theme.state's EXPORT up to date — startup only, and only over a
     * file that is already there.
     *
     * `square_chrome` (see theme_state_save) is written when a theme is PICKED,
     * so a desktop that picked Win95 under an older synui has a theme.state with
     * no such key, and the bar would round its menus over a square desktop until
     * the next visit to the theme manager — the kind of half-fix nobody reports
     * because everything else about the upgrade worked. Re-saving what the
     * config has already fully resolved (every field theme_apply_ex clobbered is
     * restored above) writes the key on the first login instead, with identical
     * content otherwise.
     *
     * Not on a reload: push_apps is the startup caller's flag, and Ctrl+Shift+R
     * has no business writing state files.
     *
     * Not when the file is ABSENT, which is a box that has never picked a theme.
     * It is on the FLAT chrome — exactly what the bar assumes when the key is
     * missing — so there is nothing to bring up to date, and creating
     * theme.state here would hand it precedence over settings.state's opacity
     * keys (synui_config_load reads it last) on a desktop that never asked.
     */
    if (push_apps) {
        char sp[256];
        if (syn_config_path(sp, sizeof(sp), "theme.state") && access(sp, F_OK) == 0)
            theme_state_save(s);
    }
}

/* ── The panel ───────────────────────────────────────────── */

void theme_show(syn_server_t *s)
{
    s->thememgr.visible   = 1;
    s->thememgr.selected  = s->config.theme;   /* start on the current theme */
    s->thememgr.status[0] = '\0';
    /* With a pushed palette in force NO row is marked active (see render.c), so
     * say why — otherwise the panel reads as a list of ten themes none of which
     * is on, and the way back to one is not obvious from a list that appears to
     * have nothing selected. */
    if (s->config.theme_custom)
        snprintf(s->thememgr.status, sizeof(s->thememgr.status),
                 "A palette from the bar is in force \xc2\xb7 "
                 "Enter on a theme replaces it");
    wlr_log(WLR_INFO, "synui: theme manager shown");
    synui_render_thememgr(s);
}

void theme_hide(syn_server_t *s)
{
    s->thememgr.visible = 0;
    synui_render_thememgr(s);
    /* No-op unless the control panel is what opened this — see ctlpanel.c. */
    ctlpanel_child_closed(s, "theme");
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

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h. A left click applies the theme it
 * lands on — Enter's job — because that is the whole panel: there is nothing
 * else a theme row does.
 *
 * Hover only moves the cursor and does NOT apply. Applying a theme rewrites
 * kdeglobals, the GTK settings and Firefox's prefs and re-decorates every
 * window; doing that to each row the pointer crosses on its way down the list
 * would be a spectacular way to make the desktop unusable for a few seconds. */

int theme_motion(syn_server_t *s, double lx, double ly)
{
    syn_thememgr_t *tm = &s->thememgr;
    if (!tm->visible) return 0;

    /* NO HOVER WHILE THE LIST IS SCROLLED, and this is not fussiness.
     * synui_render_thememgr() derives its scroll window from the selection —
     * `first = selected - rows/2`, deliberately stateless — so moving the
     * selection MOVES THE LIST. Hover-selecting a row other than the middle one
     * therefore shifts the list under the pointer, which puts a different row
     * under it, which shifts it again: the list bolts to one end and stops. It
     * only bites where the list does not fit (a 1024x768 VM, the ISO's default),
     * which is exactly where nobody would look for it. The wheel and the arrow
     * keys move the selection deliberately and one row at a time, so they are
     * fine; a pointer merely passing over is not. */
    if (tm->hit.rows < SYN_THEME_COUNT) return 1;

    int i = hit_index_at(&tm->hit, lx, ly);
    if (i < 0 || i >= SYN_THEME_COUNT || i == tm->selected) return 1;
    tm->selected = i;
    synui_render_thememgr(s);
    return 1;
}

int theme_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    syn_thememgr_t *tm = &s->thememgr;
    if (!tm->visible) return 0;

    if (!hit_in_panel(&tm->hit, lx, ly)) {
        theme_hide(s);
        return 1;
    }

    theme_motion(s, lx, ly);

    if (button != BTN_LEFT) return 1;

    int i = hit_index_at(&tm->hit, lx, ly);
    if (i < 0 || i >= SYN_THEME_COUNT) return 1;   /* chrome / the slider row */

    theme_apply(s, i, 1);
    snprintf(tm->status, sizeof(tm->status), "applied: %s", theme_name(i));
    synui_render_thememgr(s);
    return 1;
}

int theme_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    if (!s->thememgr.visible) return 0;
    if (delta == 0) return 1;

    theme_move(s, delta > 0 ? 1 : -1);
    synui_render_thememgr(s);
    return 1;
}

int theme_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    if (!s->thememgr.visible) return 0;

    /* Let modified combos through to the global binds, so Super+T closes
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
