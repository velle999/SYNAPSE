#!/bin/sh
# menu_apps.sh — every panel that is an APP is in the menus, not only on a chord
#
# velle, 2026-08-20: "don't make apps hotkey only must be in menus."
#
# The failure this prevents is not a crash, it is a feature nobody can find.
# News was Super+R and nothing else: the only ways to learn it existed were to
# read config.c or to open the shortcut palette, and nobody opens a shortcut
# palette to discover an application they do not know about. Eleven entries
# went in at pkgrel 392 and the remaining panels stayed keybind-only, so the
# rule needs enforcing rather than remembering.
#
# ── Where the list comes from ───────────────────────────────────────────────
#
# SYN_PANEL_LIST in input.c, which is the roster the pointer chain, the
# keyboard chain and the "is anything open" test already walk — so a panel that
# exists at all is in it. A hand-written list here would be a fourth copy and
# would drift exactly the way the start menu's Settings page drifted (see
# menu_cats.sh).
#
# The macro names a panel by its handler PREFIX, which is not always the bind
# action a menu entry has to dispatch (dispcfg opens with `displays`, wppick
# with `wallpaper`). That mapping is below, and an unmapped panel is a FAILURE
# rather than a skip: a new panel must be either mapped and given an entry, or
# exempted here in writing.
#
# ── What is deliberately NOT an app ─────────────────────────────────────────
#
# EXEMPT carries a reason per line. The bar's clock popup is not something you
# launch; the UI font picker is a control-panel row with no bind at all. Being
# on this list is a decision, and the reason is the point of the list.
#
# Usage: menu_apps.sh [path/to/input.c] [path/to/data]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
src=${1:-$here/../src/input.c}
data=${2:-$here/../data}

[ -f "$src" ] || { echo "  ABORT no input.c at $src"; exit 1; }
[ -d "$data" ] || { echo "  ABORT no data dir at $data"; exit 1; }

fails=0
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }

# panel prefix -> the bind action that opens it
action_for() {
    case $1 in
        dispcfg)   echo displays   ;;
        wppick)    echo wallpaper  ;;
        curpick)   echo cursor     ;;
        eq)        echo equalizer  ;;
        theme)     echo theme      ;;
        calendar)  echo calendar   ;;
        ctlpanel)  echo control    ;;
        sound)     echo sounds     ;;
        *)         echo "$1"       ;;   # emoji, calc, crop, news, saver, …
    esac
}

# Not an app, with the reason. Anything here needs no menu entry.
exempt_reason() {
    case $1 in
        clock)    echo "the bar's clock popup — you click the clock, you do not launch it" ;;
        fontpick) echo "a Control panel ▸ Appearance row with no bind at all" ;;
        crop)     echo "has an entry under its own name (Image Cropper)" ;;
        *)        echo "" ;;
    esac
}

# The entry that opens ACTION, if any: an Exec line that dispatches it, or —
# for the two panels reached by their own program — the program itself.
entry_for() {
    grep -ls -e "^Exec=synctl dispatch $1\$" -e "^Exec=synctl dispatch $1 " \
             "$data"/*.desktop 2>/dev/null | head -1
}

panels=$(sed -n '/^#define SYN_PANEL_LIST/,/^$/p' "$src" |
         sed -n 's/^ *X(\([a-z_]*\), *[a-z_]*).*/\1/p')

[ -n "$panels" ] || { echo "  ABORT could not read SYN_PANEL_LIST from $src"; exit 1; }

n=0
for p in $panels; do
    n=$((n + 1))
    reason=$(exempt_reason "$p")
    if [ -n "$reason" ]; then
        ok "$p is deliberately not an app — $reason"
        continue
    fi

    a=$(action_for "$p")
    f=$(entry_for "$a")
    if [ -n "$f" ]; then
        ok "$p is in the menus ($(basename "$f"))"
    else
        bad "$p ($a) is keybind-only — add data/synui-$a.desktop, or exempt it in this test with a reason"
    fi
done

# The two app-like things that are NOT panels. Named here because SYN_PANEL_LIST
# cannot know about them: one is a program, one hands off to a terminal.
[ -f "$data/synui-screenshot.desktop" ] && ok "the screenshot tool is in the menus" \
                                        || bad "no menu entry for synui-screenshot"
[ -n "$(entry_for network)" ] && ok "network is in the menus" \
                              || bad "network is keybind-only"

# ⚠ AND EVERY DISPATCH MUST NAME AN ACTION THAT EXISTS. This is the half that
# found two dead buttons on 2026-08-20: synui-ctlpanel.desktop dispatched
# `ctlpanel` and synui-sound.desktop dispatched `sound`, but the actions are
# `control` and `sounds` — an entry is named for the file it lives in, an action
# for what it opens, and both had shipped since pkgrel 392. An unknown action
# reaches synui_binding_execute's last else, logs one line and returns false, so
# the menu item highlighted and did nothing. Nothing else in the tree compares
# these two strings.
for f in "$data"/synui-*.desktop; do
    for a in $(sed -n 's/^Exec=synctl dispatch \([a-z_]*\).*/\1/p' "$f"); do
        if grep -q "strcmp(action, \"$a\")" "$src"; then
            ok "$(basename "$f") dispatches $a, which exists"
        else
            bad "$(basename "$f") dispatches '$a' — no such action in input.c (DEAD BUTTON)"
        fi
    done
done

# ⚠ AND THEY MUST ALL BE HIDDEN OUTSIDE SYNUI. Every one of these dispatches to
# a socket that only synui is listening on, so under KDE or GNOME an entry that
# showed would be a menu item that did nothing — the exact complaint that put
# the first eleven entries here.
for f in "$data"/synui-*.desktop; do
    grep -q '^OnlyShowIn=synui;SynapseOS;$' "$f" || {
        bad "$(basename "$f") does not carry OnlyShowIn=synui;SynapseOS;"
    }
done

printf '  --    %d panel(s) checked\n' "$n"

if [ "$fails" -gt 0 ]; then
    printf 'menu_apps: %d failure(s)\n' "$fails" >&2
    exit 1
fi
printf 'menu_apps: ok\n'
