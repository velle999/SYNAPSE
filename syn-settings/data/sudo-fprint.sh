#!/bin/sh
#
# sudo-fprint — put pam_fprintd into the two administrator prompts — sudo, and
# polkit's password box — or take it out, to match
# /etc/syn-settings/sudo-fingerprint.off.
#
# ⛔ TWO PROMPTS, ONE SWITCH. sudo is the terminal; polkit is the box that the
# settings window, synpkg (the command line too — it goes through pkexec) and
# every other "authenticate to continue" opens. The first version covered sudo
# only, and the box that `synpkg upgrade` put up asked for a password with no
# finger in sight. Nobody thinks of those as two settings.
#
# ⚠ polkit KEEPS ITS STACK IN /usr/lib/pam.d, and PAM prefers /etc/pam.d. So for
# polkit this WRITES /etc/pam.d/polkit-1: polkit's own file plus our line, with
# a marker on its second line saying it was generated. It is regenerated on
# every apply, so a polkit upgrade that changes its file comes through, and
# switching off deletes it, which hands PAM back to polkit's own. An
# /etc/pam.d/polkit-1 somebody wrote themselves is edited in place like sudo's,
# or left alone.
#
# ⚠ polkit 127 authenticates in a sandboxed system service
# (polkit-agent-helper@), not in the session, so pam_fprintd cannot tell a
# local prompt from a pkexec over ssh there: over ssh the box waits its ten
# seconds for a finger before asking for the password. sudo over ssh does not.
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
SUDO_PAM=${SYN_SUDO_PAM:-/etc/pam.d/sudo}
POLKIT_PAM=${SYN_POLKIT_PAM:-/etc/pam.d/polkit-1}
POLKIT_VENDOR=${SYN_POLKIT_VENDOR:-/usr/lib/pam.d/polkit-1}
FLAG=${SYN_SUDO_FPRINT_OFF:-/etc/syn-settings/sudo-fingerprint.off}

LINE='auth       sufficient   pam_fprintd.so timeout=10'

# ⛔ STRICT ON PURPOSE: removal keys off this. Ours is exactly
# "auth sufficient pam_fprintd.so" with an optional timeout= and nothing else.
# A line somebody wrote by hand with options of their own is theirs, and no
# upgrade, boot or switch may take it out.
OURS='^auth[[:space:]]+sufficient[[:space:]]+pam_fprintd\.so([[:space:]]+timeout=[0-9]+)?[[:space:]]*$'
INCL='^auth[[:space:]]+include[[:space:]]+system-auth'

# The second line of an /etc/pam.d/polkit-1 this script wrote. What marks the
# file as OURS to regenerate or delete — an admin's own file never carries it.
GEN_MARK='# syn-settings: generated from /usr/lib/pam.d/polkit-1 with a fingerprint line'

say() { [ "${SYN_SUDO_FPRINT_QUIET:-0}" = 1 ] || printf '%s\n' "$*"; }
CHANGED=0     # set by anything that wrote, so a boot that changed nothing is quiet

# ⚠ A PATH THE SUITE CAN MOVE, not an "assume it is there" switch. A test of
# the no-module case that read the real /usr/lib/security would pass on a box
# without fprintd and fail on every one with it — the machines this is FOR.
MODULE=${SYN_FPRINT_MODULE:-/usr/lib/security/pam_fprintd.so}
have_module() { [ -e "$MODULE" ]; }

# Move a rewritten stack into place, but only if it still includes system-auth.
# A half-written auth stack is a machine nobody can administer, and the window
# for that is however long the write takes.
commit() {   # commit <tmpfile> <dest>
    if ! grep -qE "$INCL" "$1"; then
        rm -f "$1"
        return 1
    fi
    chmod 644 "$1"
    mv -f "$1" "$2"
}

generated() { [ -f "$1" ] && [ "$(sed -n 2p "$1")" = "$GEN_MARK" ]; }

# ── one stack, edited in place ──────────────────────────────────────────────

add_line() {   # add_line <file>
    f=$1
    if grep -q 'pam_fprintd' "$f"; then
        grep -qF "$LINE" "$f" && return 0            # already exactly right
        grep -qE "$OURS" "$f" || return 0            # somebody else's line: leave it
        tmp=$(mktemp "$f.syn.XXXXXX") || return 1
        sed -E "s|$OURS|$LINE|" "$f" > "$tmp" || { rm -f "$tmp"; return 1; }
        commit "$tmp" "$f"
        return
    fi
    # ⛔ ABOVE THE system-auth INCLUDE, which is where the password is asked. A
    # stack that is not the shape this expects is left alone and says what to add.
    if ! grep -qE "$INCL" "$f"; then
        say ">>> syn-settings: $f is not the stack this expected; leaving it alone."
        say "    Add this above its first auth line for a fingerprint there:"
        say "        $LINE"
        return 0
    fi
    tmp=$(mktemp "$f.syn.XXXXXX") || return 1
    awk -v line="$LINE" '
        !done && /^auth[[:space:]]+include[[:space:]]+system-auth/ { print line; done = 1 }
        { print }
    ' "$f" > "$tmp" || { rm -f "$tmp"; return 1; }
    grep -qF "$LINE" "$tmp" || { rm -f "$tmp"; return 1; }
    commit "$tmp" "$f" || return 1
    CHANGED=1
    say ">>> syn-settings: fingerprint added to $f."
}

take_line_out() {   # take_line_out <file>
    f=$1
    grep -qE "$OURS" "$f" || return 0
    tmp=$(mktemp "$f.syn.XXXXXX") || return 1
    grep -vE "$OURS" "$f" > "$tmp" || { rm -f "$tmp"; return 1; }
    commit "$tmp" "$f" || return 1
    say ">>> syn-settings: fingerprint taken out of $f."
}

