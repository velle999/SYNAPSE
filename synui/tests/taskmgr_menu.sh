#!/usr/bin/env bash
# taskmgr_menu.sh — the right-click menu has ONE path to a signal
#
# The task manager can end a process, and it has always done so behind a
# confirmation line that names the pid it pinned and refuses init and synui
# itself. The right-click menu is a second way to reach that, and the thing
# worth guarding is that it is a second way to reach THE SAME PATH — not a
# second place that calls kill().
#
# A menu item that signalled directly would look identical in use and would
# quietly have none of it: no confirmation, no refusal for pid 1, no pinned
# name, and a SIGKILL one click from a table that re-sorts every second.
#
# The behaviour itself cannot be tested here: the task manager is drawn by the
# compositor, and pressing a mouse button at it means running synui, which no
# test in this repository may do on the live seat. What can be checked is the
# shape — and the shape IS the safety.
#
# Usage: taskmgr_menu.sh [path/to/taskmgr.c] [path/to/render.c]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
tm=${1:-$here/../src/taskmgr.c}
rn=${2:-$here/../src/render.c}

[ -f "$tm" ] || { echo "  ABORT no taskmgr.c at $tm"; exit 1; }
[ -f "$rn" ] || { echo "  ABORT no render.c at $rn"; exit 1; }

fails=0

# 1. Exactly one kill() in the whole file, and it is the one in do_kill.
#
# ⚠ String literals are stripped first. taskmgr.c logs the call it just made
# with wlr_log(..., "kill(%d, %d): %s", ...), and a plain grep counts that
# message as a second call — the needle matching something other than what it
# names, which is how a test reports a problem that is not there.
kills=$(sed 's/"[^"]*"//g' "$tm" | grep -c '\bkill(')
if [ "$kills" -ne 1 ]; then
    echo "  FAIL taskmgr.c calls kill() $kills times; there must be exactly one"
    echo "       every route to a signal goes through taskmgr_do_kill(), which"
    echo "       is what carries the confirmation and the refusals"
    grep -n '\bkill(' "$tm" | sed 's/^/       /'
    fails=$((fails + 1))
fi

# 2. The menu arms the confirmation rather than signalling.
if ! grep -q 'taskmgr_menu_choose' "$tm"; then
    echo "  FAIL no taskmgr_menu_choose in $tm"
    fails=$((fails + 1))
elif ! sed -n '/static void taskmgr_menu_choose/,/^}/p' "$tm" |
        grep -q 'taskmgr_ask_kill'; then
    echo "  FAIL taskmgr_menu_choose does not go through taskmgr_ask_kill"
    echo "       an item that signals directly skips the confirmation, the"
    echo "       pid-1 refusal and the pinned name"
    fails=$((fails + 1))
fi

# 3. The gentle item is first. Force quit sitting at the top would make the
#    destructive choice the one a mouse lands on.
first=$(sed -n '/^const char \*taskmgr_menu_label/,/^}/p' "$tm" |
        grep -oE 'return "[^"]+"' | head -1)
case "$first" in
    *'"End task"'*) ;;
    *) echo "  FAIL the first menu item is $first, not \"End task\""
       echo "       the gentle choice goes first: it is the one a mouse"
       echo "       reaches without travelling"
       fails=$((fails + 1)) ;;
esac

# 4. The renderer registers one hit spot per item, beside the drawing.
#    Private rectangles are the drift hit.c exists to stop.
if ! sed -n '/if (t->menu_open)/,/^    }/p' "$rn" | grep -q 'hit_add_spot'; then
    echo "  FAIL the menu is drawn without registering hit spots"
    echo "       a drawn item and a clickable item that are described"
    echo "       separately are two things that drift apart"
    fails=$((fails + 1))
fi

# 5. Hiding the panel closes the menu. A menu left armed would be up again
#    next time, over a table resampled since — pointing at whatever now sits
#    in that row.
if ! sed -n '/^void taskmgr_hide/,/^}/p' "$tm" | grep -q 'taskmgr_menu_close'; then
    echo "  FAIL taskmgr_hide does not close the menu"
    fails=$((fails + 1))
fi

if [ "$fails" -eq 0 ]; then
    echo "  ok  the right-click menu has one path to a signal"
    exit 0
fi
exit 1
