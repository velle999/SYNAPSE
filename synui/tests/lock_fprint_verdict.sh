#!/bin/bash
# lock_fprint_verdict.sh — which letter the lock's fingerprint helper sends,
# for every (PAM code, how long it took) pair.
#
# ⛔ THIS IS THE WHOLE RETRY POLICY, AND IT WAS WRONG INSIDE A SWITCH.
#
# velle, 2026-08-30: "fingerprint reader not there after standby". Measured on
# the ThinkPad across a day of locks: every lock logged "fingerprint off for
# this lock (reader never became available)" about 2m20s in — suspend or no
# suspend — while locking and swiping straight away worked every time.
#
# The cause: the helper computes `waited` (did this attempt take longer than a
# person takes to swipe?) and then consulted it only on PAM_AUTH_ERR. Synaptics
# 06cb:00bd with fprintd 1.94.5 answers PAM_AUTHINFO_UNAVAIL after waiting its
# full ~30 seconds for a finger that never came, which fell through to an
# unconditional R — and four R's retire the reader for the rest of the lock.
# So nobody being in the room was charged to the device.
#
# ⚠ A GREP CANNOT SEE THIS. An unconditional `emit("R\n")` and a conditional one
# read almost identically, and the branch that mattered was the one nothing
# tested. So the rule is compiled and asked.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")/.." && pwd)
SRC="$HERE/src/synui-lock-fprint.c"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0

check() {  # check <what> <want> <got>
    if [ "$2" = "$3" ]; then echo "  ok    $1"
    else echo "  FAIL  $1"; echo "        want: [$2]"; echo "        got:  [$3]"
         fails=$((fails + 1)); fi
}

# ⛔ THE REAL FUNCTION, LIFTED OUT OF THE REAL FILE. A replica of the rule is a
# second rule, and the one that goes wrong is always the one nobody copied.
# sed pulls fp_verdict() from its opening line to its closing brace.
sed -n '/^char fp_verdict(int rc, bool waited)$/,/^}$/p' "$SRC" > "$T/rule.c"
if ! grep -q "PAM_AUTHINFO_UNAVAIL" "$T/rule.c"; then
    echo "  FAIL  could not lift fp_verdict() out of $SRC"
    exit 1
fi

cat > "$T/main.c" <<'EOF'
#include <security/pam_appl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "rule.c"

static int fails;
static void ck(const char *what, char want, int rc, bool waited)
{
    char got = fp_verdict(rc, waited);
    if (got == want) { printf("  ok    %s\n", what); return; }
    printf("  FAIL  %s\n        want: [%c]\n        got:  [%c]\n", what, want, got);
    fails++;
}

int main(void)
{
    puts("the lock's fingerprint verdict");

    /* ── the one that broke ─────────────────────────────────────────────── */
    ck("⛔ UNAVAIL after a long wait is a TIMEOUT — nobody swiped, and the "
       "device is not to blame", 'T', PAM_AUTHINFO_UNAVAIL, true);
    ck("⚠ …while an IMMEDIATE unavail really is the reader not being there",
       'R', PAM_AUTHINFO_UNAVAIL, false);

    /* ── the branch that already had it ─────────────────────────────────── */
    ck("a quick AUTH_ERR is a finger that did not match", 'F', PAM_AUTH_ERR, false);
    ck("…and a slow one is nobody being there", 'T', PAM_AUTH_ERR, true);
    ck("MAXTRIES follows the same rule", 'T', PAM_MAXTRIES, true);

    /* ── the ones duration must NOT change ──────────────────────────────── */
    ck("a match is a match however long it took", 'A', PAM_SUCCESS, true);
    ck("…and however quick", 'A', PAM_SUCCESS, false);
    /* ⛔ U is terminal — the lock stops asking for good. It must mean "this
     * machine cannot do this", never "you were slow". */
    ck("no pam_fprintd.so is terminal, and waiting does not soften it",
       'U', PAM_MODULE_UNKNOWN, true);
    ck("…so is a PAM system error", 'U', PAM_SYSTEM_ERR, true);
    ck("…and an abort", 'U', PAM_ABORT, false);

    printf(fails ? "\n%d check(s) failed\n" : "\nall checks passed\n", fails);
    return fails ? 1 : 0;
}
EOF

cc -o "$T/t" "$T/main.c" -I"$T" -Wall -Wextra 2>"$T/cc.log" || {
    echo "  FAIL  the lifted rule does not compile"; sed 's/^/        /' "$T/cc.log" | head -10
    exit 1; }
"$T/t" || fails=1

# ⚠ AND THE THRESHOLD ITSELF. A rule keyed on a duration is only as good as the
# number, and pam_fprintd's own patience is 30 seconds — below it, or the
# timeout never reads as one; well above a swipe, or a slow finger is called a
# timeout and the reader never retires when it genuinely should.
secs=$(sed -n 's/^#define FP_IDLE_SECS *\([0-9]*\).*/\1/p' "$SRC")
check "the timeout threshold sits under pam_fprintd's own patience" "yes" \
      "$([ "${secs:-0}" -lt 30 ] && echo yes || echo no)"
check "…and well above any real swipe" "yes" \
      "$([ "${secs:-0}" -ge 5 ] && echo yes || echo no)"

# ⛔ AND THE RESUME RE-ARM, which is the other half: the retry budgets are per
# LOCK and a suspend does not start a new one, so a screen that sat locked long
# enough to retire the reader used to come back with it still retired.
check "a resume starts the fingerprint over" "yes" \
      "$(grep -q 'lock_fprint_resume' "$HERE/src/logind.c" && echo yes || echo no)"
check "…and it clears the state both re-arm sites are gated on" "yes" \
      "$(sed -n '/^void lock_fprint_resume/,/^}$/p' "$HERE/src/lock.c" |
         grep -q 'fp_state *= *SYN_FP_IDLE' && echo yes || echo no)"
check "…and the budgets that retired it" "yes" \
      "$(sed -n '/^void lock_fprint_resume/,/^}$/p' "$HERE/src/lock.c" |
         grep -q 'fp_unavail *= *0' && echo yes || echo no)"

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
