/*
 * config.c — Parse synuirc configuration
 *
 * Reads ~/.config/synui/synuirc or /etc/synui/synuirc.
 * Format: key = value (one per line), # comments.
 *
 * Keybindings:
 *   bind = <mod>+<key> <action> [arg]
 * e.g.
 *   bind = super+return term
 *   bind = super+shift+e spawn wofi --show drun
 *   bind = super+ctrl+3 movews 3
 * Modifiers: super/logo/mod4, shift, ctrl/control, alt/mod1. Keys are XKB
 * keysym names (case-insensitive: q, return, space, tab, backspace, f1…).
 * A bind on a digit ("super+1") also answers to that digit on the *numpad* —
 * the keypad never sends a digit keysym (KP_1 with NumLock on, KP_End with it
 * off or with Shift held), so input.c maps them back. Bind "super+kp_1"
 * explicitly to give the keypad key its own action.
 *
 * …and to take one away:
 *   unbind = <mod>+<key>
 * The defaults are seeded before any file is read, so re-pointing a key
 * elsewhere leaves the shipped bind live on its old combo; this is how a
 * default is REMOVED rather than replaced. It is also half of what the rebind
 * helper writes (Super+/, then F2 — see binds.state in keys.c): moving a
 * shortcut is a `bind` for the new chord and an `unbind` for the old one.
 *
 * The one shortcut that is not a bind, because it is defined by the ABSENCE of
 * a chord — a modifier pressed and released with nothing in between:
 *   tap_key = super|ctrl|alt|shift|none   (default super)
 *   tap_action = <action> [arg]           (default start_menu)
 * The first is WHICH key, the second is WHAT it opens — any bind action, so
 * `tap_action = spawn rofi -show drun` or `tap_action = cmdbar` are as valid as
 * the default. Two keys because they are two questions: a user who moves the
 * tap onto Alt has not said anything about what it should open.
 * That tap opens the start menu (input.c's syn_server::tap_armed). `none` turns
 * it off, which is the only way to stop a Super that is being used as a
 * modifier from occasionally opening the menu on a keyboard that reports the
 * release late. The rebind helper writes this key too — it is a shortcut in
 * every sense the user cares about, so it goes back with the rest of them on
 * Ctrl+Shift+R rather than being the one that has to be undone by hand.
 * Actions: spawn <cmd>, term, cmdbar, overlay, displays, menu, close, quit,
 * focus_app <app-id>, close_app <app-id>,
 * layout_cycle, retile, cascade, overview, about, focus_next/prev, alt_tab,
 * alt_tab_prev,
 * stack_next/prev, master_shrink/grow,
 * float_toggle, fullscreen_toggle, maximize_toggle, minimize_toggle,
 * minimize_restore, decorations_toggle, ai_ask,
 * ws <1-9>, movews <1-9>, move_output [prev], wallpaper, wallpaper_reload,
 * cursor, cursor_reload,
 * filters, effects_toggle, power, lock, game, taskmgr, network, news, keys.
 * A bind with the same combo as a default replaces it.
 * "keys" (Super+/ or Super+?) is the shortcut palette: every bind below, filtered
 * as you type, Enter to run the one you land on. It reads this table live, so a
 * bind added here needs no second edit to show up in it.
 * "filters" (Super+E) opens the visual-effects panel — CRT filter strengths, and
 * Tab for the window effects (corners, shadow, blur, translucency), which are
 * the same keys as the lines further down this file; "effects_toggle" is the
 * older blind on/off flip, kept for anyone who bound it.
 * "decorations_toggle" (Super+Shift+D) hides every titlebar until you press it
 * again; `titlebar_height = 0` below is the permanent version.
 *
 * Wallpaper (wallpaper.c):
 *   wallpaper = /path/to/image.png   (PNG or JPEG; ~ expands to $HOME)
 *   wallpaper = default|matrix|none  (bundled image / kanji rain / flat colour)
 *   wallpaper_mode = fill|fit|stretch|center   (default fill)
 * Defaults to `default`, the bundled SYNAPSE image: a config that never says
 * otherwise should still look like a desktop. `none`, or a decode failure,
 * falls back to the solid background color. Super+Shift+W (or a SIGHUP)
 * reloads synuirc and repaints from the current wallpaper path/mode.
 *
 * Client CSD margins (deco.c):
 *   clip_csd_margin = on|off    (default on)
 * Crop each client to the window geometry it declared, so a client that
 * ignores xdg-decoration and keeps drawing its own drop shadow (Firefox)
 * doesn't wear a second, wider, square-cornered one outside synui's border.
 *
 * Cursor theme (cursor.c) — Super+Shift+P opens the picker ("pointer"; super+c
 * and super+shift+c were both already taken). An empty theme inherits
 * XCURSOR_THEME, which is what synui did before the picker existed:
 *   cursor_theme = Adwaita      (a directory under any icons dir with cursors/)
 *   cursor_size  = 24           (8-256; pinned so Xwayland clients match)
 * The picker writes cursor.state, which overrides these lines the same way
 * wallpaper.state overrides `wallpaper` — delete it to hand control back.
 * Install themes with synui-cursor(1); it also handles the source-tree archives
 * that opendesktop.org ships, which are not usable as-is.
 *
 * Window snapping (snap.c):
 *   snap = on|off               (default on)
 * Drag a window to the top edge to maximize it, to a side for that half, into a
 * corner for that quarter; dragging it off again restores its old size.
 *
 * Where the compositor's own panels appear (synui_main.c, render.c):
 *   panel_follow_pointer = on|off   (default off)
 * On, a panel re-centres on whichever monitor the pointer is over every time it
 * repaints, so an open task manager crosses the desk with the mouse. Off pins
 * it to the monitor it was opened on until it is closed.
 *
 * Alt+Tab switcher (render.c, input.c, overview.c):
 *   alt_tab_style        = overview|switcher   (default overview)
 *   alt_tab_preview      = on|off   (default on)
 *   alt_tab_all_desktops = on|off   (default on)
 *   alt_tab_minimized    = on|off   (default on)
 * `alt_tab_style` picks WHICH switcher the key is. `overview` (the default)
 * opens mission control — the whole desk, tiled at a size you can see — and
 * `switcher` is the MRU thumbnail strip. The gesture is the same either way:
 * hold Alt, tap Tab to walk, let go to pick. The three toggles below describe
 * the strip; on `overview` only alt_tab_all_desktops is not the overview's own
 * (it always shows the desktop you are on, with the rest along the bottom).
 * The grid of window thumbnails Alt+Tab shows while Alt is held. Off keeps the
 * cycle and loses only the picture of it. The other two say what the cycle can
 * reach: windows on other virtual desktops, and minimized ones. Landing on
 * either switches desktop / restores the window when Alt comes up.
 *
 * Cat mode (cat.c):
 *   cat = on|off                (default off; Super+Shift+C toggles at runtime)
 *
 * Welcome menu (render.c) — Super+Escape opens it regardless; this is only
 * whether it greets you on login. The menu's "Show At Startup" row toggles it
 * live and writes welcome.state, which then overrides this line:
 *   welcome_at_startup = on|off (default on)
 *   notif_dnd = on|off (default off) — Do Not Disturb: no toast, no chime.
 *     Critical urgency still gets through. Super+Shift+N toggles it and writes
 *     dnd.state, which then overrides this line; delete it to hand control back.
 *
 * Screen recording (record.c, synui-record) — Super+Shift+R. Audio means the
 * default sink's monitor (what you can hear), never the microphone; the control
 * panel's Sound ▸ Record audio row toggles it live and writes record.state,
 * which then overrides this line:
 *   record_audio = on|off       (default off)
 *   record_edit  = on|off       (default off)  DNxHR .mov, ~1.1 GB/min
 *
 * Keyboard (input.c):
 *   numlock = on|off            (default on — lock NumLock at attach so the
 *                                numpad types digits from login onwards,
 *                                including on the swaylock screen)
 *
 * Cascade layout (layout.c, Super+Shift+Y) — small overlapping cards, offset so
 * every titlebar stays reachable, dealt into a grid of piles. A card is capped
 * at a third of the screen wide and half of it tall, so the piles fill the desk
 * before any of them grows deep. This is how deep one may then get, and the
 * limit is readability, not geometry: a 1440p screen has room for eighteen
 * offsets and nobody can use a pile of eighteen:
 *   cascade_stack_max = 5       (2-12; windows per pile before a second starts)
 *
 * Dock (dock.c):
 *   dock_enabled = on|off       (default on)
 *   dock_autohide = on|off      (default on; off = always on screen)
 *   night_light  = on|off       (default off)   Super+Shift+B toggles
 *   night_light_temp = 4000     (Kelvin, 1000-6500; 6500 is daylight)
 *   dock_height = 64            (px)
 *   dock_hover_margin = 4       (px trigger strip at the bottom edge)
 *   dock_pin = firefox foot ...  (space-separated app_ids/.desktop basenames)
 *   dock_edge = bottom|top|left|right   (left/right draw a vertical column)
 *   dock_style = auto|solid|glass  (auto = glass on a glass theme; see
 *                                   dock_style_is_glass)
 *   dock_opacity = 0.72         (0.00-1.00; how much wallpaper shows through.
 *                                0.00 is a row of icons on the wallpaper — the
 *                                icons are opaque whatever this says)
 *   dock_radius = 26            (px; clamped to half the bar's thickness)
 *
 * One slider for the whole desktop's glass, and the switches around it
 * (synui.h's glass_sync / glass_pins / glass_legibility):
 *   glass_level = auto|off|0-100  (how much you see through; auto = the theme)
 *   glass_sync  = on|off        (default on; every per-surface alpha below
 *                                follows glass_level — the two window
 *                                opacities, foot_alpha, bar_opacity and
 *                                dock_opacity)
 *   glass_pinned = dock_opacity bar_opacity ...
 *                               (rows taken OFF the slider. Written by the
 *                                control panel when a driven row is dragged;
 *                                switching glass_sync back on clears the lot)
 *   glass_legibility = on|off   (default on; a surface may raise its own alpha
 *                                until its text clears AA against the wallpaper
 *                                behind it. Off draws exactly what was asked,
 *                                including nothing at all)
 *   scene_ink = on|off          (default on; does a see-through surface measure
 *                                the WINDOW behind it, or only the wallpaper?
 *                                Off is the wallpaper alone, which is what
 *                                every release before this one did)
 *   wallpaper_accent = auto|off|on
 *                               (default auto = Prism and nothing else. Whether
 *                                the accent is MEASURED off the wallpaper
 *                                (palette.c) instead of taken from the theme)
 *
 * …and one the compositor parses but does not act on, because its reader is
 * quickshell (WidgetFrame.qml). Here so the key has one spelling and one clamp,
 * exactly like bar_shell/bar_edge below:
 *   widget_glass = auto|off|on  (auto follows the theme, like dock_style)
 *
 * The bar (quickshell) — a SEPARATE PROCESS, so the compositor parses these
 * four and acts on none of them. They live here so each setting has one
 * spelling and the control panel can persist it through settings.state like
 * every other row:
 *   bar_shell = synapse|antiquity   (which QML tree synui-bar starts; next login)
 *   bar_edge  = top|bottom          (BarConfig.qml watches this — it moves live)
 *   bar_opacity = auto|0.00-1.00    (auto = the theme decides; 0 = no background
 *       at all, its ink taken off the wallpaper — which needs a wallpaper that
 *       HAS a legible ink, or the bar keeps its background. See contrast.h)
 *   bar_shape = full-width|rounded-ends|floating-pill
 *     — what the bar does with `corner_radius`. rounded-ends curves the two
 *       corners facing the desktop; floating-pill also lifts it off the edge and
 *       insets it from both sides, closing it into a capsule. ALL OF THEM ARE
 *       NO-OPS WITH THE CORNERS OFF (radius 0, or a retro chrome), so this is
 *       "what shape when rounded" and not a second switch — see syn_bar_shape_t.
 * Only two edges where the dock has four: the bar is a horizontal row (start
 * button, desktop pills, centred clock, tray) and has no vertical form.
 *
 * Launcher (launcher.c) — the "◢ SYNAPSE" start-menu button, top-left:
 *   launcher_style = text|logo  (default; logo = ◢ caret + the dendrite emblem)
 *     — the default only: the control panel and start-menu Settings toggle this
 *       at run time, persisting to launcher.state (laid back over synuirc here).
 *
 * Power saving (power.c) — idle seconds per stage, 0 = never. Each is
 * measured from the last input event, so they are independent, not
 * cumulative. Super+P edits them live and writes power.state, which then
 * overrides these lines (delete it to hand control back to synuirc):
 *   power_enabled = on|off             (default on)
 *   power_dim_timeout = 240
 *   power_blank_timeout = 600
 *   power_lock_timeout = 900
 *   power_suspend_timeout = 0
 *   power_lock_cmd = swaylock -f -c 000000
 *   power_suspend_cmd = systemctl suspend
 *
 * Native lock screen (lock.c):
 *   lock_fingerprint = on|off   (default on — offer the fingerprint reader
 *                                beside the password. A machine with no reader
 *                                detects that by itself and shows nothing, so
 *                                this is only for turning a working reader OFF)
 *
 * Laptop lid (power.c) — what closing the lid does. Three cases, resolved
 * docked first, then mains, then battery, exactly as logind resolves its own
 * three. system|ignore|blank|lock|suspend, where `system` leaves the lid to
 * logind.conf and every other value has synui take logind's
 * handle-lid-switch inhibitor and act itself. `blank` turns the built-in panel
 * off and leaves external monitors alone. The defaults match systemd's own;
 * Super+P edits them and writes them to power.state:
 *   lid_close_action = suspend           (on battery)
 *   lid_close_ac_action = suspend        (charger plugged in)
 *   lid_close_docked_action = ignore     (external monitor; beats both)
 *
 * Screens (Super+D, or control panel ▸ Display ▸ Screens) — how the monitors
 * are arranged. `extend` gives each its own space from the arrangement grid;
 * `mirror` puts them all at the same origin and forces the largest resolution
 * they all share, so they show the same thing; `external` switches the
 * built-in laptop panel off and DETACHES it from the layout, which is the
 * closed-lid case and what stops windows opening on a screen that is off:
 *   display_mode = extend                (extend|mirror|external)
 *
 * Screen audio (control panel ▸ Sound ▸ Screen audio) — move the default audio
 * sink to a TV or monitor when one is plugged in, and back when it goes.
 * `auto` is on where this is wanted (a machine with a battery) and off where
 * it is a nuisance (a desk whose monitors never leave). synui-hdmi-audio(1)
 * does the work and decides, from the ALSA ELD, whether an attached display
 * can take audio at all:
 *   hdmi_audio = auto                    (auto|on|off)
 *
 * Network (Super+I / welcome menu) — nmtui in a terminal. synui has no text
 * entry to type a passphrase into, so there is nothing native to point at yet:
 *   network_cmd = foot -e nmtui
 *
 * About OS (control panel ▸ System ▸ About OS) — areofyl/fetch in a terminal,
 * which already gathers everything an About box would show, including the
 * theme, wallpaper and cursor synui itself set:
 *   about_cmd = kitty -e fetch --infinite
 *
 * News (news.c, Super+R) — RSS/Atom feeds. With no news_source lines the
 * built-in list is used (Hacker News, Lobsters, Arch news, Arch security,
 * kernel releases, LWN, Phoronix, GamingOnLinux). The *first* news_source line
 * replaces that list rather than adding to it, so listing your own feeds gives
 * you those and only those. TAG is the column the panel shows:
 *   news_source = LWN|https://lwn.net/headlines/newrss
 *   news_source = HN|https://news.ycombinator.com/rss
 *   news_refresh = 15                  (minutes; only while the panel is open)
 *
 * Game mode (game.c) — a fullscreen XWayland client is taken to be a game, and
 * while one runs synapd is stopped (it holds ~4GB of VRAM and has no unload
 * IPC) and the idle stages are held off (a gamepad is not seat input, so the
 * screen would otherwise dim mid-game). Super+G forces it on/off:
 *   game_mode = on|off                 (default on)
 *   game_suspend_ai = on|off           (default on — stop synapd while gaming)
 *   game_inhibit_idle = on|off         (default on — no dim/blank/lock)
 *   game_drop_effects = on|off         (default on — restores DIRECT SCANOUT)
 *       The post-process pass renders the scene offscreen and forces whole-
 *       output damage every frame, so a fullscreen game never reaches direct
 *       scanout while it is on. Dropping it for the game is invisible.
 *   game_pause_wallpaper = on|off      (default on — stop wallpaperengine)
 *   game_stop_bar = on|off             (default OFF — ~400 MB, visible restart)
 *   game_quiet_kmod = on|off           (default OFF — near-zero saving)
 *   game_exclude = firefox chibi tepris nexus-chat foot
 *       Space-separated app_ids that are NOT games; REPLACES the built-in list.
 *       This is what keeps a fullscreen Firefox video from stopping the AI.
 *   game_include = gamescope             (REPLACES the built-in list)
 *       Wayland-NATIVE app_ids that ARE games. "Fullscreen XWayland" misses a
 *       gamescope started from a Wayland session: it uses its own Wayland
 *       backend and runs the game on a nested Xwayland synui never sees, so
 *       every title with `gamescope -f -- %command%` in its Steam launch
 *       options went undetected. An allow-list, not "any fullscreen Wayland
 *       client" — that would make every fullscreen video a game.
 *   game_output = primary|focused|ask    (default primary)
 *       Which monitor a detected game is fullscreened onto. Games pick badly
 *       and cannot be told otherwise: an X11 game follows RandR order, and
 *       gamescope on the Wayland backend ignores both --prefer-output and the
 *       X11 primary flag. `primary` is the monitor marked PRIMARY in the
 *       display panel (Super+D, `p`); `ask` restores the old obey-the-client
 *       behaviour.
 *   game_ai_stop_cmd = sudo -n systemctl stop synapd.socket synapd.service
 *   game_ai_start_cmd = sudo -n systemctl start synapd.socket synapd.service
 *       synapd is a *system* unit, so a plain `systemctl stop` from the session
 *       user gets bounced by polkit ("interactive authentication required") —
 *       and synui_spawn is fire-and-forget, so that failure was invisible: game
 *       mode logged that it had suspended synapd while synapd kept running.
 *       sudo -n never prompts; the grant is /etc/sudoers.d/synapd-gamemode,
 *       exactly these two commands (see syn-install.sh). Change one and you
 *       MUST change the other: sudoers matches the whole command line, so a
 *       command that no longer matches fails silently all over again.
 *
 *       THE SOCKET IS PART OF THE COMMAND, and not for tidiness. synapd.service
 *       is socket-activated (Requires=synapd.socket). Stopping the service alone
 *       leaves the socket listening, so the next client to connect — synguard,
 *       synnet, chibi, vibe, synsh, any of them — makes systemd start synapd
 *       straight back up, in the same second. Measured: `Stopped` immediately
 *       followed by `Started`, with no `Scheduled restart job` line, which is
 *       how you tell socket activation from Restart=. Game mode therefore
 *       looked intermittent — it worked whenever nothing happened to connect
 *       during the game, and silently did nothing whenever something did.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "synui.h"

static char *strip(char *s)
{
    while (isspace(*s)) s++;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace(*e)) *e-- = '\0';
    return s;
}

/* The `bar_shell` spellings, in syn_bar_shell_t order. Lives here rather than
 * next to a bar implementation because there is none in this process: synui
 * parses this key and nothing else touches it. systemd/synui-bar.sh matches the
 * same two words, and the control panel's row draws its own capitalised copy —
 * three surfaces, but only this one and the shell script have to AGREE, and
 * they agree on two literals that are also the synuirc spelling. */
