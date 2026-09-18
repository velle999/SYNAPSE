#!/bin/sh
#
# sudo-fprint — put pam_fprintd into sudo's auth stack, or take it out, to match
# /etc/syn-settings/sudo-fingerprint.off.
#
#   sudo-fprint [apply]   make /etc/pam.d/sudo agree with the flag
#   sudo-fprint on        remove the flag, then apply
#   sudo-fprint off       create the flag, then apply
#   sudo-fprint purge     take our line out whatever the flag says (package removal)
#   sudo-fprint status    print on / off / stuck:<why>; changes nothing
#
# ⚠ ON BY DEFAULT — the flag is an OFF switch. The login-screen fingerprint in
# synui is opt-in because greetd runs PAM in front of every password login and
# a reader that never answers cost 36 seconds there. sudo is not that: with no
# reader, or with nothing enrolled for this user, pam_fprintd answers at once
# and the password prompt follows; the wait exists only for somebody who has
# enrolled a finger, which is somebody who asked for it.
#
# ⛔ sufficient, NEVER required. A reader that is absent, broken or simply not
# swiped must fall through to the password. `required` would make a failed
# reader a machine nobody can administer.
#
# ⚠ NOT A WAIT ON A REMOTE SESSION. pam_fprintd (1.94) asks
# sd_session_is_remote() and checks PAM_RHOST, and skips the reader for a
# remote session — so `sudo` over ssh goes straight to the password instead of
# waiting for a finger on a laptop nobody is touching. And `sudo -n` never gets
# as far as PAM: sudo's noninteractive_auth is off by default.
#
# ⚠ WHY EDIT sudo's FILE AT ALL. /etc/pam.d/sudo belongs to the sudo package;
# shipping our own copy would be a file conflict. It IS in sudo's backup= array,
# so pacman keeps the edit across sudo upgrades and writes a .pacnew instead.
#
# ⛔ A SETTING THAT ONLY APPLIES DURING A PACKAGE UPGRADE IS NOT A SETTING — the
# lesson synui's login fingerprint learned. So the logic lives HERE, once, and
# three things call it: the pacman scriptlet, a oneshot at boot, and the
# settings window's switch (through pkexec).
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# Overridable so the suite can drive every path in a temp directory. Tests only;
# nothing on a real system sets these.
PAM=${SYN_SUDO_PAM:-/etc/pam.d/sudo}
FLAG=${SYN_SUDO_FPRINT_OFF:-/etc/syn-settings/sudo-fingerprint.off}

LINE='auth       sufficient   pam_fprintd.so timeout=10'

# ⛔ STRICT ON PURPOSE: removal keys off this. Ours is exactly
# "auth sufficient pam_fprintd.so" with an optional timeout= and nothing else.
# A line somebody wrote by hand with options of their own is theirs, and no
# upgrade, boot or switch may take it out.
OURS='^auth[[:space:]]+sufficient[[:space:]]+pam_fprintd\.so([[:space:]]+timeout=[0-9]+)?[[:space:]]*$'

say() { [ "${SYN_SUDO_FPRINT_QUIET:-0}" = 1 ] || printf '%s\n' "$*"; }

# ⚠ A PATH THE SUITE CAN MOVE, not an "assume it is there" switch. A test of
# the no-module case that read the real /usr/lib/security would pass on a box
# without fprintd and fail on every one with it — the machines this is FOR.
MODULE=${SYN_FPRINT_MODULE:-/usr/lib/security/pam_fprintd.so}
have_module() { [ -e "$MODULE" ]; }

# Move a rewritten stack into place, but only if it still includes system-auth.
# A half-written sudo stack is a machine nobody can administer, and the window
# for that is however long the write takes.
commit() {   # commit <tmpfile>
    if ! grep -qE '^auth[[:space:]]+include[[:space:]]+system-auth' "$1"; then
        rm -f "$1"
        return 1
    fi
    chmod 644 "$1"
    mv -f "$1" "$PAM"
}

