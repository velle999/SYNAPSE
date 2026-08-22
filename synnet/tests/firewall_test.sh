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
# ⚠ POINTED AT A FIXTURE BEFORE ANYTHING RUNS. The package ships
# /etc/synnet/trusted-ifaces with waydroid0 in it, so without this override a
# developer's box would put two extra rules into every ruleset asserted below
# and CI's container would not — the same test proving different things
# depending on where it ran.
export SYNNET_FW_IFACES_FILE="$tmp/trusted-ifaces"

# ⚠ THIS RUNS BOTH AS A USER AND AS ROOT. CI is a container running as root,
# a developer is not, and the first version of this file assumed the second:
# every assertion about "needs root" wording failed the moment it ran in CI.
#
# So the REGRESSION itself is tested without depending on privilege at all. The
# bug was "any nft failure is read as absence", and a stub `nft` that exits
# non-zero produces that failure for either user. Privilege only decides which
# WORDING is expected, and those few checks say which case they are in.
AM_ROOT=$([ "$(id -u)" = 0 ] && echo yes || echo no)
echo "running as root: $AM_ROOT"

mkdir -p "$tmp/bin"
export NFT_LOG="$tmp/nft.log"

# nft that cannot answer. Whatever the reason — no CAP_NET_ADMIN, no binary, a
# container without nf_tables — synnet must not turn it into a claim that the
# firewall is not there.
stub_nft_failing() {
    cat > "$tmp/bin/nft" <<'STUB'
#!/bin/sh
echo "nft: Operation not permitted" >&2
exit 1
STUB
    chmod +x "$tmp/bin/nft"
}

# nft that works, and records the ruleset it was handed (it arrives on stdin
# from a heredoc). ⚠ Must exit 0, or synnet reports failure and never gets as
# far as publishing the state file.
stub_nft_recording() {
    cat > "$tmp/bin/nft" <<'STUB'
#!/bin/sh
printf '%s\n' "ARGV: $*" >> "$NFT_LOG"
cat >> "$NFT_LOG" 2>/dev/null
exit 0
STUB
    chmod +x "$tmp/bin/nft"
}

echo ""
echo "=== --status tells the truth about the firewall ==="

# ── 1. THE REGRESSION ───────────────────────────────────────────────────────
# nft cannot answer, and nothing has been published. synnet knows two things
# here and neither of them is "the daemon has not run".
stub_nft_failing
out=$(PATH="$tmp/bin:$PATH" "$SYNNET" --status 2>&1)
hasnt "daemon has not run" "$out" \
      "an nft that cannot answer is not reported as an absent firewall"
has "Input firewall" "$out" "…and the input firewall is reported at all"

PATH="$tmp/bin:$PATH" "$SYNNET" --status >/dev/null 2>&1
[ $? = 0 ] && ok "a status that cannot see everything still exits 0" \
            || bad "status exits non-zero when it cannot read the ruleset"

# The wording depends on WHY it cannot look, and that depends on privilege.
if [ "$AM_ROOT" = no ]; then
    has "needs root" "$out" "as a user, it says the live view needs root"
else
    hasnt "needs root" "$out" "as root, it does not tell root to become root"
fi

# ── 2. the three states the daemon can publish ──────────────────────────────
printf 'state=active\npolicy=drop\ntrust=lan\nsince=1787000000\nreasserts=0\n' \
    > "$SYNNET_FW_STATE_FILE"
out=$("$SYNNET" --status 2>&1)
has "ACTIVE" "$out" "an asserted firewall reads as ACTIVE"
hasnt "rebuilt" "$out" "…with no rebuild warning when it has not been rebuilt"

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
# --firewall refuses below root, on purpose: it loads a chain. As root we are
# already there; as a user, fakeroot fakes exactly the geteuid() the guard
# reads, and no real privilege is involved either way.
stub_nft_recording
: > "$NFT_LOG"
if [ "$AM_ROOT" = yes ]; then
    PATH="$tmp/bin:$PATH" "$SYNNET" --firewall >/dev/null 2>&1
    ran=yes