const char *const syn_bar_shell_names[SYN_BAR_SHELL_COUNT] = {
    "synapse", "antiquity",
};

/* The `bar_edge` spellings, in syn_bar_edge_t order — and deliberately the same
 * two words `dock_edge` uses, so "put it at the bottom" is spelled one way for
 * both pieces of furniture. Here for the same reason as bar_shell above: the
 * compositor parses the key and never acts on it; quickshell's BarConfig.qml is
 * what reads it back. */
const char *const syn_bar_edge_names[SYN_BAR_EDGE_COUNT] = {
    "top", "bottom",
};

/* The `bar_shape` spellings, in syn_bar_shape_t order. Hyphenated because the
 * control panel persists an enum by lower-casing the name it DISPLAYS, so
 * "Floating pill" would reach this table as "floating pill" and not match —
 * the same rule ctl_names_anim_curve's "Ease-in-out" follows. Parsed here and
 * acted on by BarConfig.qml, like the two keys above it. */
const char *const syn_bar_shape_names[SYN_BAR_SHAPE_COUNT] = {
    "full-width", "rounded-ends", "floating-pill",
};

/* ── Keybindings ─────────────────────────────────────────── */
static uint32_t parse_mod(const char *name)
{
    if (!strcasecmp(name, "super") || !strcasecmp(name, "logo") ||
        !strcasecmp(name, "mod4"))
        return WLR_MODIFIER_LOGO;
    if (!strcasecmp(name, "shift"))
        return WLR_MODIFIER_SHIFT;
    if (!strcasecmp(name, "ctrl") || !strcasecmp(name, "control"))
        return WLR_MODIFIER_CTRL;
    if (!strcasecmp(name, "alt") || !strcasecmp(name, "mod1"))
        return WLR_MODIFIER_ALT;
    return 0;
}

/* "<mod>+…+<key>" → a (mods, sym) pair, or 0 if the key part is not a keysym.
 *
 * Public because the rebind helper has to go BOTH ways and the two directions
 * have to agree exactly: it writes a combo into binds.state with
 * syn_bind_format_combo() below and the next login reads it back through here.
 * A formatter that spelled a key differently from what this accepts would be a
 * shortcut that silently vanished at the next login — the failure that is
 * hardest to attribute, because the file looks right. */
bool syn_bind_parse_combo(const char *combo, uint32_t *mods_out,
                          xkb_keysym_t *sym_out)
{
    uint32_t mods = 0;
    xkb_keysym_t sym = XKB_KEY_NoSymbol;

    char buf[128];
    snprintf(buf, sizeof(buf), "%s", combo);
    char *save = NULL;
    for (char *tok = strtok_r(buf, "+", &save); tok;
         tok = strtok_r(NULL, "+", &save)) {
        uint32_t m = parse_mod(tok);
        if (m) { mods |= m; continue; }
        sym = xkb_keysym_from_name(tok, XKB_KEYSYM_CASE_INSENSITIVE);
    }
    if (sym == XKB_KEY_NoSymbol) return false;

    if (mods_out) *mods_out = mods;
    if (sym_out)  *sym_out  = xkb_keysym_to_lower(sym);
    return true;
}

/* The reverse: a combo in the spelling synuirc takes and syn_bind_parse_combo()
 * reads back. Lower case throughout, and the modifiers in the order the shipped
 * binds are written, so a hand-written config and a generated binds.state line
 * for the same chord come out as the same bytes.
 *
 * NOT the same as ctlpanel.c's combo_str(), and that is the point: that one
 * spells keycaps for a human ("Super+Shift+Q", "Esc", "="), this one spells
 * xkb ("super+shift+q", "escape", "equal"). Mixing them up gives a file that
 * reads perfectly and parses to nothing. */
void syn_bind_format_combo(uint32_t mods, xkb_keysym_t sym, char *out, size_t n)
{
    char key[64] = {0};
    if (xkb_keysym_get_name(sym, key, sizeof(key)) <= 0)
        snprintf(key, sizeof(key), "NoSymbol");

    snprintf(out, n, "%s%s%s%s%s",
             (mods & WLR_MODIFIER_LOGO)  ? "super+" : "",
             (mods & WLR_MODIFIER_CTRL)  ? "ctrl+"  : "",
             (mods & WLR_MODIFIER_ALT)   ? "alt+"   : "",
             (mods & WLR_MODIFIER_SHIFT) ? "shift+" : "",
             key);

    /* xkb spells most keys lower case already, but not all of them ("Escape",
     * "Print", "XF86AudioMute"). parse_mod and xkb_keysym_from_name are both
     * case-insensitive, so folding here costs nothing and makes the file
     * uniform — which matters because these lines sit next to hand-written
     * ones in the same language. */
    for (char *p = out; *p; p++)
        *p = (char)tolower((unsigned char)*p);
}

/* ── The tap key ─────────────────────────────────────────────
 *
 * `tap_key` is a modifier and nothing else — it is not half of a chord here, it
 * IS the shortcut — so these two are a mask↔name pair rather than the
 * combo pair above. They live beside that pair for the same reason it exists:
 * the rebind helper writes `tap_key` into binds.state and this parser reads it
 * back, and a second spelling would be a tap that works all session and is gone
 * at the next login.
 *
 * Only the four that `parse_mod` knows. Caps Lock and the level-3 shift are
 * modifiers too, but neither is one a desktop may quietly redefine.
 */
uint32_t syn_tap_mod_from_sym(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_Super_L:   case XKB_KEY_Super_R:
    case XKB_KEY_Meta_L:    case XKB_KEY_Meta_R:
        return WLR_MODIFIER_LOGO;
    case XKB_KEY_Control_L: case XKB_KEY_Control_R:
        return WLR_MODIFIER_CTRL;
    case XKB_KEY_Alt_L:     case XKB_KEY_Alt_R:
        return WLR_MODIFIER_ALT;
    case XKB_KEY_Shift_L:   case XKB_KEY_Shift_R:
        return WLR_MODIFIER_SHIFT;
    default:
        return 0;
    }
}

/* The synuirc spelling of a tap modifier — lower case, the words parse_mod
 * takes back. 0 is "none", which is a real setting (the tap turned off) and not
 * an error, so it has a name like the rest. */
const char *syn_tap_mod_name(uint32_t mod)
{
    switch (mod) {
    case WLR_MODIFIER_LOGO:  return "super";
    case WLR_MODIFIER_CTRL:  return "ctrl";
    case WLR_MODIFIER_ALT:   return "alt";
    case WLR_MODIFIER_SHIFT: return "shift";
    default:                 return "none";
    }
}

/* Take a combo out of the table. Returns false if nothing was bound to it,
 * which is what lets `unbind` report a line that does nothing rather than
 * succeeding silently.
 *
 * Order-preserving (memmove, not swap-with-last): the bind table's order is
 * what the shortcuts column and the palette list in, so removing one entry must
 * not reshuffle the rest under the reader. */
bool config_unbind_combo(syn_config_t *cfg, uint32_t mods, xkb_keysym_t sym)
{
    for (int i = 0; i < cfg->bind_count; i++) {
        if (cfg->binds[i].mods != mods || cfg->binds[i].sym != sym) continue;
        memmove(&cfg->binds[i], &cfg->binds[i + 1],
                (size_t)(cfg->bind_count - i - 1) * sizeof(cfg->binds[0]));
        cfg->bind_count--;
        return true;
    }
    return false;
}

bool config_unbind(syn_config_t *cfg, const char *combo)
{
    uint32_t mods; xkb_keysym_t sym;
    if (!syn_bind_parse_combo(combo, &mods, &sym)) {
        wlr_log(WLR_ERROR, "synui: unbind: bad key in '%s'", combo);
        return false;
    }
    return config_unbind_combo(cfg, mods, sym);
}

/* Register a (mods, sym) chord → "<action> [arg]", replacing whatever held that
 * chord. The string form below funnels into this; the rebind helper calls it
 * directly, because it already has the chord as a keypress and turning it into
 * text just to parse it back would be one more place for the two spellings to
 * disagree. */
void config_bind_set(syn_config_t *cfg, uint32_t mods, xkb_keysym_t sym,
                     const char *action, const char *arg)
{
    syn_bind_t *b = NULL;
    for (int i = 0; i < cfg->bind_count; i++) {
        if (cfg->binds[i].mods == mods && cfg->binds[i].sym == sym) {
            b = &cfg->binds[i];
            break;
        }
    }
    if (!b) {
        if (cfg->bind_count >= SYN_BINDS_MAX) {
            wlr_log(WLR_ERROR, "synui: bind table full (%d)", SYN_BINDS_MAX);
            return;
        }
        b = &cfg->binds[cfg->bind_count++];
    }
    b->mods = mods;
    b->sym  = sym;
    snprintf(b->action, sizeof(b->action), "%s", action);
    snprintf(b->arg, sizeof(b->arg), "%s", arg ? arg : "");
}

/* Register "<mod>+…+<key>" → "<action> [arg]". Same-combo binds replace the
 * earlier entry so user config overrides the seeded defaults. */
static void config_bind(syn_config_t *cfg, const char *combo,
                        const char *action_and_arg)
{
    uint32_t mods = 0;
    xkb_keysym_t sym = XKB_KEY_NoSymbol;

    if (!syn_bind_parse_combo(combo, &mods, &sym)) {
        wlr_log(WLR_ERROR, "synui: bind: bad key in '%s'", combo);
        return;
    }

    /* Split the action from its argument on the first whitespace. */
    char action[SYN_BIND_ACTION_LEN] = {0};
    const char *sp = action_and_arg;
    while (*sp && !isspace(*sp)) sp++;
    size_t alen = (size_t)(sp - action_and_arg);
    if (alen == 0 || alen >= sizeof(action)) {
        wlr_log(WLR_ERROR, "synui: bind %s: bad action '%s'",
                combo, action_and_arg);
        return;
    }
    memcpy(action, action_and_arg, alen);
    while (isspace(*sp)) sp++;

    config_bind_set(cfg, mods, sym, action, sp);
}

/* Indexed by syn_focus_mode_t. These spellings are the synuirc vocabulary, so
 * they are a FORMAT: renaming one silently turns an existing config line into
 * an unknown word. Lives here rather than beside the focus code in input.c
 * because the parser below is the thing that must have it — and because the
 * panel's own table test links config.c without linking the compositor. */
const char *const syn_focus_mode_names[SYN_FOCUS_MODE_COUNT] = {
    "click", "sloppy", "strict",
};

/*
 * Same contract as above, for the animation enums.
 *
 * These must equal the control panel's display names LOWER-CASED, character
 * for character: ctl_persist() writes an enum row to settings.state by folding
 * the display name's case and nothing else, and the parser below has to accept
 * what it wrote. A hyphen that is a space on the other side is a setting that
 * adjusts fine, persists fine, and is silently back to its default at the next
 * login. tests/ctlpanel_table_test.c walks every option of every enum row for
 * exactly this reason — one notch of Right is not enough to catch it.
 */
const char *const syn_anim_window_names[ANIM_WINDOW_COUNT] = {
    "off", "fade", "rise",
};
const char *const syn_anim_ws_names[ANIM_WS_COUNT] = {
    "off", "fade", "slide",
};
const char *const syn_anim_curve_names[ANIM_CURVE_COUNT] = {
    "ease-out", "linear", "ease-in-out", "ease-in",
};

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* An animation length, clamped. 0 = off; the ceiling is there because a
 * multi-second fade is a broken desktop, not a preference — and because the
 * control panel's slider has to have an end. Shared by the three duration
 * keys so they cannot drift apart. */
static int anim_ms_of(const char *val)
{
    int ms = atoi(val);
    if (ms < 0)    ms = 0;
    if (ms > 1000) ms = 1000;
    return ms;
}

/* Parse "#rrggbb" (or "rrggbb") into RGBA floats; alpha fixed at 1.0.
 * Returns 0 and leaves out[] untouched on malformed input. */
static int parse_hex_color(const char *val, float out[4])
{
    if (val[0] == '#') val++;
    if (strlen(val) != 6) return 0;
    char *end;
    long v = strtol(val, &end, 16);
    if (end != val + 6) return 0;
    out[0] = (float)((v >> 16) & 0xff) / 255.0f;
    out[1] = (float)((v >>  8) & 0xff) / 255.0f;
    out[2] = (float)( v        & 0xff) / 255.0f;
    out[3] = 1.0f;
    return 1;
}

/* The two actions the Super+Space / Super+= pair ships on, spelled ONCE.
 * There used to be a swap (and a control-panel row) that moved them between the
 * two keys; it was a second declaration of a keybinding and it fought the
 * rebind helper, so the shortcuts palette is now the only owner of both chords.
 * These stay named because seed_default_binds() and the tests both want the
 * launcher command in exactly one place. */
#define SYN_BIND_LAUNCHER "spawn_toggle rofi -show drun"
#define SYN_BIND_CMDBAR   "cmdbar"

