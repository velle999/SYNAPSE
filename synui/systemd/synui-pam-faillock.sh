#!/bin/sh
#
# synui-pam-faillock — a password prompt nobody answered is not a wrong password.
#
# ⛔ THE BUG. pam_faillock locks an account after N failed logins, and pambase's
# system-auth hands it every pam_unix failure — including the one pam_unix
# returns when its password prompt is never answered (PAM_AUTHTOK_ERR, "auth
# could not identify password"). Nothing was guessed; it is counted anyway.
# Three things on this desktop end a prompt without answering it:
#
#   · the login screen. synui's greeter keeps the fingerprint reader armed while
#     it sits idle, and every time pam_fprintd times out (10 s) PAM moves on to
#     the password prompt and the greeter hangs up to arm the reader again. On
#     velle's ThinkPad, 2026-09-18: a failure at 15:39:33, :43 and :53, "account
#     temporarily locked" — from an idle login screen. The finger still worked,
#     because it sits above the stack; sudo, which is the password, did not, and
#     an update rejected a correct password three times.
#   · sudo with no terminal to ask on (an agent, a task runner) — locked the
#     account twice in August.
#   · Ctrl+C, or walking away from, a sudo prompt.
#
# ⛔ THE FIX IS ONE WORD ON pam_unix's LINE: `authtok_err=die`. A prompt that
# produced no password now ends authentication THERE, before the authfail line
# can count it. A wrong password is PAM_AUTH_ERR and is counted exactly as before
# — the lock still stops guessing. Measured against real Linux-PAM in a user
# namespace: six unanswered prompts, zero records; three wrong passwords, three.
# ⚠ NOT conv_err=die, which is the obvious spelling and does nothing: pam_unix
# reports an unanswered prompt as AUTHTOK_ERR, not CONV_ERR.
#
# And the two changes syn-install has made since 2026-08-27, which until now
# reached fresh installs only — an existing machine never got them:
#   · preauth `required` → `requisite`, so an attempt against an account that
#     is already locked stops there instead of writing a fresh failure and
#     pushing the unlock time forward. pambase's own comment beneath the line
#     names requisite as the supported alternative.
#   · deny/fail_interval/unlock_time written out in faillock.conf (5/900/600).
#
# ⚠ ONLY PAMBASE'S OWN LINES ARE TOUCHED. /etc/pam.d/system-auth belongs to
# pambase and is in its backup= array, so an edit survives upgrades. A line
# somebody changed by hand is theirs: it is left alone and this says so.
#
# ⛔ ONE IMPLEMENTATION. The synui scriptlet runs this on install and upgrade, a
# oneshot runs it before greetd at every boot, and syn-install calls it for a
# fresh install rather than keeping a sed of its own.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# Overridable so the suites can drive every path on fixtures. Tests and the
# installer (which points these into /mnt) only.
AUTH=${SYNUI_SYSTEM_AUTH:-/etc/pam.d/system-auth}
CONF=${SYNUI_FAILLOCK_CONF:-/etc/security/faillock.conf}

say() { [ "${SYNUI_FAILLOCK_QUIET:-0}" = 1 ] || printf '%s\n' "$*"; }

# pambase's two lines, exactly. Anything else is somebody's own.
PREAUTH_STOCK='^auth[[:space:]]+required[[:space:]]+pam_faillock\.so[[:space:]]+preauth[[:space:]]*$'
UNIX_STOCK='^auth[[:space:]]+\[success=1[[:space:]]+default=bad\][[:space:]]+pam_unix\.so([[:space:]]|$)'
UNIX_DONE='^auth[[:space:]]+\[[^]]*authtok_err=die[^]]*\][[:space:]]+pam_unix\.so([[:space:]]|$)'

if [ -f "$AUTH" ]; then
    tmp=$(mktemp "$AUTH.synui.XXXXXX") || exit 1
    # ⚠ ENVIRON, NOT -v: awk processes backslash escapes in a -v value, so `\[`
    # and `\.` would arrive as `[` and `.` and the pattern would match other
    # lines than the one it names.
    PRE="$PREAUTH_STOCK" UNX="$UNIX_STOCK" awk '
        $0 ~ ENVIRON["PRE"] { sub(/required/, "requisite"); print; next }
        $0 ~ ENVIRON["UNX"] { sub(/\[success=1[[:space:]]+default=bad\]/, "[success=1 authtok_err=die default=bad]"); print; next }
        { print }
    ' "$AUTH" > "$tmp" || { rm -f "$tmp"; exit 1; }

    # ⛔ PROVE THE REWRITE BEFORE IT REPLACES A LOGIN STACK. Same number of
    # lines, pam_unix still there as an auth line — a half-written system-auth
    # is a machine nobody can log into or sudo on.
    if [ "$(wc -l < "$tmp")" != "$(wc -l < "$AUTH")" ] \
       || ! grep -qE '^auth[[:space:]].*pam_unix\.so' "$tmp"; then
        rm -f "$tmp"
        say ">>> synui: the rewritten $AUTH did not check out; left it as it was."
    elif cmp -s "$tmp" "$AUTH"; then
        rm -f "$tmp"
    else
        chmod 644 "$tmp"
        mv -f "$tmp" "$AUTH"
        say ">>> synui: $AUTH — an unanswered password prompt no longer counts as a failed login."
    fi

    if ! grep -qE "$UNIX_DONE" "$AUTH"; then
        say ">>> synui: $AUTH has a pam_unix line that is not pambase's; left alone."
        say "    Add authtok_err=die to its [...] so an unanswered prompt is not a failed login."
    fi
fi

# The thresholds, readable on the machine they govern. Appended once: a second
# run must not stack another block, and a deny= somebody set is theirs.
if [ -f "$CONF" ] && ! grep -qE '^[[:space:]]*deny[[:space:]]*=' "$CONF"; then
    cat >> "$CONF" << 'SYN_FAILLOCK'

# ── SynapseOS ───────────────────────────────────────────────────────────────
# unlock_time is the one that matters, and it was always 600. It only ever
# behaved as "until the next reboot" because system-auth's preauth line was
# `required` and every retry rewrote the tally; that line is `requisite` now
# and this expires on its own. The tally files stay under /run/faillock —
# tmpfs — so a reboot still clears them, which is why rebooting "fixed" it.
deny = 5
fail_interval = 900
unlock_time = 600
SYN_FAILLOCK
fi

exit 0
