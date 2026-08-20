#!/usr/bin/env bash
# firewall_test.sh — the box IS firewalled, and its status command says so
#
# THE BUG THIS EXISTS FOR (2026-08-20). synnet has carried a default-drop input
# firewall for a while and applies it at every start. `synnet --status` reported
# "(no synnet table loaded — daemon has not run or nft unavailable)" anyway,
# because listing an nftables object needs CAP_NET_ADMIN and the old status ran
# `nft list` as whoever typed it, then treated ANY failure as "not there". So a
# fully firewalled box, with the daemon running and "base input firewall active"
# in its own journal, answered its own status command by denying it — which is
# how the firewall came to be believed missing.
#
# Two classes of check here, and both avoid needing root:
#
#   1. WHAT --status SAYS, driven through $SYNNET_FW_STATE_FILE, which points
#      the published-state reader at a fixture. Every state the daemon can
#      publish, plus the unprivileged case that started this.
#   2. WHAT THE FIREWALL ACTUALLY IS, by putting a stub `nft` first on PATH and
#      running `synnet --firewall`. The stub records the ruleset it was handed,
#      so the shape of the chain — default drop, and exactly what is trusted —
#      is asserted against the real script rather than against a copy of it
#      here. ⚠ That is the half a grep of the source could not do honestly: the
#      script is built from #defines and a heredoc, and reading it back out of
#      the C is reading the recipe, not the meal.
#
# Usage: firewall_test.sh [path/to/synnet]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
SYNNET=${1:-$here/../_b/synnet}
[ -x "$SYNNET" ] || { echo "SKIP: no synnet binary at $SYNNET"; exit 77; }
SYNNET=$(readlink -f "$SYNNET")

pass=0 fails=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fails=$((fails + 1)); }
has() { case "$2" in *"$1"*) ok "$3" ;; *) bad "$3" ;; esac; }
hasnt() { case "$2" in *"$1"*) bad "$3" ;; *) ok "$3" ;; esac; }

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
export SYNNET_FW_STATE_FILE="$tmp/firewall.state"

echo "=== --status tells the truth about the firewall ==="

# ── 1. THE REGRESSION ───────────────────────────────────────────────────────
# Nothing published, run unprivileged. It must not claim the daemon has not run
# — it cannot know that from here, and saying so is what hid a live firewall.
out=$("$SYNNET" --status 2>&1)
hasnt "daemon has not run" "$out" \
      "an unprivileged status does not claim the daemon has not run"
has "Input firewall" "$out" "…and reports the input firewall at all"
has "needs root" "$out" "…and says plainly that the live view needs root"

# Exit 0: a status command that cannot see everything has still answered.
"$SYNNET" --status >/dev/null 2>&1
[ $? = 0 ] && ok "an unprivileged status still exits 0" \
            || bad "an unprivileged status exits non-zero"

# ── 2. the three states the daemon can publish ──────────────────────────────
printf 'state=active\npolicy=drop\ntrust=lan\nsince=1787000000\nreasserts=0\n' \
    > "$SYNNET_FW_STATE_FILE"
out=$("$SYNNET" --status 2>&1)
has "ACTIVE" "$out" "an asserted firewall reads as ACTIVE"
hasnt "⚠ rebuilt" "$out" "…with no rebuild warning when it has not been rebuilt"

printf 'state=active\nreasserts=7\n' > "$SYNNET_FW_STATE_FILE"
out=$("$SYNNET" --status 2>&1)
has "rebuilt 7 time(s)" "$out" \
    "a firewall that keeps vanishing says so, with the count"

printf 'state=failed\n' > "$SYNNET_FW_STATE_FILE"
out=$("$SYNNET" --status 2>&1)
has "NOT ingress-filtered" "$out" "a failed apply says the box is unfiltered"
hasnt "ACTIVE" "$out" "…and does not also claim to be active"

rm -f "$SYNNET_FW_STATE_FILE"
out=$("$SYNNET" --status 2>&1)
has "not asserted" "$out" "no state file reads as not asserted"

echo ""
echo "=== the ruleset synnet actually loads ==="

# ── 3. what --firewall hands to nft ─────────────────────────────────────────
#
# A stub nft that records its stdin (the ruleset arrives on a heredoc) and its
# argv. ⚠ It must exit 0, or synnet reports a failure and never gets as far as
# writing the state file.
mkdir -p "$tmp/bin"
cat > "$tmp/bin/nft" <<'STUB'
#!/bin/sh
printf '%s\n' "ARGV: $*" >> "$NFT_LOG"
cat >> "$NFT_LOG" 2>/dev/null
exit 0
STUB
chmod +x "$tmp/bin/nft"
export NFT_LOG="$tmp/nft.log"
: > "$NFT_LOG"

# ⚠ --firewall refuses below root, on purpose — it loads a chain. `fakeroot`
# only fakes uid to the process itself, which is exactly what geteuid() reads,
# so it is the right tool here and no real privilege is involved.
if ! command -v fakeroot >/dev/null 2>&1; then
    echo "  SKIP  the ruleset checks need fakeroot to get past the root guard"
else
    PATH="$tmp/bin:$PATH" fakeroot "$SYNNET" --firewall >/dev/null 2>&1
    rules=$(cat "$NFT_LOG")

    has "type filter hook input priority 0 ; policy drop ;" "$rules" \
        "the input chain is a base chain with a DROP policy"
    has 'iif "lo" accept' "$rules" "loopback is trusted"
    has "ct state established,related accept" "$rules" \
        "replies to our own connections are trusted"
    has "meta l4proto icmpv6 accept" "$rules" \
        "ICMPv6 is trusted — without it IPv6 neighbour discovery breaks"
    has "10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16" "$rules" \
        "RFC1918 sources are trusted, so the LAN stays reachable"
    has "fc00::/7, fe80::/10" "$rules" "IPv6 ULA and link-local too"
    has "udp dport { 68, 546 } accept" "$rules" \
        "DHCP replies are trusted, which arrive before we hold a LAN address"

    # ⚠ The add/delete/re-add idiom is what makes a restart idempotent. Without
    # the delete, redefining the chain in place STACKS its rules every boot and
    # the ruleset grows without bound while still appearing to work.
    has "delete chain inet synnet input" "$rules" \
        "the chain is torn down before it is rebuilt, so restarts do not stack"

    # It must not flush the whole ruleset: the egress `blocked` set lives in the
    # same table and holds every IP the AI has flagged.
    hasnt "flush ruleset" "$rules" "it never flushes the whole ruleset"

    # …and the state file is published, which is the only reason an
    # unprivileged --status can answer at all.
    [ -f "$SYNNET_FW_STATE_FILE" ] \
        && ok "applying the firewall publishes its state" \
        || bad "no state file after --firewall; unprivileged status stays blind"
    has "state=active" "$(cat "$SYNNET_FW_STATE_FILE" 2>/dev/null)" \
        "…and it records the firewall as active"
fi

echo ""
echo "=== the guard rails ==="

out=$("$SYNNET" --firewall 2>&1)
has "needs root" "$out" "--firewall below root says so rather than failing at nft"

printf '%d passed, %d failed\n' "$pass" "$fails"
[ "$fails" -eq 0 ]
