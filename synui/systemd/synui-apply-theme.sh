#!/usr/bin/env bash
# synui-apply-theme — push the desktop theme's light/dark scheme out to the
# toolkits synui does not draw: GTK apps, Dolphin (Qt/KDE) and Firefox.
#
# synui recolours its OWN chrome (borders, titlebars) directly; it cannot reach
# into someone else's toolkit, so theme.c fires this after every theme change.
# Everything here is BEST EFFORT and idempotent: each step guards on the tool
# being present, so a box with no KDE simply gets the GTK half, and a headless
# CI box gets neither without failing. That is why there is no `set -e` — one
# missing tool must not abort the steps after it.
#
# Firefox *transparency* needs nothing from here: the compositor's per-window
# opacity applies to Firefox like any other window. This only carries dark/light,
# which Firefox-on-Wayland follows from the GTK theme + the gsettings color-scheme.
#
# Usage: synui-apply-theme <dark|light> <accent_r> <accent_g> <accent_b>
#                          [glyph_r] [glyph_g] [glyph_b]
#                          [base_r] [base_g] [base_b] [text_r] [text_g] [text_b]
#
# The glyph triple is the colour for the bar's module glyphs (cpu/mem/net/audio/
# gamemode). It defaults to the accent — which is what every theme but one wants
# — and is passed separately only because SYNAPSE's accent is its neon magenta
# selection colour while the launcher caret next to those glyphs is teal.
#
# The base/text pair is the app WINDOW colour and its foreground. Without it
# every dark theme handed Dolphin the same generic dark grey, which is exactly
# what a rice must not do — Gruvbox windows have to be Gruvbox brown, Catppuccin
# windows Catppuccin, XP's #ECE9D8 beige and 95's #C0C0C0 silver. It defaults to
# the old hardcoded pair for the scheme, so an older synui passing four or seven
# args behaves exactly as before.
set -u

scheme=${1:-dark}
ar=${2:-61} ag=${3:-125} ab=${4:-255}
gr=${5:-$ar} gg=${6:-$ag} gb=${7:-$ab}
br=${8:-} bg_=${9:-} bb=${10:-}
tr=${11:-} tg=${12:-} tb=${13:-}

case "$scheme" in
    dark|light) ;;
    *) echo "usage: synui-apply-theme <dark|light> <r> <g> <b>" >&2; exit 2 ;;
esac

# Pick a real theme name for GTK / KDE widget style, preferring Breeze (ships
# with the KDE integration) and falling back to Adwaita, which GTK always has.
gtk_theme=Adwaita
[ "$scheme" = dark ] && gtk_theme=Adwaita-dark
if [ -d /usr/share/themes/Breeze ] || [ -d "$HOME/.local/share/themes/Breeze" ]; then
    gtk_theme=Breeze
    [ "$scheme" = dark ] && gtk_theme=Breeze-Dark
fi

prefer_dark=0; color_scheme=default
if [ "$scheme" = dark ]; then prefer_dark=1; color_scheme=prefer-dark; fi

