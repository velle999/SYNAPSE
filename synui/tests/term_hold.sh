#!/bin/sh
# term_hold.sh — every window synui opens to run ONE command, and the two
# incompatible spellings of the flag that keeps it open.
#
# ⚠ THE TRAP THIS EXISTS FOR. Three terminals have --hold and they do not take
# the command the same way:
#
#   syntty --hold -e CMD...     the command comes after -e
#   kitty  --hold    CMD...     trailing arguments
#   foot   --hold -e CMD...     either
#
# Hand syntty the trailing form and it reads the first word as a SUBCOMMAND and
# dies on the next argument with "unknown option". That is not a crash anybody
# sees: the row was clicked, no window opened, and there is nothing on screen
# or in the journal to say why. The same shape as every other silent-launch bug
# on this system — a failed exec has no user-visible symptom at all.
#
# So: source-only, no compositor, no QML engine. It reads the two files that
# launch terminals and asserts the pairing.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

QML=${1:?usage: term_hold.sh /path/to/StartMenu.qml /path/to/ai_interface.c}
AIC=${2:?}

fails=0
say_fail() { echo "  FAIL $*"; fails=$((fails + 1)); }

# ── the start menu's rows ───────────────────────────────────────────────────
#
# Every argv that holds a command open must be syntty, and every syntty --hold
# must be followed by -e. Written as two greps rather than one so a failure
# says WHICH of the two mistakes was made.
if grep -n '"kitty", *"--hold"' "$QML" >/dev/null 2>&1; then
    say_fail "StartMenu.qml still launches kitty --hold:"
    grep -n '"kitty", *"--hold"' "$QML" | sed 's/^/       /'
    echo "       syntty has had --hold since 0.1.0-27; use [\"syntty\", \"--hold\", \"-e\", …]"
fi

bad=$(grep -n '"syntty", *"--hold"' "$QML" | grep -v '"syntty", *"--hold", *"-e"')
if [ -n "$bad" ]; then
    say_fail "syntty --hold without -e — the command would be read as a subcommand:"
    printf '%s\n' "$bad" | sed 's/^/       /'
fi

held=$(grep -c '"--hold"' "$QML")
if [ "$held" -eq 0 ]; then
    say_fail "no row in StartMenu.qml holds its window open any more"
    echo "       System Status and Update System both exist to leave output on screen"
fi

# ── the command bar's terminal ──────────────────────────────────────────────
#
# ⚠ ORDER MATTERS HERE IN A WAY IT DOES NOT ABOVE. This is a chain of execlp()
# calls, and execlp only RETURNS when the exec fails — so a syntty that starts
# and then dies at argument parsing never reaches the kitty line. The first
# entry has to be both present and correctly spelled or there is no fallback at
# all.
# ⚠ Scoped to cmdbar_launch_term, not to the file. ai_interface.c execs other
# things too — synsh's intent check is the first execlp in it — and a test that
# took "the first execlp in the file" was reading a line that has nothing to do
# with terminals. It passed or failed on whichever unrelated exec happened to
# be highest up.
first=$(sed -n '/^static bool cmdbar_launch_term/,/^}/p' "$AIC" |
        grep -n 'execlp("' | head -1)
case "$first" in
    *'execlp("syntty", "syntty", "--hold", "-e",'*) ;;
    "") say_fail "the command bar no longer launches a terminal at all" ;;
    *)  say_fail "the command bar's first terminal is not syntty --hold -e:"
        printf '       %s\n' "$first" ;;
esac

if ! grep -q 'execlp("kitty"' "$AIC"; then
    say_fail "the command bar has no fallback behind syntty"
    echo "       an install that predates syntty, or one where it failed to build,"
    echo "       would get no window and no message"
fi

if [ "$fails" -ne 0 ]; then
    echo "term_hold: $fails problem(s)"
    exit 1
fi

echo "term_hold: ok (${held} held row(s) on the start menu, command bar chain intact)"
