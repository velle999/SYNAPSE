#!/usr/bin/env bash
# synui-apply-theme — push the desktop theme's light/dark scheme out to the
# toolkits synui does not draw: GTK apps, Dolphin (Qt/KDE), kitty and Firefox.
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

# Pick a real theme name for GTK, preferring Breeze (ships with the KDE
# integration) and falling back to Adwaita, which GTK always has.
gtk_theme=Adwaita
[ "$scheme" = dark ] && gtk_theme=Adwaita-dark
if [ -d /usr/share/themes/Breeze ] || [ -d "$HOME/.local/share/themes/Breeze" ]; then
    gtk_theme=Breeze
    [ "$scheme" = dark ] && gtk_theme=Breeze-Dark
fi

# And the Qt widget style, which is a SEPARATE question from the GTK theme above
# and was being answered without asking it: kdeglobals got widgetStyle=Breeze
# unconditionally, on a distro that does not ship the `breeze` package at all.
# Qt then found no such style, fell back to its default, and every Qt app ran
# with a config claiming a style that was never loaded — invisible, because the
# COLOURS still land (KDE apps read the Colors:* groups below through
# KColorScheme regardless of which QStyle is in use), so only the widget shapes
# were ever wrong.
#
# The two live in different places, which is why the check above does not cover
# this: /usr/share/themes/Breeze is the GTK theme, while the Qt style is a
# plugin under the Qt tree. Fusion is the honest fallback — it ships with
# qt6-base so it is always present, and it follows QPalette properly, which is
# what makes the themed colours land on a system with no Breeze.
qt_style=Fusion
for d in /usr/lib/qt6/plugins/styles /usr/lib/qt/plugins/styles \
         /usr/lib64/qt6/plugins/styles; do
    for so in "$d"/breeze6.so "$d"/breeze.so; do
        [ -e "$so" ] && { qt_style=Breeze; break 2; }
    done
done

prefer_dark=0; color_scheme=default
if [ "$scheme" = dark ]; then prefer_dark=1; color_scheme=prefer-dark; fi

