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

# ⛔ AND NO CALENDAR ARITHMETIC EITHER. The grid arrives as records carrying the
# row and column each day belongs in — see month.h. A window that worked out
# which weekday a month opens on would be the second answer to that question,
# and the one nobody fixes when February is wrong.
grep -q '"--rec", "month"' "$QML"
check "the month grid comes from the binary too" $?

! grep -qE 'getDay\(\)|daysInMonth|% *4 *===? *0|isLeap' "$QML"
check "…so the window works out no weekday and no leap year of its own" $?

grep -q 'model: \["Week", "Month"\]' "$QML"
check "both views are reachable, each button naming the view it gives you" $?

# ⛔ AND NOT WHICH DAY THE WEEK STARTS ON EITHER. That is a setting the binary
# owns; a fixed heading list here is right for one value of it and a day out for
# the other, which draws a whole month under the wrong labels.
! grep -q '\["Mon", "Tue", "Wed"' "$QML"
check "…and the weekday headings are not a fixed list" $?

grep -q 'names\[root.monthCells\[i\].dow\]' "$QML"
check "…they are read off the records, column by column" $?

# ⛔ A CELL THAT CANNOT SHOW THEM ALL SAYS SO. Silently drawing the first three
# of five is a calendar that hides two appointments.
grep -q '"+" + (cell.shown.length - cell.nshow) + " more"' "$QML"
check "…and a full day says how many it is not showing" $?

# ⛔ SETUP THAT LOOKS FINISHED MUST HAVE FETCHED SOMETHING. A first discovery
# switches its calendars on, so the window syncs straight after rather than
# leaving a ticked list above an empty month.
grep -q 'root.sync()' "$QML"
check "finding calendars is followed by a sync" $?

grep -q 'root.bin, "discover"' "$QML"
check "…and the window is what asks for them, not a terminal" $?

# ⛔ AND THE WINDOW CAN MAKE AN EVENT. Everything else here reads; without this
# the calendar is a viewer for one somebody else has to write.
grep -q 'text: "New event"' "$QML"
check "there is a button that makes an event, saying so" $?

grep -q '"new", evTitle.text.trim()' "$QML"
check "…which goes through the binary, like everything else" $?

grep -q 'onClicked: root.evEdit(modelData)' "$QML"
check "…an event in the agenda opens for editing" $?

grep -q 'root.bin, "delete", root.evUid' "$QML"
check "…and can be deleted" $?

# ⛔ AND A DAY IN THE GRID IS A WAY IN. Pointing at the day you mean and getting
# a form for it is the obvious gesture; making people find a button in the
# header and then retype the date is not.
grep -q 'onClicked: root.evNewOn(cell.cellData.date)' "$QML"
check "clicking a day makes an event on that day" $?

grep -q 'onClicked: root.evEdit(cell.shown\[index\])' "$QML"
check "…and clicking a title on that day opens that event instead" $?

# An empty calendar is where somebody most wants to add something, and it had
# nothing to press.
grep -q '"Nothing in the next " + root.days' "$QML"
check "…the empty week still says it is empty" $?

grep -A32 'Nothing in the next ' "$QML" | grep -q 'onClicked: root.evNew()'
check "…and offers a way to fill it" $?

# ⛔ SAVED HERE IS NOT SAVED ANYWHERE ELSE. A window that says "Saved" and
# leaves the appointment on one machine is telling somebody their meeting is
# booked when nobody else can see it.
grep -A6 'root.status = "Saved' "$QML" | grep -q 'root.sync()'
check "…and saving syncs, rather than stopping at this machine" $?

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

# ── signing in, from the window ─────────────────────────────────────────────

# ⛔ THE WINDOW SIGNS IN. This pane once said "add one from a terminal", which
# made the GUI a viewer for something only the CLI could set up.
! grep -q 'Add one from a terminal' "$QML"
check "the empty state does not send the user to a terminal" $?

grep -q 'text: "Add account"' "$QML"
check "…it offers a button that says what it does" $?

grep -q 'root.signIn(modelData.name, modelData.kind)' "$QML"
check "…and an account with no token can be signed in from its own row" $?

# ⚠ isatty IS THE WRONG QUESTION FOR A WINDOW. Without --browser the sign-in
# prints a URL into a pipe nobody reads and then times out looking like failure.
grep -q '"login", name, "--browser"' "$QML"
check "the OAuth sign-in tells syn-cal to open a browser" $?

grep -q -- '--browser' "$(dirname "$0")/../src/main.c"
check "…and the binary accepts that flag" $?

# ⛔ A CREDENTIAL NEVER GOES IN argv — /proc/<pid>/cmdline is world-readable.
# syn-cal reads a password from stdin when stdin is not a terminal, so the
# shell pipes it in from the environment.
! grep -qE '"login".*authPass\.text|authPass\.text.*"login"' "$QML"
check "the CalDAV password is never passed as an argument" $?

grep -q 'SYNCAL_PW' "$QML"
check "…it crosses in the environment instead" $?

# ⚠ AND IT IS CLEARED AFTERWARDS. loginProc outlives the panel, so a password
# left on its environment would be handed to the next command this window runs.
grep -q 'loginProc.environment = ({})' "$QML"
check "…and is cleared from the Process when the child exits" $?

# Same no-op trap as the sync button.
grep -q 'loginProc.running = false' "$QML"
check "the login Process is stopped before it is started" $?

# ⛔ A HEADER ROW MUST NOT ASSUME THE WIDTH OF ITS OWN TITLE. This was a Row
# with a `parent.width - ui(340)` spacer; "The next 7 days" is wider than 340
# allowed for, so the row overflowed and clipped the "Next" button off the
# right-hand edge of the window.
! grep -q 'width: parent.width - root.ui(340)' "$QML"
check "the agenda header does not size itself from a hardcoded title width" $?

grep -q 'right: nav.left' "$QML"
check "…the title yields to the buttons instead" $?

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
