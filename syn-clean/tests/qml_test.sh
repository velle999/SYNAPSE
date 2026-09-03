#!/usr/bin/env bash
#
# qml_test.sh — the cleanup window, as far as it can be checked without a
# compositor.
#
# ⛔ NO HEADLESS RENDER: quickshell needs a Wayland session, and every way of
# giving it one from a test ends at the developer's live desktop.
#
# SynapseOS Project — GPL-2.0-or-later
set -uo pipefail

# ⛔ THE LOCALE THIS SUITE ASSERTS IN IS PINNED. Every assertion below looks for
# an English phrase, and once syn-clean is installed the binary answers the
# desktop's language — so on a German box they fail for a program that is
# working exactly as intended.
# ⚠ LANGUAGE is UNSET, not set: gettext reads it before LC_ALL, so an ambient
# LANGUAGE=de wins over LC_ALL=C and the pin does nothing.
export LC_ALL=C.UTF-8
unset LANGUAGE


QML=${1:-data/syn-clean.qml}
[ -f "$QML" ] || { echo "no such file: $QML" >&2; exit 1; }

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
chk() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

echo "qml"

# ⛔ Qt 6's LINTER, BY ITS FULL PATH. /usr/bin/qmllint belongs to another
# toolkit, accepts the file, and reports nothing whatever is wrong with it.
LINT=/usr/lib/qt6/bin/qmllint

# ⛔ THESE THREE ARE PROMOTED TO ERRORS. qmllint calls an assignment to a
# property that does not exist a *Warning*, but quickshell REFUSES to load the
# file over it and the window never opens at all — which is exactly how
# syn-vault shipped a dead menu entry past a green suite.
LINTARGS=(--missing-property error --unresolved-type error --unresolved-alias error)

if [ -x "$LINT" ]; then
    errs=$("$LINT" "${LINTARGS[@]}" "$QML" 2>&1 | grep -c '^Error')
    [ "$errs" = 0 ]
    chk "qmllint (Qt 6) reports no errors" $?
    [ "$errs" = 0 ] || "$LINT" "${LINTARGS[@]}" "$QML" 2>&1 | grep '^Error' | head -5
else
    echo "  --    $LINT is not installed"
fi

# ⛔ EVERY NUMBER COMES FROM THE BINARY. A window that summed a directory itself
# would be a second answer to "how big is this", and the wrong one sits above a
# button that cannot be undone.
grep -q '"--rec", "scan"' "$QML"
chk "the sizes come from the binary" $?

! sed 's,//.*,,' "$QML" | grep -qE '\.cache|/tmp|\.local/share/Trash'
chk "…and the window composes no paths of its own" $?

# ⛔ A SCROLLING VIEW SAYS SO.
grep -q 'ScrollBar.vertical' "$QML"
chk "the category list has a scrollbar" $?

# The desktop's font, not this window's idea of one.
grep -q 'font.state' "$QML"
chk "it follows the desktop font" $?

# ⛔ THE COPY-ON-WRITE WARNING IS IN THE WINDOW, not only in --help. Somebody
# deciding whether overwriting is good enough needs it while deciding.
grep -qi 'copy-on-write' "$QML"
chk "the shred page warns about copy-on-write" $?
grep -q 'shredSnaps' "$QML"
chk "…and names snapshots when there are any" $?
# ⚠ A PHRASE, so the string must not be wrapped through the middle of it. That
# is a real constraint on the source and it is stated at the sentence itself: a
# rewrap once reported this line gone while it was sitting right there.
grep -qi 'full-disk encryption' "$QML"
chk "…and says what would actually work" $?

# ⚠ --yes IS REQUIRED FROM A FRONT END, because the binary refuses to read
# silence on a pipe as consent. Without it the window would hang on a question
# nobody can see.
grep -q '"--yes"' "$QML"
chk "the window passes --yes, having asked itself" $?

# A button is its own label.
grep -q 'Overwrite and delete it' "$QML"
chk "the destroy button says what it does" $?

# ⚠ The picked-set is REBUILT, never mutated: assigning into a var object emits
# no change signal, so every binding keeps the old value.
grep -q 'Object.assign({}, root.picked)' "$QML"
chk "the ticked set is rebuilt so bindings see it" $?

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
