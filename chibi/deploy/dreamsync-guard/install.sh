#!/usr/bin/env bash
# Install the dream-sync guard. Site-specific and opt-in; no package does this.
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail
cd "$(dirname "$(readlink -f "$0")")" || exit 2

# Catch a bad ruleset HERE. If it only fails at boot, the unit fails and chibi
# binds 8077 anyway -- unfenced, with nothing to notice it.
sudo nft -c -f ./chibi-dreamsync.nft || { echo "chibi-dreamsync.nft is INVALID"; exit 1; }

sudo install -Dm644 chibi-dreamsync.nft /etc/nftables.d/chibi-dreamsync.nft
sudo install -Dm644 chibi-dreamsync-guard.service \
     /etc/systemd/system/chibi-dreamsync-guard.service
sudo systemctl daemon-reload
sudo systemctl enable --now chibi-dreamsync-guard.service

# Verify the TABLE, not the unit: "active" only means nft exited 0 once.
if sudo nft list table inet chibi_dreamsync_guard >/dev/null 2>&1; then
    echo "OK: chibi_dreamsync_guard loaded"
else
    echo "FAIL: unit ran but the table is absent"; exit 1
fi