static void seed_default_binds(syn_config_t *cfg)
{
    static const struct { const char *combo, *action; } defaults[] = {
        { "super+return",    "term" },
        /* Super+Space is the app launcher, because that is what Super+Space is
         * on every other desktop and it was the one key here that did something
         * else. rofi rather than a native panel: it reads the same .desktop
         * roots the bar menu already curates, and it is the plain "start a
         * program" key with nothing clever behind it.
         *
         * spawn_toggle, so the key that opens it also puts it away — every
         * other panel bind below toggles, and this one not toggling was the
         * odd one out. rofi single-instances, so a second press used to be a
         * no-op you could not tell from a dropped keypress: the launcher stayed
         * up and only Escape closed it. Now the second press closes it, and the
         * lifetime synui has to manage to do that is one pid (see spawn_toggle
         * in input.c), not rofi's window. */
        { "super+space",     SYN_BIND_LAUNCHER },
        /* The AI command bar, displaced from Super+Space by rofi above. Super+=
         * puts it next to Super+Backspace (ai_ask) — on a US layout `=` is the
         * key immediately left of Backspace, so the two AI popups are physical
         * neighbours. Still a toggle, unlike rofi. */
        { "super+equal",     SYN_BIND_CMDBAR },
        { "super+a",         "overlay" },
        { "super+d",         "displays" },
        { "super+escape",    "menu" },
        { "super+c",         "control" },
        /* The shortcut palette. '/' is the find key in every pager, editor and
         * panel this desktop already has (including the control panel's own
         * search box), so it is the one key that needs no explaining.
         *
         * Bound TWICE on purpose. On a US layout Super+? is Super+Shift+/, and
         * xkb hands the compositor the SHIFTED keysym — `question`, not
         * `slash` — so a single "super+slash" bind is a key that mysteriously
         * stops working the moment you hold Shift, which is exactly what a
         * hand reaching for "?" does. Both spellings, one action. */
        { "super+slash",          "keys" },
        { "super+shift+question", "keys" },
        { "super+b",         "bluetooth" },
        { "super+shift+b",   "night_light" },   /* "blue light" */
        { "super+shift+r",   "record" },        /* start/stop screen recording */
        { "super+v",         "clipboard" },     /* clipboard history */
        /* The emoji picker, on the free half of the convention every other
         * desktop uses. Windows offers Win+. and Win+; for the same panel and
         * GNOME's IBus picker is Ctrl+.; here super+period is already
         * column_expel (niri's own key, and a tiling bind is not something to
         * move for this), so the semicolon spelling is the one that is free.
         * Nothing else on this layout wants it. */
        { "super+semicolon", "emoji" },
        /* Brightness keys. No modifier: they are dedicated keys on every
         * laptop, and nothing else claims them. */
        { "xf86monbrightnessup",   "brightness_up" },
        { "xf86monbrightnessdown", "brightness_down" },
        { "super+q",         "close" },
        { "super+shift+q",   "quit" },
        { "super+tab",       "layout_cycle" },
        /* THE switcher key, and it opens MISSION CONTROL by default —
         * `alt_tab_style = switcher` in synuirc (or the control panel row) puts
         * the MRU thumbnail strip back. Either way the gesture is the one every
         * desktop has: hold Alt, tap Tab to walk, let go to pick.
         *
         * Not the stacking-order walk that super+j/k do. Mission control had
         * super+x of its own until 2026-08-07 and gave it up for this — velle
         * asked for the overview to BE Alt+Tab, and a feature on the key
         * everybody already presses does not also need a letter. */
        { "alt+tab",         "alt_tab" },
        { "alt+shift+tab",   "alt_tab_prev" },
        { "super+h",         "master_shrink" },
        { "super+shift+l",   "master_grow" },
        /* niri (scrollable tiling) column moves, on the keys niri itself uses.
         * Comma pulls the focused window into the column on its left, period
         * pushes it back out into a column of its own. Both are no-ops on the
         * other four layouts, which have no columns to move it between — the
         * width keys above are shared instead, because "wider/narrower" means
         * the same thing on a master slot and on a niri column. */
        { "super+comma",     "column_consume" },
        { "super+period",    "column_expel" },
        { "super+l",         "lock" },
        { "super+j",         "focus_next" },
        { "super+k",         "focus_prev" },
        { "super+shift+j",   "stack_next" },
        { "super+shift+k",   "stack_prev" },
        { "super+f",         "float_toggle" },
        { "super+shift+f",   "fullscreen_toggle" },
        { "super+m",         "maximize_toggle" },
        { "super+shift+d",   "decorations_toggle" },
        { "super+n",         "minimize_toggle" },
        { "super+shift+n",   "minimize_restore" },
        /* Do Not Disturb. M for MUTE — velle's own word for it ("add mute
         * notifications") — and Shift+M was the free half of a key whose
         * unshifted form is maximize. N would have been the obvious letter and
         * is gone twice over: super+n is minimize and super+shift+n is the
         * restore above, both of which predate there being anything to
         * silence. */
        { "super+shift+m",   "dnd" },
        { "super+backspace", "ai_ask" },
        { "super+w",         "wallpaper" },
        { "super+shift+w",   "wallpaper_reload" },
        { "super+e",         "filters" },
        { "super+p",         "power" },
        /* The screensaver panel. Z for zzz — the only mnemonic anybody guesses
         * for a screensaver, and one of the few unbound letters left: S is the
         * sound picker, Shift+S the region screenshot. */
        { "super+z",         "saver" },
        /* Cursor theme picker. "pointer" rather than "cursor" because super+c is
         * the control panel and super+shift+c is cat mode — C was gone twice
         * over before this feature existed. */
        { "super+shift+p",   "cursor" },
        /* The image cropper, opening on its recent-images list. X for "cut",
         * for the same reason the cursor picker is on P: C was already gone
         * twice over, and a crop bound to some third letter of "crop" would be
         * less guessable than the scissors key. Both X spellings were free.
         *
         * This is the one panel that could not be bound at all until the list
         * existed — crop_open() needs a path and a keybind has none to give, so
         * a bare `crop` used to do nothing but close. */
        { "super+shift+x",   "crop" },
        /* The image viewer, on the free half of the network panel's key. I for
         * image; V is the clipboard history and has been since long before
         * there was anything to view. Like the cropper it opens on the
         * recent-images list when it is given nothing to show — the list is
         * told which of the two faces Enter leads to, so the same rows serve
         * both keys without either one lying about where it goes.
         *
         * This is also the action behind the Image Viewer menu entry and
         * therefore behind opening an image from a file manager: SynapseOS had
         * no image viewer at all until this, and /usr/share/applications named
         * a BROWSER as the system answer to "open this PNG" — it was the only
         * installed thing that had declared the type. */
        { "super+shift+i",   "view" },
        /* The calculator, on the chord this comment spent a day reserving.
         *
         * SUPER+X WAS FREE. Mission control had it — X for eXposé — until
         * 2026-08-07, when velle moved the overview onto Alt+Tab: "take mission
         * control off super x and make the alt tab default". A feature reached
         * by the key everybody already presses does not need a second one, and
         * an unbound chord is worth more than a duplicate. This is the feature
         * it was being kept for.
         *
         * X rather than a letter of "calc", for the reason the cursor picker is
         * on P and the cropper on Shift+X: C is gone twice over (super+c is the
         * control panel, super+shift+c is cat mode), and the third letter of a
         * word is not a shortcut anybody guesses. X is the free key.
         *
         * The `overview` action is still there and still dispatchable — bind it
         * to anything with a `bind = <chord> overview` line, or Super+/ then F2
         * on the row. The control panel's Desktop ▸ Mission control row opens it
         * too. */
        { "super+x",         "calc" },
        /* Themes, not the task manager. The task manager had two binds and needs
         * one — ctrl+alt+delete below is the one everybody already reaches for,
         * so super+t goes to the theme manager, which had only the far less
         * guessable super+shift+a. That freed super+shift+a, which stayed
         * unbound until the desktop widgets claimed it below. */
        { "super+t",         "theme" },
        /* T for TILE. This key has moved twice in a week and this is where it
         * settles: velle asked for it back on 2026-08-07, having had cascade on
         * it for one afternoon. T is the letter of the thing everybody reaches
         * for, and the thing everybody reaches for is "put these windows in a
         * grid" — so retile keeps it and cascade takes the neighbouring key,
         * not the other way round.
         *
         * It took T off the calendar on 2026-07-31, which lost nothing by it:
         * the bar clock opens the calendar, which is how everyone reaches it
         * anyway. Nothing is bound to `calendar` now; bind it back with a
         * `bind =` line if you want a key for it. */
        { "super+shift+t",   "retile" },
        /* Cascade: deal the desktop out as overlapping cards, splitting into
         * several piles once one would run off the screen. Y because it is next
         * to T — the two keys that rearrange the whole desk sit under the same
         * finger — and because nothing else wanted it. */
        { "super+shift+y",   "cascade" },
        /* G for grid: tidy a FLOATING desktop without leaving it. Super+G is
         * game mode and super+shift+g was free.
         *
         * The difference from retile, which is the only reason this key exists:
         * retile ends with the desktop on the TILING layout. This one keeps
         * whatever layout you chose and only undoes the dragging, so somebody
         * who likes floating windows can tidy them and still have floating
         * windows afterwards. On any other layout it is the same gesture as
         * retile minus the switch. */
        { "super+shift+g",   "float_arrange" },
        { "super+i",         "network" },
        /* Not super+n: that is minimize, and has been since before there was
         * anything to read. R for RSS — the panel is a feed reader. */
        { "super+r",         "news" },
        /* The one shortcut everybody already has in their fingers. Nothing
         * below us claims it: logind's ctrl-alt-del handling is a VT/console
         * thing, so inside a Wayland session the key reaches the compositor. */
        { "ctrl+alt+delete", "taskmgr" },
        /* Print grabs the monitor you are looking at (the `screenshot` action
         * resolves it — grim cannot). Shift+Print drags out an area with
         * slurp; Ctrl+Print takes the whole layout, every monitor at once.
         * super+shift+s is the same area-select under the shortcut most people
         * already have in their fingers from elsewhere. */
        { "print",           "screenshot" },
        { "shift+print",     "spawn synui-screenshot region" },
        { "ctrl+print",      "spawn synui-screenshot full" },
        { "super+shift+s",   "spawn synui-screenshot region" },
        /* The USB volume knob (LCTECH LCKEY) reports plain KEY_VOLUMEUP /
         * KEY_VOLUMEDOWN / KEY_MUTE, which the evdev keymap turns into these
         * XF86Audio keysyms with no modifier held. synui had no audio binds at
         * all, so the knob's events reached the bind table and matched nothing.
         * Bare-key binds are fine here — `print` above is one. */
        { "xf86audioraisevolume", "volume up" },
        { "xf86audiolowervolume", "volume down" },
        { "xf86audiomute",        "volume mute" },
        { "super+g",         "game" },
        /* super+shift+a was left FREE when the theme manager moved to super+t,
         * explicitly for the next feature. This is it: the desktop widgets
         * (visualiser, system monitor, clock, quick-launch, post-it note).
         *
         * A spawn rather than a native action, because synui does not own the
         * state — synui-widgets is the single writer of widgets.state and the
         * bar watches that file, so the keybind, the control panel row and the
         * command line all go through one implementation. Group-toggles: if any
         * widget is on it turns them all off, so the key is always a reliable
         * "clear the desktop". */
        /* The widget manager, one row per widget. It was `spawn synui-widgets
         * toggle` — a blind group flip — until the panel existed; Space from
         * any row in the panel still does exactly that, so nothing got slower.
         * The old form stays bindable for anyone who preferred no UI. */
        { "super+shift+a",   "widgets" },
        /* Event sounds. Plain super+s was the only unused letter left, and it
         * is the obvious one. */
        { "super+s",         "sounds" },
        { "super+shift+c",   "cat" },
        { "super+o",         "move_output" },
        { "super+shift+o",   "move_output prev" },
    };
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        /* Shout about a duplicate rather than shipping one. handle_keybinding
         * takes the FIRST match, so a second bind on a combo is not a conflict
         * anyone sees — it is the older feature silently going dead. Adding
         * night_light on super+shift+n did exactly that to minimize_restore and
         * only turned up because the table happened to get grepped. */
        for (size_t j = 0; j < i; j++) {
            if (strcmp(defaults[i].combo, defaults[j].combo) == 0)
                wlr_log(WLR_ERROR, "synui: config: DUPLICATE default bind %s "
                        "(%s shadows %s) — the second one is dead",
                        defaults[i].combo, defaults[j].action, defaults[i].action);
        }
        config_bind(cfg, defaults[i].combo, defaults[i].action);
    }

    for (int i = 1; i <= WORKSPACE_MAX; i++) {
        char combo[32], act[16];
        snprintf(combo, sizeof(combo), "super+%d", i);
        snprintf(act, sizeof(act), "ws %d", i);
        config_bind(cfg, combo, act);
        snprintf(combo, sizeof(combo), "super+shift+%d", i);
        snprintf(act, sizeof(act), "movews %d", i);
        config_bind(cfg, combo, act);
    }
}

/* ── Config-dir paths ────────────────────────────────────── */

/* The single resolver for everything under synui's config dir: synuirc,
 * outputs.conf, and the *.state files (wallpaper/dock/power/filters/welcome).
 * Those state files each used to hardcode ~/.config/synui and ignore
 * XDG_CONFIG_HOME, which synuirc and outputs.conf honour — so pointing
 * XDG_CONFIG_HOME elsewhere read the settings from one directory and the
 * persisted picker/dock/power choices from another. Route every one of them
 * through here and they can't drift apart again. */
bool syn_config_path(char *buf, size_t n, const char *name)
{
    const char *xdg  = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(buf, n, "%s/synui/%s", xdg, name);
    else if (home && *home)
        snprintf(buf, n, "%s/.config/synui/%s", home, name);
    else
        return false;
    return true;
}

/* Create the config dir (and its parent) if absent, so a *_state_save() into
 * a fresh XDG_CONFIG_HOME writes instead of failing to a log line nobody
 * reads. Only writers need this; readers treat a missing file as "no
 * persisted choice". mkdir(2) errors other than EEXIST are left for the
 * caller's fopen() to report against the full path. */
void syn_config_ensure_dir(void)
{
    char dir[256];
    if (!syn_config_path(dir, sizeof(dir), "")) return;

    /* syn_config_path() left a trailing '/' from the empty name. */
    size_t len = strlen(dir);
    while (len > 1 && dir[len - 1] == '/') dir[--len] = '\0';

    /* mkdir -p: XDG_CONFIG_HOME can point at a path with any number of
     * missing components, so creating just the last one (or just its parent)
     * still fails with ENOENT. Walk it and create each in turn; intermediate
     * failures are left to the final mkdir to report against the full path. */
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(dir, 0755);
        *p = '/';
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST)
        wlr_log(WLR_ERROR, "synui: mkdir %s failed: %s", dir, strerror(errno));
}

/*
 * Every setting's out-of-the-box value, in one place.
 *
 * Split out of synui_config_load() so it can be run against a scratch config as
 * well as the live one. The control panel keeps such a scratch copy and diffs
 * against it: that is what lets any row say whether it is still at its default
 * and offer to go back to it, without each row having to name its own default a
 * second time. A default written twice is a default that will disagree with
 * itself eventually.
 */
