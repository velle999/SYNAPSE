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

# ── The prompt text has to survive the parse ────────────────────────────────
#
# ⛔ THE BUG THAT MADE ALL OF THIS LOOK LIKE DEAD HARDWARE. greetd sends
#
#   {"type":"auth_message","auth_message_type":"info","auth_message":"Place your finger…"}
#
# and the message TYPE is literally the string "auth_message" — so a search for
# that key matches the VALUE of "type" first. The old parser took that hit,
# found a comma where a colon belonged, and reported the field missing. Every
# prompt arrived with empty text, so the login screen drew nothing while
# pam_fprintd sat waiting ten seconds for a finger:
#
#   synui greeter: pam says [info] ""
#
# Compiled and run, not grepped: this is a parser, and the only way to know a
# parser works is to feed it the bytes.
JF=$(mktemp -d) || exit 1
trap 'rm -rf "$JF" "${T:-}"' EXIT
{
    printf '#include <stdio.h>\n#include <string.h>\n'
    sed -n '/^static int json_field/,/^}$/p' "$HERE/../src/greeter.c"
    cat <<'MAIN'
int main(void) {
    char o[256]; int bad = 0;
    const char *m = "{\"type\":\"auth_message\",\"auth_message_type\":\"info\","
                    "\"auth_message\":\"Place your finger on Synaptics Sensors\"}";
    if (!json_field(m, "auth_message", o, sizeof o) ||
        strcmp(o, "Place your finger on Synaptics Sensors")) { puts("prompt text lost"); bad = 1; }
    if (!json_field(m, "auth_message_type", o, sizeof o) || strcmp(o, "info")) { puts("type lost"); bad = 1; }
    if (!json_field(m, "type", o, sizeof o) || strcmp(o, "auth_message")) { puts("outer type lost"); bad = 1; }
    const char *p = "{\"type\":\"auth_message\",\"auth_message_type\":\"secret\","
                    "\"auth_message\":\"Password: \"}";
    if (!json_field(p, "auth_message", o, sizeof o) || strcmp(o, "Password: ")) { puts("secret text lost"); bad = 1; }
    const char *e = "{\"type\":\"auth_message\",\"auth_message\":\"say \\\"hi\\\" now\"}";
    if (!json_field(e, "auth_message", o, sizeof o) || strcmp(o, "say \"hi\" now")) { puts("escape lost"); bad = 1; }
    if (json_field(m, "nosuch", o, sizeof o)) { puts("invented a field"); bad = 1; }
    return bad;
}
MAIN
} > "$JF/jf.c"
if cc -o "$JF/jf" "$JF/jf.c" 2>"$JF/cc.err"; then
    "$JF/jf"
    chk "a prompt's text survives a key that also appears as a value" $?
else
    bad "json_field would not compile standalone ($(head -1 "$JF/cc.err"))"
fi

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

# ⛔ AND IT HAS TO REACH A PIXEL, WHICH THE CHECK ABOVE CANNOT SEE. It passed
# for six releases while the login screen drew nothing: lock_draw_core() returns
# early on the greeter's two-field block, and the fingerprint row lived after
# that return — so greeter.c wrote fp_msg, lock.c drew fp_msg, and the two never
# met on the one screen that needed them to. The row is called from
# lock_draw_panel now, outside the core, like the layout chip beside it.
#
# ⚠ THE REAL PROOF IS tests/lock_fp_row_test.c, which renders the panel and
# reads the band back. These two are the cheap guard on the shape it needs.
L="$HERE/../src/lock.c"
! awk '/^static void lock_draw_core/,/^}$/' "$L" | grep -q 'nlock.fp_msg'
chk "the fingerprint row is not stranded behind the greeter's early return" $?

awk '/^static void lock_draw_panel/,/^}$/' "$L" | grep -q 'lock_draw_fp_row'
chk "…it is drawn from the panel, which both screens go through" $?

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

# ── The PAM line, the opt-in, and the thing that applies it ─────────────────
#
# ⛔ OPT-IN, BECAUSE A READER THAT DOES NOT WORK IS NOT FREE. pam_fprintd blocks
# the PAM conversation while it waits and greetd runs one at a time, so on a
# machine whose sensor never answers every login pays that timeout and gets
# nothing. The ThinkPad's sensor enumerates and has never once become available.
#
# ⛔ AND THE SWITCH HAS TO APPLY WITHOUT A PACKAGE UPGRADE. The first version put
# all of this in synui.install, so creating the flag did nothing until pacman
# next reinstalled synui — the flag was set and the line was absent, which is
# exactly what "not working" looked like.
S="$HERE/../systemd/synui-login-fprint.sh"
[ -x "$S" ] || bad "no $S"