# Black or white ink for a background, whichever carries better. Top level and
# not inside the KDE block below, because kitty needs it too and a box with no
# kwriteconfig must still get a readable terminal selection. The KDE selection
# foreground used to be a hardcoded 255,255,255, which is right for 95's navy
# and wrong for any theme whose accent is pale — white on bubblegum pink is a
# selected filename you cannot read. WCAG relative luminance.
ink_for() {  # ink_for <r> <g> <b> → "r,g,b"
    awk -v r="$1" -v g="$2" -v b="$3" '
        function ch(v) { v /= 255; return (v <= 0.03928) ? v / 12.92 : ((v + 0.055) / 1.055) ^ 2.4 }
        BEGIN {
            l  = 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)
            cw = 1.05 / (l + 0.05)      # contrast against white
            cb = (l + 0.05) / 0.05      # contrast against black
            print (cb > cw) ? "0,0,0" : "255,255,255"
        }'
}
rgb_hex() { printf '#%02x%02x%02x' "$1" "$2" "$3"; }

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
    sel="$ar,$ag,$ab"
    selfg=$(ink_for "$ar" "$ag" "$ab")

    # Blend two "r,g,b" triples. Every derived role below is a blend of a colour
    # the theme gave us with another one it gave us, so nothing here can drift
    # away from the palette the way a hardcoded grey does.
    mix() {  # mix <"r,g,b"> <"r,g,b"> <percent of the first> → "r,g,b"
        awk -v a="$1" -v b="$2" -v p="$3" 'BEGIN {
            split(a, x, ","); split(b, y, ",")
            printf "%d,%d,%d", (x[1]*p + y[1]*(100-p)) / 100,
                               (x[2]*p + y[2]*(100-p)) / 100,
                               (x[3]*p + y[3]*(100-p)) / 100
        }'
    }

    # The three semantic foregrounds carry MEANING — a failed operation, a
    # warning, a success — so like kitty's ANSI sixteen they are curated per
    # scheme rather than derived from an accent that might be any hue at all.
    # Breeze's values, lightened for dark so they clear their background.
    if [ "$scheme" = dark ]; then
        neg="237,102,109"; neu="246,180,89"; pos="119,209,138"
    else
        neg="191,3,3";     neu="176,128,0";  pos="0,110,40"
    fi

    set_col() { "$kw" --file kdeglobals --group "$1" --key "$2" "$3" 2>/dev/null; }
    "$kw" --file kdeglobals --group KDE --key widgetStyle "$qt_style" 2>/dev/null

    # The ICON theme is a separate setting from the colours and was never being
    # answered, so a dark desktop got Dolphin's toolbar in icons drawn for a
    # light one — dark glyphs on a dark bar, the one part of the window that
    # stayed obviously untouched by a theme switch. Both halves of Breeze ship
    # separately, so this only claims one that is actually on disk.
    for it in "$HOME/.local/share/icons" /usr/share/icons; do
        if [ "$scheme" = dark ] && [ -d "$it/breeze-dark" ]; then
            "$kw" --file kdeglobals --group Icons --key Theme breeze-dark 2>/dev/null; break
        elif [ "$scheme" = light ] && [ -d "$it/breeze" ]; then
            "$kw" --file kdeglobals --group Icons --key Theme breeze 2>/dev/null; break
        fi
    done

    # ── The scheme as a real FILE ───────────────────────────────────────────
    # kdeglobals is what KColorScheme actually reads, so the groups below are
    # what colours Dolphin — but [General] ColorScheme was naming "SynapseTheme"
    # with no such scheme existing anywhere on disk. A dangling name is not
    # cosmetic: it is the handle KDE's own appearance settings resolve to decide
    # which scheme is SELECTED, so the desktop's theme showed up nowhere in
    # System Settings and any KDE app that re-derives the palette from the
    # scheme name rather than from kdeglobals found nothing to load and fell
    # back to Breeze. Writing the file makes the name real, and makes the light
    # and dark variants a thing KDE can actually see it switch between.
    #
    # It is emitted from the SAME loop that writes kdeglobals, one group at a
    # time, because these are two encodings of one palette: a scheme file that
    # has drifted from kdeglobals is a "reset to the theme" in System Settings
    # that visibly changes the colours. cs= empty means no writable directory,
    # and every append below is then skipped.
    cs="$HOME/.local/share/color-schemes"
    mkdir -p "$cs" 2>/dev/null || cs=
    csf=
    if [ -n "$cs" ]; then
        csf="$cs/$name.colors.tmp"
        printf '[General]\nName=%s\nColorScheme=%s\n' "$name" "$name" > "$csf" \
            || csf=
    fi
    emit() { [ -n "$csf" ] && printf '%s=%s\n' "$1" "$2" >> "$csf"; return 0; }

    # Header is what KDE 6 paints Dolphin's toolbar and column headings with. It
    # is a distinct group from Window, so leaving it out did not fall back to the
    # window colour — it fell back to BREEZE's, which is why the file list could
    # be Gruvbox brown under a toolbar that was still Breeze grey.
    for grp in "Colors:Window" "Colors:View" "Colors:Button" \
               "Colors:Selection" "Colors:Tooltip" "Colors:Header"; do
        case "$grp" in
            "Colors:View")      b=$vb;  f=$vf  ;;
            "Colors:Button")    b=$btn; f=$bf  ;;
            "Colors:Selection") b=$sel; f=$selfg ;;
            "Colors:Tooltip")   b=$vb;  f=$vf  ;;
            *)                  b=$wb;  f=$wf  ;;   # Window and Header
        esac
        # Both encodings of the same role, so they cannot disagree.
        put() { set_col "$grp" "$1" "$2"; emit "$1" "$2"; }

        [ -n "$csf" ] && printf '\n[%s]\n' "$grp" >> "$csf"

        put BackgroundNormal "$b"
        put ForegroundNormal "$f"
        # Focus and hover are drawn from these, per group. Unset, KDE uses its
        # own blue no matter what the desktop accent is — the highlight you get
        # arrowing through a file list.
        put DecorationFocus "$sel"
        put DecorationHover "$sel"

        # ── The rest of the roles ───────────────────────────────────────────
        # Normal was the only pair being written, and KColorScheme does NOT fall
        # back to it for the others — an unset role takes Breeze's LIGHT default.
        # On a dark theme that is text you cannot read: BackgroundAlternate is a
        # near-white stripe on every other row of Dolphin's list, under
        # foreground text still drawn in the theme's near-white. Inactive text
        # (the status line's item count, disabled entries, the Places headings)
        # came out mid-grey on near-black for the same reason. That is the "hard
        # to see text on dark themes" — not ForegroundNormal, which was already
        # the theme's own near-white.
        put BackgroundAlternate "$(mix "$b" "$f" 94)"
        put ForegroundInactive  "$(mix "$f" "$b" 62)"
        put ForegroundActive    "$sel"
        put ForegroundLink      "$sel"
        put ForegroundVisited   "$(mix "$sel" "$b" 70)"
        put ForegroundNegative  "$neg"
        put ForegroundNeutral   "$neu"
        put ForegroundPositive  "$pos"
    done

    # Renamed into place for the same reason theme.json is: a KDE app that
    # happens to read while this is being written must not see half a scheme.
    if [ -n "$csf" ]; then
        mv -f "$csf" "$cs/$name.colors" 2>/dev/null || rm -f "$csf"
    fi

    # ── Make a RUNNING app re-read ──────────────────────────────────────────
    # kwriteconfig only writes the file; an already-open Dolphin reads kdeglobals
    # at launch and never again, so without a nudge it repaints on next start.
    #
    # The nudge used to be a dbus-send of org.kde.KGlobalSettings.notifyChange,
    # which is a KDE 4/KF5 interface: KGlobalSettings was REMOVED in KF6 and the
    # string does not appear anywhere in the KF6 libraries Dolphin links
    # (libKF6ColorScheme, libKF6ConfigWidgets, libKF6ConfigCore) — it was a
    # signal into an empty room. What KF6 listens on is KConfigWatcher, and the
    # way to raise it from a script is kwriteconfig's own --notify.
    #
    # ColorScheme is written LAST so that everything a listener would re-read is
    # already on disk — colour keys, widget style, and the scheme file — before
    # anything is told to look. An app that re-reads on the signal therefore
    # cannot catch a half-applied palette.
    set_col General ColorScheme "$name"

    # --notify raises it through KConfig itself, but only for keys this write
    # actually DIRTIED: re-applying the theme you are already on changes no
    # value, so on its own this is silent exactly when a re-apply is what you
    # asked for. It is still worth doing for the first switch of a session.
    "$kw" --file kdeglobals --group General --key ColorScheme --notify "$name" \
        2>/dev/null

    # So also emit the signal directly, which does not care whether anything was
    # dirty. This is the KF6 contract KConfigWatcher connects to: interface
    # org.kde.kconfig.notify, signal ConfigChanged, object path "/" + the config
    # file's name, group → the keys that changed. dbus-send cannot build a nested
    # container, hence gdbus, which ships with glib2.
    #
    # ── The argument type is a{saay}, and getting it wrong is NOT cosmetic ────
    # The slot takes QHash<QString, QByteArrayList>, so each key name is a BYTE
    # ARRAY (`ay`) and the value is an array of them (`aay`) — the whole argument
    # is a{saay}, not a{sas}. This was emitted as a{sas} for one pkgrel and the
    # result was that every KDE app open during a theme switch was killed by the
    # OOM killer: QtDBus demarshalls whatever arrives into the type the slot
    # declares, so an `as` fed to a QByteArrayList reader is read as garbage
    # element lengths, and it allocates against them until the box runs out of
    # memory (~28 GB, in about twenty seconds). A wrong D-Bus signature does not
    # bounce — it is demarshalled as nonsense. Confirmed against the type in
    # libKF6ConfigCore and by capturing kwriteconfig6 --notify's OWN signal on
    # the bus; that capture is what this has to match, byte for byte.
    #
    # `ay` also has to be built numerically. GVariant's bytestring literal
    # (b'ColorScheme') is NUL-TERMINATED, and KConfig's own signal is not — a
    # key arriving as "ColorScheme\0" simply matches nothing, which is a silent
    # no-op rather than a crash, so it looks exactly like the signal working.
    ay() {  # ay <name> → "0x43,0x6f,…" — the name as bare bytes, no terminator
        printf '%s' "$1" | od -An -tx1 -v |
            awk '{ for (i = 1; i <= NF; i++) printf "%s0x%s", (n++ ? "," : ""), $i }'
    }
    if command -v gdbus >/dev/null 2>&1; then
        bgfg="[[$(ay BackgroundNormal)],[$(ay ForegroundNormal)]]"
        gdbus emit --session --object-path /kdeglobals \
            --signal org.kde.kconfig.notify.ConfigChanged \
            "@a{saay} {'General': [[$(ay ColorScheme)]],
                       'Colors:Window': $bgfg,
                       'Colors:View': $bgfg,
                       'Colors:Button': $bgfg,
                       'Colors:Selection': $bgfg,
                       'Colors:Tooltip': $bgfg,
                       'Colors:Header': $bgfg,
                       'Icons': [[$(ay Theme)]],
                       'KDE': [[$(ay widgetStyle)]]}" 2>/dev/null
    fi

    # And the KDE 4 / KF5 interface, for anything still built against it.
    # KGlobalSettings was REMOVED in KF6 — the string appears nowhere in the KF6
    # libraries Dolphin links — so on this desktop it is a signal into an empty
    # room, kept only because it costs nothing and an old app may still be there.
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
# Base colour and alpha are kept apart rather than pre-baked into an rgba()
# string: the CSS below wants the string, but the quickshell bar wants numbers
# (QML's colour type does not parse CSS rgba()), and deriving one from the other
# by re-parsing would be a second place for the palette to go wrong.
if [ "$scheme" = dark ]; then
    bar_base="${bar_rgb:-11,11,20}";  bar_alpha=0.85
    menu_base="${bar_rgb:-11,11,20}"; menu_alpha=0.97
    fg="#c8e3ee"; [ -n "$tr" ] && fg=$(printf '#%02x%02x%02x' "$tr" "$tg" "$tb")
    # The base yellow (#ffd319) reads fine on the dark bar; keep it.
    clock_fg="#ffd319"
