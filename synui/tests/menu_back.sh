#!/usr/bin/env bash
# menu_back.sh — walking back out of a category keeps your place
#
# The start menu rebuilds its row list whenever the page changes, and the
# rebuild put the selection back on row 0. So browsing with the arrows —
# Right into a category, Left back out — dropped the highlight at the TOP of
# the menu every time, which is the one moment you most want it kept.
#
# The fix records the page being left in `list.returningTo` and the rebuild
# selects the row that leads back into it, matched BY PAGE ID rather than by a
# remembered index: the root list is not the same list it was, and an index
# into it means something different once a search or a rebuild has moved
# things around.
#
# This is a TEXT check, and it is one deliberately. The start menu is a
# quickshell panel that talks to the running compositor — loading it to press
# arrow keys at it would drive the live session, which no test in this
# repository is allowed to do. What can be checked without running it is the
# shape that the bug was: a back path that clears MenuState.page WITHOUT first
# recording it, and a rebuild that resets to zero without consulting the
# record.
#
# Usage: menu_back.sh [path/to/StartMenu.qml]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
qml=${1:-$here/../quickshell/StartMenu.qml}

[ -f "$qml" ] || { echo "  ABORT no StartMenu.qml at $qml"; exit 1; }

fails=0

# 1. Every place that leaves a page records which one it left.
#
# Scoped to the lines that clear it: `MenuState.page = ""` is the whole of
# "go back", and each one must be preceded by the record within a couple of
# lines. grep -B is the readable way to say "just above".
clears=$(grep -c 'MenuState\.page[[:space:]]*=[[:space:]]*""' "$qml")
if [ "$clears" -lt 2 ]; then
    echo "  ABORT only $clears place(s) clear MenuState.page; expected Left and Escape"
    exit 1
fi
recorded=$(grep -B 2 'MenuState\.page[[:space:]]*=[[:space:]]*""' "$qml" |
           grep -c 'returningTo[[:space:]]*=[[:space:]]*MenuState\.page')
if [ "$recorded" -lt "$clears" ]; then
    echo "  FAIL $clears back path(s) clear MenuState.page but only $recorded record it first"
    echo "       a back that does not record the page it left drops the"
    echo "       selection at the top of the menu — the bug this guards"
    fails=$((fails + 1))
fi

# 2. The rebuild consults the record BEFORE falling back to row 0.
#
# Order matters and is the whole of the fix: a rebuild that assigns
# selected = 0 first and then looks at returningTo would flash to the top and
# stay there, because the lookup returns early only if it runs first.
body=$(sed -n '/function onRowsChanged/,/^        }/p' "$qml")
if [ -z "$body" ]; then
    echo "  ABORT could not find onRowsChanged in $qml"
    exit 1
fi
look=$(printf '%s\n' "$body" | grep -n 'returningTo' | head -1 | cut -d: -f1)
zero=$(printf '%s\n' "$body" | grep -n 'selected[[:space:]]*=[[:space:]]*0' | head -1 | cut -d: -f1)
if [ -z "$look" ] || [ -z "$zero" ]; then
    echo "  FAIL onRowsChanged does not both consult returningTo and reset to 0"
    fails=$((fails + 1))
elif [ "$look" -gt "$zero" ]; then
    echo "  FAIL onRowsChanged resets the selection to 0 (line $zero) before"
    echo "       consulting returningTo (line $look) — the reset wins and the"
    echo "       menu still loses your place"
    fails=$((fails + 1))
fi

# 3. Matched by PAGE ID, not by a remembered index.
#
# An index into the root list is not stable across a rebuild — a search, a
# renamed category or a menu that gained a row all move it — so a fix that
# stored `selected` and put it back would work in testing and put the
# highlight on the wrong row in use.
if ! printf '%s\n' "$body" | grep -q 'kind[[:space:]]*===[[:space:]]*"page"'; then
    echo "  FAIL the rebuild does not find the row back by page id"
    echo "       (a remembered index is not stable across a rebuild)"
    fails=$((fails + 1))
fi

if [ "$fails" -eq 0 ]; then
    echo "  ok  walking back out of a category keeps its row selected"
    exit 0
fi
exit 1