static void config_set_defaults(syn_config_t *cfg)
{
    /* syntty as of 0.1.0-359: it is ours, it starts in ~3 ms against kitty's
     * ~230, and it links no GL at all — which is also why it works on a VM
     * where kitty needs a GPU. kitty stays installed and stays one keystroke
     * away in SYNAPSE Settings; an existing synuirc is never rewritten, so
     * only new configurations move. */
    strncpy(cfg->terminal, "syntty", sizeof(cfg->terminal) - 1);
    cfg->night_light = 0;
    cfg->night_light_temp = 4000;
    cfg->display_mode = SYN_DISPLAY_EXTEND;
    cfg->hdmi_audio = -1;   /* auto: on iff the machine has a battery */
    cfg->autostart_count = 1;
    strncpy(cfg->autostart[0], "syntty", sizeof(cfg->autostart[0]) - 1);
    cfg->border_width = BORDER_WIDTH_DEFAULT;
    cfg->gap = GAP_DEFAULT;
    cfg->float_inset = FLOAT_INSET_DEFAULT;
    cfg->float_gap   = FLOAT_GAP_DEFAULT;
    cfg->master_factor = 0.60f;
    cfg->cascade_stack_max = CASCADE_STACK_MAX_DEF;
    cfg->titlebar_height = TITLEBAR_HEIGHT_DEF;
    cfg->remember_geometry = true;
    cfg->desktop_icons     = false;   /* opt-in; the menu flips it live, and
                                         deskicons.state remembers the flip */
    cfg->desktop_icon_arrange = SYN_ARRANGE_NAME;
    cfg->animation_ms      = ANIMATION_MS_DEF;
    cfg->anim_window_ms    = ANIMATION_MS_DEF;
    cfg->anim_workspace_ms = ANIMATION_MS_DEF;
    cfg->anim_window       = ANIM_WINDOW_FADE;
    cfg->anim_workspace    = ANIM_WS_FADE;
    cfg->anim_curve        = ANIM_CURVE_EASE_OUT;
    cfg->anim_rise_px      = ANIM_RISE_PX_DEF;
    { static const float c[4] = COLOR_TITLEBAR_NORM;    memcpy(cfg->titlebar_color,       c, sizeof(c)); }
    { static const float c[4] = COLOR_TITLEBAR_FOCUS;   memcpy(cfg->titlebar_color_focus, c, sizeof(c)); }
    { static const float c[4] = COLOR_TITLE_TEXT;       memcpy(cfg->titlebar_text,        c, sizeof(c)); }
    { static const float c[4] = COLOR_TITLE_TEXT_FOCUS; memcpy(cfg->titlebar_text_focus,  c, sizeof(c)); }
    cfg->ai_layout = 1;
    cfg->ai_ctx_decor = 1;
    cfg->start_overlay = 0;
    cfg->snap = 1;
    cfg->snap_zone = 28;              /* the old fixed SNAP_EDGE */

    /* Every window-behaviour default is what synui did before the setting
     * existed, so an upgrade changes nothing until someone opens the panel. */
    cfg->focus_mode     = SYN_FOCUS_CLICK;
    cfg->focus_delay_ms = 0;
    /* The exception to the paragraph above: this one DOES change what synui did
     * before the setting existed, on purpose. The old behaviour was a bug
     * wearing a feature's clothes — an open panel followed the pointer onto
     * whatever monitor it wandered to, mid-read. */
    cfg->panel_follow_pointer = 0;
    cfg->alt_tab_preview = 1;
    cfg->alt_tab_all_desktops = 1;
    cfg->alt_tab_minimized    = 1;
    /* Mission control IS the switcher by default (velle, 2026-08-07). It is the
     * one default here that changes an existing desktop's behaviour on upgrade,
     * which is why it is one line in synuirc and one row in the control panel
     * to put back. */
    cfg->alt_tab_overview     = 1;

    /* Theme + transparency. SYNAPSE's colours ARE the border/titlebar defaults
     * set just above, so leaving theme = SYNAPSE changes nothing; a `theme =`
     * line (parsed below) or theme.state (applied at startup) reskins from here.
     * Transparency is opt-in: off, the opacity levels are dormant. */
    cfg->theme            = SYN_THEME_SYNAPSE;
    /* Flat chrome, with the gradient ends and the 3D face seeded from the
     * caption colours: deco.c always reads these, so they must be valid even for
     * a config that never sees a theme (a `theme =` line or theme.state then
     * overwrites the lot via theme_load_colors). */
    cfg->chrome           = SYN_CHROME_FLAT;
    memcpy(cfg->titlebar_grad,       cfg->titlebar_color,       sizeof(cfg->titlebar_grad));
    memcpy(cfg->titlebar_grad_focus, cfg->titlebar_color_focus, sizeof(cfg->titlebar_grad_focus));
    memcpy(cfg->chrome_face,         cfg->border_color_norm,    sizeof(cfg->chrome_face));
    cfg->transparency     = 0;
    /* Unset, not 0: every theme but Prism has hand-tuned opacities and a
     * level of 0 would flatten them all to solid. Prism's own default is set
     * where the theme is applied, not here — a default that depended on the
     * theme would be a second place the theme is decided. */
    cfg->glass_level      = SYN_GLASS_UNSET;
    /* On, and it costs nothing on a desktop that has never set a level: the sync
     * only does anything once glass_level is something other than unset, so this
     * default changes no existing machine. What it does change is what happens
     * the FIRST time somebody moves the Glass row — which is the whole point,
     * since a master control that has to be switched on before it is a master is
     * a master nobody finds. */
    cfg->glass_sync       = 1;
    cfg->glass_pins       = 0;
    /* On: every surface still measures its own backdrop and refuses an alpha its
     * text cannot survive. Off is the escape hatch, not the default. */
    cfg->glass_legibility = 1;
    cfg->active_opacity   = 1.00f;
    cfg->inactive_opacity = 0.92f;
    /* Unset: foot follows the slider exactly as before until someone sets a
     * foot_alpha, so this key changes nothing for a config that omits it. */
    cfg->foot_alpha       = -1.0f;

    /* scenefx glass defaults (Stage 5). Rounded corners are on out of the box —
     * they cost nothing on opaque windows and are the single most visible piece
     * of the "glass" look. Blur is on but only paints behind translucent windows. */
    cfg->corner_radius    = 12;
    cfg->blur             = 1;
    cfg->blur_passes      = 3;
    cfg->blur_radius      = 5;
    cfg->blur_noise       = 0.02f;
    cfg->blur_brightness  = 0.90f;
    cfg->blur_contrast    = 1.00f;
    cfg->blur_saturation  = 1.15f;
    /* Glass halo: OFF. It costs a wider blur pass per window and it is a strong
     * look — opt in with `glass_halo = 14` or so. */
    cfg->glass_halo       = 0;
    /* Crop clients to their window geometry: ON. synui draws the shadow for
     * every window, so a client that ignores xdg-decoration and paints its own
     * on top (Firefox) is a second, bigger, square-cornered ring on one app.
     * `clip_csd_margin = off` puts the client's margin back. */
    cfg->clip_csd_margin  = 1;
    /* Drop shadow: on, a soft dark halo dropped a touch downward.
     *
     * Halved from 18/6 on 2026-07-26. An 18px sigma throws a shadow wider than
     * the titlebar is tall, which on a tiled desktop reads as haze in every gap
     * rather than as depth under a window — and it was set before the glass
     * work gave windows a blurred backdrop to sit on, which does much of the
     * same job. 9/3 keeps the same shape at half the reach. Anyone who wants
     * the old weight has both `shadow_blur_sigma = 18` and, now, the Shadow
     * size row in Super+E. */
    cfg->shadow           = 1;
    cfg->shadow_blur_sigma = 9.0f;
    /* No spread: the shadow is a pure gaussian tail outside the window, the
     * look synui has always had. Non-zero is the opt-in "GTK weight" look. */
    cfg->shadow_spread    = 0.0f;
    cfg->shadow_offset_x  = 0;
    cfg->shadow_offset_y  = 3;   /* halved with the sigma above, to keep the drop
                                    proportional to the blur it falls out of */
    cfg->shadow_color[0]  = 0.00f; cfg->shadow_color[1] = 0.00f;
    cfg->shadow_color[2]  = 0.00f; cfg->shadow_color[3] = 0.45f;
    /* SYNAPSE's neon cyan — the panel accent render.c starts on; theme_apply()
     * (via theme.state at startup, or a synuirc `theme =`) reskins it. */
    cfg->panel_accent[0] = 0.00f; cfg->panel_accent[1] = 0.85f;
    cfg->panel_accent[2] = 0.75f; cfg->panel_accent[3] = 1.00f;

    /* GLES post-process: OFF by default. Scanlines, curvature and chromatic
     * aberration over the whole screen are a look, not a desktop — and every
     * config that never mentions `effects` was getting them, which is how the
     * live ISO and a fresh install came up as a CRT simulation before their
     * owner had chosen anything. Opt in with `effects = on`, or turn it on
     * live in the Super+E panel. The strengths below stay tuned for that
     * moment, so `effects = on` alone still gives the full look. */
    cfg->effects           = 0;
    cfg->effect_scanline   = 0.35f;
    cfg->effect_curvature  = 0.25f;
    cfg->effect_aberration = 0.40f;
    cfg->effect_glitch     = 0.60f;
    cfg->effect_phosphor   = SYN_PHOSPHOR_OFF;   /* colour, until a tint is picked */
    cfg->effect_mono       = 0.90f;              /* strong monochrome when it is */
    cfg->effect_bloom      = 0.55f;              /* phosphor glow, the way a real tube blooms */
    /* A touch of lift: the unlit field reads as a tube that is switched on
     * rather than a black rectangle, without tinting a bare desktop. Both
     * ends of this slider have shipped as the only setting; see effects.c. */
    cfg->effect_lift       = 0.40f;
    /* Not centred, on purpose. 0.5 is the tint table exactly, and velle's
     * report after 403 is that the fitted amber still reads too yellow — so the
     * shipped default sits one notch (-6 degrees) toward orange, which is the
     * one adjustment this row exists to make. It is a knob precisely because it
     * is taste: put it back at 0.50 for the table's own colour. */
    cfg->effect_hue        = 0.45f;

    {
        static const float norm[4]  = COLOR_BORDER_NORM;
        static const float focus[4] = COLOR_BORDER_FOCUS;
        static const float ai[4]    = COLOR_BORDER_AI;
        static const float warn[4]  = COLOR_BORDER_WARN;
        memcpy(cfg->border_color_norm,  norm,  sizeof(norm));
        memcpy(cfg->border_color_focus, focus, sizeof(focus));
        memcpy(cfg->border_color_ai,    ai,    sizeof(ai));
        memcpy(cfg->border_color_warn,  warn,  sizeof(warn));
    }

    /* Input defaults: keymap fields stay empty (XKB_DEFAULT_* env / system
     * default); libinput tri-states -1 = leave the device alone. */
    cfg->repeat_rate    = 25;
    cfg->repeat_delay   = 600;
    cfg->tap_to_click   = -1;
    cfg->natural_scroll = -1;
    cfg->left_handed    = -1;
    cfg->accel_speed    = 0.0f;
    cfg->accel_speed_set = 0;

    /* The bundled SYNAPSE image, not an empty path. An empty one paints
     * bg_color and a desktop with no wallpaper reads as a blank screen rather
     * than a distro — which is what the live ISO and a fresh install looked
     * like, since neither writes a `wallpaper` line. `wallpaper = none` is
     * still how you ask for the flat colour. */
    strncpy(cfg->wallpaper, SYNUI_DATADIR "/wallpaper.png",
            sizeof(cfg->wallpaper) - 1);
    cfg->wallpaper_mode = SYN_WALLPAPER_FILL;
    cfg->wallpaper_src  = SYN_WP_SRC_IMAGE;
    cfg->wallpaper_out_n = 0;   /* every monitor follows the keys above */

    /* Screensaver (saver.c). OFF by default — saver_timeout 0 — so an existing
     * install's idle behaviour is exactly what it was until the saver is asked
     * for. The MODE still defaults to something worth looking at, so turning it
     * on is one keypress in the Super+Z panel rather than two. */
    cfg->saver_timeout  = 0;
    cfg->saver_mode     = SYN_SAVER_CLOCK;
    cfg->saver_lock     = 0;
    cfg->saver_dir[0]   = '\0';   /* empty = the wallpapers the picker offers */
    cfg->saver_interval = 30;

    /* Lock / greeter appearance. DESKTOP is the default: the lock shows the
     * wallpaper it just covered, dimmed enough to read a password over. 55% and
     * a 16px blur were picked by looking at the bundled wallpaper and the
     * photos in ~/Pictures/Wallpapers — dark enough for the ink ladder in
     * lock.c, light enough that the picture is still recognisably there. */
    cfg->lock_bg           = SYN_LOCK_BG_DESKTOP;
    cfg->lock_bg_image[0]  = '\0';
    cfg->lock_bg_dim       = 55;
    cfg->lock_bg_blur      = 16;
    cfg->lock_theme_follow = 1;
    /* Only consulted when lock_theme_follow is off; seeded with the SYNAPSE
     * cyan the lock screen was hardcoded to before it was themeable, so
     * switching to "custom" lands on the old look rather than on black. */
    cfg->lock_accent[0] = 0.45f; cfg->lock_accent[1] = 0.90f;
    cfg->lock_accent[2] = 0.85f; cfg->lock_accent[3] = 1.0f;

    /* Empty theme = inherit XCURSOR_THEME, which is exactly what synui did
     * before cursor.c existed, so an untouched system behaves identically.
     * 24 matches the size the compositor was previously hardcoded to. */
    cfg->cursor_theme[0] = '\0';
    cfg->cursor_size     = 24;

    /* Empty font = "monospace", the fontconfig alias every panel drew in before
     * the picker existed — so an untouched system looks exactly as it did.
     *
     * text.c is deliberately NOT touched here. This function runs against a
     * SCRATCH config (see the header) and text.c's copy is process-global, so a
     * syn_text_set_ui_font() on this line reaches straight past the scratch
     * struct and repaints the live desktop. That is not hypothetical: the
     * control panel diffs every row against synui_config_defaults() on each
     * repaint, so the first open of the panel in a session reset the UI font to
     * monospace while settings.state still named the chosen family — the font
     * came back at the next login and vanished again on the next open.
     *
     * synui_config_load() applies cfg->ui_font once at the end instead, which
     * still covers the reload case this used to be here for: the font is pushed
     * from the FINAL resolved value, so a reload with nothing naming a font
     * lands back on the default. */
    cfg->ui_font[0] = '\0';

    cfg->cat_start         = 0;   /* opt-in; Super+Shift+C toggles it live */
    cfg->cat_breed         = CAT_BREED_NEON;   /* the house cat */

    cfg->welcome_at_startup = 1;
    cfg->notif_dnd = 0;
    cfg->numlock            = 1;

    /* Recording sound is opt-in, like every other capture on this desktop. */
    cfg->record_audio       = 0;
    /* A mezzanine is ~1.1 GB/min against ~200 KB for an ordinary take, so
     * it is opt-in for size, not for privacy like the audio switch. */
    cfg->record_edit        = 0;

    cfg->dock_enabled      = 1;
    cfg->dock_autohide     = 1;
    cfg->dock_height       = 64;
    cfg->dock_hover_margin = 4;
    cfg->dock_edge         = SYN_DOCK_EDGE_BOTTOM;
    /* AUTO, so the theme decides: glass on macOS 26, the tinted slab on the
     * other twelve. See dock_style_is_glass(). */
    cfg->dock_style        = SYN_DOCK_STYLE_AUTO;
    /* 0.72, not the 0.80 this was a literal at for its whole life. The dock is
     * the one panel that floats over the wallpaper and it should read as
     * floating; 0.80 is a slab with a hint of the desktop behind it. Still well
     * clear of the point where a dark icon on a dark wallpaper stops reading —
     * the row goes down to 0.20 for anyone who wants that. */
    cfg->dock_opacity      = 0.72f;
    /* 26 against the old literal 16, and clamped to half the bar's thickness at
     * draw time so it can never round past a capsule. At the default 64px
     * thickness that is 26 of a possible 32 — noticeably round, the shape the
     * Mac dock has, without becoming a lozenge on a 200px dock. */
    cfg->dock_radius       = 26;
    cfg->widget_glass      = SYN_WIDGET_GLASS_AUTO;
    cfg->scene_ink         = 1;
    cfg->wallpaper_accent  = SYN_WP_ACCENT_AUTO;
    cfg->dock_pin_count    = 0;
    cfg->launcher_style    = SYN_LAUNCHER_TEXT;
    /* A tapped Super opens the start menu, the way it does everywhere else —
     * the default the key is named after. */
    cfg->tap_mod           = WLR_MODIFIER_LOGO;
    /* ...and it opens the start menu, which is the only thing the tap could do
     * before `tap_action` existed. Keeping that as the default is what makes
     * the new key invisible to everyone who does not want it. */
    snprintf(cfg->tap_action, sizeof(cfg->tap_action), "start_menu");
    cfg->tap_arg[0]        = '\0';
    /* The shipped bar, and the system icon theme. Both read by synui-bar, not
     * by the compositor — see syn_bar_shell_t. */
    cfg->bar_shell         = SYN_BAR_SHELL_SYNAPSE;
    /* The bar is on, and the pair that turns it off and back on. See the
     * fields' comment in synui.h for why this is commands and not a flag.
     *
     * The stop names BOTH shipped bars, because "off" failing silently on the
     * one the user happens to run is the worst outcome for a switch — pkill -x
     * on a name that is not running is a no-op, so naming both costs nothing.
     * -x, never -f: the -f form matches against whole command lines, which
     * includes the argv of whatever launched this, and a `pkill -f` has taken
     * out more than it meant to in this tree before. */
    /* All three open as WINDOWS: a close button, a header you drag them by, and
     * no claim on the pointer or the keyboard once you click something else.
     * They were modal chrome and it was wrong for panels you work in — velle,
     * on the calculator: "won't let you click anywhere else when open and wont
     * move". Per panel rather than one setting for all three, so a control
     * panel that should vanish when you look away and a calculator you park in
     * a corner can disagree. `clickoff` restores the original behaviour. */
    cfg->calc_close        = SYN_PANEL_CLOSE_WINDOW;
    cfg->ctlpanel_close    = SYN_PANEL_CLOSE_WINDOW;
    cfg->taskmgr_close     = SYN_PANEL_CLOSE_WINDOW;
    cfg->bar_enabled       = 1;
    snprintf(cfg->bar_stop_cmd,  sizeof(cfg->bar_stop_cmd),
             "pkill -x quickshell ; pkill -x waybar");
    snprintf(cfg->bar_start_cmd, sizeof(cfg->bar_start_cmd), "synui-bar");
    cfg->bar_edge          = SYN_BAR_EDGE_TOP;
    cfg->bar_shape         = SYN_BAR_SHAPE_FULL;
    /* Negative is "the theme decides", not a number the bar could use — see the
     * field. Every desktop that never opens the row stays here. */
    cfg->bar_opacity       = -1.0f;
    cfg->bar_icon_theme[0] = '\0';

    cfg->power_enabled = 1;
    cfg->power_dim     = 240;
    cfg->power_blank   = 600;
    cfg->power_lock    = 900;
    /* Suspending this box would take down anything it serves over the
     * network, so idle-suspend is opt-in from the panel, never a default. */
    cfg->power_suspend = 0;
    /* On by default because it costs a machine without a reader one fork per
     * lock and nothing else — see the field's comment in synui.h. */
    cfg->lock_fingerprint = 1;
    /* The pgrep guard keeps a second idle period from stacking another
     * swaylock on top of the one already covering the screen. */
    /* `-c 000000` alone drew a featureless black rectangle with no indicator
     * until a key was pressed, so a lock was indistinguishable from a dead or
     * blanked screen — "it didn't go to a lock screen, it blacked out". Give it
     * the SynapseOS neon and keep the ring on screen (--indicator-idle-visible)
     * so it is obviously asking for a password, and so a wrong one is visibly
     * wrong (--ring-wrong-color) rather than silent.
     *
     * swaylock 1.8.6 has no --clock. */
    /* NB: there is no bare `--indicator` in swaylock 1.8.6. getopt_long treats
     * it as an ambiguous prefix of --indicator-radius/-thickness/-idle-visible/
     * -caps-lock/-x-position/-y-position, prints usage and EXITS — so passing
     * it means no lock screen appears at all. --indicator-idle-visible alone is
     * what keeps the ring on screen. */
    snprintf(cfg->power_lock_cmd, sizeof(cfg->power_lock_cmd),
             "pgrep -x swaylock >/dev/null || swaylock -f -c 000814"
             " --indicator-idle-visible"
             " --indicator-radius 110 --indicator-thickness 8"
             " --ring-color 00ffff --ring-ver-color ff00c8"
             " --ring-wrong-color ff3355 --key-hl-color ff00c8"
             " --inside-color 080814 --text-color 00ffff"
             " --line-color 080814 --separator-color 080814");
    snprintf(cfg->power_suspend_cmd, sizeof(cfg->power_suspend_cmd),
             "systemctl suspend");

    /* Same defaults systemd ships (HandleLidSwitch and
     * HandleLidSwitchExternalPower both suspend, HandleLidSwitchDocked
     * ignores), so a laptop that never opens Super+P behaves the way its owner
     * already expects — synui just does it itself, which is what makes the
     * three cases separately configurable at all. No effect on a machine with
     * no lid switch. */
    cfg->lid_close_action        = SYN_LID_SUSPEND;
    cfg->lid_close_ac_action     = SYN_LID_SUSPEND;
    cfg->lid_close_docked_action = SYN_LID_IGNORE;

    /* -e is the form every terminal here takes: foot and xterm define it,
     * kitty accepts it undocumented, and syntty grew it in 0.1.0-19 precisely
     * so that naming it as the default terminal could not break this line. */
    snprintf(cfg->network_cmd, sizeof(cfg->network_cmd),
             "syntty -e nmtui");

    /* About OS. `--infinite` because the default is 2000 frames: an About box
     * that closes itself after a couple of minutes would look like a crash. It
     * exits on any keypress, so it is not a window that has to be hunted for a
     * close button. */
    snprintf(cfg->about_cmd, sizeof(cfg->about_cmd),
             "syntty -e fetch --infinite");

    /* News (news.c). No sources here: an empty list means "use the built-in
     * ones" (news.c owns that table), and the first `news_source =` line in
     * synuirc replaces the lot. */
    cfg->news_sources_n  = 0;
    cfg->news_refresh_min = 15;

    cfg->game_mode         = 1;
    cfg->game_suspend_ai   = 1;
    cfg->game_inhibit_idle = 1;
    snprintf(cfg->game_ai_stop_cmd,  sizeof(cfg->game_ai_stop_cmd),
             "sudo -n systemctl stop synapd.socket synapd.service");
    snprintf(cfg->game_ai_start_cmd, sizeof(cfg->game_ai_start_cmd),
             "sudo -n systemctl start synapd.socket synapd.service");
    /* On by default: both are invisible behind an opaque fullscreen game and
     * both give back real work. Off by default: the bar (RAM only, and the
     * restart shows) and the kmod (measurably near-nothing, and it costs
     * synguard its event stream while it applies). */
    cfg->game_drop_effects    = 1;
    cfg->game_pause_wallpaper = 1;
    cfg->game_stop_bar        = 0;
    cfg->game_quiet_kmod      = 0;
    snprintf(cfg->game_wp_stop_cmd,  sizeof(cfg->game_wp_stop_cmd),
             "synui-wpengine off all");
    snprintf(cfg->game_wp_start_cmd, sizeof(cfg->game_wp_start_cmd),
             "synui-wpengine restore");
    /* pkill -x, because the bar is quickshell(1) and killing the wrong
     * quickshell would take out any other shell the user runs. synui-bar is the
     * only supported way back up — it resolves which of the two QML trees
     * bar_shell selected. */
    snprintf(cfg->game_bar_stop_cmd,  sizeof(cfg->game_bar_stop_cmd),
             "pkill -x quickshell");
    snprintf(cfg->game_bar_start_cmd, sizeof(cfg->game_bar_start_cmd),
             "synui-bar");
    /* Needs root: /sys/kernel/synapse/config is root-owned 0644. Like
     * game_ai_stop_cmd this is fire-and-forget, so without the matching
     * sudoers rule it fails INVISIBLY — which is exactly how the synapd stop
     * went unnoticed for a month. game_quiet_kmod is off by default partly for
     * that reason. */
    snprintf(cfg->game_kmod_quiet_cmd, sizeof(cfg->game_kmod_quiet_cmd),
             "sudo -n /usr/lib/synui/synui-kmod-events off");
    snprintf(cfg->game_kmod_restore_cmd, sizeof(cfg->game_kmod_restore_cmd),
             "sudo -n /usr/lib/synui/synui-kmod-events on");
    /* The fullscreen X11 clients on this system that are NOT games. Without
     * these, going fullscreen on a YouTube video would stop synapd. The
     * firefox-app-mode apps (tepris, nexus-chat) report their own app_id via
     * MOZ_APP_REMOTINGNAME, so they need naming separately from firefox. */
    static const char *const defaults[] = {
        "firefox", "chibi", "tepris", "nexus-chat", "syntty", "kitty", "foot",
    };
    cfg->game_exclude_count = 0;
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++)
        snprintf(cfg->game_exclude[cfg->game_exclude_count++],
                 sizeof(cfg->game_exclude[0]), "%s", defaults[i]);

    /* Wayland-native clients that ARE games. gamescope is the only one that
     * ships here, and it is the one that matters: Steam launch options of the
     * form `gamescope -f -- %command%` are the recommended fix for half a
     * dozen other problems, so a growing share of the library arrives wrapped
     * in it and none of it was being detected. */
    static const char *const wrappers[] = { "gamescope" };
    cfg->game_include_count = 0;
    for (size_t i = 0; i < sizeof(wrappers) / sizeof(wrappers[0]); i++)
        snprintf(cfg->game_include[cfg->game_include_count++],
                 sizeof(cfg->game_include[0]), "%s", wrappers[i]);

    cfg->game_output = GAME_OUT_PRIMARY;

    cfg->bind_count = 0;
    seed_default_binds(cfg);
}