else
    bar_base="${bar_rgb:-236,239,244}";  bar_alpha=0.95
    menu_base="${bar_rgb:-246,247,249}"; menu_alpha=0.99
    fg="#1a1a1c"; [ -n "$tr" ] && fg=$(printf '#%02x%02x%02x' "$tr" "$tg" "$tb")
    # #ffd319 yellow is illegible on a light bar — swap the clock and the other
    # yellow *text* modules for a dark gold that still reads as "amber".
    clock_fg="#8a6d00"
fi
bar_bg="rgba($bar_base,$bar_alpha)"
menu_bg="rgba($menu_base,$menu_alpha)"
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

# ── quickshell (the bar, again) ──────────────────────────────────────────────
# The same palette as numbers, for the QML bar. It cannot read the CSS above:
# QML's colour type parses "#RRGGBB" and SVG names but NOT CSS rgba(), so a
# themed bar would silently fall back to whatever Theme.qml hardcoded — which is
# exactly the bug this fixes (a win95 desktop with one neon magenta strip).
#
# Written to a temp file and renamed: Theme.qml watches this path, and rename is
# atomic, so the bar can never read a half-written palette and repaint itself
# from it. Colours are [r,g,b] triples plus a separate alpha for the same reason
# they are kept apart above.
qs="$HOME/.config/synui/theme.json"
cat > "$qs.tmp" <<JSON
{
  "_comment":   "Generated by synui-apply-theme — do not edit; a theme switch overwrites it.",
  "scheme":     "$scheme",
  "accent":     [$ar, $ag, $ab],
  "glyph":      [$gr, $gg, $gb],
  "bar":        [$bar_base],
  "barAlpha":   $bar_alpha,
  "popup":      [$menu_base],
  "popupAlpha": $menu_alpha,
  "fg":         "$fg",
  "clockFg":    "$clock_fg"
}
JSON
mv -f "$qs.tmp" "$qs" 2>/dev/null || rm -f "$qs.tmp"

