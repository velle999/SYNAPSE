#!/usr/bin/env bash
# faillock_test.sh — the account lockout expires instead of ratcheting
#
# Same shape of risk as candy_test.sh, with a worse failure. Both files belong
# to somebody else — /etc/pam.d/system-auth to pambase, /etc/security/
# faillock.conf to pam — and both get reworded upstream. A sed whose anchor has
# moved matches nothing, changes nothing, and exits 0, so the default silently
# does not apply.
#
# What it silently does not apply matters here: with pambase's `required`
# preauth line, every retry against an already-locked account writes a fresh
# failure record and pushes unlock_time forward, so the lock never ages out and
# only a reboot clears it. And the lock is on the ACCOUNT — greetd and
# synui-lock reach the same stack — so the symptom is a login screen rejecting
# a correct password. An install that quietly missed this ships that.
#
# So pam_faillock_configure() reports whether the stack actually reads
# `requisite` afterwards rather than whether sed ran, and this asserts it
# against real fixtures: the stock pambase file, a re-run, a file whose anchor
# has been reworded, and a missing file.
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

TMP=$(mktemp -d /tmp/faillock.XXXXXX)
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

# pambase's system-auth auth block, verbatim upstream.
stock_auth() {
    cat > "$1" <<'EOF'
#%PAM-1.0

auth       required                    pam_faillock.so      preauth
# Optionally use requisite above if you do not want to prompt for the password
# on locked accounts.
-auth      [success=2 default=ignore]  pam_systemd_home.so
auth       [success=1 default=bad]     pam_unix.so          try_first_pass nullok
auth       [default=die]               pam_faillock.so      authfail
auth       optional                    pam_permit.so
auth       required                    pam_env.so
auth       required                    pam_faillock.so      authsucc

account    required                    pam_unix.so
EOF
}

# pam's faillock.conf ships every key commented out.
stock_conf() {
    cat > "$1" <<'EOF'
# Configuration for locking the user after multiple failed
# authentication attempts.
# deny = 3
# fail_interval = 900
# unlock_time = 600
EOF
}

echo "faillock_test — pam_faillock_configure()"
echo

# ── the stock pair ─────────────────────────────────────────────────────────
a="$TMP/system-auth"; c="$TMP/faillock.conf"
stock_auth "$a"; stock_conf "$c"
pam_faillock_configure "$a" "$c"; rc=$?

check "the stock pambase/pam pair is accepted" "0" "$rc"
check "…preauth is requisite afterwards" "1" \
    "$(grep -cE '^auth[[:space:]]+requisite[[:space:]]+pam_faillock\.so[[:space:]]+preauth' "$a")"
check "…and nothing is left claiming required on that line" "0" \
    "$(grep -cE '^auth[[:space:]]+required[[:space:]]+pam_faillock\.so[[:space:]]+preauth' "$a")"

# authfail and authsucc are load-bearing and are NOT the line being changed.
# A greedy regex that caught them too would stop failures being counted at all
# and stop a success clearing the tally — a lockout that never happens and one
# that never ends, from one careless character.
check "…authfail is untouched" "1" \
    "$(grep -cE '^auth[[:space:]]+\[default=die\][[:space:]]+pam_faillock\.so[[:space:]]+authfail' "$a")"
check "…authsucc is still required" "1" \
    "$(grep -cE '^auth[[:space:]]+required[[:space:]]+pam_faillock\.so[[:space:]]+authsucc' "$a")"
check "…pam_unix is untouched" "1" \
    "$(grep -cE '^auth[[:space:]]+\[success=1 default=bad\][[:space:]]+pam_unix\.so' "$a")"

check "…the thresholds are written" "1" "$(grep -c '^deny = 5$' "$c")"
check "…unlock_time is stated" "1" "$(grep -c '^unlock_time = 600$' "$c")"
check "…fail_interval is stated" "1" "$(grep -c '^fail_interval = 900$' "$c")"

# ── re-run ─────────────────────────────────────────────────────────────────
pam_faillock_configure "$a" "$c"; rc=$?
check "a second run succeeds" "0" "$rc"
check "…and does not stack a second threshold block" "1" "$(grep -c '^deny = 5$' "$c")"
check "…and preauth is still requisite exactly once" "1" \
    "$(grep -cE '^auth[[:space:]]+requisite[[:space:]]+pam_faillock\.so[[:space:]]+preauth' "$a")"

# ── a reworded anchor must REPORT failure, not pass quietly ────────────────
a2="$TMP/moved"; c2="$TMP/moved.conf"
printf '#%%PAM-1.0\n\nauth  required  pam_faillock.so  preauth  audit\n' > "$a2"
stock_conf "$c2"
pam_faillock_configure "$a2" "$c2"; rc=$?
check "a preauth line with extra arguments reports failure" "1" "$rc"
check "…and is left alone" "1" \
    "$(grep -c 'auth  required  pam_faillock.so  preauth  audit' "$a2")"

# ── missing files ──────────────────────────────────────────────────────────
pam_faillock_configure "$TMP/nope" "$c"; rc=$?
check "a missing system-auth reports failure" "1" "$rc"
check "…and creates nothing" "no" \
    "$([ -e "$TMP/nope" ] && echo yes || echo no)"

pam_faillock_configure "$a" "$TMP/nope.conf"; rc=$?
check "a missing faillock.conf reports failure" "1" "$rc"

echo
if [ "$fails" -gt 0 ]; then
    echo "$fails check(s) FAILED"
    exit 1
fi
echo "all checks passed"
