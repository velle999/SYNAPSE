#!/usr/bin/env bash
#
# greeter_fprint.sh — the login screen's fingerprint contract.
#
# ⛔ WHY THIS FILE EXISTS. Three releases "fixed" the fingerprint at the login
# screen by editing PAM, and none of them could have worked: greetd owns the PAM
# conversation and only runs one while it is CREATING a session, and this
# greeter did not ask for a session until Enter had been pressed with a password
# already typed. pam_fprintd in /etc/pam.d/greetd was necessary and never
# sufficient — the reader was armed for the 36 seconds AFTER you had finished
# typing, and nothing on screen said so because the prompt text was discarded.
#
# What is asserted here is the shape that makes it possible at all, plus the two
# ways it can go badly wrong on a machine somebody has to log into.
#
# SynapseOS Project — GPL-2.0-or-later
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
G="$HERE/../src/greeter.c"
H="$HERE/../src/synui.h"
I="$HERE/../synui.install"
for f in "$G" "$H" "$I"; do
    [ -r "$f" ] || { echo "not readable: $f" >&2; exit 1; }
done

pass=0 fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail+1)); }
chk() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

echo "greeter fingerprint"

# ── The reader is armed when the screen appears ─────────────────────────────
#
# create_session is what starts a PAM conversation, and a PAM conversation is
# the only thing that runs pam_fprintd. If that only happens on Enter, there is
# no way to present a finger at all.
grep -q 'greeter_arm(s);' "$G"
chk "the greeter arms a session rather than waiting for Enter" $?

awk '/^void greeter_start/,/^}/' "$G" | grep -q 'greeter_arm'
chk "…armed from greeter_start, so the reader is live with the screen" $?

# ⛔ THE ONE THAT MUST NEVER REGRESS. An empty response to a `secret` prompt is
# a failed password attempt, and this desktop runs faillock — a greeter that
# answered every arm with "" would lock the account it exists to log in.
awk '/secret.*== 0/,/^    }/' "$G" | grep -q 'if (!s->greetd.submitted)'
chk "a password prompt is never answered before the user submits one" $?

grep -q 'if (s->nlock.pw_len == 0) return;' "$G"
chk "…and an empty field still submits nothing at all" $?

# ⚠ The prompt TEXT is what tells somebody the reader is waiting. Discarding it
# is why "it never worked" and "it worked and nobody could tell" looked the same.
grep -q 'auth_message"' "$G" && grep -q 'nlock.fp_msg' "$G"
chk "PAM's message text is shown, not thrown away" $?

# ⛔ NO SPIN. On a machine with no pam_fprintd, PAM goes straight to the
# password prompt with an empty field — which is exactly the re-arm condition.
# Without the guard that is forty greetd workers in a second.
grep -q 'saw_fp_prompt && s->nlock.pw_len == 0' "$G"
chk "a conversation with no finger prompt is not re-armed" $?

grep -q 'GREETER_MAX_REARMS' "$G" && grep -q 'rearms >= GREETER_MAX_REARMS' "$G"
chk "…and re-arming is capped, so an idle screen does not cycle forever" $?

# ⛔ THE PASSWORD PATH SURVIVES EVERYTHING. If the arm never happened — no
# GREETD_SOCK, cap reached, connect refused — Enter must still open a session.
# This is the path that makes the machine loggable-into when the rest is wrong.
awk '/^void greeter_submit/,/^}/' "$G" | grep -q 'greeter_open_session(s)'
chk "Enter still opens a session when nothing was armed" $?

awk '/^static void greeter_arm/,/^}/' "$G" | grep -q 'if (!greeter_open_session(s)) return;'
chk "…and a failed arm is silent, leaving the password screen as it was" $?

# ⚠ Arming must not take the keyboard. `busy` swallows keys and draws
# "Checking…" — doing that at idle would be a login screen nobody can type into.
awk '/^static void greeter_arm/,/^}/' "$G" | grep -q 'busy  = 0'
chk "…and arming does not swallow the keyboard" $?

