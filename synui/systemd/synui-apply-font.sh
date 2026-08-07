#!/bin/bash
# synui-apply-font — push the chosen UI font out to the whole desktop.
#
#   synui-apply-font <family> [size]   apply it everywhere and remember it
#   synui-apply-font --default         back to each toolkit's own default
#   synui-apply-font --reapply         re-assert what was last applied
#   synui-apply-font status            key=value, what is in force
#
# The font picker (Super panel ▸ Appearance ▸ UI font, src/fontpick.c) used to
# set the font synui draws its OWN panels in and nothing else, so picking one
# left every application on the desktop in whatever it was already using. That
# is not what "pick a UI font" means. This is the other 90%: GTK 2/3/4, Qt/KDE,
# the bar, rofi, and the terminal.
#
# ── WHY IT OUTRANKS THE THEME ───────────────────────────────────────────────
#
# synui-apply-theme is the single writer of the desktop's COLOURS and it
# regenerates whole files to do it (rofi's synui.rasi, kitty's
# synui-colors.conf). A font written into one of those would live until the next
# theme switch and then vanish, which is the worst possible failure — it works
# when you set it and breaks later, for a reason nobody would connect to it.
#
# So the font never shares a file with the theme where it can be avoided, and
# synui-apply-theme ends by calling `synui-apply-font --reapply`. The ordering
# is the guarantee: a theme switch re-writes its files and the font is put back
# on top, every time, rather than the two racing over who wrote last.
#
# ── WHY THE TERMINAL IS TREATED SEPARATELY ──────────────────────────────────
#
# A terminal needs a MONOSPACED face. "Apply my UI font everywhere" taken
# literally would set kitty to a proportional font, where columns stop lining
# up, box-drawing breaks and every TUI on the box is ruined — an outcome nobody
# picking "Adwaita Sans" was asking for. So the terminals follow only when the
# chosen family is monospaced (fontconfig spacing=100), and are left alone
# otherwise. Same for kdeglobals' `fixed`, which is Qt's monospace role.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -uo pipefail

STATE_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/synui"
STATE="$STATE_DIR/font.state"

# The size every toolkit gets when none is given. 10pt is what KDE and GTK both
# ship as their default; the picker has no size row, so this is what "pick a
# family" means in a world where a font setting is a family AND a size.
DEFAULT_SIZE=10

family=
size=$DEFAULT_SIZE

state_load() {
    [[ -f $STATE ]] || return 0
    local k v
    while IFS='=' read -r k v; do
        case "$k" in
            family) family=$v ;;
            size)   size=$v   ;;
        esac
    done < "$STATE"
}

state_save() {
    mkdir -p "$STATE_DIR" || return 0
    # Written atomically: the BAR watches this file and a half-written line
    # would be parsed as a family name.
    {
        echo "family=$family"
        echo "size=$size"
        echo "mono=$(is_mono "$family" && echo yes || echo no)"
    } > "$STATE.tmp" && mv -f "$STATE.tmp" "$STATE" || rm -f "$STATE.tmp"
}

# Is this family monospaced? Asked of fontconfig rather than guessed from the
# name: "Hack" and "Adwaita Mono" are monospaced and do not both say so, and
# "DejaVu Math TeX Gyre" is not despite looking like it should be.
is_mono() {
    [[ -n ${1:-} ]] || return 1
    [[ -n "$(fc-list ":family=$1:spacing=100" family 2>/dev/null | head -1)" ]]
}

# A family with a comma in it cannot be expressed in Qt's font string, which is
# comma-separated. Rather than write a broken value into kdeglobals — where the
# symptom is every Qt app falling back to its default with nothing logged —
# refuse the whole run.
check_family() {
    case "$1" in
        *,*) echo "synui-apply-font: '$1' contains a comma; Qt's font format cannot carry it" >&2
             exit 2 ;;
    esac
}

