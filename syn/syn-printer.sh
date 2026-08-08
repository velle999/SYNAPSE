#!/usr/bin/env bash
# syn-printer — install printer drivers that cannot ship on the ISO.
#
# WHY THIS EXISTS AT ALL: almost every printer made since ~2015 speaks IPP
# Everywhere / AirPrint, so cups discovers it over mDNS and prints with no
# driver. This is for the ones that predate that, where the only working path is
# a vendor driver whose licence forbids us shipping it.
#
# The Samsung Xpress M2020/M2020W is the case in hand: an SPL device cups cannot
# drive without Samsung's ULD `rastertospl` filter. Those binaries used to be
# committed to the SYNAPSE repository and built into every ISO. Samsung's EULA
# grants a licence "strictly for the personal use" and says, in the same
# paragraph, "No other use, copying or distribution of the SOFTWARE PRODUCT is
# permitted" — with no redistribution carve-out anywhere in it. So the ISO
# stopped carrying it, and this installs it on the machine that will print with
# it, from Samsung, which is the arrangement the licence actually describes.
#
# It is the same shape as `syn resolve install`: we automate everything except
# the part that is the user's to agree to.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE

set -uo pipefail

VERSION="0.1.0"
SRC="${SYN_PRINTER_SRC:-/var/lib/synapse-src}"

if [ -t 1 ]; then
    C_B=$'\e[1m'; C_DIM=$'\e[2m'; C_OK=$'\e[38;5;82m'
    C_WARN=$'\e[38;5;214m'; C_ERR=$'\e[38;5;203m'; C_R=$'\e[0m'
else
    C_B=""; C_DIM=""; C_OK=""; C_WARN=""; C_ERR=""; C_R=""
fi
say()  { printf '%s\n' "$*"; }
ok()   { printf '  %s✓%s %s\n' "$C_OK" "$C_R" "$*"; }
bad()  { printf '  %s✗%s %s\n' "$C_ERR" "$C_R" "$*" >&2; }
warn() { printf '  %s!%s %s\n' "$C_WARN" "$C_R" "$*" >&2; }
info() { printf '    %s\n' "$*"; }

# ── samsung ──────────────────────────────────────────────────

cmd_samsung() {
    local yes=0
    [ "${1:-}" = "--accept-license" ] && yes=1

    if pacman -Qq samsung-m2020 >/dev/null 2>&1; then
        ok "samsung-m2020 $(pacman -Q samsung-m2020 | awk '{print $2}') is already installed"
        say ""
        say "  Add the printer:  ${C_B}system-config-printer${C_R}, or the CUPS web UI at"
        say "                    http://localhost:631 — pick the Samsung M2020 Series PPD."
        return 0
    fi

    # makepkg refuses to run as root and calls sudo itself where it needs to.
    [ "$(id -u)" -ne 0 ] || {
        bad "run this as your normal user, not root — makepkg refuses to run as root"
        return 1; }

    local t missing=()
    for t in makepkg curl; do
        command -v "$t" >/dev/null 2>&1 || missing+=("$t")
    done
    [ ${#missing[@]} -eq 0 ] || {
        bad "missing: ${missing[*]}"
        info "sudo pacman -S --needed base-devel curl"
        return 1; }

    # The PKGBUILD lives in the source tree syn-update already maintains, so
    # there is one copy of it rather than a second embedded here.
    local dir="$SRC/samsung-m2020"
    if [ ! -f "$dir/PKGBUILD" ]; then
        bad "no PKGBUILD at $dir"
        info "The source tree is missing. Fetch it once with:  syn-update check"
        return 1
    fi

    say ""
    say "  ${C_B}Samsung Xpress M2020 / M2020W${C_R}"
    say ""
    say "  This downloads Samsung's Unified Linux Driver from Samsung and builds"
    say "  it into a package on this machine. It is not shipped with SynapseOS."
    say ""
    say "  ${C_WARN}Samsung licenses these binaries for personal use and does not permit"
    say "  redistributing them${C_R} — which is why SynapseOS cannot ship the driver and"
    say "  you are installing it yourself. The full EULA is installed with the"
    say "  package at /usr/share/licenses/samsung-m2020/EULA, and is also in the"
    say "  download at uld/noarch/license/eula.txt."
    say ""

    if [ "$yes" != 1 ]; then
        # A licence you must accept is not something to assume from a flag being
        # absent, and not something to auto-answer. No TTY means no agreement.
        [ -t 0 ] || {
            bad "cannot ask for agreement: no terminal"
            info "Run it in a terminal, or pass --accept-license if you have read the EULA."
            return 1; }
        local reply
        read -r -p "  Download and install it? [y/N] " reply
        case "$reply" in [yY]|[yY][eE][sS]) ;; *) say "  Cancelled."; return 1 ;; esac
    fi

    say ""
    ( cd "$dir" && makepkg -sfi --noconfirm ) || {
        bad "build failed"
        info "If the download 404'd, Samsung may have moved the file; the URL is"
        info "pinned in $dir/PKGBUILD."
        return 1; }

    say ""
    ok "installed"
    say ""
    say "  Add the printer:  ${C_B}system-config-printer${C_R}, or the CUPS web UI at"
    say "                    http://localhost:631 — pick the Samsung M2020 Series PPD."
    say ""
    say "  ${C_DIM}cups must be running:  sudo systemctl enable --now cups${C_R}"
}

# ── entry ────────────────────────────────────────────────────

usage() {
    cat <<HELP

syn printer $VERSION — printer drivers that cannot ship with the OS

  syn printer samsung [--accept-license]
        Samsung Xpress M2020 / M2020W (and the rest of the ULD range).
        Downloads Samsung's driver and builds it here. Samsung does not permit
        redistributing it, so SynapseOS cannot ship it for you.

Any printer made since roughly 2015 needs none of this — cups finds it over the
network and prints driverless. Try that first: plug it in, then look in
system-config-printer or at http://localhost:631.
HELP
}

case "${1:-help}" in
    samsung)        shift; cmd_samsung "$@" ;;
    -h|--help|help) usage ;;
    *)              bad "unknown printer: $1"; usage; exit 1 ;;
esac
