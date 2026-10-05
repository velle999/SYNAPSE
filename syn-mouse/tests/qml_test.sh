#!/usr/bin/env bash
#
# qml_test.sh — the Mouse Buttons window, as far as it can be checked without
# a compositor.
#
# ⛔ NO HEADLESS RENDER: quickshell needs a Wayland session, and every way of
# giving it one from a test ends at the developer's live desktop.
#
# SynapseOS Project — GPL-2.0-or-later
set -uo pipefail
export LC_ALL=C.UTF-8
unset LANGUAGE

# ⚠ NEVER `cmd | grep -q` UNDER pipefail. grep -q exits at its first match, the
# writer takes SIGPIPE on its next line, and the pipeline fails with 141 on
# output that was right — a race, so it passes on a fast build and fails under ASan.
# q reads everything. The same goes for `| head`.
q() { grep "$@" >/dev/null; }

QML=${1:-data/syn-mouse.qml}
[ -f "$QML" ] || { echo "no such file: $QML" >&2; exit 1; }

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
chk() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

echo "qml"

# ⛔ Qt 6's LINTER, BY ITS FULL PATH. /usr/bin/qmllint belongs to another
# toolkit, accepts the file, and reports nothing whatever is wrong with it.
LINT=/usr/lib/qt6/bin/qmllint
# ⛔ PROMOTED TO ERRORS: quickshell REFUSES to load a file that assigns a
# property that does not exist, and the window never opens at all.
LINTARGS=(--missing-property error --unresolved-type error --unresolved-alias error)

if [ -x "$LINT" ]; then
    out=$("$LINT" "${LINTARGS[@]}" "$QML" 2>&1)
    errs=$(printf '%s\n' "$out" | grep -c '^Error')
    [ "$errs" = 0 ]
    chk "qmllint (Qt 6) reports no errors" $?
    [ "$errs" = 0 ] || printf '%s\n' "$out" | grep '^Error' | sed -n 1,5p
    # ⚠ ShellRoot HAS PROPERTIES OF ITS OWN — `settings` among them — and a
    # window property of the same name shadows quickshell's.
    ! printf '%s\n' "$out" | q 'property-override'
    chk "no property shadows one of ShellRoot's" $?
else
    echo "  --    $LINT is not installed"
fi

code() { sed 's,//.*,,' "$QML"; }

# ⛔ EVERY CHANGE IS THE BINARY'S. The window never touches the file.
for c in profiles status devices apps keys buttons; do
    grep -q "\"--rec\", \"$c\"" "$QML" || { bad "reads \`syn-mouse --rec $c\`"; continue; }
done
ok "everything it shows comes from \`syn-mouse --rec\`"
! code | q 'bindings.conf'
chk "…and it never opens the bindings file" $?
grep -q '"bind", root.cur, root.sel' "$QML" && grep -q '"unbind", root.cur' "$QML"
chk "a binding is saved with \`syn-mouse bind\`" $?

# ⚠ A KEY IS CAPTURED BY POSITION: nativeScanCode is evdev + 8 on Wayland,
# and Qt's own key code is the character the layout makes.
grep -q 'nativeScanCode - 8' "$QML"
chk "keys are captured as evdev codes, not characters" $?
# …and named by the binary's own table.
grep -q 'root.keyNames\[' "$QML"
chk "…and named from \`syn-mouse --rec keys\`, not a table of its own" $?

# ⛔ A SCROLLING VIEW SAYS SO.
[ "$(grep -c 'ScrollBar.vertical' "$QML")" -ge 2 ]
chk "the page and the window list both have scrollbars" $?

grep -q 'font.state' "$QML"
chk "it follows the desktop font" $?

# A button is its own label.
grep -q 'Save for %1' "$QML"
chk "the save button names the button it changes" $?
grep -q 'Stop everything that is on' "$QML"
chk "the stop button says what it stops" $?
# ⚠ A BUTTON THE MOUSE'S OWN MEMORY SENDS AS A KEY has no fixed name; it is
# listed from the keys the daemon has heard, and saved as key:<name>.
grep -q 'f\[0\] === "sent"' "$QML" && grep -q 'Sends the key %1' "$QML"
chk "a key the mouse sends gets a row of its own, from status" $?

# ⚠ An object property is REBUILT, never mutated in place: assigning into a
# var object emits no change signal, so every binding keeps the old value.
! code | q -E 'root\.(binds|conf|st|keyNames)\[[^]]+\] *='
chk "no record object is mutated in place" $?

# ⛔ THE LEFT-BUTTON RULE IS THE BINARY'S. The window explains it; it does not
# re-implement it, so it cannot be argued with from here.
! code | q -E 'sel *=== *"left".*(return|disabled)'
chk "the window does not decide which buttons may be bound" $?

# ⚠ qsTr() translates nothing in quickshell 0.3.1.
! code | q 'qsTr('
chk "no qsTr() — I18n.tr() is the one that works" $?

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