/*
 * The defaults as a value, built once.
 *
 * The panel needs something to compare the live config against, and the only
 * honest source for that is the same code that seeded it. Built on first ask
 * rather than at startup because most sessions never open the panel.
 */
void config_parse_kv(syn_config_t *cfg, const char *key, char *val);

/*
 * Push the resolved UI font into text.c.
 *
 * text.c holds the copy every draw path reads and has no server handle, so a
 * font parsed but never applied is a setting that silently does nothing. This
 * is the ONE place that applies it, and it is called only from
 * synui_config_load() — after synuirc, settings.state and the rest have all had
 * their say, so it pushes the final answer rather than each source's guess.
 *
 * Keeping it out of config_set_defaults() and config_parse_kv() is what makes
 * those two safe to run against a scratch config; see config_set_defaults().
 */
static void config_apply_ui_font(const syn_config_t *cfg)
{
    syn_text_set_ui_font(cfg->ui_font[0] ? cfg->ui_font : NULL);
}

const syn_config_t *synui_config_defaults(void)
{
    static syn_config_t def;
    static int built = 0;
    if (!built) { memset(&def, 0, sizeof(def)); config_set_defaults(&def); built = 1; }
    return &def;
}

/*
 * glass_level, resolved onto the individual alphas every consumer already
 * reads.
 *
 * Applied HERE, after every source has been read, and that ordering is the
 * whole design: the slider is an explicit answer to "how see-through is this
 * desktop", so it has to win over the theme's own opacities and over the
 * per-key ones in settings.state — otherwise moving it would do nothing on any
 * machine that had ever touched the opacity row, which is every machine that
 * has one.
 *
 * Unset is a no-op, so the twelve themes that are not Prism keep exactly the
 * opacities they were tuned with, and so does a synuirc written before this
 * key existed.
 *
 * ⚠ EVERY SURFACE, AND EVERY ONE OF THEM RELEASABLE — which is the pair of
 * changes this grew. It used to write three fields unconditionally: the two
 * window opacities and the bar, leaving the dock and the terminal behind and
 * giving a user who wanted to keep one of the three no way to say so. So a
 * desktop set from the slider had two surfaces at the wrong amount of glass, and
 * a desktop set by hand had three rows that would be overwritten at the next
 * login without ever having been touched. syn_glass_drives() is both halves of
 * the question — is the sync on, and is this row still the slider's — and every
 * field goes through it.
 *
 * Called from the runtime path too (ctl_apply's CTL_APPLY_GLASS), not only at
 * load, because the slider has to move the desktop while you are looking at it.
 */
/*
 * The other direction: the slider has let go, so give the rows it was driving
 * back.
 *
 * ⚠ WITHOUT THIS, "AUTO" WAS A ONE-WAY DOOR. Moving the Glass row writes five
 * alphas; moving it back to Auto only stops writing them, and the last values it
 * wrote stay in the config with nothing recording that anybody chose them. A
 * desktop that passed through 40% on its way back to Auto kept 40%'s dock and
 * bar for the rest of the session and then jumped somewhere else at the next
 * login, because settings.state — which is what the login rebuilds from — never
 * had those numbers in it. The screen and the file disagreed, and the file won
 * later, which is the worst order for that to happen in.
 *
 * The compiled defaults are exactly the right source, and only because a driven
 * row is by definition one the user has NOT set: the moment they set it, it is
 * pinned, and a pinned row is skipped here and keeps the value settings.state
 * holds for it. So this restores "no opinion" to precisely the fields that have
 * none.
 *
 * ⚠ NOT called from the load path, and that is not an oversight. At load a
 * synuirc `dock_opacity = 0.5` on a desktop with no level set is an opinion —
 * one written by hand, which is a place pins do not reach — and resetting it to
 * the compiled default would be this function quietly deleting a config line.
 * The release is an ACTION, taken when a control is moved; the two callers are
 * the two master rows in ctlpanel.c and there should never be a third.
 */
void synui_config_glass_release(syn_config_t *cfg)
{
    const syn_config_t *def = synui_config_defaults();

    if (!(cfg->glass_pins & SYN_GLASS_PIN_ACTIVE))
        cfg->active_opacity = def->active_opacity;
    if (!(cfg->glass_pins & SYN_GLASS_PIN_INACTIVE))
        cfg->inactive_opacity = def->inactive_opacity;
    if (!(cfg->glass_pins & SYN_GLASS_PIN_FOOT))
        cfg->foot_alpha = def->foot_alpha;
    if (!(cfg->glass_pins & SYN_GLASS_PIN_BAR))
        cfg->bar_opacity = def->bar_opacity;
    if (!(cfg->glass_pins & SYN_GLASS_PIN_DOCK))
        cfg->dock_opacity = def->dock_opacity;
}

void synui_config_apply_glass_sync(syn_config_t *cfg)
{
    if (!syn_glass_set(cfg)) return;

    if (syn_glass_drives(cfg, SYN_GLASS_PIN_ACTIVE))
        cfg->active_opacity = syn_glass_window_alpha(cfg);

    /* The unfocused pair follows the focused one rather than the level: what it
     * expresses is "a step further back than whatever you are using", and a
     * second curve would let the two cross. Its own floor goes with the
     * legibility switch for the same reason the window curve's does. */
    if (syn_glass_drives(cfg, SYN_GLASS_PIN_INACTIVE)) {
        cfg->inactive_opacity = cfg->active_opacity - 0.06f;
        float floor = cfg->glass_legibility ? 0.50f : 0.00f;
        if (cfg->inactive_opacity < floor) cfg->inactive_opacity = floor;
    }

    if (syn_glass_drives(cfg, SYN_GLASS_PIN_FOOT))
        cfg->foot_alpha = syn_glass_foot_alpha(cfg);

    if (syn_glass_drives(cfg, SYN_GLASS_PIN_BAR))
        cfg->bar_opacity = syn_glass_bar_alpha(cfg);

    if (syn_glass_drives(cfg, SYN_GLASS_PIN_DOCK))
        cfg->dock_opacity = syn_glass_dock_alpha(cfg);
}

void synui_config_load(syn_config_t *cfg)
{
    config_set_defaults(cfg);

    /* SYNUI_CONFIG overrides everything (used by the test harness for a
     * hermetic run), then user config, then system-wide. */
    const char *paths[3] = { getenv("SYNUI_CONFIG"), NULL, "/etc/synui/synuirc" };
    char user_path[256] = {0};
    syn_config_path(user_path, sizeof(user_path), "synuirc");
    paths[1] = user_path;

    FILE *f = NULL;
    for (int i = 0; i < 3; i++) {
        if (!paths[i] || !paths[i][0]) continue;
        f = fopen(paths[i], "r");
        if (f) break;
    }
    if (!f) {
        /* No config file: still honour persisted picker/dock choices. */
        wallpaper_state_load(cfg);
        cursor_state_load(cfg);
        dock_state_load(cfg);
        power_state_load(cfg);
        saver_state_load(cfg);
        welcome_state_load(cfg);
        launcher_state_load(cfg);
        record_state_load(cfg);
        deskicons_state_load(cfg);
        settings_state_load(cfg);
        filters_state_load_config(cfg);
        uifx_state_load_config(cfg);
        theme_state_load_config(cfg);
        notif_dnd_state_load_config(cfg);
        binds_state_load(cfg);
        config_apply_ui_font(cfg);
        synui_config_apply_glass_sync(cfg);
        return;
    }

    /* Config file found — reset autostart so file entries replace defaults */
    cfg->autostart_count = 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = strip(line);
        if (!*s || *s == '#') continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = strip(s);
        char *val = strip(eq + 1);

        /* Inline comments: a whitespace-preceded '#' ends the value —
         * unless it's the value's first character, so color values like
         * `border_color_focus = #ff296d` survive. */
        if (*val) {
            for (char *p = val + 1; (p = strchr(p, '#')); p++) {
                if (p[-1] == ' ' || p[-1] == '\t') {
                    *p = '\0';
                    break;
                }
            }
            val = strip(val);
        }

        config_parse_kv(cfg, key, val);
    }

    fclose(f);

    /* A live picker choice (wallpaper.state) is the most recent explicit
     * intent, so it overrides the synuirc `wallpaper` line. Delete the state
     * file to hand control back to synuirc. Same for the dock's edge/pins. */
    wallpaper_state_load(cfg);
    cursor_state_load(cfg);
    dock_state_load(cfg);
    power_state_load(cfg);
    saver_state_load(cfg);
    welcome_state_load(cfg);
    launcher_state_load(cfg);
    record_state_load(cfg);
    deskicons_state_load(cfg);

    /* Last, because it is the most recent explicit intent of the lot: every one
     * of these is something the user changed by hand in a panel, and
     * settings.state is the one that can carry ANY key. Same precedent as the
     * others — delete the file to hand control back to synuirc. */
    settings_state_load(cfg);

    /* After settings.state, because that is the order they were read in when
     * both were loaded at startup — the Super+E panel writes an ABSOLUTE record
     * of what it can see on screen, so it is the more recent explicit intent of
     * the two and has to win. Being read HERE rather than from synui_main() is
     * what stops a config RELOAD resurrecting CRT effects: synuirc ships
     * `effects = on`, so a reload with filters.state unread turned the shader
     * back on — reported as Ctrl+Shift+R in the shortcut palette (which resets
     * binds THROUGH a reload) switching on window effects with an amber tint.
     * See filters.c. */
    filters_state_load_config(cfg);
    uifx_state_load_config(cfg);

    /* Last of the state files, which is where it effectively sat before it was
     * one: theme_state_load() used to run after the entire config load, from
     * synui_main() — so theme.state's active_opacity and foot_alpha won over
     * settings.state's, and they still do. Being read HERE rather than once at
     * startup is what makes a config RELOAD keep the theme instead of resetting
     * the desktop to stock SYNAPSE; see theme.c. */
    theme_state_load_config(cfg);

    /* Here, not from synui_main(), for the reason filters.state and theme.state
     * are: synui_config_reload() does `s->config = fresh`, so a flag read only
     * at startup is switched back to whatever synuirc says on every reload.
     * Ctrl+Shift+R would have turned the ringer back on in the middle of
     * whatever Do Not Disturb was switched on for. See notif.c. */
    notif_dnd_state_load_config(cfg);

    /* After settings.state and everything above it, because it MEASURES ITSELF
     * against them: binds.state holds only the difference between the bind
     * table as every other source left it and the table after the rebind
     * helper's edits. Snapshotting the baseline any earlier would record a
     * synuirc `bind =` line as a user rebind and write it back out forever. */
    binds_state_load(cfg);

    /* Every source has now been read, so this is the final answer for the font
     * rather than one source's guess — which is why it is applied here and
     * nowhere else. */
    config_apply_ui_font(cfg);
    /* And the same argument for the glass slider, one line later: it is an
     * explicit answer that has to win over the theme's own opacities. */
    synui_config_apply_glass_sync(cfg);
}

