#!/usr/bin/env bash
#
# synpkg-enable-cachyos — add the CachyOS repository to a running SynapseOS.
#
# Invoked by `synpkg cachyos enable-repo`, which is what syn-settings' Kernel
# pane runs before installing a linux-cachyos kernel. Shell rather than C for
# the same reason as the BlackArch helper beside it: every line is orchestration
# of curl, pacman-key and pacman.
#
# ── WHY THIS DOES NOT RUN UPSTREAM'S BOOTSTRAP ─────────────────────────────
#
# The BlackArch helper runs upstream's strap.sh, because reimplementing its
# trust handling would be worse than trusting it. CachyOS is the opposite call,
# and the reason is what `cachyos-repo.sh --install` does BESIDES adding a repo:
#
#   1. It installs CachyOS's own fork of PACMAN over the system one
#      (pacman-7.1.0.r9.g54d9411-4-x86_64.pkg.tar.zst, in the same `pacman -U`
#      as the keyring). Replacing the package manager is not a step anyone
#      takes to get a kernel, and it is not reversible by removing a repo.
#
#   2. It detects the CPU and adds cachyos-v3, cachyos-v4 or cachyos-znver4 —
#      whole-system repositories of -march-optimised REBUILDS of core and
#      extra. It inserts them BEFORE [core] in pacman.conf, so they take
#      precedence, and the next -Syu quietly replaces base system packages with
#      CachyOS builds.
#
#   3. It rewrites `Architecture` to `auto`.
#
# All three are reasonable if you are becoming a CachyOS machine. None of them
# is implied by "I would like to try the Cachy kernel". So this takes the two
# steps that are, and leaves the rest alone.
#
# What is kept identical to upstream is the part worth not getting creative
# with: the SAME signing key, received and locally signed the same way.
#
# ── SCOPE ──────────────────────────────────────────────────────────────────
#
#   installs   cachyos-keyring, cachyos-mirrorlist         (nothing else)
#   adds       [cachyos]                                    (no v3/v4/znver4)
#   placement  AFTER the existing repositories, never before
#   untouched  pacman itself, Architecture, core, extra, multilib
#
# Placement is the load-bearing one. Upstream puts its repos first so they win;
# appending means core and extra win every name collision, so enabling this
# cannot change what any already-installed package resolves to. The Cachy
# kernels are uniquely named, so they are still reachable — being last costs
# nothing for the thing we actually came for.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

# CachyOS signing key, as pinned by upstream's cachyos-repo.sh. This is the
# trust anchor; if it ever disagrees with upstream, STOP rather than guess.
CACHY_KEY="F3B607488DB35A47"
KEYSERVER="keyserver.ubuntu.com"
MIRROR="https://mirror.cachyos.org/repo/x86_64/cachyos"
PACMAN_CONF=/etc/pacman.conf
MIRRORLIST=/etc/pacman.d/cachyos-mirrorlist

# One dead mirror in a repo we enabled aborts the ENTIRE -Syu, including base
# system security updates, and the error names the repo rather than the
# consequence. That happened with BlackArch on 2026-08-07 and cost a day. So
# the server count is checked here and reported rather than assumed.
MIN_MIRRORS=2

msg()  { printf '  %s\n' "$*"; }
warn() { printf '  warning: %s\n' "$*" >&2; }
die()  { printf '  error: %s\n' "$*" >&2; exit 1; }

ACTION="${1:-enable}"
case "$ACTION" in
    enable|disable) ;;
    *) die "usage: ${0##*/} [enable|disable]" ;;
esac

[ "$(id -u)" -eq 0 ] || die "must run as root (try: synpkg cachyos enable-repo)"

# ── the repo section ────────────────────────────────────────────────────────
#
# Matched on a real section header, not a substring: `grep cachyos` also
# matches the Include line, a comment, or a comment someone wrote about this
# script, and a false positive here means concluding "already enabled" on a
# machine where it is not.
repo_present() { grep -qE '^[[:space:]]*\[cachyos\]' "$PACMAN_CONF"; }

