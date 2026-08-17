#!/usr/bin/env bash
# candy_test.sh — ILoveCandy reaches the installed system's pacman.conf
#
# The option itself is cosmetic; the way it is written is not. It is inserted by
# sed against an anchor in a file syn-install does not own — /etc/pacman.conf
# comes from the `pacman` package and upstream rewords it — and an anchor that
# has moved makes sed match nothing, change nothing, and exit 0. The result is a
# default that silently did not apply, on every install, until somebody happens
# to look.
#
# So pacman_conf_enable_candy() reports whether the line is actually there
# afterwards rather than whether sed ran, and this asserts that against real
# fixtures: the stock Arch file, the fallback file with no '# Misc options'
# comment, a file where neither anchor exists, and a re-run.
#
# Sources syn-install.sh with SYN_INSTALL_SOURCE_ONLY=1, which stops it before
# it touches anything.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
export SYN_INSTALL_SOURCE_ONLY=1
# shellcheck source=/dev/null
. "$here/../syn-install.sh"

TMP=$(mktemp -d /tmp/candy.XXXXXX)
trap 'rm -rf "$TMP"' INT TERM EXIT

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

# The stock Arch [options] block, trimmed to the lines that matter here. The
# '# Misc options' comment and the keys under it are verbatim upstream.
stock() {
    cat > "$1" <<'EOF'
[options]
#RootDir     = /
HoldPkg     = pacman glibc
Architecture = auto
# Misc options
#UseSyslog
#Color
#NoProgressBar
CheckSpace
#VerbosePkgLists
ParallelDownloads = 5

[core]
Include = /etc/pacman.d/mirrorlist
EOF
}

# ── the stock file ───────────────────────────────────────────
f="$TMP/stock.conf"; stock "$f"
pacman_conf_enable_candy "$f"; rc=$?
check "the stock pacman.conf takes ILoveCandy" "0" "$rc"
check "…exactly once" "1" "$(grep -c '^ILoveCandy' "$f")"
# ⚠ WHERE it lands is part of the contract: under the Misc options heading, with
# Color and NoProgressBar, which is where a user goes looking for it. A line
# appended to the end of the file would pass every other check here — and would
# land inside whatever repo section happens to be last, where pacman reads it as
# a repo directive rather than a global one.
check "…under the Misc options heading" "# Misc options" \
    "$(grep -B1 '^ILoveCandy' "$f" | head -1)"
check "…and the section it lands in is still [options]" "[options]" \
    "$(sed -n '1,/^ILoveCandy$/p' "$f" | grep '^\[' | tail -1)"

# ── it is idempotent ─────────────────────────────────────────
# The installer is re-runnable and a user can run it twice against the same
# target. Two ILoveCandy lines are harmless to pacman but are the visible trace
# of a guard that is not there.
pacman_conf_enable_candy "$f"; rc=$?
check "a second run succeeds" "0" "$rc"
check "…and does not write a second copy" "1" "$(grep -c '^ILoveCandy' "$f")"

# ── the fallback anchor ──────────────────────────────────────
# A pacman.conf with no '# Misc options' comment — upstream has reworded that
# block before, and this is the branch that catches it having done so again.
f="$TMP/nomisc.conf"
cat > "$f" <<'EOF'
[options]
HoldPkg     = pacman glibc
Architecture = auto

[core]
Include = /etc/pacman.d/mirrorlist
EOF
pacman_conf_enable_candy "$f"; rc=$?
check "a pacman.conf with no Misc block still takes it" "0" "$rc"
check "…falling back to the [options] header" "[options]" \
    "$(grep -B1 '^ILoveCandy' "$f" | head -1)"

# ── neither anchor ───────────────────────────────────────────
# ⚠ THE ONE THAT MATTERS. A file with no [options] section at all is what an
# upstream rewrite looks like from here, and the required behaviour is to REPORT
# it rather than to carry on having written nothing. If this check ever fails,
# the installer's warn() is unreachable and the default has been silently absent.
f="$TMP/alien.conf"
printf '[core]\nInclude = /etc/pacman.d/mirrorlist\n' > "$f"
pacman_conf_enable_candy "$f"; rc=$?
check "a pacman.conf with no [options] at all REPORTS failure" "1" "$rc"
check "…and is left alone" "0" "$(grep -c '^ILoveCandy' "$f")"

# ── a missing file ───────────────────────────────────────────
pacman_conf_enable_candy "$TMP/does-not-exist.conf"; rc=$?
check "a missing pacman.conf reports failure rather than creating one" "1" "$rc"
check "…and creates nothing" "no" \
    "$([ -e "$TMP/does-not-exist.conf" ] && echo yes || echo no)"

# ── pacman itself agrees, where pacman-conf is available ─────
# The whole point is that pacman reads it back. pacman-conf is the parser pacman
# uses, and synpkg delegates to it too (synpkg/src/pconf.c), so this is the same
# answer every consumer on the system gets. Skipped rather than failed off-box.
if command -v pacman-conf >/dev/null 2>&1; then
    f="$TMP/stock.conf"
    check "pacman-conf reads ILoveCandy back out of it" "ILoveCandy" \
        "$(pacman-conf --config "$f" ILoveCandy 2>/dev/null)"
    check "…without disturbing the repo list" "core" \
        "$(pacman-conf --config "$f" --repo-list 2>/dev/null | tr '\n' ' ' | tr -d ' ')"
else
    printf '  skip  pacman-conf not installed (not an Arch host)\n'
fi

echo
if [ "$fails" -gt 0 ]; then
    echo "$fails check(s) FAILED"
    exit 1
fi
echo "all checks passed"