# ── kitty (the default terminal) ─────────────────────────────────────────────
# Colours only. synui-glass owns background_opacity and dynamic_background_opacity
# in kitty.conf and rewrites that file with an awk that reprints every line it
# does not recognise, so the include added here survives an opacity change — and
# the two never write the same key.
#
# The palette goes in a SEPARATE file rather than into kitty.conf, because
# kitty.conf is a file the user is expected to own and this one is regenerated
# on every theme switch. The include is appended rather than prepended so the
# theme actually lands: kitty takes the last value for a key, and a theme that
# loses to a stale line the user forgot they wrote is a theme switch that
# silently does nothing.
#
# The ANSI SIXTEEN are curated per scheme rather than derived from the accent.
# They carry MEANING — git prints errors in color1 and additions in color2 — so
# a Gruvbox desktop must not repaint red as brown. Same reason the clock's
# yellow and the keyring warning are not themed. What IS themed is everything
# that describes the terminal rather than its contents: surface, ink, cursor,
# selection, tab bar, links.
if command -v kitty >/dev/null 2>&1; then
    kdir="$HOME/.config/kitty"
    kcol="$kdir/synui-colors.conf"
    kconf="$kdir/kitty.conf"
    if mkdir -p "$kdir" 2>/dev/null; then
        IFS=, read -r kbr kbg kbb <<<"$bar_base"
        k_bg=$(rgb_hex "$kbr" "$kbg" "$kbb")
        k_accent=$(rgb_hex "$ar" "$ag" "$ab")
        IFS=, read -r kir kig kib <<<"$(ink_for "$ar" "$ag" "$ab")"
        k_on_accent=$(rgb_hex "$kir" "$kig" "$kib")

        if [ "$scheme" = dark ]; then
            ansi="color0  #21222c