# ── GTK 3 / GTK 4 ───────────────────────────────────────────────────────────
# One key under [Settings], leaving everything synui-apply-theme wrote there
# alone. Same awk shape that script uses, for the same reason: a rewrite of the
# whole file would drop the theme's keys and vice versa.
#
# GTK reads settings.ini at STARTUP. There is no live signal for fonts on
# Wayland — xdg-desktop-portal's org.freedesktop.appearance carries the colour
# scheme, the accent and the contrast, and nothing else — so running GTK apps
# keep their old font until they are restarted. That is a property of GTK, not
# a bug here.
gtk_ini() {   # gtk_ini <file> <value|"">
    local f=$1 val=$2 tmp
    mkdir -p "$(dirname "$f")" 2>/dev/null || return 0
    [[ -f $f ]] || { [[ -n $val ]] || return 0; printf '[Settings]\n' > "$f"; }
    tmp=$(mktemp) || return 0
    awk -v val="$val" '
        BEGIN { insec = 0; put = 0 }
        /^\[Settings\]/ { print; insec = 1
                          if (val != "") { print "gtk-font-name=" val; put = 1 }
                          next }
        /^\[/ { insec = 0 }
        insec && /^[[:space:]]*gtk-font-name[[:space:]]*=/ { next }
        { print }
        END { if (val != "" && !put) { print "[Settings]"; print "gtk-font-name=" val } }
    ' "$f" > "$tmp" && mv "$tmp" "$f" || rm -f "$tmp"
}

# GTK 2 is a different file and a different syntax (the value is QUOTED there).
# Still worth writing: wine's GTK-themed dialogs, a few long-lived utilities and
# anything the user installed from an old package still read it.
gtk2_rc() {   # gtk2_rc <value|"">
    local f="$HOME/.gtkrc-2.0" val=$1 tmp
    [[ -f $f ]] || { [[ -n $val ]] || return 0; : > "$f"; }
    tmp=$(mktemp) || return 0
    grep -v '^[[:space:]]*gtk-font-name[[:space:]]*=' "$f" > "$tmp" 2>/dev/null
    [[ -n $val ]] && printf 'gtk-font-name="%s"\n' "$val" >> "$tmp"
    mv "$tmp" "$f" || rm -f "$tmp"
}

# ── Qt / KDE ────────────────────────────────────────────────────────────────
# Qt's font string, in the 16-field form Qt 6 writes:
#
#   family,pointSize,pixelSize,styleHint,weight,style,underline,strikeOut,
#   fixedPitch,rawMode,capitalization,letterSpacing,wordSpacing,stretch,
#   styleStrategy,styleName-is-set
#
# Everything after the size is the default: hint 5 (AnyStyle), weight 400
# (Normal — Qt 6 uses real weights here, NOT the 0-99 scale Qt 5 used, and a 50
# in this field renders as a hairline), no style flags.
qt_font() { printf '%s,%s,-1,5,400,0,0,0,0,0,0,0,0,0,0,1' "$1" "$2"; }

kde_apply() {   # kde_apply <family|""> <size> <mono-family|"">
    local fam=$1 sz=$2 mono=$3 kw=
    for c in kwriteconfig6 kwriteconfig5; do
        command -v "$c" >/dev/null 2>&1 && { kw=$c; break; }
    done
    [[ -n $kw ]] || return 0

    # kwriteconfig merges one key at a time, so the colours, the widget style
    # and the terminal service that already live in kdeglobals are untouched.
    set_font() {   # set_font <group> <key> <value|"">
        if [[ -z ${3:-} ]]; then
            "$kw" --file kdeglobals --group "$1" --key "$2" --delete 2>/dev/null
        else
            "$kw" --file kdeglobals --group "$1" --key "$2" "$3" 2>/dev/null
        fi
    }

    local gen="" small="" fixed=""
    if [[ -n $fam ]]; then
        gen=$(qt_font "$fam" "$sz")
        # The "smallest readable" role is genuinely meant to be smaller — it is
        # what KDE draws secondary labels in — so it tracks the choice rather
        # than ignoring it or matching it exactly.
        small=$(qt_font "$fam" "$(( sz > 8 ? sz - 1 : sz ))")
    fi
    # `fixed` is Qt's MONOSPACE role: the file-size column in Dolphin, Kate's
    # editor, every terminal widget. It follows only a monospaced choice — see
    # the header. Cleared when the choice is proportional so it goes back to the
    # system default rather than keeping a stale mono family from last time.
    [[ -n $mono ]] && fixed=$(qt_font "$mono" "$sz")

    set_font General font              "$gen"
    set_font General menuFont          "$gen"
    set_font General toolBarFont       "$gen"
    set_font General smallestReadableFont "$small"
    set_font General fixed             "$fixed"
    # The window CAPTION, which is drawn by the decoration rather than the
    # widget style. synui draws its own titlebars in its own font already; this
    # is for the KDE apps that draw client-side decorations.
    set_font WM activeFont             "$gen"

    # ── Make RUNNING Qt apps re-read ────────────────────────────────────────
    # QT_QPA_PLATFORMTHEME=kde, so the listener is plasma-integration's
    # KHintsSettings, which connects to org.kde.KGlobalSettings.notifyChange
    # with slotNotifyChange(int, int) — NOT KConfigWatcher. Its switch covers
    # 0..6 and **1 is FontChanged**; the second argument is unused for that
    # type. (0 is PaletteChanged, 4 IconChanged — synui-apply-theme sends those
    # for the same reason.)
    #
    # The signature is (int, int) and cannot OOM anything the way a mistyped
    # a{saay} ConfigChanged did; still, send exactly two ints.
    if command -v dbus-send >/dev/null 2>&1; then
        dbus-send --session --type=signal /KGlobalSettings \
            org.kde.KGlobalSettings.notifyChange int32:1 int32:0 2>/dev/null
    fi
    # ...and the KConfigWatcher half, for apps that use it instead. --notify
    # only fires for keys this write actually dirtied, which is the right
    # behaviour: re-applying the font you already have should be silent.
    #
    # The empty case has to go through --delete. kwriteconfig takes the value as
    # a mandatory argument and writes it literally, so re-notifying with an
    # empty $gen put a bare `font=` back into kdeglobals one line after the
    # delete above removed it — a key whose value is the empty string is not the
    # same as an absent key, and Qt parses it as a font with no family.
    if [[ -n $gen ]]; then
        "$kw" --file kdeglobals --group General --key font --notify "$gen" 2>/dev/null
    else
        "$kw" --file kdeglobals --group General --key font --notify --delete "" 2>/dev/null
    fi
}

# ── rofi (Super+Space) ──────────────────────────────────────────────────────
# Its OWN file, imported after the theme's. synui.rasi is regenerated wholesale
# by every theme switch, so a font line written into it would disappear the
# first time the user changed theme — the failure this script's header is about.
rofi_apply() {   # rofi_apply <family|""> <size>
    local dir="$HOME/.config/rofi" f="$HOME/.config/rofi/synui-font.rasi"
    local conf="$dir/config.rasi"
    command -v rofi >/dev/null 2>&1 || return 0

    mkdir -p "$dir" 2>/dev/null || return 0
    # EMPTIED rather than deleted when there is no font to set. config.rasi
    # carries an @import of this file, and rofi treats a missing import as a
    # theme parse error — deleting it would break the launcher to undo a font.
    {
        echo "/* Generated by synui-apply-font — do not edit; the font picker overwrites it. */"
        [[ -n ${1:-} ]] && echo "* { font: \"$1 $2\"; }"
    } > "$f.tmp"
    mv -f "$f.tmp" "$f" 2>/dev/null || { rm -f "$f.tmp"; return 0; }

    # AFTER synui.rasi, or the theme's own defaults would win — rofi takes the
    # last assignment. A bare name, since rofi resolves an @import against its
    # own config directory.
    if [[ ! -e $conf ]]; then
        printf '@import "synui-font.rasi"\n' > "$conf"
    elif ! grep -q 'synui-font\.rasi' "$conf" 2>/dev/null; then
        printf '@import "synui-font.rasi"\n' >> "$conf"
    fi
}

# ── kitty / foot ────────────────────────────────────────────────────────────
# Monospaced choices only — see the header. kitty gets its own include beside
# the theme's synui-colors.conf; foot has no include mechanism, so its key is
# edited in place.
kitty_apply() {   # kitty_apply <mono-family|""> <size>
    # Three `local` statements, not one: a name declared in the same `local` as
    # the one it expands is not yet set when the expansion runs, and under
    # `set -u` that is a FATAL error, not an empty string — it took the whole
    # script down after the Qt half had already been written, which is the worst
    # place to stop.
    local dir="$HOME/.config/kitty"
    local conf="$dir/kitty.conf"
    local f="$dir/synui-font.conf"
    command -v kitty >/dev/null 2>&1 || return 0

    mkdir -p "$dir" 2>/dev/null || return 0
    # EMPTIED rather than deleted with nothing to set — kitty.conf includes it,
    # and an include of a file that is not there is a warning on every terminal
    # launch. Emptying it also RESTORES kitty's own default, which is the right
    # answer when the user has just picked a proportional family: the terminal
    # stops following rather than keeping the last monospaced pick forever.
    {
        echo "# Generated by synui-apply-font — do not edit; the font picker overwrites it."
        if [[ -n ${1:-} ]]; then
            echo "font_family      $1"
            echo "font_size        $2"
        else
            echo "# No monospaced UI font is set, so kitty keeps its own default."
        fi
    } > "$f.tmp"
    mv -f "$f.tmp" "$f" 2>/dev/null || { rm -f "$f.tmp"; return 0; }
    # kitty ships no default kitty.conf — it runs on built-in defaults — so the
    # file has to exist for the include to be read at all.
    [[ -e $conf ]] || printf '# Created by synui-apply-font.\n' > "$conf"
    if ! grep -q '^[[:space:]]*include[[:space:]]\+synui-font\.conf' "$conf" 2>/dev/null; then
        printf '\n# Desktop UI font, written by synui-apply-font.\ninclude synui-font.conf\n' >> "$conf"
    fi
    # Re-reads its config on SIGUSR1, so an open terminal changes font in place.
    pkill -USR1 -x kitty 2>/dev/null
    return 0
}

foot_apply() {   # foot_apply <mono-family|""> <size>
    local f="$HOME/.config/foot/foot.ini" tmp
    command -v foot >/dev/null 2>&1 || return 0
    [[ -f $f ]] || { [[ -n ${1:-} ]] || return 0; mkdir -p "$(dirname "$f")" 2>/dev/null || return 0
                     printf '[main]\n' > "$f"; }
    tmp=$(mktemp) || return 0
    # foot's font lives in the [main] section as `font=family:size=N`. The awk
    # is the settings.ini shape again rather than a sed, so a `font=` under some
    # other section (foot has [colors], [key-bindings]…) is not touched.
    awk -v val="${1:-}" -v sz="${2:-}" '
        BEGIN { insec = 0; put = 0 }
        /^\[main\]/ { print; insec = 1
                      if (val != "") { print "font=" val ":size=" sz; put = 1 }
                      next }
        /^\[/ { insec = 0 }
        insec && /^[[:space:]]*font[[:space:]]*=/ { next }
        { print }
        END { if (val != "" && !put) { print "[main]"; print "font=" val ":size=" sz } }
    ' "$f" > "$tmp" && mv "$tmp" "$f" || rm -f "$tmp"
    # foot re-reads on SIGUSR1 too (it reloads the config, keeping the session).
    pkill -USR1 -x foot 2>/dev/null
    return 0
}

# ── GNOME-side setting ──────────────────────────────────────────────────────
# Not for GTK's own rendering (that comes from settings.ini above) but for the
# apps and shells that read GSettings directly, and because leaving it stale
# means two places disagree about what the desktop font is.
gsettings_apply() {   # gsettings_apply <family|""> <size>
    command -v gsettings >/dev/null 2>&1 || return 0
    if [[ -z ${1:-} ]]; then
        gsettings reset org.gnome.desktop.interface font-name 2>/dev/null
    else
        gsettings set org.gnome.desktop.interface font-name "$1 $2" 2>/dev/null
    fi
    return 0
}

# ── The whole application ───────────────────────────────────────────────────
apply() {   # apply <family|""> <size>
    local fam=${1:-} sz=${2:-$DEFAULT_SIZE} mono=""
    local val=""
    [[ -n $fam ]] && val="$fam $sz"
    is_mono "$fam" && mono=$fam

    gtk_ini "$HOME/.config/gtk-3.0/settings.ini" "$val"
    gtk_ini "$HOME/.config/gtk-4.0/settings.ini" "$val"
    gtk2_rc "$val"
    kde_apply "$fam" "$sz" "$mono"
    rofi_apply "$fam" "$sz"
    kitty_apply "$mono" "$sz"
    foot_apply  "$mono" "$sz"
    gsettings_apply "$fam" "$sz"

    # LAST, and the bar's cue: Theme.qml watches font.state, so writing it is
    # what moves the bar. Written after the rest so a bar that repaints the
    # instant it changes is repainting into a desktop that already agrees.
    family=$fam
    size=$sz
    if [[ -z $fam ]]; then rm -f "$STATE"; else state_save; fi
}

# ── Entry point ─────────────────────────────────────────────────────────────

case "${1:-status}" in
--default)
    apply "" "$DEFAULT_SIZE"
    ;;

--reapply)
    # What synui-apply-theme calls. Silent and successful when no font has ever
    # been picked — the common case, and not an error.
    state_load
    [[ -n $family ]] || exit 0
    apply "$family" "$size"
    ;;

status)
    state_load
    echo "family=$family"
    echo "size=$size"
    echo "mono=$(is_mono "$family" && echo yes || echo no)"
    ;;

-h|--help)
    sed -n '2,8p' "$0"
    ;;

*)
    check_family "$1"
    apply "$1" "${2:-$DEFAULT_SIZE}"
    ;;
esac