# ── GTK 3 / GTK 4 settings.ini ──────────────────────────────────────────────
# Set two keys under [Settings] without clobbering the rest of the file. awk
# rewrites the section in place (or appends it), so an existing gtk-theme-name a
# user set by hand is replaced, but unrelated keys survive.
gtk_ini() {
    local f=$1 tmp
    mkdir -p "$(dirname "$f")" 2>/dev/null || return 0
    [ -f "$f" ] || printf '[Settings]\n' > "$f"
    tmp=$(mktemp) || return 0
    awk -v dark="$prefer_dark" -v theme="$gtk_theme" '
        BEGIN { insec=0; put=0 }
        /^\[Settings\]/ { print; insec=1;
            print "gtk-application-prefer-dark-theme=" dark;
            print "gtk-theme-name=" theme; put=1; next }
        /^\[/ { insec=0 }
        insec && /^[[:space:]]*gtk-application-prefer-dark-theme[[:space:]]*=/ { next }
        insec && /^[[:space:]]*gtk-theme-name[[:space:]]*=/ { next }
        { print }
        END { if (!put) { print "[Settings]";
            print "gtk-application-prefer-dark-theme=" dark;
            print "gtk-theme-name=" theme } }
    ' "$f" > "$tmp" && mv "$tmp" "$f" || rm -f "$tmp"
}
gtk_ini "$HOME/.config/gtk-3.0/settings.ini"
gtk_ini "$HOME/.config/gtk-4.0/settings.ini"

# ── gsettings (the live signal Firefox + GTK apps watch) ────────────────────
# xdg-desktop-portal serves org.freedesktop.appearance from these, which is what
# flips Firefox content prefers-color-scheme on Wayland.
if command -v gsettings >/dev/null 2>&1; then
    gsettings set org.gnome.desktop.interface color-scheme "$color_scheme" 2>/dev/null
    gsettings set org.gnome.desktop.interface gtk-theme "$gtk_theme"       2>/dev/null
fi

# ── Dolphin / Qt / KDE via kdeglobals ───────────────────────────────────────
# kwriteconfig merges a single key without touching the rest of kdeglobals (the
# menu/style keys the KDE integration wrote stay put). Use whichever major
# version is installed.
kw=
for c in kwriteconfig6 kwriteconfig5; do
    command -v "$c" >/dev/null 2>&1 && { kw=$c; break; }
done
if [ -n "$kw" ]; then
    name=SynapseLight; wb="239,240,241"; wf="35,38,41"
    vb="252,252,252";  vf="35,38,41";    btn="239,240,241"; bf="35,38,41"
    if [ "$scheme" = dark ]; then
        name=SynapseDark; wb="30,30,36"; wf="235,235,242"
        vb="24,24,28";    vf="235,235,242"; btn="45,45,54"; bf="235,235,242"
    fi

    # A theme that named its own window colour replaces the generic pair above.
    # The view (list/text area) and button faces are derived from it rather than
    # given separately: every palette wants the same relationship — a light theme
    # puts near-white under text and keeps buttons on the face (that IS the look
    # for XP and 95), a dark one sinks the view below the window and lifts the
    # buttons above it.
    shade() {   # shade <r> <g> <b> <percent>  → "r,g,b" clamped
        local i o=""
        for i in "$1" "$2" "$3"; do
            i=$(( i * $4 / 100 ))
            [ "$i" -gt 255 ] && i=255
            o="${o:+$o,}$i"
        done
        printf '%s' "$o"
    }
    if [ -n "$br" ] && [ -n "$bg_" ] && [ -n "$bb" ]; then
        name=SynapseTheme
        wb="$br,$bg_,$bb"
        if [ "$scheme" = dark ]; then
            vb=$(shade "$br" "$bg_" "$bb" 82)    # views sink
            btn=$(shade "$br" "$bg_" "$bb" 125)  # buttons lift
        else
            # Toward white, not a percentage: 125% of #ECE9D8 is just clipped.
            vb=$(( br + (255 - br) * 3 / 4 )),$(( bg_ + (255 - bg_) * 3 / 4 )),$(( bb + (255 - bb) * 3 / 4 ))
            btn="$br,$bg_,$bb"
        fi
    fi
    if [ -n "$tr" ] && [ -n "$tg" ] && [ -n "$tb" ]; then
        wf="$tr,$tg,$tb"; vf="$wf"; bf="$wf"
    fi
    set_col() { "$kw" --file kdeglobals --group "$1" --key "$2" "$3" 2>/dev/null; }
    set_col General ColorScheme "$name"
    "$kw" --file kdeglobals --group KDE --key widgetStyle Breeze 2>/dev/null
    set_col "Colors:Window"    BackgroundNormal "$wb"
    set_col "Colors:Window"    ForegroundNormal "$wf"
    set_col "Colors:View"      BackgroundNormal "$vb"
    set_col "Colors:View"      ForegroundNormal "$vf"
    set_col "Colors:Button"    BackgroundNormal "$btn"
    set_col "Colors:Button"    ForegroundNormal "$bf"
    set_col "Colors:Selection" BackgroundNormal "$ar,$ag,$ab"
    set_col "Colors:Selection" ForegroundNormal "255,255,255"
    set_col "Colors:Tooltip"   BackgroundNormal "$vb"
    set_col "Colors:Tooltip"   ForegroundNormal "$vf"

    # kwriteconfig only writes the file; a Dolphin that is already open reads
    # kdeglobals once at launch and never again, so without this it repaints only
    # on next start. Nudge every running KDE app to re-read: PaletteChanged(0) for
    # the colours, StyleChanged(2) for the widget style. Best effort.
    if command -v dbus-send >/dev/null 2>&1; then
        for t in 0 2; do
            dbus-send --session --type=signal /KGlobalSettings \
                org.kde.KGlobalSettings.notifyChange int32:$t int32:0 2>/dev/null
        done
    fi
fi

# ── waybar (the bar) ─────────────────────────────────────────────────────────
# synui cannot draw into waybar (a separate GTK process), so — as with GTK/KDE —
# it writes a per-theme style and reloads. synui-waybar points `-s` at this file
# when it exists; it @imports the packaged base and overrides only the themed
# selectors, so the bar tracks the desktop accent + light/dark without forking
# the whole stylesheet. SIGUSR2 makes waybar reload its CSS live.
gen="$HOME/.config/synui/waybar-style.css"
mkdir -p "$(dirname "$gen")" 2>/dev/null
accent="rgb($ar,$ag,$ab)"
accent_dim="rgba($ar,$ag,$ab,0.15)"
glyph="rgb($gr,$gg,$gb)"

# A theme's own window colour drives the bar too, so the bar belongs to the rice
# instead of being the one dark strip on a Gruvbox desktop. Falls back to the
# historical pair when synui passed no palette.
bar_rgb=; [ -n "$br" ] && bar_rgb="$br,$bg_,$bb"
if [ "$scheme" = dark ]; then
    bar_bg="rgba(${bar_rgb:-11,11,20},0.85)"
    menu_bg="rgba(${bar_rgb:-11,11,20},0.97)"
    fg="#c8e3ee"; [ -n "$tr" ] && fg=$(printf '#%02x%02x%02x' "$tr" "$tg" "$tb")
    # The base yellow (#ffd319) reads fine on the dark bar; keep it.
    clock_fg="#ffd319"
else
    bar_bg="rgba(${bar_rgb:-236,239,244},0.95)"
    menu_bg="rgba(${bar_rgb:-246,247,249},0.99)"
    fg="#1a1a1c"; [ -n "$tr" ] && fg=$(printf '#%02x%02x%02x' "$tr" "$tg" "$tb")
    # #ffd319 yellow is illegible on a light bar — swap the clock and the other
    # yellow *text* modules for a dark gold that still reads as "amber".
    clock_fg="#8a6d00"
fi
cat > "$gen" <<CSS
/* Generated by synui-apply-theme — do not edit; a theme switch overwrites it.
 * Imports the packaged base, then recolours the themed selectors. */
@import url("file:///usr/share/synui/waybar/style.css");
window#waybar { background: $bar_bg; color: $fg; border-bottom: 2px solid $accent; }
/* The badge is the bar's copy of the launcher caret (launcher.c names this very
 * selector), so it takes the glyph colour, not the accent — same reason. */
#custom-synapse { color: $glyph; }
#custom-synapse:hover { background: $accent_dim; }
#cpu, #memory, #network, #pulseaudio, #custom-gamemode { color: $glyph; }
/* Yellow text is unreadable on a light bar; the light scheme darkens it. The
 * gamemode.active pill keeps its yellow *background* (its text is already dark). */
#custom-clock, #network.disabled { color: $clock_fg; }
menu { background: $menu_bg; color: $fg; border: 1px solid $accent; }
menu menuitem:hover { background: $accent; color: $menu_bg; }
tooltip { background: $menu_bg; color: $fg; border: 1px solid $accent; }
CSS
# Reload a running bar (harmless if none). pkill matches the waybar process the
# synui-waybar wrapper exec'd into.
pkill -USR2 -x waybar 2>/dev/null

# ── Firefox glass tint ───────────────────────────────────────────────────────
# The dark/light half of Firefox rides on gsettings above. The *tint* of the
# glass chrome cannot: it lives in userChrome.css, which used to hardcode a navy
# rgba(11,11,20,...). That left the browser as the one window still wearing the
# SYNAPSE navy on a Gruvbox or bubblegum desktop. So write the theme's own base
# colour into a generated sheet that userChrome/userContent @import.
#
# We only write into a profile that ALREADY has a userChrome.css — the glass is
# opt-in (it needs three prefs in user.js besides this), and a box that never set
# it up must not suddenly grow a chrome/ directory from a theme switch.
#
# Firefox reads userChrome at STARTUP ONLY. There is no live reload, so this
# lands on the next full Firefox restart, not on the theme switch itself.
ff_rgb="${br:+$br,$bg_,$bb}"; ff_rgb="${ff_rgb:-11,11,20}"
# A light theme's tint is near-white, and at the dark alphas its dark chrome text
# washes out over a busy wallpaper — give the light schemes more body.
if [ "$scheme" = dark ]; then ga=0.28; gh=0.48; gt=0.45
else                          ga=0.42; gh=0.62; gt=0.58
fi

for ffdir in "$HOME/.config/mozilla/firefox" "$HOME/.mozilla/firefox"; do
    [ -d "$ffdir" ] || continue
    for uc in "$ffdir"/*/chrome/userChrome.css; do
        [ -f "$uc" ] || continue          # glob matched nothing → skip
        cat > "$(dirname "$uc")/synapse-theme.css" <<CSS
/* Generated by synui-apply-theme — do not edit; a theme switch overwrites it.
 * Only the glass tint lives here; the layout rules stay in userChrome.css.
 * Deleting this file is safe: every use there carries a fallback. */
:root {
    --sy-glass: rgba($ff_rgb, $ga);
    --sy-glass-hi: rgba($ff_rgb, $gh);
    --sy-tile: rgba($ff_rgb, $gt);
}
CSS
    done
done

exit 0
