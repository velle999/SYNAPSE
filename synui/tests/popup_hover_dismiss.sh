#!/usr/bin/env bash
#
# popup_hover_dismiss.sh — the bar menu and the mixer must not dismiss
# themselves while the pointer is resting ON them.
#
# Both hold a real xdg_popup grab, so a click outside is what normally closes
# them; the Timer is only a backstop for a grab the compositor refused. The bug
# this test pins down is what ARMS that backstop.
#
# ⚠ Qt hands the hover enter/exit pair to exactly ONE item — the topmost under
# the pointer. A MouseArea filling the popup is therefore `exited` the moment
# the pointer reaches a row, a slider or a button INSIDE it, which is the only
# reason anyone opened it. That armed the dismissal timer with the pointer on
# the popup, and it closed a few seconds later under a resting hand. Raising
# the interval (1200/1600 → 8000ms, 0.1.0-485) only made it take longer.
#
# The fix is a HoverHandler, which reports the whole SUBTREE, plus a guard on
# the timer's FIRING and not merely on its restarts. A real pointer is driven
# through the identical hover graph by synfiles/tests/ctx_flyout_hover.qml —
# this test is the cheap check that the wiring here has not gone back.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

QS=${1:-quickshell}
pass=0 fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }

echo "popup hover dismissal — $QS"

check_popup() {
    local file=$1 name=$2 hover=$3
    if [ ! -f "$file" ]; then bad "$name: $file is missing"; return; fi

    grep -q "HoverHandler { id: $hover }" "$file" \
        && ok "$name tracks hover across its whole subtree" \
        || bad "$name lost its HoverHandler — a row inside it reads as a leave"

    # The panel-filling MouseArea, in either of its two shapes.
    if grep -q 'onExited: closeTimer.restart()' "$file"; then
        bad "$name still restarts its dismissal timer from a MouseArea exit"
    else
        ok "$name has no MouseArea deciding when the pointer left"
    fi

    # Per-row `closeTimer.stop()` patches are the tell that something else is
    # arming it wrongly — and they cannot work anyway: the enter of the next
    # row arrives BEFORE the exit of the previous one, so stop-then-restart is
    # the order that actually runs.
    if grep -q 'onEntered: closeTimer.stop()' "$file"; then
        bad "$name carries per-row stop() patches — the panel MouseArea is back"
    else
        ok "$name needs no per-row stop() patches"
    fi

    grep -q "$hover.hovered" "$file" \
        && ok "$name re-checks the pointer before it closes" \
        || bad "$name closes on a timer that never asks where the pointer is"
}

check_popup "$QS/components/BarMenu.qml" "the bar menu" menuHover
check_popup "$QS/components/Mixer.qml"   "the mixer"    mixerHover

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
