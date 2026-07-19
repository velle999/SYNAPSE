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
set -u

scheme=${1:-dark}
ar=${2:-61} ag=${3:-125} ab=${4:-255}

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
    vb="252,252,252";  vf="35,38,41";    bb="239,240,241"; bf="35,38,41"
    if [ "$scheme" = dark ]; then
        name=SynapseDark; wb="30,30,36"; wf="235,235,242"
        vb="24,24,28";    vf="235,235,242"; bb="45,45,54"; bf="235,235,242"
    fi
    set_col() { "$kw" --file kdeglobals --group "$1" --key "$2" "$3" 2>/dev/null; }
    set_col General ColorScheme "$name"
    "$kw" --file kdeglobals --group KDE --key widgetStyle Breeze 2>/dev/null
    set_col "Colors:Window"    BackgroundNormal "$wb"
    set_col "Colors:Window"    ForegroundNormal "$wf"
    set_col "Colors:View"      BackgroundNormal "$vb"
    set_col "Colors:View"      ForegroundNormal "$vf"
    set_col "Colors:Button"    BackgroundNormal "$bb"
    set_col "Colors:Button"    ForegroundNormal "$bf"
    set_col "Colors:Selection" BackgroundNormal "$ar,$ag,$ab"
    set_col "Colors:Selection" ForegroundNormal "255,255,255"
    set_col "Colors:Tooltip"   BackgroundNormal "$vb"
    set_col "Colors:Tooltip"   ForegroundNormal "$vf"
fi

exit 0
