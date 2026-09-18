#!/usr/bin/env bash
#
# pam_faillock.sh — an unanswered password prompt is not a failed login.
#
# Two halves. The first drives systemd/synui-pam-faillock.sh on fixtures: what
# it changes in pambase's stock system-auth (two lines, nothing else), what it
# leaves alone, and that it can run any number of times.
#
# ⛔ THE SECOND RUNS REAL Linux-PAM against the exact file the script produced —
# because the obvious fix, conv_err=die, reads right and does nothing: pam_unix
# reports an unanswered prompt as PAM_AUTHTOK_ERR, and only a real
# pam_authenticate() shows that. A tiny program with a conversation that errors
# out (the greeter hanging up, a sudo with no terminal) is run as root inside a
# user namespace, with /etc an overlay over copies and /run/faillock a tmpfs;
# nothing reaches the machine running this. The stock stack runs beside it as
# the control: if that stops counting too, this half proves nothing.
#
# SynapseOS Project — GPL-2.0-or-later
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
S="$HERE/../systemd/synui-pam-faillock.sh"
G="$HERE/../src/greeter.c"
[ -r "$S" ] || { echo "not readable: $S" >&2; exit 1; }

pass=0 fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail+1)); }
skip() { printf '  --    %s\n' "$1"; }

T=$(mktemp -d)
cleanup() {
    unshare --map-root-user --map-auto --fork -- rm -rf "$T" 2>/dev/null || rm -rf "$T"
}
trap cleanup EXIT

echo "pam faillock"

stock() {
    printf '%s\n' '#%PAM-1.0' '' \
        'auth       required                    pam_faillock.so      preauth' \
        '# Optionally use requisite above if you do not want to prompt for the password' \
        '# on locked accounts.' \
        '-auth      [success=2 default=ignore]  pam_systemd_home.so' \
        'auth       [success=1 default=bad]     pam_unix.so          try_first_pass nullok' \
        'auth       [default=die]               pam_faillock.so      authfail' \
        'auth       optional                    pam_permit.so' \
        'auth       required                    pam_env.so' \
        'auth       required                    pam_faillock.so      authsucc' \
        '' \
        '-account   [success=1 default=ignore]  pam_systemd_home.so' \
        'account    required                    pam_unix.so' \
        'account    optional                    pam_permit.so' > "$1"
}
run() { SYNUI_SYSTEM_AUTH="$1" SYNUI_FAILLOCK_CONF="$2" SYNUI_FAILLOCK_QUIET=1 sh "$S"; }

# ── on fixtures ─────────────────────────────────────────────────────────────
stock "$T/auth"; cp "$T/auth" "$T/orig"; printf '# deny = 3\n' > "$T/conf"
run "$T/auth" "$T/conf"
d=$(diff "$T/orig" "$T/auth" || true)
[ "$(grep -c '^[<>]' <<<"$d")" = 4 ] \
  && grep -qx '> auth       requisite                    pam_faillock.so      preauth' <<<"$d" \
  && grep -qE '^> auth +\[success=1 authtok_err=die default=bad\] +pam_unix\.so +try_first_pass nullok$' <<<"$d" \
  && ok "stock pambase: preauth made requisite, pam_unix given authtok_err=die, nothing else" \
  || bad "stock pambase was not rewritten as expected: $d"
grep -qx 'account    required                    pam_unix.so' "$T/auth" \
  && ok "…the account line is not touched" || bad "the account pam_unix line changed"
[ "$(stat -c %a "$T/auth")" = 644 ] && ok "…and the stack stays 0644" || bad "mode is $(stat -c %a "$T/auth")"
[ "$(grep -cE '^deny = 5$' "$T/conf")" = 1 ] && grep -qx 'unlock_time = 600' "$T/conf" \
  && ok "thresholds written out (a commented deny does not count as one)" \
  || bad "faillock.conf thresholds missing"

cp "$T/auth" "$T/once"; cp "$T/conf" "$T/conf-once"; run "$T/auth" "$T/conf"
cmp -s "$T/auth" "$T/once" && cmp -s "$T/conf" "$T/conf-once" \
  && ok "a second run changes nothing and stacks no second block" || bad "a second run changed something"

stock "$T/auth2"; sed -i 's/\[success=1 default=bad\]     pam_unix.so/sufficient pam_unix.so/' "$T/auth2"
cp "$T/auth2" "$T/theirs"; printf 'deny = 9\n' > "$T/conf2"
out=$(SYNUI_SYSTEM_AUTH="$T/auth2" SYNUI_FAILLOCK_CONF="$T/conf2" sh "$S")
grep -q 'sufficient pam_unix.so' "$T/auth2" && ! grep -q 'authtok_err' "$T/auth2" \
  && ok "a pam_unix line somebody wrote is left alone…" || bad "a hand-written pam_unix line was changed"
grep -q 'not pambase' <<<"$out" && ok "…and it says so" || bad "no message for a foreign pam_unix line"
[ "$(cat "$T/conf2")" = "deny = 9" ] && ok "a deny somebody set is theirs" || bad "their deny was overwritten"

run "$T/nope" "$T/nope.conf"; rc=$?
[ "$rc" = 0 ] && [ ! -e "$T/nope" ] && [ ! -e "$T/nope.conf" ] \
  && ok "no stack, no config: nothing created, exit 0" || bad "missing files: rc=$rc or files created"

# The greeter reads the same word the script writes.
grep -q 'authtok_err=die' "$G" && grep -q 'greeter_abandon_is_free()) {' "$G" \
  && ok "the greeter re-arms the reader only when that line says authtok_err=die" \
  || bad "greeter.c no longer gates the idle re-arm on authtok_err=die"

