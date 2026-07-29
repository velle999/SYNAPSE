#!/bin/bash
#
# deploy.sh — install this directory's site config, or report how far the
#             installed copies have drifted from it.
#
# Nothing here is packaged (see README.md for why), so pacman does not update
# these files and nothing notices when the repo moves ahead of /etc. That gap
# is not hypothetical: the ordering-cycle fix in synapd-bridge-guard.service
# was committed here and never applied, so the machine kept booting the old
# unit and kept losing synapd-bridge.socket to the cycle it fixed. Both the
# repo and the running system looked fine; only a diff between them showed it.
#
#   ./deploy.sh --check    diff /etc against this directory, change nothing
#   ./deploy.sh            install, reload, restart the guards
#
# --check is exit-status clean: 0 in sync, 1 drifted. Run it from a shell
# profile, a timer, or before touching anything else in here.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

set -uo pipefail
cd "$(dirname "$(readlink -f "$0")")" || exit 2

# repo file : installed path
MAP=(
    "synapd-bridge.socket:/etc/systemd/system/synapd-bridge.socket"
    "synapd-bridge.service:/etc/systemd/system/synapd-bridge.service"
    "synapd-bridge-guard.service:/etc/systemd/system/synapd-bridge-guard.service"
    "synapd-bridge.nft:/etc/nftables.d/synapd-bridge.nft"
    "ollama-guard.service:/etc/systemd/system/ollama-guard.service"
    "ollama.nft:/etc/nftables.d/ollama.nft"
    "ollama.service.d-override.conf:/etc/systemd/system/ollama.service.d/override.conf"
)

# The peer address in the .nft files is this site's. Deploying to a different
# machine means editing those first -- an allowlist naming someone else's host
# is not an allowlist.
check() {
    local drift=0 pair src dst
    for pair in "${MAP[@]}"; do
        src=${pair%%:*}; dst=${pair#*:}
        if [ ! -e "$dst" ]; then
            echo "MISSING  $dst"
            drift=1
        elif ! sudo diff -q "$src" "$dst" >/dev/null 2>&1; then
            echo "DRIFTED  $dst"
            sudo diff -u "$dst" "$src" | sed 's/^/    /'
            drift=1
        else
            echo "ok       $dst"
        fi
    done
    return $drift
}

install_all() {
    local pair src dst
    for pair in "${MAP[@]}"; do
        src=${pair%%:*}; dst=${pair#*:}
        sudo install -Dm644 "$src" "$dst" || return 1
        echo "installed $dst"
    done

    # A bad ruleset must be caught here, not by a guard unit failing at boot
    # and taking its port with it.
    sudo nft -c -f ./synapd-bridge.nft || { echo "synapd-bridge.nft is INVALID"; return 1; }
    sudo nft -c -f ./ollama.nft        || { echo "ollama.nft is INVALID"; return 1; }

    sudo systemctl daemon-reload
    sudo systemctl restart synapd-bridge-guard.service ollama-guard.service
    sudo systemctl restart ollama.service
    sudo systemctl start synapd-bridge.socket

    # Check the PORTS, not the units. A guard can be active, its table loaded
    # and no unit failed while the port was never opened at all -- that is
    # exactly the 16-boot failure described in README.md.
    echo
    local rc=0
    ss -ltn | grep -q ':11435' || { echo "FAIL: 11435 is not listening"; rc=1; }
    ss -ltn | grep -q ':11434' || { echo "FAIL: 11434 is not listening"; rc=1; }
    sudo nft list table inet synapd_bridge_guard >/dev/null 2>&1 || {
        echo "FAIL: synapd_bridge_guard table absent"; rc=1; }
    sudo nft list table inet ollama_guard >/dev/null 2>&1 || {
        echo "FAIL: ollama_guard table absent"; rc=1; }
    [ $rc -eq 0 ] && echo "both ports listening, both guard tables loaded"
    return $rc
}

case "${1:-}" in
    --check) check ;;
    "")      install_all ;;
    *)       echo "usage: $0 [--check]" >&2; exit 2 ;;
esac