take_ours_out() {
    grep -qE "$OURS" "$PAM" || return 0
    tmp=$(mktemp "$PAM.syn.XXXXXX") || return 1
    grep -vE "$OURS" "$PAM" > "$tmp" || { rm -f "$tmp"; return 1; }
    commit "$tmp" || return 1
    say ">>> syn-settings: fingerprint off for sudo — pam_fprintd removed from $PAM."
}

mode=${1:-apply}

case "$mode" in
    status)
        if [ ! -f "$PAM" ]; then echo "stuck:no-sudo"; exit 0; fi
        if grep -qE "$OURS" "$PAM"; then echo on; exit 0; fi
        if grep -q 'pam_fprintd' "$PAM"; then echo "stuck:foreign-line"; exit 0; fi
        if [ -e "$FLAG" ]; then echo off; exit 0; fi
        if ! have_module; then echo "stuck:no-module"; exit 0; fi
        # Asked for and not applied yet — the next boot or `apply` does it.
        if grep -qE '^auth[[:space:]]+include[[:space:]]+system-auth' "$PAM"; then
            echo pending; exit 0
        fi
        echo "stuck:unexpected-stack"
        exit 0 ;;
    on)
        rm -f "$FLAG" ;;
    off)
        mkdir -p "$(dirname "$FLAG")" && : > "$FLAG" \
            || { echo "sudo-fprint: cannot write $FLAG" >&2; exit 1; } ;;
    purge)
        [ -f "$PAM" ] || exit 0
        take_ours_out
        exit $? ;;
    apply) ;;
    *)
        echo "usage: sudo-fprint [apply|on|off|purge|status]" >&2
        exit 2 ;;
esac

[ -f "$PAM" ] || exit 0          # no sudo here

# ── Turned off: take ours back out ──────────────────────────────────────────
if [ -e "$FLAG" ]; then
    take_ours_out
    exit $?
fi

# ── On ──────────────────────────────────────────────────────────────────────

# ⚠ ONLY WHEN THE MODULE IS INSTALLED. fprintd is optional; a `sufficient` line
# naming a module PAM cannot load is harmless to authentication but logs a
# complaint on every sudo. The boot oneshot adds it the day fprintd arrives.
if ! have_module; then
    say ">>> syn-settings: pam_fprintd is not installed, so sudo cannot use a fingerprint."
    exit 0
fi

if grep -q 'pam_fprintd' "$PAM"; then
    grep -qF "$LINE" "$PAM" && exit 0            # already exactly right
    grep -qE "$OURS" "$PAM" || exit 0            # somebody else's line: leave it
    tmp=$(mktemp "$PAM.syn.XXXXXX") || exit 1
    sed -E "s|$OURS|$LINE|" "$PAM" > "$tmp" || { rm -f "$tmp"; exit 1; }
    commit "$tmp" || exit 1
    exit 0
fi

# ⛔ ABOVE THE system-auth INCLUDE, which is where the password is asked. A
# stack that is not the shape this expects is left alone and says what to add.
if ! grep -qE '^auth[[:space:]]+include[[:space:]]+system-auth' "$PAM"; then
    say ">>> syn-settings: $PAM is not the stack this expected; leaving it alone."
    say "    Add this above its first auth line for a fingerprint at sudo:"
    say "        $LINE"
    exit 0
fi

tmp=$(mktemp "$PAM.syn.XXXXXX") || exit 1
awk -v line="$LINE" '
    !done && /^auth[[:space:]]+include[[:space:]]+system-auth/ { print line; done = 1 }
    { print }
' "$PAM" > "$tmp" || { rm -f "$tmp"; exit 1; }
grep -qF "$LINE" "$tmp" || { rm -f "$tmp"; exit 1; }
commit "$tmp" || exit 1

say ">>> syn-settings: fingerprint added to sudo ($PAM)."
say "    It is sufficient, so a failed or absent swipe still asks for the"
say "    password. Turn it off in Settings > Fingerprint."
exit 0