elif command -v fakeroot >/dev/null 2>&1; then
    PATH="$tmp/bin:$PATH" fakeroot "$SYNNET" --firewall >/dev/null 2>&1
    ran=yes
else
    echo "  SKIP  the ruleset checks need root or fakeroot to pass the guard"
    ran=no
fi

if [ "$ran" = yes ]; then
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
echo "=== container / VM links get DHCP and DNS, and nothing else ==="
#
# THE BUG THIS HALF EXISTS FOR (2026-08-22). A Waydroid guest came up with no
# network. The LAN-trust rule accepts 192.168/16 and the guest's bridge is
# 192.168.240.0/24, so everything it sends is trusted — once it HAS an address.
# The packet that asks for one is sent from 0.0.0.0 to 255.255.255.255:67,
# matches nothing, and hits the drop policy. waydroid-net.sh's own
# `iptables -I INPUT -i waydroid0 --dport 67 -j ACCEPT` does not rescue it:
# that is a different base chain, and a packet traverses them all — an accept in
# one only ends that chain, while our drop is final.

if [ "$ran" = yes ]; then
    printf '# comment line\nwaydroid0\n\n   virbr0   # trailing comment\nwaydroid0\nbad name!\n../etc/passwd\n' \
        > "$SYNNET_FW_IFACES_FILE"
    : > "$NFT_LOG"
    if [ "$AM_ROOT" = yes ]; then
        PATH="$tmp/bin:$PATH" "$SYNNET" --firewall >/dev/null 2>&1
    else
        PATH="$tmp/bin:$PATH" fakeroot "$SYNNET" --firewall >/dev/null 2>&1
    fi
    rules=$(cat "$NFT_LOG")

    has 'iifname "waydroid0" udp dport { 53, 67, 547 } accept' "$rules" \
        "a trusted link accepts DHCP — the packet the drop policy was eating"
    has 'iifname "waydroid0" tcp dport { 53, 67 } accept' "$rules" \
        "…and DNS over TCP, which dnsmasq falls back to"
    has 'iifname "virbr0"' "$rules" "an entry with a trailing comment is read"

    # ⚠ THE ONE THAT MATTERS MOST. `iif` resolves the name to an ifindex when
    # the rule is LOADED and errors with "Interface does not exist" otherwise —
    # and these bridges are created when the container starts, hours after this
    # chain came up at boot. The chain is one atomic `nft -f`, so a single `iif`
    # on an absent interface does not lose that rule, it aborts the load and the
    # box ends up with NO TABLE AT ALL. Verified against the real nft in a
    # netns: the whole ruleset vanishes.
    # ⚠ single-quoted: a backtick inside a double-quoted string is a command
    # substitution, and "iif" is not a command.
    hasnt 'iif "waydroid0"' "$rules" \
        'links are matched by NAME — iif on an absent bridge kills the load'

    # Not `allow in on <iface>`: an addressed guest is already trusted by the
    # RFC1918 rule, so a blanket accept would add nothing but would open every
    # host port to whatever the guest runs.
    hasnt 'iifname "waydroid0" accept' "$rules" \
        "a trusted link is not trusted wholesale, only for the gateway services"

    # A name that cannot be pasted into a ruleset costs its own line and nothing
    # else. If it reached nft it would be a syntax error, and a syntax error in
    # an atomic load unfilters the machine.
    hasnt "bad name" "$rules" "an illegal interface name never reaches nft"
    hasnt "passwd" "$rules" "…nor does a path-shaped one"
    has "policy drop" "$rules" "…and the firewall still comes up without them"

    # Two identical entries would stack two identical rules on every apply.
    n=$(printf '%s\n' "$rules" | grep -c 'iifname "waydroid0" udp')
    [ "$n" = 1 ] && ok "a name listed twice produces one rule, not two" \
                 || bad "duplicate entries stack rules (got $n)"

    has "links=2" "$(cat "$SYNNET_FW_STATE_FILE" 2>/dev/null)" \
        "the published state records how many links were applied"

    # No file is the normal state of a machine running no containers, and must
    # not be an error, a warning, or a missing firewall.
    rm -f "$SYNNET_FW_IFACES_FILE"
    : > "$NFT_LOG"
    if [ "$AM_ROOT" = yes ]; then
        PATH="$tmp/bin:$PATH" "$SYNNET" --firewall >/dev/null 2>&1
    else
        PATH="$tmp/bin:$PATH" fakeroot "$SYNNET" --firewall >/dev/null 2>&1
    fi
    rules=$(cat "$NFT_LOG")
    hasnt "iifname" "$rules" "no list, no link rules"
    has "policy drop" "$rules" "…and the base firewall is unaffected"
