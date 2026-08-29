#!/usr/bin/env bash
#
# qml_test.sh — the window, as far as it can be checked without a compositor.
#
# ⛔ THERE IS NO HEADLESS RENDER HERE, DELIBERATELY. quickshell needs a Wayland
# session, and the ways of giving it one from a test all end at the LIVE
# desktop: a bare `cage` takes the real GPU, and a client with WAYLAND_DISPLAY
# unset still falls back to the running compositor's socket. A test suite that
# can paint on the user's screen is worse than one that checks less.
#
# So this is what the rest of the suite does for its windows: Qt's own linter,
# plus assertions on the contracts a linter cannot see.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

QML=${1:-data/syn-cal.qml}
[ -f "$QML" ] || { echo "no such file: $QML" >&2; exit 1; }

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
check() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# ⛔ Qt 6's LINTER, BY ITS FULL PATH. /usr/bin/qmllint on this system is NOT
# Qt 6's — it belongs to another toolkit, accepts the file, and reports nothing
# whatever is wrong with it. A green run from the wrong binary is worse than no
# run at all.
LINT=/usr/lib/qt6/bin/qmllint
if [ -x "$LINT" ]; then
    errs=$("$LINT" "$QML" 2>&1 | grep -c '^Error')
    [ "$errs" = 0 ]
    check "qmllint (Qt 6) reports no errors" $?
    [ "$errs" = 0 ] || "$LINT" "$QML" 2>&1 | grep '^Error' | head -5
else
    echo "  skip  $LINT is not installed"
fi

# ── contracts a linter cannot see ───────────────────────────────────────────

# ⛔ EVERY FIELD IS DECODED AT THE PARSE. The alternative is a list of "fields
# that need it", which drifts — and the day it drifts a tab inside a meeting
# title shifts every column of a row.
# ⚠ EVERY USE IS CHECKED, NOT A COUNT. A threshold passes as soon as enough
# fields are decoded, which is not the rule — the rule is that no field is read
# raw. Three forms are legitimate: root.disp(f[n]) for text, parseInt(f[n]) for
# a number, and f[n] === "1" for a flag. Anything else is a field being trusted.
raw=$(python3 - "$QML" <<'PY'
import re, sys
src = open(sys.argv[1], encoding="utf-8").read()
bad = []
for m in re.finditer(r'f\[(\d+)\]', src):
    before = src[max(0, m.start() - 14):m.start()]
    after = src[m.end():m.end() + 12]
    if before.endswith("root.disp(") or before.endswith("parseInt("):
        continue
    if re.match(r'\s*===\s*"1"', after):
        continue
    line = src[:m.start()].count("\n") + 1
    bad.append(f"{line}:{m.group(0)}")
print(" ".join(bad))
PY
)
[ -z "$raw" ]
check "no field from --rec is read without being decoded${raw:+ (raw: $raw)}" $?

# The binary does the work. A window that expanded its own recurrence rules
# would get the clock change wrong in a way the CLI does not.
grep -q '"--rec", "agenda"' "$QML"
check "the agenda comes from the binary, not from QML date arithmetic" $?

! grep -qi 'RRULE\|FREQ=\|icaltime' "$QML"
check "…and the window contains no recurrence logic of its own" $?

# ⛔ A BUTTON IS ITS OWN LABEL. Not an icon that needs a tooltip to say what it
# does; only KEYS follow a setting.
grep -q 'text: root.busy ? "Syncing…" : "Sync now"' "$QML"
check "the sync button carries its own words" $?

# ⛔ `running = true` ON AN ALREADY-RUNNING Process IS A SILENT NO-OP. Two
# clicks would drop the second, and the button looks broken exactly when
# somebody presses it twice because it looked broken.
grep -q 'syncProc.running = false' "$QML"
check "the sync Process is stopped before it is started" $?

grep -q 'if (root.busy) return' "$QML"
check "…and a second click while syncing is refused outright" $?

# ⛔ EVERY SCROLLING VIEW SHOWS A SCROLLBAR, and a capped height needs clip too
# or the list draws over what is underneath it.
n_views=$(grep -c 'ListView {' "$QML")
n_bars=$(grep -c 'ScrollBar.vertical: SynScrollBar' "$QML")
[ "$n_views" -gt 0 ] && [ "$n_views" = "$n_bars" ]
check "every ListView has a scrollbar ($n_bars of $n_views)" $?

[ "$(grep -c 'clip: true' "$QML")" -ge "$n_views" ]
check "…and clips, so a long list cannot draw over the buttons below it" $?

# The day heading compares FORMATTED DATES, not timestamps: two events in the
# same local day can be more than 86400 seconds apart across a clock change.
grep -q 'Qt.formatDate(new Date(root.events\[index - 1\].start), "yyyy-MM-dd")' "$QML"
check "day grouping compares local dates, not a 24-hour arithmetic" $?

# The window names itself, or the dock cannot find its .desktop and shows a
# generic icon with no "New Window".
grep -q 'QS_APP_ID' "$(dirname "$0")/../src/main.c"
check "the window sets its own Wayland app_id" $?

d="$(dirname "$0")/../data/syn-cal.desktop"
grep -q '^StartupWMClass=syn-cal$' "$d"
check "…and the .desktop names the same class back" $?
grep -q '^Exec=syn-cal gui$' "$d"
check "…and launches the window, not the bare binary" $?

echo
echo "$pass/$((pass+fail)) passed"
exit $([ "$fail" = 0 ] && echo 0 || echo 1)
