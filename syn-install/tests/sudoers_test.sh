#!/usr/bin/env bash
# sudoers_test.sh — the drop-ins written and the drop-ins verified are one list
#
# syn-install writes its sudoers drop-ins inside a masked chroot block, then
# hard-verifies them afterwards because that block has failed silently twice.
# But the verify loops carry their OWN hand-written list of names, so they are a
# SECOND ROSTER: a drop-in added to the writer and not to the loops is verified
# by nothing, and an install that lost it still calls itself verified.
#
# That is not hypothetical either. zz-synui-kmod-events was written from
# pkgrel 79 and was on neither loop until 99, so for twenty releases the one
# check standing between a fresh install and a game mode that cannot quiet
# synapse_kmod did not look at it.
#
# So this asserts the two lists against each other by scraping the script,
# rather than restating the names a third time and creating the same problem
# again. It also asserts the ordering rule the zz- prefix exists to satisfy.
#
# Scrapes rather than sources: the verify loops live BELOW the test seam, so
# SYN_INSTALL_SOURCE_ONLY=1 stops before reaching them.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
inst="$here/../syn-install.sh"

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

echo "sudoers_test — written drop-ins vs verified drop-ins"
echo

[ -f "$inst" ] || { echo "  FAIL  cannot find syn-install.sh"; exit 1; }

# Every '> /etc/sudoers.d/NAME' redirect in the chroot block.
written=$(grep -oE '> /etc/sudoers\.d/[a-z-]+' "$inst" | sed 's#.*/##' | sort -u)
# The presence loop is the one naming 'wheel'; the ordering loop is the other.
presence=$(grep -oE '^for f in zz-[^;]*wheel; do' "$inst" \
    | sed -e 's/^for f in //' -e 's/; do$//' | tr ' ' '\n' | sort -u)
ordering=$(grep -oE '^for f in zz-[^;]*; do' "$inst" \
    | grep -v wheel | sed -e 's/^for f in //' -e 's/; do$//' | tr ' ' '\n' | sort -u)

check "the chroot block writes drop-ins at all" "yes" \
    "$([ -n "$written" ] && echo yes || echo no)"
check "the presence loop was found" "yes" \
    "$([ -n "$presence" ] && echo yes || echo no)"
check "the ordering loop was found" "yes" \
    "$([ -n "$ordering" ] && echo yes || echo no)"

# ── the two rosters must agree ─────────────────────────────────────────────
check "every drop-in written is also checked for presence" "" \
    "$(comm -23 <(echo "$written") <(echo "$presence") | tr '\n' ' ' | sed 's/ $//')"
check "the presence loop names nothing that is never written" "" \
    "$(comm -13 <(echo "$written") <(echo "$presence") | tr '\n' ' ' | sed 's/ $//')"

written_zz=$(echo "$written" | grep '^zz-' | sort -u)
check "every zz- drop-in written is also order-checked" "" \
    "$(comm -23 <(echo "$written_zz") <(echo "$ordering") | tr '\n' ' ' | sed 's/ $//')"
check "the ordering loop names nothing that is never written" "" \
    "$(comm -13 <(echo "$written_zz") <(echo "$ordering") | tr '\n' ' ' | sed 's/ $//')"

# ── the rule the zz- prefix exists for ─────────────────────────────────────
# sudo takes the LAST match in sorted lexical order, and /etc/sudoers.d/wheel
# carries the blanket '%wheel ALL=(ALL:ALL) ALL' with no NOPASSWD. A name
# sorting before 'wheel' is silently overridden by it.
bad=""
for f in $written_zz; do
    [ "$(printf '%s\nwheel\n' "$f" | sort | tail -n1)" = "$f" ] || bad="$bad $f"
done
check "every NOPASSWD drop-in sorts after 'wheel'" "" "${bad# }"

# ── mode ───────────────────────────────────────────────────────────────────
# 440 or visudo -c fails the whole ruleset with 'bad permissions'.
missing_chmod=""
for f in $written; do
    grep -qF "chmod 440 /etc/sudoers.d/$f" "$inst" || missing_chmod="$missing_chmod $f"
done
check "every drop-in written is chmod 440" "" "${missing_chmod# }"

# ── the one that started this ──────────────────────────────────────────────
check "zz-synui-kmod-events is written" "1" \
    "$(echo "$written" | grep -c '^zz-synui-kmod-events$')"
check "…and is checked for presence" "1" \
    "$(echo "$presence" | grep -c '^zz-synui-kmod-events$')"
check "…and is order-checked" "1" \
    "$(echo "$ordering" | grep -c '^zz-synui-kmod-events$')"

echo
if [ "$fails" -gt 0 ]; then
    echo "$fails check(s) FAILED"
    exit 1
fi
echo "all checks passed"