fi

echo ""
echo "=== --trust-if / --untrust-if ==="

if [ "$ran" = yes ]; then
    run_as_root() {
        if [ "$AM_ROOT" = yes ]; then PATH="$tmp/bin:$PATH" "$SYNNET" "$@"
        else PATH="$tmp/bin:$PATH" fakeroot "$SYNNET" "$@"; fi
    }

    printf '# keep me\nvirbr0\n' > "$SYNNET_FW_IFACES_FILE"
    run_as_root --trust-if waydroid0 >/dev/null 2>&1
    body=$(cat "$SYNNET_FW_IFACES_FILE")
    has "waydroid0" "$body" "--trust-if records the link"
    has "# keep me" "$body" \
        "…without eating the comments in a file meant to be hand-edited"

    # Applying it is the point: the daemon's re-assert tick only rebuilds a
    # chain that has GONE, so a chain that is merely out of date looks healthy
    # and would keep dropping the container's DHCP until the next reboot.
    has 'iifname "waydroid0"' "$(cat "$NFT_LOG")" \
        "…and reloads the chain there and then, rather than at the next reboot"

    run_as_root --trust-if waydroid0 >/dev/null 2>&1
    n=$(grep -c '^waydroid0' "$SYNNET_FW_IFACES_FILE")
    [ "$n" = 1 ] && ok "adding the same link twice is idempotent" \
                 || bad "--trust-if stacked duplicate entries (got $n)"

    printf 'waydroid0   # android\n' > "$SYNNET_FW_IFACES_FILE"
    run_as_root --untrust-if waydroid0 >/dev/null 2>&1
    hasnt "waydroid0" "$(cat "$SYNNET_FW_IFACES_FILE")" \
        "--untrust-if matches the ENTRY, not the raw line, so a commented one goes"

    out=$(run_as_root --trust-if 'no good' 2>&1)
    has "not a legal interface name" "$out" \
        "an illegal name is refused at the CLI, not pasted into a ruleset"

    printf 'waydroid0\n' > "$SYNNET_FW_IFACES_FILE"
    out=$("$SYNNET" --status 2>&1)
    has "waydroid0" "$out" "--status lists the trusted links"
    has "matched by name" "$out" \
        "…and says an absent bridge is expected, not a typo"

    rm -f "$SYNNET_FW_IFACES_FILE"
    out=$("$SYNNET" --status 2>&1)
    has "none" "$out" "…and says so when there are none"

    if [ "$AM_ROOT" = no ]; then
        out=$("$SYNNET" --trust-if waydroid0 2>&1)
        has "needs root" "$out" "--trust-if below root says so"
    fi
fi

echo ""
echo "=== the guard rails ==="

if [ "$AM_ROOT" = no ]; then
    out=$("$SYNNET" --firewall 2>&1)
    has "needs root" "$out" \
        "--firewall below root says so rather than failing at nft"
else
    echo "  SKIP  the below-root guard cannot be checked while running as root"
fi

printf '%d passed, %d failed\n' "$pass" "$fails"
[ "$fails" -eq 0 ]
