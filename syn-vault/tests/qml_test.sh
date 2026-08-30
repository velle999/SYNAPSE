#!/usr/bin/env bash
#
# qml_test.sh — the vault window, as far as it can be checked without a
# compositor. Qt 6's linter, plus the contracts a linter cannot see.
#
# ⛔ NO HEADLESS RENDER, for the reason syn-cal's suite gives at length:
# quickshell needs a Wayland session, and every way of giving it one from a test
# ends at the developer's live desktop.
#
# SynapseOS Project — GPL-2.0-or-later
set -uo pipefail

QML=${1:-data/syn-vault.qml}
[ -f "$QML" ] || { echo "no such file: $QML" >&2; exit 1; }

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
chk() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

echo "qml"

# ⛔ Qt 6's LINTER, BY ITS FULL PATH. /usr/bin/qmllint belongs to another
# toolkit, accepts the file, and reports nothing whatever is wrong with it.
LINT=/usr/lib/qt6/bin/qmllint
if [ -x "$LINT" ]; then
    errs=$("$LINT" "$QML" 2>&1 | grep -c '^Error')
    [ "$errs" = 0 ]
    chk "qmllint (Qt 6) reports no errors" $?
    [ "$errs" = 0 ] || "$LINT" "$QML" 2>&1 | grep '^Error' | head -5
else
    echo "  --    $LINT is not installed"
fi

# ⛔ THE PASSWORD GOES ON STDIN, NEVER IN argv. /proc/<pid>/cmdline is
# world-readable: a password on a command line is visible to every process on
# the machine for as long as the command runs.
grep -q 'actProc.write(pwField.text' "$QML"
chk "the password is written to the process, not passed to it" $?

! grep -qE 'command: \[.*pwField' "$QML"
chk "…and never appears in a command array" $?

grep -q 'echoMode: TextInput.Password' "$QML"
chk "…the field does not echo it" $?

grep -q 'Qt.ImhSensitiveData' "$QML"
chk "…and keeps out of predictive text and the clipboard" $?

# ⛔ A MISTYPED PASSWORD AT CREATION IS UNRECOVERABLE. The binary confirms only
# when it has a terminal; piped input is this window, so this window confirms.
grep -q 'pwField.text !== pwAgain.text' "$QML"
chk "creating a vault confirms the password" $?

grep -q 'second copy of this password' "$QML"
chk "…and says there is no way back before they choose one" $?

# The window decides nothing about encryption on its own.
grep -q '"--rec", "list"' "$QML"
chk "the vault list comes from the binary" $?

# ⚠ COMMENTS STRIPPED FIRST. The file's own header says it contains no
# gocryptfs, and a naive grep matches that sentence and fails the test the
# sentence is describing.
! sed 's,//.*,,' "$QML" | grep -qiE 'gocryptfs|fusermount|/proc/self/mounts'
chk "…and the window contains no mounting logic of its own" $?

# ⛔ THE STRAY FLAG IS NOT COSMETIC: it means files sit unencrypted in a closed
# vault's mountpoint, which is the one state where somebody believes they are
# protected and they are not.
grep -q 'NOT encrypted' "$QML"
chk "unencrypted files in a closed vault are called out" $?

# ⛔ A BUTTON IS ITS OWN LABEL, not a padlock glyph needing a tooltip to say
# which way it is about to go.
grep -q '"Lock" : "Unlock"' "$QML"
chk "the lock button says which way it goes" $?

echo "$pass/$((pass + fail)) passed"
[ "$fail" = 0 ]
