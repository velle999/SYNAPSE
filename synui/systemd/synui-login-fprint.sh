#!/bin/sh
#
# synui-login-fprint — put pam_fprintd into greetd's auth stack, or take it out,
# to match /etc/synui/login-fingerprint.enable.
#
# ⛔ THIS EXISTS BECAUSE A SETTING THAT ONLY APPLIES DURING A PACKAGE UPGRADE IS
# NOT A SETTING. The first version of the opt-in lived entirely in synui.install,
# so creating the flag did nothing until the next `syn-update` happened to
# install synui again — which could be days, and looked exactly like the feature
# being broken. It was reported as "not working" within minutes, correctly.
#
# So the logic lives here, and three things call it: the pacman scriptlet, a
# systemd oneshot ordered before greetd, and a person with sudo. One
# implementation; the scriptlet no longer has a copy to drift from.
#
# ⛔ sufficient, NEVER required. A reader that is absent, broken, unenrolled or
# simply not swiped must fall through to the password prompt. `required` would
# mean a laptop whose reader failed cannot be logged into at all.
#
# ⚠ WHY EDIT greetd's FILE AT ALL. /etc/pam.d/greetd belongs to the greetd
# package; shipping our own copy would be a file conflict and neither package
# would install. It IS in greetd's backup= array, so pacman preserves the edit
# across greetd upgrades and writes a .pacnew rather than overwriting.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# Overridable so the suite can drive every path in a temp directory. Tests only;
# nothing on a real system sets these.
PAM=${SYNUI_GREETD_PAM:-/etc/pam.d/greetd}
FLAG=${SYNUI_FPRINT_FLAG:-/etc/synui/login-fingerprint.enable}

LINE='auth       sufficient   pam_fprintd.so timeout=10'

# ⛔ STRICT ON PURPOSE: removal keys off this. Ours is exactly
# "auth sufficient pam_fprintd.so" with an optional timeout= and nothing else.
# A line somebody wrote by hand with its own options is theirs, and neither an
# upgrade nor a boot may take it out.
OURS='^auth[[:space:]]+sufficient[[:space:]]+pam_fprintd\.so([[:space:]]+timeout=[0-9]+)?[[:space:]]*$'

say() { [ "${SYNUI_FPRINT_QUIET:-0}" = 1 ] || printf '%s\n' "$*"; }

have_module() {
    [ -n "${SYNUI_FPRINT_ASSUME_MODULE:-}" ] && return 0
    [ -e /usr/lib/security/pam_fprintd.so ] || [ -e /lib/security/pam_fprintd.so ]
}

# Move a rewritten stack into place, but only if it still looks like the stack it
# started as. A half-written PAM file is a machine nobody can log into, and the
# window for that is however long the write takes.
commit() {   # commit <tmpfile>
    if ! grep -q 'system-local-login' "$1"; then
        rm -f "$1"
        return 1
    fi
    chmod 644 "$1"
    mv -f "$1" "$PAM"
}

[ -f "$PAM" ] || exit 0          # no greetd here, which is most desktops

# ── Not asked for: take ours back out ───────────────────────────────────────
#
# ⛔ THE HALF THAT MATTERS. Releases before 0.1.0-552 added the line
# unconditionally, so machines are already carrying it — including ones whose
# sensor never answers, where it costs a pam_fprintd timeout on every login and
# can never succeed.
if [ ! -e "$FLAG" ]; then
    grep -qE "$OURS" "$PAM" || exit 0
    tmp=$(mktemp "$PAM.synui.XXXXXX") || exit 0
    grep -vE "$OURS" "$PAM" > "$tmp" || { rm -f "$tmp"; exit 0; }
    commit "$tmp" || exit 0
    say ">>> synui: login fingerprint off — pam_fprintd removed from $PAM."
    say "    touch $FLAG to turn it on."
    exit 0
fi

# ── Asked for ───────────────────────────────────────────────────────────────

# ⚠ ONLY WHEN THE MODULE IS INSTALLED. fprintd is an optdepend; a `sufficient`
# line naming a module PAM cannot dlopen is harmless to authentication but logs
# a complaint on every single login.
if ! have_module; then
    say ">>> synui: $FLAG is set, but pam_fprintd is not installed."
    say "    pacman -S fprintd, then run $0 again (or reboot)."
    exit 0
fi

if grep -q 'pam_fprintd' "$PAM"; then
    grep -qF "$LINE" "$PAM" && exit 0            # already exactly right
    grep -qE "$OURS" "$PAM" || exit 0            # somebody else's line: leave it

    # Ours, from the version that wrote no timeout — the default left 36 seconds
    # of "Checking…" in front of every password login.
    tmp=$(mktemp "$PAM.synui.XXXXXX") || exit 0
    sed -E "s|$OURS|$LINE|" "$PAM" > "$tmp" || { rm -f "$tmp"; exit 0; }
    commit "$tmp" || exit 0
    say ">>> synui: login fingerprint given a 10s timeout."
    exit 0
fi

# ⛔ BEFORE system-local-login, AFTER securetty/nologin. Those two are
# `required`/`requisite` gates that must still run — a fingerprint is not a way
# past "this account is barred from logging in".
if ! grep -q '^auth.*include.*system-local-login' "$PAM"; then
    say ">>> synui: $PAM is not the stack this expected; leaving it alone."
    say "    Add this above the auth include for a fingerprint at login:"
    say "        $LINE"
    exit 0
fi

tmp=$(mktemp "$PAM.synui.XXXXXX") || exit 0
awk -v line="$LINE" '
    !done && /^auth[[:space:]]+include[[:space:]]+system-local-login/ { print line; done = 1 }
    { print }
' "$PAM" > "$tmp" || { rm -f "$tmp"; exit 0; }
grep -q 'pam_fprintd' "$tmp" || { rm -f "$tmp"; exit 0; }
commit "$tmp" || exit 0

say ">>> synui: fingerprint added to the login screen ($PAM)."
say "    It is sufficient, so a failed or absent swipe still asks for the"
say "    password. Remove $FLAG to undo it."