# ── polkit's, generated from the vendor file ────────────────────────────────

polkit_generate() {
    [ -f "$POLKIT_VENDOR" ] && grep -qE "$INCL" "$POLKIT_VENDOR" || return 0
    tmp=$(mktemp "$POLKIT_PAM.syn.XXXXXX") || return 1
    {
        printf '%s\n' '#%PAM-1.0' "$GEN_MARK" \
            '# Regenerated at every boot and syn-settings upgrade; an edit here is' \
            '# lost. Switch it in Settings > Fingerprint — off deletes this file and' \
            '# PAM goes back to /usr/lib/pam.d/polkit-1.'
        awk -v line="$LINE" '
            NR == 1 && /^#%PAM/ { next }
            !done && /^auth[[:space:]]+include[[:space:]]+system-auth/ { print line; done = 1 }
            { print }
        ' "$POLKIT_VENDOR"
    } > "$tmp" || { rm -f "$tmp"; return 1; }
    # Every line of polkit's own file is still there, and ours on top of it.
    if ! grep -qF "$LINE" "$tmp" || \
       [ "$(grep -cvE '^[[:space:]]*(#|$)' "$tmp")" != "$(( $(grep -cvE '^[[:space:]]*(#|$)' "$POLKIT_VENDOR") + 1 ))" ]; then
        rm -f "$tmp"
        say ">>> syn-settings: could not build $POLKIT_PAM from $POLKIT_VENDOR; left it."
        return 1
    fi
    if [ -f "$POLKIT_PAM" ] && cmp -s "$tmp" "$POLKIT_PAM"; then
        rm -f "$tmp"
        return 0
    fi
    commit "$tmp" "$POLKIT_PAM" || return 1
    CHANGED=1
    say ">>> syn-settings: fingerprint added to the administrator password box ($POLKIT_PAM)."
}

polkit_on() {
    if [ -f "$POLKIT_PAM" ]; then
        if generated "$POLKIT_PAM"; then
            if [ -f "$POLKIT_VENDOR" ]; then polkit_generate
            else rm -f "$POLKIT_PAM"; fi        # polkit is gone; so is ours
        else
            add_line "$POLKIT_PAM"              # an admin's own file
        fi
    else
        polkit_generate
    fi
}

polkit_off() {
    if generated "$POLKIT_PAM"; then
        rm -f "$POLKIT_PAM"
        say ">>> syn-settings: fingerprint off for the administrator password box — $POLKIT_PAM removed."
    elif [ -f "$POLKIT_PAM" ]; then
        take_line_out "$POLKIT_PAM"
    fi
}

# polkit is converged when its prompt will ask for a finger, or there is no
# polkit stack to speak of.
polkit_done() {
    generated "$POLKIT_PAM" && return 0
    [ -f "$POLKIT_PAM" ] && grep -qE "$OURS" "$POLKIT_PAM" && return 0
    [ -f "$POLKIT_PAM" ] || [ -f "$POLKIT_VENDOR" ] || return 0
    return 1
}

all_off() {
    [ -f "$SUDO_PAM" ] && take_line_out "$SUDO_PAM"
    polkit_off
    return 0
}

mode=${1:-apply}

case "$mode" in
    status)
        # sudo decides the word, as it always has; polkit can only hold it back
        # at `pending` — asked for and not yet in both.
        if [ ! -f "$SUDO_PAM" ]; then echo "stuck:no-sudo"; exit 0; fi
        if grep -qE "$OURS" "$SUDO_PAM"; then
            if [ ! -e "$FLAG" ] && ! polkit_done; then echo pending; else echo on; fi
            exit 0
        fi
        if grep -q 'pam_fprintd' "$SUDO_PAM"; then echo "stuck:foreign-line"; exit 0; fi
        if [ -e "$FLAG" ]; then echo off; exit 0; fi
        if ! have_module; then echo "stuck:no-module"; exit 0; fi
        # Asked for and not applied yet — the next boot or `apply` does it.
        if grep -qE "$INCL" "$SUDO_PAM"; then echo pending; exit 0; fi
        echo "stuck:unexpected-stack"
        exit 0 ;;
    on)
        rm -f "$FLAG" ;;
    off)
        mkdir -p "$(dirname "$FLAG")" && : > "$FLAG" \
            || { echo "sudo-fprint: cannot write $FLAG" >&2; exit 1; } ;;
    purge)
        all_off
        exit 0 ;;
    apply) ;;
    *)
        echo "usage: sudo-fprint [apply|on|off|purge|status]" >&2
        exit 2 ;;
esac

# ── Turned off: take ours back out ──────────────────────────────────────────
if [ -e "$FLAG" ]; then
    all_off
    exit 0
fi

# ── On ──────────────────────────────────────────────────────────────────────

# ⚠ ONLY WHILE THE MODULE IS INSTALLED. fprintd is optional; a `sufficient`
# line naming a module PAM cannot load is harmless to authentication but logs a
# complaint every time. So without it ours come OUT — the day fprintd arrives,
# the boot oneshot puts them back.
if ! have_module; then
    all_off
    say ">>> syn-settings: pam_fprintd is not installed, so no prompt can use a fingerprint."
    exit 0
fi

[ -f "$SUDO_PAM" ] && add_line "$SUDO_PAM"
polkit_on
if [ "$CHANGED" = 1 ]; then
    say "    It is sufficient, so a failed or absent swipe still asks for the"
    say "    password. Turn it off in Settings > Fingerprint."
fi
exit 0
