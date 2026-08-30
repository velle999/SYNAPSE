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

# ── The PAM line ────────────────────────────────────────────────────────────
grep -q 'pam_fprintd.so timeout=10' "$I"
chk "pam_fprintd is given a bounded timeout" $?

# ⛔ AND AN OLD LINE IS UPGRADED. The first version wrote no timeout, and the
# "already present" check returned before touching it — so every machine that
# already had the line kept the 36-second default and no upgrade could ever
# change it. A post_upgrade that cannot fix its own earlier output is a fix that
# never arrives.
grep -q 'sufficient\[\[:space:\]\]+pam_fprintd' "$I" || grep -q 'pam_fprintd\\.so\[\[:space:\]\]\*\$' "$I"
chk "an earlier line without a timeout is rewritten, not skipped" $?

grep -q 'sufficient' "$I"
chk "…and it stays sufficient, so a dead reader still asks for the password" $?

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