# ── The PAM line, and the opt-in that gates it ──────────────────────────────
#
# ⛔ OPT-IN, BECAUSE A READER THAT DOES NOT WORK IS NOT FREE. pam_fprintd blocks
# the PAM conversation while it waits and greetd runs one at a time, so on a
# machine whose sensor never answers every login pays that timeout and gets
# nothing. The ThinkPad's sensor enumerates and has never once become
# available; the line was pure cost there. It goes in only when asked for, and
# comes back OUT on an unmarked machine.
grep -q 'pam_fprintd.so timeout=10' "$I"
chk "pam_fprintd is given a bounded timeout" $?

grep -q 'login-fingerprint.enable' "$I"
chk "…and is gated on an opt-in flag" $?

# The whole scriptlet, driven against real files. Sourcing it defines the
# functions without running them, so each path can be exercised in a temp dir.
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
cat > "$T/base" <<'PAM'
#%PAM-1.0

auth       required     pam_securetty.so
auth       requisite    pam_nologin.so
auth       include      system-local-login
account    include      system-local-login
session    include      system-local-login
PAM

drive() {   # drive <yes|no> <file>
    (
        . "$I"
        _greetd_pam="$2"
        _fp_flag="$T/flag"
        # Every build host lacks fprintd, which is why this is a function.
        _synui_have_pam_fprintd() { return 0; }
        [ "$1" = yes ] && : > "$T/flag" || rm -f "$T/flag"
        _synui_fprint_in_greeter
    ) >/dev/null 2>&1
}

cp "$T/base" "$T/a"; drive no "$T/a"
cmp -s "$T/base" "$T/a"
chk "an unmarked machine with a clean stack is left alone" $?

# ⛔ THE HALF THAT MATTERS. Earlier releases added the line unconditionally, so
# machines already carry it. An upgrade that could not undo its own output would
# leave that cost in place forever.
cp "$T/base" "$T/b"
sed -i 's|^auth       include|auth       sufficient   pam_fprintd.so\nauth       include|' "$T/b"
drive no "$T/b"
[ "$(grep -c pam_fprintd "$T/b")" = 0 ]
chk "…and one we added before is taken back out" $?
[ "$(grep -c system-local-login "$T/b")" = 3 ]
chk "…leaving the rest of the stack intact" $?

# ⛔ NEVER SOMEBODY ELSE'S LINE. Removal keys off a strict pattern; a line with
# its own options belongs to whoever wrote it, and an upgrade must not eat it.
cp "$T/base" "$T/c"
sed -i 's|^auth       include|auth       sufficient   pam_fprintd.so max-tries=5 debug\nauth       include|' "$T/c"
drive no "$T/c"
grep -q 'max-tries=5' "$T/c"
chk "…but a hand-written line with options is never touched" $?

cp "$T/base" "$T/d"; drive yes "$T/d"
grep -q 'pam_fprintd.so timeout=10' "$T/d"
chk "asking for it adds the line" $?
# ⛔ ABOVE the include: securetty and nologin are gates a finger must not skip.
[ "$(grep -n pam_fprintd "$T/d" | cut -d: -f1)" -lt \
  "$(grep -n 'auth       include' "$T/d" | cut -d: -f1)" ]
chk "…above system-local-login, below securetty and nologin" $?

cp "$T/base" "$T/e"
sed -i 's|^auth       include|auth       sufficient   pam_fprintd.so\nauth       include|' "$T/e"
drive yes "$T/e"
grep -q 'timeout=10' "$T/e"
chk "…and an older line of ours gains the timeout" $?

drive yes "$T/e"
[ "$(grep -c pam_fprintd "$T/e")" = 1 ]
chk "…and running twice changes nothing" $?

grep -q 'sufficient' "$I"
chk "it stays sufficient, so a dead reader still asks for the password" $?

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