# ── mirror health ───────────────────────────────────────────────────────────
check_mirrors() {
    [ -f "$MIRRORLIST" ] || { warn "no $MIRRORLIST to check"; return 0; }
    local active
    active=$(grep -cE '^[[:space:]]*Server[[:space:]]*=' "$MIRRORLIST")
    if [ "$active" -lt "$MIN_MIRRORS" ]; then
        warn "[cachyos] has only $active active mirror(s)."
        warn "If that one goes down, EVERY 'pacman -Syu' on this machine fails"
        warn "until it comes back or [cachyos] is removed:"
        warn "    synpkg cachyos disable-repo"
    else
        msg "[cachyos] has $active mirrors configured."
    fi
}

if [ "$ACTION" = disable ]; then
    repo_present || { msg "CachyOS is not enabled."; exit 0; }

    cp -a "$PACMAN_CONF" "$PACMAN_CONF.bak" || die "could not back up $PACMAN_CONF"

    # Drop the [cachyos] section: its header, everything under it, and the
    # comment block we wrote above it. Stops at the next section header so a
    # repo added after this one survives — a disable that ate [multilib]
    # because it happened to sit below would be a far worse bug than the one
    # this is undoing.
    awk '
        /^[[:space:]]*# CachyOS — added by synpkg/ { skip = 1; next }
        /^[[:space:]]*\[cachyos\][[:space:]]*$/    { skip = 1; next }
        /^[[:space:]]*\[/                          { skip = 0 }
        !skip
    ' "$PACMAN_CONF" > "$PACMAN_CONF.new" || die "could not rewrite $PACMAN_CONF"

    [ -s "$PACMAN_CONF.new" ] || die "the rewrite produced an empty file — nothing changed"
    grep -qE '^[[:space:]]*\[core\]' "$PACMAN_CONF.new" \
        || die "the rewrite lost [core] — refusing to install it; $PACMAN_CONF is unchanged"

    cat "$PACMAN_CONF.new" > "$PACMAN_CONF" || die "could not write $PACMAN_CONF"
    rm -f "$PACMAN_CONF.new"

    repo_present && die "[cachyos] is still in $PACMAN_CONF after the rewrite"

    msg "[cachyos] removed from $PACMAN_CONF (backup at $PACMAN_CONF.bak)."
    # The packages are left in place ON PURPOSE. Removing cachyos-keyring while
    # a linux-cachyos kernel is still installed would leave that kernel's
    # signatures unverifiable on the next upgrade. Say so rather than decide.
    if pacman -Qq 2>/dev/null | grep -q '^linux-cachyos'; then
        warn "a linux-cachyos kernel is still installed; it will no longer receive updates."
        warn "Remove it from the Kernel pane BEFORE the running kernel needs one."
    fi
    msg "cachyos-keyring and cachyos-mirrorlist were left installed (harmless)."
    exit 0
fi

if repo_present; then
    msg "CachyOS is already enabled."
    # The repo can be present while the keyring PACKAGE is not — the state
    # upstream's bootstrap leaves behind, and the one where key rotations never
    # arrive. Worth repairing on a machine enabled before this script existed.
    if ! pacman -Q cachyos-keyring >/dev/null 2>&1; then
        msg "Installing the missing cachyos-keyring package..."
        pacman -S --noconfirm --needed cachyos-keyring \
            || warn "could not install cachyos-keyring"
    fi
    check_mirrors
    exit 0
fi

command -v curl >/dev/null 2>&1 || die "curl is required"

# ── trust ───────────────────────────────────────────────────────────────────
#
# Before anything is downloaded from the repo: the keyring package itself is
# signed by this key, so the key has to be trusted first. Same two commands
# upstream runs, same keyserver.
msg "Importing the CachyOS signing key ($CACHY_KEY)..."
pacman-key --recv-keys "$CACHY_KEY" --keyserver "$KEYSERVER" \
    || die "could not receive the CachyOS signing key from $KEYSERVER"
pacman-key --lsign-key "$CACHY_KEY" \
    || die "could not locally sign the CachyOS key"

# Verify rather than trust the exit status: --recv-keys can report success and
# leave nothing usable behind.
pacman-key --list-keys "$CACHY_KEY" >/dev/null 2>&1 \
    || die "the CachyOS key is not in the keyring after import"
msg "Key imported and locally signed."

# ── keyring + mirrorlist ────────────────────────────────────────────────────
#
# Filenames are DISCOVERED from the mirror index, not pinned. Upstream pins
# them and edits the script when they roll; a pin copied from there goes stale
# on their schedule and then every enable fails on a package that is simply
# named something else now.
tmp=$(mktemp -d) || die "could not create a temp dir"
trap 'rm -rf "$tmp"' EXIT

msg "Looking up the current keyring and mirrorlist packages..."
curl -fsSL --proto '=https' --tlsv1.2 --max-time 30 -o "$tmp/index" "$MIRROR/" \
    || die "could not reach $MIRROR"

pick() {
    grep -oE "$1-[0-9][^\"]*\.pkg\.tar\.zst" "$tmp/index" | sort -u | tail -1
}
keyring_pkg=$(pick cachyos-keyring)
mirror_pkg=$(pick cachyos-mirrorlist)

[ -n "$keyring_pkg" ] || die "no cachyos-keyring package found at $MIRROR"
[ -n "$mirror_pkg" ]  || die "no cachyos-mirrorlist package found at $MIRROR"

msg "Installing $keyring_pkg and $mirror_pkg..."
# ⚠ These TWO and nothing else. Upstream's line also carries the v3 and v4
# mirrorlists and its pacman fork; adding those here would reintroduce exactly
# what this script exists to leave out.
pacman -U --noconfirm --needed "$MIRROR/$keyring_pkg" "$MIRROR/$mirror_pkg" \
    || die "could not install the CachyOS keyring and mirrorlist"

[ -f "$MIRRORLIST" ] || die "cachyos-mirrorlist installed but $MIRRORLIST is missing"

# ── the repo section ────────────────────────────────────────────────────────
#
# APPENDED. See the header: last means core and extra win every collision, so
# this cannot change what an already-installed package resolves to.
cp -a "$PACMAN_CONF" "$PACMAN_CONF.bak" \
    || die "could not back up $PACMAN_CONF"

{
    printf '\n'
    printf '# CachyOS — added by synpkg for the linux-cachyos kernels.\n'
    printf '# Deliberately LAST: core and extra take precedence over it.\n'
    printf '# Remove with: synpkg cachyos disable-repo\n'
    printf '[cachyos]\n'
    printf 'Include = %s\n' "$MIRRORLIST"
} >> "$PACMAN_CONF" || die "could not write $PACMAN_CONF"

repo_present || die "wrote $PACMAN_CONF but [cachyos] is not in it"

# ── verify by content, not by exit status ───────────────────────────────────
check_mirrors

pacman -Sy --noconfirm >/dev/null 2>&1 || warn "pacman -Sy did not complete cleanly"

count=$(pacman -Sl cachyos 2>/dev/null | wc -l)
if [ "$count" -eq 0 ]; then
    warn "[cachyos] is configured but lists no packages — rolling back"
    cp -a "$PACMAN_CONF.bak" "$PACMAN_CONF"
    die "CachyOS was not enabled (pacman.conf restored from .bak)"
fi

# The thing we actually came for. A repo that syncs but does not carry the
# kernels would leave the Kernel pane offering rows that cannot install.
pacman -Sl cachyos 2>/dev/null | grep -q ' linux-cachyos ' \
    || warn "[cachyos] synced but linux-cachyos was not found in it"

msg "CachyOS enabled — $count packages available."
msg "The Kernel pane in SYNAPSE Settings can now install the Cachy kernels."