color8  #6272a4
color1  #ff5555
color9  #ff6e6e
color2  #50fa7b
color10 #69ff94
color3  #f1fa8c
color11 #ffffa5
color4  #6272ff
color12 #8b9bff
color5  #ff79c6
color13 #ff92df
color6  #8be9fd
color14 #a4ffff
color7  #f8f8f2
color15 #ffffff"
        else
            # The light set is not the dark one lightened: every one of these has
            # to read as TEXT on a pale surface, so they are darkened instead.
            # color7 ("white") is conventionally a light grey, which on a light
            # background is invisible — here 7 and 15 run dark, and the BRIGHT
            # half stays more vivid than the normal half rather than lighter,
            # because lighter is the wrong direction on a pale surface.
            #
            # Measured against #C0C0C0, the darkest light base any shipped theme
            # uses (95's silver; XP's beige is paler and only gains contrast).
            # The normal half clears 4.5:1 there and the bright half 3.5:1. The
            # first draft of this palette was a set of mid-tones that looked
            # right and put color3 at 2.71:1 on silver — yellow on grey, exactly
            # the pairing that fails.
            ansi="color0  #21222c
color8  #55555f
color1  #9a1717
color9  #b91c1c
color2  #0f5b2b
color10 #126d34
color3  #704405
color11 #865206
color4  #1942b8
color12 #1d4ed8
color5  #84178f
color13 #9f1bac
color6  #0a566a
color14 #0c6780
color7  #3f3f46
color15 #18181b"
        fi

        cat > "$kcol.tmp" <<KITTYCOL
# Generated by synui-apply-theme — do not edit; a theme switch overwrites it.
# Included from kitty.conf. Opacity is NOT here: synui-glass owns that.
background            $k_bg
foreground            $fg
cursor                $k_accent
cursor_text_color     $k_on_accent
selection_background  $k_accent
selection_foreground  $k_on_accent
url_color             $k_accent
active_tab_background   $k_accent
active_tab_foreground   $k_on_accent
inactive_tab_background $k_bg
inactive_tab_foreground $fg
active_border_color     $k_accent
inactive_border_color   $k_bg

$ansi
KITTYCOL
        mv -f "$kcol.tmp" "$kcol" 2>/dev/null || rm -f "$kcol.tmp"

        # kitty ships no default kitty.conf — it runs on built-in defaults — so
        # this creates the file when it is absent for the same reason
        # synui-glass does, or the palette would be written and never read.
        [ -e "$kconf" ] || printf '# Created by synui-apply-theme.\n' > "$kconf"
        if [ -f "$kconf" ] && ! grep -q '^[[:space:]]*include[[:space:]]\+synui-colors\.conf' "$kconf"; then
            printf '\n# Desktop theme colours, regenerated by synui-apply-theme.\ninclude synui-colors.conf\n' >> "$kconf"
        fi

        # kitty re-reads its config on SIGUSR1, so a running terminal recolours
        # in place — the equivalent of waybar's SIGUSR2 and Dolphin's D-Bus
        # nudge. Unlike opacity, colours have no "must be set at startup" caveat.
        pkill -USR1 -x kitty 2>/dev/null
    fi
fi

# ── Firefox ──────────────────────────────────────────────────────────────────
# The dark/light half of Firefox rides on the gsettings above. The *tint* of the
# glass chrome cannot: it lives in a userChrome.css inside the profile, so it
# needs a file written per theme or the browser keeps the navy it was born with
# on a Gruvbox or bubblegum desktop. All of that (install, tint, and the off
# switch) belongs to synui-firefox-glass; hand it the scheme and base colour and
# let it decide whether the user still wants any of it.
#
# Firefox reads userChrome at STARTUP ONLY — no live reload exists, so unlike
# waybar (SIGUSR2) or Dolphin (dbus) this lands on the next full FF restart.
if command -v synui-firefox-glass >/dev/null 2>&1; then
    synui-firefox-glass "$scheme" ${br:+"$br" "$bg_" "$bb"} 2>/dev/null
fi

exit 0