/*
 * One `key = value`, applied to cfg. The whole of synuirc's vocabulary.
 *
 * Extracted from the read loop so that settings.state — which the control panel
 * writes, and which is read back through this same function — cannot understand
 * a different language than synuirc does. A panel that wrote a key its own
 * parser accepted and this one did not would produce a setting that survived
 * the save and vanished at the next login: the worst way for a settings panel
 * to be wrong, because it looks like it worked.
 *
 * `val` is mutable: the bind case splits it in place.
 */
void config_parse_kv(syn_config_t *cfg, const char *key, char *val)
{
    if (strcmp(key, "terminal") == 0) {
        /* ⚠ A TRUNCATED VALUE IS WORSE THAN A REFUSED ONE. The field is 64
         * bytes; strncpy cuts a longer value silently and leaves a path that
         * looks plausible in the file, cannot possibly run, and fails with no
         * message anywhere — the key does nothing and nothing says why.
         * Refuse it, keep whatever was already there, and name the limit. */
        if (strlen(val) >= sizeof(cfg->terminal)) {
            wlr_log(WLR_ERROR, "synui: terminal = %.40s… is %zu bytes; the "
                               "limit is %zu. Ignored, keeping '%s' — use a "
                               "wrapper on PATH for a long command.",
                    val, strlen(val), sizeof(cfg->terminal) - 1, cfg->terminal);
        } else {
            strncpy(cfg->terminal, val, sizeof(cfg->terminal) - 1);
        }
    }
    else if (strcmp(key, "autostart") == 0 && cfg->autostart_count < SYN_AUTOSTART_MAX)
        strncpy(cfg->autostart[cfg->autostart_count++], val, 127);
    else if (strcmp(key, "border_width") == 0) {
        cfg->border_width = atoi(val);
        if (cfg->border_width < 0)  cfg->border_width = 0;
        if (cfg->border_width > 32) cfg->border_width = 32;
    }
    else if (strcmp(key, "gap") == 0) {
        cfg->gap = atoi(val);
        if (cfg->gap < 0)   cfg->gap = 0;
        if (cfg->gap > 128) cfg->gap = 128;
    }
    /* The floating desktop's aesthetic tiler. Deliberately its own two knobs
     * rather than a multiple of `gap`: tiling wants a hairline between windows
     * and this wants a margin you can see, so tuning one must not drag the
     * other with it. */
    else if (strcmp(key, "float_inset") == 0) {
        /* Percent of the usable box kept clear at each edge. Capped well short
         * of 50 — past FLOAT_INSET_MAX the margin is bigger than the windows,
         * which is not a look, it is a mistake. */
        int pc = atoi(val);
        if (pc < 0) pc = 0;
        if (pc > FLOAT_INSET_MAX) pc = FLOAT_INSET_MAX;
        cfg->float_inset = pc;
    }
    else if (strcmp(key, "float_gap") == 0) {
        cfg->float_gap = atoi(val);
        if (cfg->float_gap < 0)   cfg->float_gap = 0;
        if (cfg->float_gap > 256) cfg->float_gap = 256;
    }
    else if (strcmp(key, "master_factor") == 0)
        cfg->master_factor = strtof(val, NULL);
    /*
     * animation_ms is the LEGACY key: it predates there being two events to
     * time, so it sets both. An existing synuirc keeps meaning what it said,
     * and a config that names the specific keys after it wins by being later —
     * the parser is a single pass over the file, so order in the file is the
     * only precedence rule there has ever been here.
     */
    else if (strcmp(key, "animation_ms") == 0) {
        int ms = anim_ms_of(val);
        cfg->animation_ms      = ms;
        cfg->anim_window_ms    = ms;
        cfg->anim_workspace_ms = ms;
    }
    else if (strcmp(key, "anim_window_ms") == 0)
        cfg->anim_window_ms = anim_ms_of(val);
    else if (strcmp(key, "anim_workspace_ms") == 0)
        cfg->anim_workspace_ms = anim_ms_of(val);
    else if (strcmp(key, "anim_window") == 0) {
        for (int i = 0; i < ANIM_WINDOW_COUNT; i++)
            if (strcmp(val, syn_anim_window_names[i]) == 0) {
                cfg->anim_window = i;
                break;
            }
    }
    else if (strcmp(key, "anim_workspace") == 0) {
        for (int i = 0; i < ANIM_WS_COUNT; i++)
            if (strcmp(val, syn_anim_ws_names[i]) == 0) {
                cfg->anim_workspace = i;
                break;
            }
    }
    else if (strcmp(key, "anim_curve") == 0) {
        for (int i = 0; i < ANIM_CURVE_COUNT; i++)
            if (strcmp(val, syn_anim_curve_names[i]) == 0) {
                cfg->anim_curve = i;
                break;
            }
    }
    else if (strcmp(key, "anim_rise_px") == 0) {
        int px = atoi(val);
        if (px < 0)   px = 0;
        if (px > 200) px = 200;
        cfg->anim_rise_px = px;
    }
    else if (strcmp(key, "titlebar_height") == 0) {
        /* 0 disables the titlebar entirely; clamp the rest to something a
         * button glyph can actually be drawn in. */
        int th = atoi(val);
        if (th < 0)  th = 0;
        if (th > 64) th = 64;
        if (th > 0 && th < 14) th = 14;
        cfg->titlebar_height = th;
    }
    else if (strcmp(key, "remember_geometry") == 0)
        cfg->remember_geometry = strcmp(val, "on") == 0 ||
                                 strcmp(val, "1") == 0;
    else if (strcmp(key, "desktop_icons") == 0)
        cfg->desktop_icons = strcmp(val, "on") == 0 ||
                             strcmp(val, "1") == 0;
    else if (strcmp(key, "desktop_icon_arrange") == 0) {
        /* An unreadable value keeps the default rather than picking a mode
         * the user did not ask for; deskicons.state may override this. */
        syn_arrange_t mode;
        if (syn_arrange_parse(val, &mode))
            cfg->desktop_icon_arrange = mode;
        else
            wlr_log(WLR_ERROR, "synui: config: desktop_icon_arrange '%s' is "
                    "not name|type|size|date", val);
    }
    else if (strcmp(key, "titlebar_color") == 0)
        parse_hex_color(val, cfg->titlebar_color);
    else if (strcmp(key, "titlebar_color_focus") == 0)
        parse_hex_color(val, cfg->titlebar_color_focus);
    else if (strcmp(key, "titlebar_text") == 0)
        parse_hex_color(val, cfg->titlebar_text);
    else if (strcmp(key, "titlebar_text_focus") == 0)
        parse_hex_color(val, cfg->titlebar_text_focus);
    else if (strcmp(key, "ai_layout") == 0)
        cfg->ai_layout = strcmp(val, "on") == 0;
    else if (strcmp(key, "ai_ctx_decor") == 0)
        cfg->ai_ctx_decor = strcmp(val, "on") == 0;
    else if (strcmp(key, "start_overlay") == 0)
        cfg->start_overlay = strcmp(val, "on") == 0;
    else if (strcmp(key, "snap") == 0)
        cfg->snap = strcmp(val, "on") == 0;
    else if (strcmp(key, "snap_zone") == 0) {
        cfg->snap_zone = atoi(val);
        if (cfg->snap_zone < 2)   cfg->snap_zone = 2;
        if (cfg->snap_zone > 200) cfg->snap_zone = 200;
    }
    /* Spelled, not numbered: a focus policy written as `focus_mode = 1` in a
     * config file is unreadable and silently means something else the day the
     * enum grows a member. An unknown word leaves the default alone rather
     * than falling through to click — a typo should not change behaviour. */
    else if (strcmp(key, "focus_mode") == 0) {
        for (int i = 0; i < SYN_FOCUS_MODE_COUNT; i++)
            if (strcmp(val, syn_focus_mode_names[i]) == 0) {
                cfg->focus_mode = i;
                break;
            }
    }
    else if (strcmp(key, "focus_delay_ms") == 0) {
        cfg->focus_delay_ms = atoi(val);
        if (cfg->focus_delay_ms < 0)    cfg->focus_delay_ms = 0;
        if (cfg->focus_delay_ms > 3000) cfg->focus_delay_ms = 3000;
    }
    else if (strcmp(key, "panel_follow_pointer") == 0)
        cfg->panel_follow_pointer = strcmp(val, "on") == 0;
    else if (strcmp(key, "alt_tab_preview") == 0)
        cfg->alt_tab_preview = strcmp(val, "on") == 0;
    else if (strcmp(key, "alt_tab_all_desktops") == 0)
        cfg->alt_tab_all_desktops = strcmp(val, "on") == 0;
    else if (strcmp(key, "alt_tab_minimized") == 0)
        cfg->alt_tab_minimized = strcmp(val, "on") == 0;
    /* Spelled as the two things it picks between rather than on|off, because
     * "alt_tab_style = off" would not say which switcher you got. The control
     * panel's row is a toggle over the same field. */
    else if (strcmp(key, "alt_tab_style") == 0)
        cfg->alt_tab_overview = (strcmp(val, "overview") == 0 ||
                                 strcmp(val, "mission") == 0 ||
                                 strcmp(val, "on") == 0);
    else if (strcmp(key, "theme") == 0) {
        /* Seeds the chrome colours from the preset; an explicit
         * border_color_* / titlebar_* line placed AFTER this still wins,
         * because it parses later and overwrites the field. */
        for (int t = 0; t < SYN_THEME_COUNT; t++)
            if (strcmp(val, syn_theme_names[t]) == 0) {
                theme_load_colors(cfg, (syn_theme_t)t);
                break;
            }
    }
    else if (strcmp(key, "transparency") == 0)
        cfg->transparency = strcmp(val, "on") == 0;
    else if (strcmp(key, "glass_level") == 0) {
        /* `off` is a spelling of "no opinion", so a config can hand the
         * decision back to the theme without deleting the line. */
        if (strcmp(val, "off") == 0 || strcmp(val, "auto") == 0) {
            cfg->glass_level = SYN_GLASS_UNSET;
        } else {
            cfg->glass_level = atoi(val);
            if (cfg->glass_level < 0)   cfg->glass_level = 0;
            if (cfg->glass_level > 100) cfg->glass_level = 100;
        }
    }
    else if (strcmp(key, "glass_sync") == 0)
        cfg->glass_sync = strcmp(val, "on") == 0;
    else if (strcmp(key, "glass_legibility") == 0)
        cfg->glass_legibility = strcmp(val, "on") == 0;
    else if (strcmp(key, "glass_pinned") == 0) {
        /*
         * Which rows have been taken off the slider, by name. Space-separated,
         * and the whole line REPLACES what came before rather than adding to it:
         * settings.state is the later source and has to be able to say "nothing
         * is pinned" as well as "these are", which an accumulating parse could
         * not express without inventing a token for empty.
         *
         * An unknown name is ignored rather than refused. A settings.state
         * written by a later synui that pins a row this one does not have is a
         * file to read what you can out of, not one to reject — and the row it
         * names cannot be wrongly driven here, because it does not exist.
         */
        cfg->glass_pins = 0;
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", val);
        char *save = NULL;
        for (char *t = strtok_r(buf, " ,\t", &save); t;
             t = strtok_r(NULL, " ,\t", &save))
            cfg->glass_pins |= syn_glass_pin_by_name(t);
    }
    else if (strcmp(key, "active_opacity") == 0)
        cfg->active_opacity = (float)atof(val);
    else if (strcmp(key, "inactive_opacity") == 0)
        cfg->inactive_opacity = (float)atof(val);
    else if (strcmp(key, "foot_alpha") == 0) {
        cfg->foot_alpha = (float)atof(val);
        if (cfg->foot_alpha < 0.0f) cfg->foot_alpha = 0.0f;
        if (cfg->foot_alpha > 1.0f) cfg->foot_alpha = 1.0f;
    }
    else if (strcmp(key, "corner_radius") == 0) {
        cfg->corner_radius = atoi(val);
        if (cfg->corner_radius < 0)  cfg->corner_radius = 0;
        if (cfg->corner_radius > 48) cfg->corner_radius = 48;
    }
    else if (strcmp(key, "blur") == 0)
        cfg->blur = strcmp(val, "on") == 0 || strcmp(val, "1") == 0;
    else if (strcmp(key, "glass_halo") == 0) {
        cfg->glass_halo = atoi(val);
        if (cfg->glass_halo < 0)  cfg->glass_halo = 0;
        if (cfg->glass_halo > 64) cfg->glass_halo = 64;
    }
    else if (strcmp(key, "clip_csd_margin") == 0)
        cfg->clip_csd_margin = strcmp(val, "on") == 0 ||
                               strcmp(val, "1")  == 0;
    else if (strcmp(key, "blur_passes") == 0) {
        cfg->blur_passes = atoi(val);
        if (cfg->blur_passes < 1) cfg->blur_passes = 1;
        if (cfg->blur_passes > 5) cfg->blur_passes = 5;
    }
    else if (strcmp(key, "blur_radius") == 0) {
        cfg->blur_radius = atoi(val);
        if (cfg->blur_radius < 1)  cfg->blur_radius = 1;
        if (cfg->blur_radius > 20) cfg->blur_radius = 20;
    }
    else if (strcmp(key, "blur_noise") == 0)
        cfg->blur_noise = (float)atof(val);
    else if (strcmp(key, "blur_brightness") == 0)
        cfg->blur_brightness = (float)atof(val);
    else if (strcmp(key, "blur_contrast") == 0)
        cfg->blur_contrast = (float)atof(val);
    else if (strcmp(key, "blur_saturation") == 0)
        cfg->blur_saturation = (float)atof(val);
    else if (strcmp(key, "shadow") == 0)
        cfg->shadow = strcmp(val, "on") == 0 || strcmp(val, "1") == 0;
    else if (strcmp(key, "shadow_blur_sigma") == 0) {
        cfg->shadow_blur_sigma = (float)atof(val);
        if (cfg->shadow_blur_sigma < 0.0f)  cfg->shadow_blur_sigma = 0.0f;
        if (cfg->shadow_blur_sigma > 80.0f) cfg->shadow_blur_sigma = 80.0f;
    }
    else if (strcmp(key, "shadow_spread") == 0) {
        cfg->shadow_spread = (float)atof(val);
        if (cfg->shadow_spread < 0.0f)  cfg->shadow_spread = 0.0f;
        if (cfg->shadow_spread > 64.0f) cfg->shadow_spread = 64.0f;
    }
    else if (strcmp(key, "shadow_offset_x") == 0)
        cfg->shadow_offset_x = atoi(val);
    else if (strcmp(key, "shadow_offset_y") == 0)
        cfg->shadow_offset_y = atoi(val);
    else if (strcmp(key, "shadow_color") == 0) {
        /* RGB only; parse_hex_color forces alpha to 1, so keep the alpha the
         * separate shadow_opacity key (or the default) already set. */
        float rgb[4];
        if (parse_hex_color(val, rgb)) {
            cfg->shadow_color[0] = rgb[0];
            cfg->shadow_color[1] = rgb[1];
            cfg->shadow_color[2] = rgb[2];
        }
    }
    else if (strcmp(key, "shadow_opacity") == 0)
        cfg->shadow_color[3] = clamp01(strtof(val, NULL));
    else if (strcmp(key, "border_color_norm") == 0)
        parse_hex_color(val, cfg->border_color_norm);
    else if (strcmp(key, "border_color_focus") == 0)
        parse_hex_color(val, cfg->border_color_focus);
    else if (strcmp(key, "border_color_ai") == 0)
        parse_hex_color(val, cfg->border_color_ai);
    else if (strcmp(key, "border_color_warn") == 0)
        parse_hex_color(val, cfg->border_color_warn);
    else if (strcmp(key, "effects") == 0)
        cfg->effects = strcmp(val, "on") == 0;
    else if (strcmp(key, "effect_scanline") == 0)
        cfg->effect_scanline = clamp01(strtof(val, NULL));
    else if (strcmp(key, "effect_curvature") == 0)
        cfg->effect_curvature = clamp01(strtof(val, NULL));
    else if (strcmp(key, "effect_aberration") == 0)
        cfg->effect_aberration = clamp01(strtof(val, NULL));
    else if (strcmp(key, "effect_glitch") == 0)
        cfg->effect_glitch = clamp01(strtof(val, NULL));
    else if (strcmp(key, "effect_phosphor") == 0) {
        if      (strcmp(val, "off")   == 0) cfg->effect_phosphor = SYN_PHOSPHOR_OFF;
        else if (strcmp(val, "green") == 0) cfg->effect_phosphor = SYN_PHOSPHOR_GREEN;
        else if (strcmp(val, "amber") == 0) cfg->effect_phosphor = SYN_PHOSPHOR_AMBER;
        else if (strcmp(val, "white") == 0) cfg->effect_phosphor = SYN_PHOSPHOR_WHITE;
        else wlr_log(WLR_ERROR, "synui: effect_phosphor: unknown '%s'", val);
    }
    else if (strcmp(key, "effect_mono") == 0)
        cfg->effect_mono = clamp01(strtof(val, NULL));
    else if (strcmp(key, "effect_bloom") == 0)
        cfg->effect_bloom = clamp01(strtof(val, NULL));
    else if (strcmp(key, "effect_lift") == 0)
        cfg->effect_lift = clamp01(strtof(val, NULL));
    else if (strcmp(key, "effect_hue") == 0)
        cfg->effect_hue = clamp01(strtof(val, NULL));
    else if (strcmp(key, "xkb_rules") == 0)
        strncpy(cfg->xkb_rules, val, sizeof(cfg->xkb_rules) - 1);
    else if (strcmp(key, "xkb_model") == 0)
        strncpy(cfg->xkb_model, val, sizeof(cfg->xkb_model) - 1);
    else if (strcmp(key, "night_light") == 0)
        cfg->night_light = strcmp(val, "on") == 0;
    else if (strcmp(key, "night_light_temp") == 0) {
        int k = atoi(val);
        /* Below ~1000K the ramp is essentially red-only and the screen is
         * unusable; above 6500K it is colder than daylight, which is not
         * what a night light is for. Clamp rather than obey. */
        if (k < 1000) k = 1000;
        if (k > 6500) k = 6500;
        cfg->night_light_temp = k;
    }
    else if (strcmp(key, "display_mode") == 0) {
        /* By NAME, not index: the enum will grow and a saved 2 must not
         * silently become a different arrangement after it does — the same
         * rule the lid actions follow. An unknown word leaves the default,
         * which is the arrangement that always works. */
        int m = display_mode_from_name(val);
        if (m >= 0) cfg->display_mode = m;
        else wlr_log(WLR_ERROR, "synui: display_mode: unknown '%s'", val);
    }
    else if (strcmp(key, "hdmi_audio") == 0) {
        /* Tri-state, and `auto` has to be spellable: it is the default, and a
         * key that could only say on/off would give someone who wanted the
         * default back no way to write it down. The control panel persists the
         * same three words (ctl_format's CTL_VAL_TRI). */
        /* `default` as well as `auto`, and this is not politeness: the control
         * panel persists a CTL_VAL_TRI row by writing ctl_format()'s config
         * spelling, which is the word "default". A parser that only knew "auto"
         * would fail to read back the value the panel had just written — the
         * setting would work all session and be gone at the next login, which
         * is the exact failure mode settings.state sharing config_parse_kv()
         * exists to make impossible. `auto` is documented because it is the
         * clearer word to write by hand. */
        if      (strcmp(val, "auto") == 0 ||
                 strcmp(val, "default") == 0) cfg->hdmi_audio = -1;
        else if (strcmp(val, "on")   == 0)    cfg->hdmi_audio =  1;
        else if (strcmp(val, "off")  == 0)    cfg->hdmi_audio =  0;
        else wlr_log(WLR_ERROR, "synui: hdmi_audio: unknown '%s' "
                                "(auto|on|off)", val);
    }
    else if (strcmp(key, "cursor_theme") == 0)
        strncpy(cfg->cursor_theme, val, sizeof(cfg->cursor_theme) - 1);
    else if (strcmp(key, "ui_font") == 0)
        strncpy(cfg->ui_font, val, sizeof(cfg->ui_font) - 1);
    else if (strcmp(key, "cursor_size") == 0) {
        int px = atoi(val);
        /* Clamped, not obeyed: 0 makes wlroots fall back in ways that are
         * hard to explain afterwards, and a mistyped 2400 leaves a pointer
         * covering a third of the screen with no visible way to undo it. */
        if (px < 8)   px = 8;
        if (px > 256) px = 256;
        cfg->cursor_size = px;
    }
    else if (strcmp(key, "xkb_layout") == 0)
        strncpy(cfg->xkb_layout, val, sizeof(cfg->xkb_layout) - 1);
    else if (strcmp(key, "xkb_variant") == 0)
        strncpy(cfg->xkb_variant, val, sizeof(cfg->xkb_variant) - 1);
    else if (strcmp(key, "xkb_options") == 0)
        strncpy(cfg->xkb_options, val, sizeof(cfg->xkb_options) - 1);
    else if (strcmp(key, "repeat_rate") == 0)
        cfg->repeat_rate = atoi(val);
    else if (strcmp(key, "repeat_delay") == 0)
        cfg->repeat_delay = atoi(val);
    else if (strcmp(key, "tap") == 0)
        cfg->tap_to_click = strcmp(val, "on") == 0;
    else if (strcmp(key, "natural_scroll") == 0)
        cfg->natural_scroll = strcmp(val, "on") == 0;
    else if (strcmp(key, "left_handed") == 0)
        cfg->left_handed = strcmp(val, "on") == 0;
    else if (strcmp(key, "accel_speed") == 0) {
        cfg->accel_speed = strtof(val, NULL);
        if (cfg->accel_speed < -1.0f) cfg->accel_speed = -1.0f;
        if (cfg->accel_speed >  1.0f) cfg->accel_speed =  1.0f;
        cfg->accel_speed_set = 1;
    }
    /* ── Screensaver + lock appearance (saver.c / lock.c) ── */
    else if (strcmp(key, "screensaver") == 0) {
        /* A mode name, or `off`. `off` is spelled as its own word rather than
         * as `blank` because the two differ: blank is a mode that draws black,
         * off means no saver stage at all. */
        if (strcmp(val, "off") == 0) {
            cfg->saver_timeout = 0;
        } else {
            int m = saver_mode_from_name(val);
            if (m >= 0) cfg->saver_mode = m;
            else wlr_log(WLR_ERROR, "synui: config: unknown screensaver '%s'", val);
        }
    }
    else if (strcmp(key, "screensaver_timeout") == 0) {
        int v = atoi(val);
        cfg->saver_timeout = v < 0 ? 0 : v;
    }
    else if (strcmp(key, "screensaver_lock") == 0)
        cfg->saver_lock = strcmp(val, "on") == 0;
    else if (strcmp(key, "screensaver_dir") == 0)
        snprintf(cfg->saver_dir, sizeof(cfg->saver_dir), "%s", val);
    else if (strcmp(key, "screensaver_interval") == 0) {
        int v = atoi(val);
        if (v >= 5 && v <= 600) cfg->saver_interval = v;
        else wlr_log(WLR_ERROR, "synui: config: screensaver_interval %d out of "
                     "range (5-600)", v);
    }
    else if (strcmp(key, "lock_background") == 0) {
        int b = lock_bg_from_name(val);
        if (b >= 0) {
            cfg->lock_bg = b;
        } else {
            /* Anything that is not one of the three names is taken as a path,
             * which is the spelling people reach for first. */
            cfg->lock_bg = SYN_LOCK_BG_IMAGE;
            snprintf(cfg->lock_bg_image, sizeof(cfg->lock_bg_image), "%s", val);
        }
    }
    else if (strcmp(key, "lock_dim") == 0) {
        int v = atoi(val);
        cfg->lock_bg_dim = v < 0 ? 0 : (v > 100 ? 100 : v);
    }
    else if (strcmp(key, "lock_blur") == 0) {
        int v = atoi(val);
        cfg->lock_bg_blur = v < 0 ? 0 : (v > 64 ? 64 : v);
    }
    else if (strcmp(key, "lock_accent") == 0) {
        /* A colour here means "do not follow the theme" — naming one and then
         * having it ignored because a separate flag was left on is exactly the
         * kind of stranded setting this tree keeps finding. */
        if (parse_hex_color(val, cfg->lock_accent))
            cfg->lock_theme_follow = 0;
        else
            wlr_log(WLR_ERROR, "synui: config: bad lock_accent '%s'", val);
    }
    else if (strcmp(key, "wallpaper") == 0) {
        /* Built-in keywords select a bundled wallpaper; anything else is
         * a file path for the static wallpaper.c backend.
         *   matrix  → animated GLES2 kanji rain (matrix.c)
         *   default → the bundled Synapse image (/usr/share/synui)
         *   none    → solid bg_color
         * (see also the live wppick.c picker / wallpaper.state). */
        if (strcmp(val, "matrix") == 0) {
            cfg->wallpaper_src = SYN_WP_SRC_MATRIX;
        } else if (strcmp(val, "default") == 0) {
            cfg->wallpaper_src = SYN_WP_SRC_IMAGE;
            strncpy(cfg->wallpaper, SYNUI_DATADIR "/wallpaper.png",
                    sizeof(cfg->wallpaper) - 1);
        } else if (strcmp(val, "none") == 0) {
            cfg->wallpaper_src = SYN_WP_SRC_IMAGE;
            cfg->wallpaper[0] = '\0';
        } else {
            cfg->wallpaper_src = SYN_WP_SRC_IMAGE;
            strncpy(cfg->wallpaper, val, sizeof(cfg->wallpaper) - 1);
        }
    }
    else if (strcmp(key, "wallpaper_output") == 0 ||
             strcmp(key, "wallpaper_output_mode") == 0) {
        /* value = "<connector> <token>" — a per-monitor override of the two
         * keys above, e.g. "wallpaper_output = DP-1 matrix". The token
         * vocabulary is identical to `wallpaper`, so there is one thing to
         * learn rather than two; _mode takes a wallpaper_mode name instead.
         * Lines are read in order, and an override inherits whatever the
         * global keys hold when it is first named. */
        char *sp = val;
        while (*sp && !isspace(*sp)) sp++;
        if (!*sp) {
            wlr_log(WLR_ERROR, "synui: %s '%s': expected "
                    "'<output> <value>'", key, val);
        } else {
            *sp++ = '\0';
            while (isspace(*sp)) sp++;
            if (!*sp) {
                wlr_log(WLR_ERROR, "synui: %s '%s': missing value", key, val);
            } else if (strcmp(key, "wallpaper_output") == 0) {
                wallpaper_output_apply(cfg, val, sp, -1);
            } else {
                int m = wallpaper_mode_from_name(sp);
                if (m < 0)
                    wlr_log(WLR_ERROR, "synui: wallpaper_output_mode: "
                            "unknown '%s'", sp);
                else
                    wallpaper_output_apply(cfg, val, NULL, m);
            }
        }
    }
    else if (strcmp(key, "wallpaper_mode") == 0) {
        /* Driven off syn_wallpaper_mode_names so a new mode only has to be
         * added to the enum and that table — this parser and the Super+W
         * picker both pick it up for free, instead of drifting apart. */
        int found = -1;
        for (int m = 0; m < SYN_WALLPAPER_MODE_COUNT; m++)
            if (strcmp(val, syn_wallpaper_mode_names[m]) == 0) { found = m; break; }
        if (found >= 0) cfg->wallpaper_mode = (syn_wallpaper_mode_t)found;
        else wlr_log(WLR_ERROR, "synui: wallpaper_mode: unknown '%s'", val);
    }
    else if (strcmp(key, "power_enabled") == 0)
        cfg->power_enabled = strcmp(val, "on") == 0;
    else if (strcmp(key, "power_dim_timeout") == 0)
        cfg->power_dim = atoi(val) < 0 ? 0 : atoi(val);
    else if (strcmp(key, "power_blank_timeout") == 0)
        cfg->power_blank = atoi(val) < 0 ? 0 : atoi(val);
    else if (strcmp(key, "power_lock_timeout") == 0)
        cfg->power_lock = atoi(val) < 0 ? 0 : atoi(val);
    else if (strcmp(key, "power_suspend_timeout") == 0)
        cfg->power_suspend = atoi(val) < 0 ? 0 : atoi(val);
    else if (strcmp(key, "power_lock_cmd") == 0)
        snprintf(cfg->power_lock_cmd, sizeof(cfg->power_lock_cmd), "%s", val);
    else if (strcmp(key, "power_suspend_cmd") == 0)
        snprintf(cfg->power_suspend_cmd, sizeof(cfg->power_suspend_cmd), "%s", val);
    else if (strcmp(key, "lock_fingerprint") == 0)
        cfg->lock_fingerprint = strcmp(val, "on") == 0;
    /* Driven off syn_lid_action_names so a new action only has to be added
     * to the enum and that table, as with wallpaper_mode above. */
    else if (strcmp(key, "lid_close_action") == 0) {
        int a = lid_action_from_name(val);
        if (a >= 0) cfg->lid_close_action = a;
        else wlr_log(WLR_ERROR, "synui: lid_close_action: unknown '%s'", val);
    }
    else if (strcmp(key, "lid_close_ac_action") == 0) {
        int a = lid_action_from_name(val);
        if (a >= 0) cfg->lid_close_ac_action = a;
        else wlr_log(WLR_ERROR, "synui: lid_close_ac_action: unknown '%s'", val);
    }
    else if (strcmp(key, "lid_close_docked_action") == 0) {
        int a = lid_action_from_name(val);
        if (a >= 0) cfg->lid_close_docked_action = a;
        else wlr_log(WLR_ERROR,
                     "synui: lid_close_docked_action: unknown '%s'", val);
    }
    else if (strcmp(key, "cascade_stack_max") == 0) {
        int m = atoi(val);
        if (m < CASCADE_STACK_MIN) m = CASCADE_STACK_MIN;
        if (m > CASCADE_STACK_MAX) m = CASCADE_STACK_MAX;
        cfg->cascade_stack_max = m;
    }
    else if (strcmp(key, "network_cmd") == 0)
        snprintf(cfg->network_cmd, sizeof(cfg->network_cmd), "%s", val);
    else if (strcmp(key, "about_cmd") == 0)
        snprintf(cfg->about_cmd, sizeof(cfg->about_cmd), "%s", val);
    else if (strcmp(key, "news_refresh") == 0) {
        int m = atoi(val);
        cfg->news_refresh_min = m < 1 ? 1 : m;   /* never hammer a feed */
    } else if (strcmp(key, "news_source") == 0) {
        /* news_source = TAG|https://example.org/feed.xml
         * Repeatable. The first one replaces the built-in list rather than
         * adding to it: a user listing their own feeds means "these", not
         * "these as well as your eight". */
        char *bar = strchr(val, '|');
        if (!bar) {
            wlr_log(WLR_ERROR, "synui: config: news_source needs TAG|URL: %s",
                    val);
        } else if (cfg->news_sources_n >= NEWS_SOURCES_MAX) {
            wlr_log(WLR_ERROR, "synui: config: too many news_source lines "
                               "(max %d)", NEWS_SOURCES_MAX);
        } else {
            *bar = '\0';
            syn_news_source_t *src = &cfg->news_sources[cfg->news_sources_n++];
            snprintf(src->name, sizeof(src->name), "%s", strip(val));
            snprintf(src->url,  sizeof(src->url),  "%s", strip(bar + 1));
        }
    }
    else if (strcmp(key, "cat") == 0)
        cfg->cat_start = strcmp(val, "on") == 0;
    else if (strcmp(key, "cat_breed") == 0) {
        /* Matched against cat_breed_names[] rather than a second list here:
         * one spelling of "russian-blue" in the tree, and adding a coat to
         * cat_draw.c is all it takes to make it settable. */
        int found = -1;
        for (int i = 0; i < CAT_BREED_COUNT; i++)
            if (strcmp(val, cat_breed_names[i]) == 0) { found = i; break; }
        if (found >= 0) cfg->cat_breed = found;
        else wlr_log(WLR_ERROR, "synui: cat_breed: unknown '%s'", val);
    }
    else if (strcmp(key, "welcome_at_startup") == 0)
        cfg->welcome_at_startup = strcmp(val, "on") == 0;
    else if (strcmp(key, "notif_dnd") == 0)
        cfg->notif_dnd = strcmp(val, "on") == 0;
    else if (strcmp(key, "numlock") == 0)
        cfg->numlock = strcmp(val, "on") == 0;
    else if (strcmp(key, "record_audio") == 0)
        cfg->record_audio = strcmp(val, "on") == 0;
    else if (strcmp(key, "record_edit") == 0)
        cfg->record_edit = strcmp(val, "on") == 0;
    else if (strcmp(key, "dock_enabled") == 0)
        cfg->dock_enabled = strcmp(val, "on") == 0;
    else if (strcmp(key, "dock_autohide") == 0)
        cfg->dock_autohide = strcmp(val, "on") == 0;
    /* Is there a bar, and how is it stopped and started. The compositor does
     * not start the bar (the session's autostart line does), so `bar_enabled`
     * is not read at boot to decide anything — it is the control panel row's
     * memory of which way the switch is set, so that the row comes up saying
     * what the desktop actually looks like. The two commands are what the row
     * runs; see the fields' comment in synui.h. */
    /* clickoff|button|window, one key per panel. The spellings are
     * syn_panel_close_t's display names folded to lower case, which is what the
     * control panel's enum rows write back. */
    else if (strcmp(key, "calc_close") == 0 ||
             strcmp(key, "ctlpanel_close") == 0 ||
             strcmp(key, "taskmgr_close") == 0) {
        int m;
        if      (strcmp(val, "clickoff") == 0) m = SYN_PANEL_CLOSE_CLICKOFF;
        else if (strcmp(val, "button")   == 0) m = SYN_PANEL_CLOSE_BUTTON;
        else if (strcmp(val, "window")   == 0) m = SYN_PANEL_CLOSE_WINDOW;
        else { wlr_log(WLR_ERROR, "synui: %s: unknown '%s'", key, val); m = -1; }
        if (m >= 0) {
            if      (key[0] == 'c' && key[1] == 'a') cfg->calc_close     = m;
            else if (key[0] == 'c')                  cfg->ctlpanel_close = m;
            else                                     cfg->taskmgr_close  = m;
        }
    }
    else if (strcmp(key, "bar_enabled") == 0)
        cfg->bar_enabled = strcmp(val, "on") == 0;
    else if (strcmp(key, "bar_stop_cmd") == 0)
        snprintf(cfg->bar_stop_cmd, sizeof(cfg->bar_stop_cmd), "%s", val);
    else if (strcmp(key, "bar_start_cmd") == 0)
        snprintf(cfg->bar_start_cmd, sizeof(cfg->bar_start_cmd), "%s", val);
    /* OBSOLETE, and kept only to SAY SO. `super_space = launcher|cmdbar` used to
     * swap the two actions across Super+Space and Super+=; it re-applied at the
     * end of every config load, which put back any rebind the shortcuts palette
     * had made — so it is gone and the palette owns both chords. An unknown key
     * is silently ignored here, and silence is exactly wrong for a key that used
     * to work: the line stays in synuirc, the desktop stops obeying it, and
     * nothing says why. An old settings.state never reaches here — settings.c's
     * obsolete list drops the key on the way in, so the panel's next save writes
     * the file without it, and only a hand-written synuirc line gets the log. */
    else if (strcmp(key, "super_space") == 0) {
        wlr_log(WLR_INFO, "synui: super_space is obsolete and ignored — rebind "
                "Super+Space / Super+= in the shortcuts palette (Super+/, F2) "
                "or with `bind =` / `unbind =` lines");
    }
    /* Which modifier, tapped alone, opens the start menu. `none` is a value and
     * not a parse failure — it is how the tap is turned off. Everything else is
     * refused BY NAME rather than silently ignored: a mistyped tap_key would
     * otherwise read as "the tap stopped working". */
    else if (strcmp(key, "tap_key") == 0) {
        if (strcmp(val, "none") == 0 || strcmp(val, "off") == 0) {
            cfg->tap_mod = 0;
        } else {
            uint32_t m = parse_mod(val);
            if (m) cfg->tap_mod = m;
            else   wlr_log(WLR_ERROR, "synui: tap_key: unknown '%s' "
                                      "(super|ctrl|alt|shift|none)", val);
        }
    }
    /* What that tap runs. Split on the first whitespace exactly as `bind =`
     * splits its own action from its argument, and for the same reason: the tap
     * takes any bind action, so `spawn rofi -show drun` has to survive as one
     * action and one argument rather than as a truncated word.
     *
     * Not validated against the action table here — input.c owns that list and
     * logs an unknown action when the tap fires, which is the same treatment a
     * mistyped `bind =` gets. Duplicating the table to check it early would be
     * a second list to keep in step. */
    else if (strcmp(key, "tap_action") == 0) {
        const char *sp = val;
        while (*sp && !isspace((unsigned char)*sp)) sp++;
        size_t alen = (size_t)(sp - val);
        if (alen == 0 || alen >= sizeof(cfg->tap_action)) {
            wlr_log(WLR_ERROR, "synui: tap_action: bad action in '%s'", val);
        } else {
            memcpy(cfg->tap_action, val, alen);
            cfg->tap_action[alen] = '\0';
            while (*sp && isspace((unsigned char)*sp)) sp++;
            snprintf(cfg->tap_arg, sizeof(cfg->tap_arg), "%s", sp);
        }
    }
    /* Which QML tree synui-bar starts. The compositor never acts on this — it
     * parses it so that the key has ONE spelling and the control panel's row
     * can persist through settings.state like every other setting. */
    else if (strcmp(key, "bar_shell") == 0) {
        int found = 0;
        for (int i = 0; i < SYN_BAR_SHELL_COUNT; i++)
            if (strcmp(val, syn_bar_shell_names[i]) == 0) {
                cfg->bar_shell = i;
                found = 1;
                break;
            }
        if (!found)
            wlr_log(WLR_ERROR, "synui: bar_shell: unknown '%s'", val);
    }
    /* Same deal as bar_shell: parsed here so the key has one spelling, acted on
     * by quickshell. */
    else if (strcmp(key, "bar_edge") == 0) {
        int found = 0;
        for (int i = 0; i < SYN_BAR_EDGE_COUNT; i++)
            if (strcmp(val, syn_bar_edge_names[i]) == 0) {
                cfg->bar_edge = i;
                found = 1;
                break;
            }
        if (!found)
            wlr_log(WLR_ERROR, "synui: bar_edge: unknown '%s'", val);
    }
    /* And again: the bar's shape when the corners are on. Parsed here for one
     * spelling, applied by BarConfig.qml. */
    else if (strcmp(key, "bar_shape") == 0) {
        int found = 0;
        for (int i = 0; i < SYN_BAR_SHAPE_COUNT; i++)
            if (strcmp(val, syn_bar_shape_names[i]) == 0) {
                cfg->bar_shape = i;
                found = 1;
                break;
            }
        if (!found)
            wlr_log(WLR_ERROR, "synui: bar_shape: unknown '%s'", val);
    }
    /* …and how much of the wallpaper the bar lets through. `auto` is the word
     * for "the theme decides", and it is a WORD rather than a number because
     * 0.00 already means something here — a bar with no background at all,
     * which is the whole reason this key can say zero. Anything unparseable
     * lands on atof's 0.0 and would be that, so the number is taken only after
     * the keyword has had its chance. */
    else if (strcmp(key, "bar_opacity") == 0) {
        if (strcmp(val, "auto") == 0) {
            cfg->bar_opacity = -1.0f;
        } else {
            cfg->bar_opacity = (float)atof(val);
            if (cfg->bar_opacity < 0.0f) cfg->bar_opacity = 0.0f;
            if (cfg->bar_opacity > 1.0f) cfg->bar_opacity = 1.0f;
        }
    }
    /* Empty means "follow the system icon theme", which is the default and what
     * a theme switch changes. Not validated against the installed themes: the
     * bar is a separate process that may start before the theme is unpacked,
     * and refusing a name here would be refusing it in the wrong place. */
    else if (strcmp(key, "bar_icon_theme") == 0)
        snprintf(cfg->bar_icon_theme, sizeof(cfg->bar_icon_theme), "%s", val);
    else if (strcmp(key, "dock_edge") == 0) {
        if      (strcmp(val, "bottom") == 0) cfg->dock_edge = SYN_DOCK_EDGE_BOTTOM;
        else if (strcmp(val, "top")    == 0) cfg->dock_edge = SYN_DOCK_EDGE_TOP;
        else if (strcmp(val, "left")   == 0) cfg->dock_edge = SYN_DOCK_EDGE_LEFT;
        else if (strcmp(val, "right")  == 0) cfg->dock_edge = SYN_DOCK_EDGE_RIGHT;
        else wlr_log(WLR_ERROR, "synui: dock_edge: unknown '%s'", val);
    }
    else if (strcmp(key, "launcher_style") == 0) {
        if      (strcmp(val, "text") == 0) cfg->launcher_style = SYN_LAUNCHER_TEXT;
        else if (strcmp(val, "logo") == 0) cfg->launcher_style = SYN_LAUNCHER_LOGO;
        else wlr_log(WLR_ERROR, "synui: launcher_style: unknown '%s'", val);
    }
    else if (strcmp(key, "dock_height") == 0) {
        cfg->dock_height = atoi(val);
        if (cfg->dock_height < 32)  cfg->dock_height = 32;
        if (cfg->dock_height > 200) cfg->dock_height = 200;
    }
    else if (strcmp(key, "dock_hover_margin") == 0) {
        cfg->dock_hover_margin = atoi(val);
        if (cfg->dock_hover_margin < 1)  cfg->dock_hover_margin = 1;
        if (cfg->dock_hover_margin > 32) cfg->dock_hover_margin = 32;
    }
    else if (strcmp(key, "dock_style") == 0) {
        if      (strcmp(val, "auto")  == 0) cfg->dock_style = SYN_DOCK_STYLE_AUTO;
        else if (strcmp(val, "solid") == 0) cfg->dock_style = SYN_DOCK_STYLE_SOLID;
        else if (strcmp(val, "glass") == 0) cfg->dock_style = SYN_DOCK_STYLE_GLASS;
        else wlr_log(WLR_ERROR, "synui: dock_style: unknown '%s'", val);
    }
    else if (strcmp(key, "dock_opacity") == 0) {
        cfg->dock_opacity = (float)atof(val);
        /*
         * ⚠ THE 0.20 FLOOR IS GONE, AND IT WAS THE WRONG ANSWER TO A REAL
         * QUESTION. It read "an invisible dock is indistinguishable from a
         * broken one, and `dock_enabled = off` is how you ask for no dock" —
         * true of the dock's BODY, and the body is not the dock. Its icons are
         * drawn on top of this and stay fully opaque at every value, so 0.00 is
         * a row of icons floating on the wallpaper, which is a thing people
         * actually want and the one thing this made unreachable.
         *
         * It was also not the only floor: dock_paint_body clamped to 0.05, the
         * row's own vmin was 0.20, BarConfig re-clamped to 0.20 on the shell
         * side, and the widgets added 0.16 on top. Five numbers guarding the
         * same setting, none of them agreeing on what it was guarding against.
         * Legibility is measured now, per surface, against the wallpaper that
         * is actually behind it — see glass_legibility and panel_alpha_floor —
         * so the range here is the range.
         */
        if (cfg->dock_opacity < 0.00f) cfg->dock_opacity = 0.00f;
        if (cfg->dock_opacity > 1.00f) cfg->dock_opacity = 1.00f;
    }
    else if (strcmp(key, "dock_radius") == 0) {
        cfg->dock_radius = atoi(val);
        if (cfg->dock_radius < 0)  cfg->dock_radius = 0;
        if (cfg->dock_radius > 64) cfg->dock_radius = 64;
    }
    else if (strcmp(key, "widget_glass") == 0) {
        if      (strcmp(val, "auto") == 0) cfg->widget_glass = SYN_WIDGET_GLASS_AUTO;
        else if (strcmp(val, "off")  == 0) cfg->widget_glass = SYN_WIDGET_GLASS_OFF;
        else if (strcmp(val, "on")   == 0) cfg->widget_glass = SYN_WIDGET_GLASS_ON;
        else wlr_log(WLR_ERROR, "synui: widget_glass: unknown '%s'", val);
    }
    else if (strcmp(key, "wallpaper_accent") == 0) {
        if      (strcmp(val, "auto") == 0) cfg->wallpaper_accent = SYN_WP_ACCENT_AUTO;
        else if (strcmp(val, "off")  == 0) cfg->wallpaper_accent = SYN_WP_ACCENT_OFF;
        else if (strcmp(val, "on")   == 0) cfg->wallpaper_accent = SYN_WP_ACCENT_ON;
        else wlr_log(WLR_ERROR, "synui: wallpaper_accent: unknown '%s'", val);
    }
    else if (strcmp(key, "scene_ink") == 0) {
        cfg->scene_ink = strcmp(val, "on") == 0;
    }
    else if (strcmp(key, "dock_pin") == 0) {
        /* space-separated app_ids/.desktop basenames */
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", val);
        char *save = NULL;
        cfg->dock_pin_count = 0;
        for (char *tok = strtok_r(buf, " \t", &save);
             tok && cfg->dock_pin_count < DOCK_PIN_MAX;
             tok = strtok_r(NULL, " \t", &save))
            snprintf(cfg->dock_pin[cfg->dock_pin_count++], 128, "%s", tok);
    }
    else if (strcmp(key, "game_mode") == 0)
        cfg->game_mode = strcmp(val, "on") == 0;
    else if (strcmp(key, "game_suspend_ai") == 0)
        cfg->game_suspend_ai = strcmp(val, "on") == 0;
    else if (strcmp(key, "game_inhibit_idle") == 0)
        cfg->game_inhibit_idle = strcmp(val, "on") == 0;
    else if (strcmp(key, "game_ai_stop_cmd") == 0)
        snprintf(cfg->game_ai_stop_cmd, sizeof(cfg->game_ai_stop_cmd), "%s", val);
    else if (strcmp(key, "game_ai_start_cmd") == 0)
        snprintf(cfg->game_ai_start_cmd, sizeof(cfg->game_ai_start_cmd), "%s", val);
    else if (strcmp(key, "game_drop_effects") == 0)
        cfg->game_drop_effects = strcmp(val, "on") == 0;
    else if (strcmp(key, "game_pause_wallpaper") == 0)
        cfg->game_pause_wallpaper = strcmp(val, "on") == 0;
    else if (strcmp(key, "game_stop_bar") == 0)
        cfg->game_stop_bar = strcmp(val, "on") == 0;
    else if (strcmp(key, "game_quiet_kmod") == 0)
        cfg->game_quiet_kmod = strcmp(val, "on") == 0;
    else if (strcmp(key, "game_wp_stop_cmd") == 0)
        snprintf(cfg->game_wp_stop_cmd, sizeof(cfg->game_wp_stop_cmd), "%s", val);
    else if (strcmp(key, "game_wp_start_cmd") == 0)
        snprintf(cfg->game_wp_start_cmd, sizeof(cfg->game_wp_start_cmd), "%s", val);
    else if (strcmp(key, "game_bar_stop_cmd") == 0)
        snprintf(cfg->game_bar_stop_cmd, sizeof(cfg->game_bar_stop_cmd), "%s", val);
    else if (strcmp(key, "game_bar_start_cmd") == 0)
        snprintf(cfg->game_bar_start_cmd, sizeof(cfg->game_bar_start_cmd), "%s", val);
    else if (strcmp(key, "game_kmod_quiet_cmd") == 0)
        snprintf(cfg->game_kmod_quiet_cmd, sizeof(cfg->game_kmod_quiet_cmd), "%s", val);
    else if (strcmp(key, "game_kmod_restore_cmd") == 0)
        snprintf(cfg->game_kmod_restore_cmd, sizeof(cfg->game_kmod_restore_cmd), "%s", val);
    else if (strcmp(key, "game_exclude") == 0) {
        /* space-separated app_ids that are NOT games. Replaces the built-in
         * list rather than adding to it, so a user can widen or narrow it. */
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", val);
        char *save = NULL;
        cfg->game_exclude_count = 0;
        for (char *tok = strtok_r(buf, " \t", &save);
             tok && cfg->game_exclude_count < GAME_EXCLUDE_MAX;
             tok = strtok_r(NULL, " \t", &save))
            snprintf(cfg->game_exclude[cfg->game_exclude_count++],
                     sizeof(cfg->game_exclude[0]), "%s", tok);
    }
    else if (strcmp(key, "game_include") == 0) {
        /* Space-separated app_ids of Wayland-native clients that ARE games,
         * same replace-not-append rule as game_exclude. An empty value is
         * therefore how you turn wrapper detection off entirely. */
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", val);
        char *save = NULL;
        cfg->game_include_count = 0;
        for (char *tok = strtok_r(buf, " \t", &save);
             tok && cfg->game_include_count < GAME_EXCLUDE_MAX;
             tok = strtok_r(NULL, " \t", &save))
            snprintf(cfg->game_include[cfg->game_include_count++],
                     sizeof(cfg->game_include[0]), "%s", tok);
    }
    else if (strcmp(key, "game_output") == 0) {
        /* Anything unrecognised keeps the default rather than silently
         * meaning "ask the client" — a typo here should not quietly restore
         * the behaviour this setting exists to stop. */
        if      (strcmp(val, "focused") == 0) cfg->game_output = GAME_OUT_FOCUSED;
        else if (strcmp(val, "ask")     == 0) cfg->game_output = GAME_OUT_ASK;
        else if (strcmp(val, "primary") == 0) cfg->game_output = GAME_OUT_PRIMARY;
        else wlr_log(WLR_ERROR, "synui: game_output '%s': expected "
                                "primary|focused|ask — keeping primary", val);
    }
    else if (strcmp(key, "bind") == 0) {
        /* value = "<combo> <action> [arg]" — split on first whitespace */
        char *sp = val;
        while (*sp && !isspace(*sp)) sp++;
        if (*sp) { *sp++ = '\0'; while (isspace(*sp)) sp++; }
        if (*sp)
            config_bind(cfg, val, sp);
        else
            wlr_log(WLR_ERROR, "synui: bind '%s': missing action", val);
    }
    /* The counterpart `bind` never had: TAKE A COMBO AWAY.
     *
     * Without it a shipped default could only ever be replaced, not removed —
     * seed_default_binds() runs before any file is read, so every default is
     * live by the time synuirc gets a say, and re-pointing super+w at something
     * else was the only way to stop it opening the wallpaper picker.
     *
     * The rebind helper is what needed this. Moving a shortcut from super+w to
     * super+y is two operations, not one: bind the new combo, and unbind the old
     * one — otherwise the shortcut answers to BOTH, and the old key looks like a
     * rebind that silently did not take. */
    else if (strcmp(key, "unbind") == 0) {
        if (!config_unbind(cfg, val))
            wlr_log(WLR_ERROR, "synui: unbind '%s': not bound", val);
    }
}
