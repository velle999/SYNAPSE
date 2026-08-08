#!/usr/bin/env bash
# menu_cats.sh — the start menu's Settings page lists EVERY control-panel category
#
# The page is a hand-written list in QML of things the compositor defines in C,
# and nothing kept the two in step. They drifted: Windows and Input were added
# to ctlpanel.c and never to StartMenu.qml, so two whole categories of settings
# were unreachable from the start menu for as long as nobody noticed.
#
# It drifts SILENTLY, which is the reason this exists. `synctl dispatch control
# <name>` resolves the name through ctlpanel_cat_from_name(), and an unknown
# name is not an error — ctlpanel_show_cat() falls back to toggling the plain
# front door. So a misspelt row still opens the control panel, just on whatever
# category it was last left on, and a MISSING row looks like nothing at all.
#
# One-directional, exactly as readme_binds.sh is: every category in
# ctlpanel_cat_name() must appear as an arg in the Settings page. The page is
# allowed extra rows that are not categories — "Control Panel" (the front door)
# and "Lock Screen" (an action) are both deliberate.
#
# Usage: menu_cats.sh [path/to/ctlpanel.c] [path/to/StartMenu.qml]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
ctl=${1:-$here/../src/ctlpanel.c}
qml=${2:-$here/../quickshell/StartMenu.qml}

[ -f "$ctl" ] || { echo "  ABORT no ctlpanel.c at $ctl"; exit 1; }
[ -f "$qml" ] || { echo "  ABORT no StartMenu.qml at $qml"; exit 1; }

# The categories, straight out of ctlpanel_cat_name()'s switch. Matched on the
# CTL_CAT_ prefix so an unrelated `return "…"` elsewhere in the file cannot be
# mistaken for one, and "?" (the default arm) is excluded.
cats=$(sed -n '/^const char \*ctlpanel_cat_name/,/^}/p' "$ctl" |
       grep -oE 'case CTL_CAT_[A-Z_]+:[[:space:]]*return "[^"]+"' |
       grep -oE 'return "[^"]+"' | grep -oE '"[^"]+"' | tr -d '"')

if [ -z "$cats" ]; then
    echo "  ABORT could not read the category list out of $ctl"
    echo "        (looked for ctlpanel_cat_name's 'case CTL_CAT_…: return \"…\"')"
    exit 1
fi

n=$(printf '%s\n' "$cats" | wc -l)
if [ "$n" -lt 5 ]; then
    echo "  ABORT only $n categories parsed; the check would be vacuous"
    exit 1
fi

# The Settings page's arg strings. Scoped to the p["Settings"] block so a
# `control` row somewhere else on the menu cannot satisfy the check.
page=$(sed -n '/p\["Settings"\][[:space:]]*=[[:space:]]*\[/,/^[[:space:]]*\]/p' "$qml")
if [ -z "$page" ]; then
    echo "  ABORT could not find the p[\"Settings\"] block in $qml"
    exit 1
fi
args=$(printf '%s\n' "$page" | grep -oE 'arg:[[:space:]]*"[^"]+"' |
       grep -oE '"[^"]+"' | tr -d '"')

fails=0
for c in $cats; do
    want=$(printf '%s' "$c" | tr '[:upper:]' '[:lower:]')
    if ! printf '%s\n' "$args" | grep -qx "$want"; then
        echo "  FAIL category '$c' has no row on the start menu's Settings page"
        echo "       add: { kind: \"action\", label: \"$c\", action: \"control\", arg: \"$want\" }"
        fails=$((fails + 1))
    fi
done

# …and the reverse for the args that ARE present: a typo here is invisible at
# run time (it opens the front door), so it has to be caught at build time.
for a in $args; do
    if ! printf '%s\n' "$cats" | tr '[:upper:]' '[:lower:]' | grep -qx "$a"; then
        echo "  FAIL Settings row arg '$a' is not a control-panel category"
        echo "       it would silently open the plain control panel instead"
        fails=$((fails + 1))
    fi
done

if [ "$fails" -ne 0 ]; then
    echo "menu_cats: $fails problem(s)"
    exit 1
fi

echo "menu_cats: ok ($n categories, all on the Settings page)"
