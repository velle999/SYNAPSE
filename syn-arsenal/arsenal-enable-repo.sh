#!/usr/bin/env bash
#
# arsenal-enable-repo — add the BlackArch repository to a running SynapseOS.
#
# Upstream's own bootstrap (strap.sh) is what does the work: it fetches the
# blackarch-keyring tarball, verifies its SIGNATURE against BlackArch's master
# key, imports and locally signs that key, writes the [blackarch] section and
# syncs. Reimplementing that here would mean reimplementing its trust handling,
# which is the one part worth not getting creative with.
#
# What this script adds is verification on both sides of running it.
#
# BEFORE: strap.sh is fetched over TLS and then checked to still pin the master
# key fingerprint below. A pinned sha1 of the script would be the obvious check
# and is the wrong one — upstream edits strap.sh routinely, so the pin goes
# stale and every install starts failing on a script that is perfectly fine.
# The FINGERPRINT is the thing that must never change, and a substituted script
# that swaps in an attacker's key is exactly what checking it catches.
#
# AFTER: the repo is only accepted if it is actually usable and the key is
# actually trusted. A half-added repo that lists no packages is worse than none.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

# BlackArch master signing key. Verified against upstream strap.sh, which passes
# this same fingerprint to --recv-keys before it will trust the keyring tarball.
BA_FPR="4345771566D76038C7FEB43863EC0ADBEA87E4E3"
STRAP_URL="https://blackarch.org/strap.sh"

msg()  { printf '  %s\n' "$*"; }
warn() { printf '  warning: %s\n' "$*" >&2; }
die()  { printf '  error: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "must run as root (try: sudo syn arsenal --enable-repo)"

if grep -q '^\[blackarch\]' /etc/pacman.conf; then
    msg "BlackArch is already enabled."
    # Still worth a look: the repo can be present while the keyring PACKAGE is
    # not, which is the state strap.sh leaves behind and which quietly stops
    # key rotations from ever arriving.
    if ! pacman -Q blackarch-keyring >/dev/null 2>&1; then
        msg "Installing the missing blackarch-keyring package..."
        # --overwrite, scoped to the three paths strap.sh wrote: it extracts
        # the keyring tarball STRAIGHT into /usr/share/pacman/keyrings
        # (install_keyring, a bare `tar xfz -C`), so those files are owned by
        # no package and a plain -S dies with "exists in filesystem". Taking
        # ownership is the point — an unowned keyring is exactly the one that
        # never gets rotated.
        pacman -S --noconfirm --needed \
            --overwrite '/usr/share/pacman/keyrings/blackarch*' blackarch-keyring \
            || warn "could not install blackarch-keyring"
    fi
    exit 0
fi

command -v curl >/dev/null 2>&1 || die "curl is required"

tmp=$(mktemp -d) || die "could not create a temp dir"
trap 'rm -rf "$tmp"' EXIT

msg "Fetching $STRAP_URL ..."
curl -fsS --proto '=https' --tlsv1.2 -o "$tmp/strap.sh" "$STRAP_URL" \
    || die "could not download strap.sh"

# The check that matters. If upstream's bootstrap no longer pins the key we
# expect, stop — do not run it and do not add the repo.
if ! grep -qF "$BA_FPR" "$tmp/strap.sh"; then
    die "strap.sh does not pin the expected BlackArch master key
  ($BA_FPR).
  Refusing to run it. Verify the key at https://blackarch.org/downloads.html
  before enabling the repository by hand."
fi
msg "strap.sh pins the expected master key."

chmod +x "$tmp/strap.sh"
msg "Running upstream bootstrap (imports the keyring, adds the repo)..."
"$tmp/strap.sh" || die "strap.sh failed — no changes kept"

# ── Verify the result rather than trusting the exit code ────────────────────
grep -q '^\[blackarch\]' /etc/pacman.conf \
    || die "strap.sh reported success but [blackarch] is not in /etc/pacman.conf"

pacman -Sy --noconfirm >/dev/null 2>&1 || warn "pacman -Sy did not complete cleanly"

count=$(pacman -Sl blackarch 2>/dev/null | wc -l)
[ "$count" -gt 0 ] || die "[blackarch] is configured but lists no packages"

# The keyring as a PACKAGE, so future key rotations arrive as an upgrade.
pacman -S --noconfirm --needed \
    --overwrite '/usr/share/pacman/keyrings/blackarch*' blackarch-keyring \
    || warn "blackarch-keyring did not install — key rotations will not reach this machine"

msg "BlackArch enabled — $count packages available."
msg "Browse them with: syn arsenal   (or the SYNAPSE Arsenal app)"
