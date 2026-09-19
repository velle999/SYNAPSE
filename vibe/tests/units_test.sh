#!/bin/bash
# units_test.sh — vibe's systemd units say what they mean.
#
# ⛔ WHAT THIS EXISTS TO CATCH. vibe-wake.service carried StartLimitBurst and
# StartLimitIntervalSec under [Service]. systemd rejects the second there with
# "Unknown key … ignoring" on every daemon-reload and falls back to its 10s
# default; with RestartSec=10 three starts can never land inside ten seconds,
# so a unit whose comment said "restarted, but not forever" retried forever.
# Nothing failed. The only sign was a warning in the journal.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")/.." && pwd)
fails=0
ok()  { echo "  ok    $1"; }
bad() { echo "  FAIL  $1"; fails=$((fails + 1)); }

echo "vibe units"
for unit in "$HERE"/systemd/*.service; do
    name=$(basename "$unit")
    # The start-limit keys belong to [Unit]; anywhere else they are ignored.
    misplaced=$(awk '/^\[/{sec=$0} /^StartLimit[A-Za-z]*=/{ if (sec != "[Unit]") print sec" "$0 }' "$unit")
    [ -z "$misplaced" ] && ok "$name: start limits are [Unit] keys" \
                        || bad "$name: start limit outside [Unit]: $misplaced"
    if command -v systemd-analyze >/dev/null 2>&1; then
        unknown=$(systemd-analyze verify --user "$unit" 2>&1 | grep -i "unknown key\|unknown section")
        [ -z "$unknown" ] && ok "$name: systemd knows every key" \
                          || bad "$name: $unknown"
    else
        echo "  --    $name: systemd-analyze not installed, key check skipped"
    fi
done

[ "$fails" -eq 0 ] && { echo "all unit checks passed"; exit 0; }
echo "$fails unit check(s) failed"; exit 1