# ── real Linux-PAM ──────────────────────────────────────────────────────────
CC=${CC:-cc}
if ! unshare --map-root-user --map-auto --mount --fork true 2>/dev/null; then
    skip "no user namespace with subordinate ids here — real PAM not exercised"
elif ! command -v "$CC" >/dev/null || ! command -v useradd >/dev/null || ! command -v faillock >/dev/null; then
    skip "no compiler, useradd or faillock here — real PAM not exercised"
else
    cat > "$T/pamt.c" <<'C'
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *mode;
static int conv(int n, const struct pam_message **m, struct pam_response **r, void *d)
{
    (void)d;
    struct pam_response *out = calloc((size_t)n, sizeof *out);
    for (int i = 0; i < n; i++)
        if (m[i]->msg_style == PAM_PROMPT_ECHO_OFF || m[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            if (!strcmp(mode, "hangup")) { free(out); return PAM_CONV_ERR; }
            out[i].resp = strdup(mode);
        }
    *r = out;
    return PAM_SUCCESS;
}
int main(int argc, char **argv)
{
    if (argc < 4) return 99;
    mode = argv[3];
    struct pam_conv c = { conv, NULL };
    pam_handle_t *h;
    if (pam_start(argv[1], argv[2], &c, &h) != PAM_SUCCESS) return 98;
    int rc = pam_authenticate(h, 0);
    pam_end(h, rc);
    return rc == PAM_SUCCESS ? 0 : 1;
}
C
    if ! "$CC" -o "$T/pamt" "$T/pamt.c" -lpam 2>/dev/null; then
        skip "cannot build against libpam here — real PAM not exercised"
    else
        mkdir -p "$T/up/pam.d" "$T/up/security" "$T/work" "$T/home"
        cp /etc/passwd /etc/group /etc/subuid /etc/subgid "$T/up/"
        getent passwd | awk -F: '{print $1":*:20000:0:99999:7:::"}' > "$T/up/shadow"
        getent group  | awk -F: '{print $1":!::"}' > "$T/up/gshadow"
        chmod 600 "$T/up/shadow" "$T/up/gshadow"
        : > "$T/up/.pwd.lock"; chmod 600 "$T/up/.pwd.lock"
        for f in passwd group shadow gshadow subuid subgid; do cp -p "$T/up/$f" "$T/up/$f-"; done
        # The file the script produced, and pambase's own as the control.
        stock "$T/up/pam.d/t-stock"
        cp "$T/auth" "$T/up/pam.d/t-fixed"
        cp "$T/conf" "$T/up/security/faillock.conf"
        cat > "$T/inner.sh" <<'NS'
T=$1
mount -t overlay overlay -o lowerdir=/etc,upperdir=$T/up,workdir=$T/work /etc || { echo "overlay"; exit 0; }
mount --bind "$T/home" /home
# ⚠ mode=0755, as the real /run/faillock is. A bare tmpfs mounts 1777 —
# sticky and world-writable — and fs.protected_regular then refuses to reopen a
# tally owned by another user, so every failure after the first went unrecorded
# and the control read as though nothing counted.
mkdir -p /run/faillock && mount -t tmpfs -o mode=0755 tmpfs /run/faillock || { echo "tmpfs"; exit 0; }
useradd -m tuser >/dev/null 2>&1; echo 'tuser:right' | chpasswd
n() { faillock --user tuser 2>/dev/null | grep -c '^[0-9]'; }
p() { "$T/pamt" "$1" tuser "$2"; }
reset() { faillock --user tuser --reset >/dev/null 2>&1; }

reset; p t-stock hangup; echo "stock-hangup $(n)"
reset; for i in 1 2 3 4 5 6; do p t-fixed hangup; done; echo "fixed-hangups $(n)"
p t-fixed right; echo "fixed-right $? $(n)"
reset; for i in 1 2 3 4 5; do p t-fixed wrong; done; echo "fixed-wrong $(n)"
p t-fixed right; echo "fixed-right-locked $? $(n)"
rm -rf /home/*
NS
        res=$(unshare --map-root-user --map-auto --mount --fork -- bash "$T/inner.sh" "$T" 2>/dev/null)
        v() { awk -v k="$1" '$1 == k { $1 = ""; sub(/^ /, ""); print }' <<<"$res"; }
        [ "$(v stock-hangup)" = 1 ] \
          && ok "control: pambase's stock stack counts a hung-up prompt as a failed login" \
          || bad "control did not count a hangup ('$(v stock-hangup)') — this half proves nothing"
        [ "$(v fixed-hangups)" = 0 ] \
          && ok "the rewritten stack counts none of six hung-up prompts" \
          || bad "the rewritten stack counted hangups: '$(v fixed-hangups)'"
        [ "$(v fixed-right)" = "0 0" ] && ok "…and the right password still logs in" \
          || bad "right password after hangups: '$(v fixed-right)'"
        [ "$(v fixed-wrong)" = 5 ] && ok "five wrong passwords are still five failures" \
          || bad "wrong passwords were not all counted: '$(v fixed-wrong)'"
        [ "$(v fixed-right-locked)" = "1 5" ] \
          && ok "…the account is locked, and trying it does not push the lock forward (requisite)" \
          || bad "locked account: '$(v fixed-right-locked)' (want refused, still 5)"
    fi
fi

echo "pam faillock: $pass ok, $fail failed"
[ "$fail" = 0 ]