grep -q 'pam_fprintd.so timeout=10' "$S"
chk "pam_fprintd is given a bounded timeout" $?
grep -q 'login-fingerprint.enable' "$S"
chk "…and is gated on an opt-in flag" $?

# ⛔ ONE IMPLEMENTATION. The scriptlet must DELEGATE, not carry a copy.
grep -q 'usr/lib/synui/synui-login-fprint' "$I"
chk "synui.install runs the script rather than repeating it" $?
! grep -q 'pam_fprintd.so timeout' "$I"
chk "…and holds no second copy of the line to drift from" $?

# The drop-in is what makes the flag take effect at boot with nothing enabled.
D="$HERE/../systemd/greetd.service.d/synui-login-fprint.conf"
grep -q 'Wants=synui-login-fprint.service' "$D" 2>/dev/null
chk "greetd pulls the sync in, so nothing needs systemctl enable" $?
grep -q 'Before=greetd.service' "$HERE/../systemd/synui-login-fprint.service"
chk "…and it runs before greetd reads the stack" $?

# ── The script, driven against real files ───────────────────────────────────
T=$(mktemp -d) || exit 1
cat > "$T/base" <<'PAM'
#%PAM-1.0

auth       required     pam_securetty.so
auth       requisite    pam_nologin.so
auth       include      system-local-login
account    include      system-local-login
session    include      system-local-login
PAM

drive() {   # drive <yes|no> <file>
    [ "$1" = yes ] && : > "$T/flag" || rm -f "$T/flag"
    # Every build host lacks fprintd, which is why the module check is
    # overridable — without it the add and upgrade paths never ran anywhere.
    SYNUI_GREETD_PAM="$2" SYNUI_FPRINT_FLAG="$T/flag" \
    SYNUI_FPRINT_ASSUME_MODULE=1 SYNUI_FPRINT_QUIET=1 sh "$S"
}

cp "$T/base" "$T/a"; drive no "$T/a"
cmp -s "$T/base" "$T/a"
chk "an unmarked machine with a clean stack is left alone" $?

# ⛔ THE HALF THAT MATTERS: releases before 552 added the line unconditionally.
cp "$T/base" "$T/b"
sed -i 's|^auth       include|auth       sufficient   pam_fprintd.so\nauth       include|' "$T/b"
drive no "$T/b"
[ "$(grep -c pam_fprintd "$T/b")" = 0 ]
chk "…and one we added before is taken back out" $?
[ "$(grep -c system-local-login "$T/b")" = 3 ]
chk "…leaving the rest of the stack intact" $?

# ⛔ NEVER SOMEBODY ELSE'S LINE.
cp "$T/base" "$T/c"
sed -i 's|^auth       include|auth       sufficient   pam_fprintd.so max-tries=5 debug\nauth       include|' "$T/c"
drive no "$T/c"
grep -q 'max-tries=5' "$T/c"
chk "…but a hand-written line with options is never touched" $?

cp "$T/base" "$T/d"; drive yes "$T/d"
grep -q 'pam_fprintd.so timeout=10' "$T/d"
chk "asking for it adds the line" $?
[ "$(grep -n pam_fprintd "$T/d" | cut -d: -f1)" -lt \
  "$(grep -n 'auth       include' "$T/d" | cut -d: -f1)" ]
chk "…above system-local-login, below securetty and nologin" $?

# ⛔ AND TURNING IT ON IS ENOUGH — no reinstall, no upgrade. This is the case
# that shipped broken: flag present, line absent, nothing to do about it.
cp "$T/base" "$T/e"; drive no "$T/e"          # settles to "off"
drive yes "$T/e"                              # …then the flag alone turns it on
grep -q 'pam_fprintd.so timeout=10' "$T/e"
chk "…and setting the flag on an already-synced machine is enough" $?

cp "$T/base" "$T/f"
sed -i 's|^auth       include|auth       sufficient   pam_fprintd.so\nauth       include|' "$T/f"
drive yes "$T/f"
grep -q 'timeout=10' "$T/f"
chk "…and an older line of ours gains the timeout" $?

drive yes "$T/f"
[ "$(grep -c pam_fprintd "$T/f")" = 1 ]
chk "…and running twice changes nothing" $?

# Off again, by removing the flag.
drive no "$T/f"
[ "$(grep -c pam_fprintd "$T/f")" = 0 ]
chk "…and removing the flag turns it back off" $?

grep -q 'sufficient' "$S"
chk "it stays sufficient, so a dead reader still asks for the password" $?

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
