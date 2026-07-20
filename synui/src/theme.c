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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Terminals (foot) draw their own glass, so they are excluded from the
 * compositor's uniform fade (anim.c) — instead their real background alpha is
 * driven here so a translucent terminal keeps opaque text. Off = solid (1.0);
 * on = the slider value. Fire-and-forget, a no-op when foot isn't installed. */
static void glass_push(syn_server_t *s)
{
    float a = s->config.transparency ? s->config.active_opacity : 1.0f;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "synui-glass %.2f", a);
    synui_spawn(cmd);
}

void transparency_set_opacity(syn_server_t *s, float active)
{
    if (active < 0.50f) active = 0.50f;
    if (active > 1.00f) active = 1.00f;
    s->config.active_opacity   = active;
    s->config.inactive_opacity = inactive_from_active(active);
    anim_apply_alpha_all(s);
    glass_push(s);
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
    glass_push(s);
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
            if (v->mapped) {
                /* The titlebar surface is cached on size/focus/title, none of
                 * which a theme switch touches — drop it first or the window
                 * keeps its old caption until something else forces a repaint. */
                view_invalidate_titlebar(v);
                anim_apply_alpha(v);   /* calls view_update_decorations itself */
            }
    }
    theme_repaint(s);

    /* Hand the app-side reskin to the helper (safe/merge-y, and a no-op where the
     * tools aren't installed). Firefox transparency is already covered by the
     * compositor's opacity — this only carries the light/dark scheme. */
    char cmd[224];
    snprintf(cmd, sizeof(cmd),
             "synui-apply-theme %s %d %d %d %d %d %d %d %d %d %d %d %d",
             p->scheme, p->accent_r, p->accent_g, p->accent_b,
             p->glyph_r, p->glyph_g, p->glyph_b,
             p->base_r, p->base_g, p->base_b,
             p->text_r, p->text_g, p->text_b);
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
    /* Re-sync the terminal's own glass to the restored state: if the last
     * session left transparency off, foot.ini must go back to solid. */
    glass_push(s);
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
