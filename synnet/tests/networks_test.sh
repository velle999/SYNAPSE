#!/usr/bin/env bash
# networks_test.sh — which networks the firewall trusts, and asking about new
# ones.
#
# A private address is not "my network": café Wi-Fi hands out 192.168.x too.
# A physical interface NetworkManager manages is dropped unless the connection
# on it is trusted; virtual interfaces keep the private-source rule; the tailnet
# is accepted. Driven with a stub nmcli, a fake /sys/class/net, a recording nft,
# and stub zenity/pkexec — no root, no NetworkManager, no display.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u
export LC_ALL=C.UTF-8
unset LANGUAGE

here=$(cd "$(dirname "$0")" && pwd)
SYNNET=${1:-$here/../_b/synnet}
[ -x "$SYNNET" ] || { echo "SKIP: no synnet binary at $SYNNET"; exit 77; }
SYNNET=$(readlink -f "$SYNNET")

AS_ROOT=()
if [ "$(id -u)" != 0 ]; then
    command -v fakeroot >/dev/null 2>&1 || { echo "SKIP: needs root or fakeroot"; exit 77; }
    AS_ROOT=(fakeroot)
fi

pass=0 fails=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fails=$((fails + 1)); }
has()   { case "$2" in *"$1"*) ok "$3" ;; *) bad "$3" ;; esac; }
hasnt() { case "$2" in *"$1"*) bad "$3" ;; *) ok "$3" ;; esac; }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/sys/wlp6s0/device" "$T/sys/enp7s0/device" \
         "$T/sys/enp9s0/device" "$T/sys/docker0" "$T/state"
export SYNNET_NMCLI="$T/bin/nmcli" SYNNET_SYSFS_NET="$T/sys"
export SYNNET_FW_NETWORKS_FILE="$T/trusted-networks"
export SYNNET_NETWORKS_STATE_FILE="$T/networks"
export SYNNET_FW_STATE_FILE="$T/firewall.state" SYNNET_FW_PREF_FILE="$T/pref"
export SYNNET_FW_IFACES_FILE="$T/trusted-ifaces" SYNNET_FW_PORTS_FILE="$T/open-ports"
export NFT_LOG="$T/nft.log" ASK_LOG="$T/ask.log" XDG_STATE_HOME="$T/state"
export PATH="$T/bin:$PATH"

CAFE=51e4f81d-8de6-4752-a3fb-e36406a569ed
HOME_=11111111-2222-3333-4444-555555555555

# The café's name has a ':' in it, which nmcli -e yes sends as '\:'. enp9s0 is
# physical but not managed; docker0 is virtual; the last line names a device a
# hostile or broken nmcli could print, which must never reach the nft script.
cat > "$T/bin/nmcli" <<EOF
#!/bin/sh
case "\$*" in
*"device status"*)
    printf '%s\n' \
      'wlp6s0:wifi:connected:$CAFE:Caf\\:e Wi-Fi' \
      'enp7s0:ethernet:connected:$HOME_:Home' \
      'enp9s0:ethernet:unmanaged::' \
      'docker0:bridge:connected (externally):aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee:docker0' \
      'wl"x;drop:wifi:connected:$CAFE:evil' ;;
*"connection show"*)
    printf '%s\n' '$CAFE:Caf\\:e Wi-Fi' '$HOME_:Home' \
      '99999999-2222-3333-4444-555555555555:Twin' \
      '88888888-2222-3333-4444-555555555555:Twin' ;;
esac
EOF
printf '#!/bin/sh\ncat >> "$NFT_LOG" 2>/dev/null; exit 0\n' > "$T/bin/nft"
cat > "$T/bin/zenity" <<'EOF'
#!/bin/sh
printf 'ZENITY %s\n' "$*" >> "$ASK_LOG"; exit "${ZENITY_RC:-1}"
EOF
printf '#!/bin/sh\nprintf "PKEXEC %%s\\n" "$*" >> "$ASK_LOG"; exit 0\n' > "$T/bin/pkexec"
chmod +x "$T/bin/"*
printf 'tcp/22 any\n' > "$SYNNET_FW_PORTS_FILE"

apply() { : > "$NFT_LOG"; "${AS_ROOT[@]}" "$SYNNET" --firewall >/dev/null 2>&1; cat "$NFT_LOG"; }
line_of() { grep -n -- "$1" <<<"$2" | head -1 | cut -d: -f1; }

echo "=== nobody trusted yet ==="
rules=$(apply)
has 'iifname "wlp6s0" drop' "$rules" "the café interface is dropped"
has 'iifname "enp7s0" drop' "$rules" "…and so is a wired network nobody has trusted"
hasnt 'iifname "docker0" drop' "$rules" "a virtual interface keeps the private-source rule"
hasnt 'iifname "enp9s0" drop' "$rules" "an interface NetworkManager does not manage is left alone"
hasnt 'wl"x' "$rules" "a device name that is not a legal one never reaches the script"
has 'iifname "tailscale0" accept' "$rules" "the tailnet is accepted"
drop=$(line_of 'iifname "wlp6s0" drop' "$rules")
lan=$(line_of 'saddr { 10.0.0.0/8' "$rules")
port=$(line_of 'dport 22 accept' "$rules")
dhcp=$(line_of 'dport { 68, 546 }' "$rules")
[ -n "$drop" ] && [ -n "$lan" ] && [ "$drop" -lt "$lan" ] \
    && ok "the drop comes before the private-source accept" \
    || bad "the private-source accept is ahead of the drop — the café gets in (drop=$drop lan=$lan)"
[ -n "$port" ] && [ "$port" -lt "$drop" ] \
    && ok "an --open port comes before the drop, so it works on any network" \
    || bad "an --open port is behind the drop (port=$port drop=$drop)"
[ -n "$dhcp" ] && [ "$dhcp" -lt "$drop" ] \
    && ok "DHCP comes before the drop, so an untrusted network can still lease" \
    || bad "DHCP is behind the drop"
st=$(cat "$SYNNET_FW_STATE_FILE")
has "trust=networks" "$st" "the state file says trust is per network"
has "untrusted=2" "$st" "…and how many interfaces are untrusted"
net=$(cat "$SYNNET_NETWORKS_STATE_FILE")
has "$CAFE	untrusted	wlp6s0	Caf:e Wi-Fi" "$net" \
    "the published list names the café, its name unescaped"

echo "=== trusting and untrusting ==="
out=$("${AS_ROOT[@]}" "$SYNNET" --trust-network Home 2>&1)
has "trusting “Home”" "$out" "--trust-network by name says so"
has "$HOME_ Home" "$(cat "$SYNNET_FW_NETWORKS_FILE")" "…and records the uuid with the name"
rules=$(cat "$NFT_LOG")
: > "$NFT_LOG"; "${AS_ROOT[@]}" "$SYNNET" --firewall >/dev/null 2>&1; rules=$(cat "$NFT_LOG")
hasnt 'iifname "enp7s0" drop' "$rules" "a trusted network is no longer dropped"
has 'iifname "wlp6s0" drop' "$rules" "…while the café still is"
has "untrusted=1" "$(cat "$SYNNET_FW_STATE_FILE")" "the count follows"

"${AS_ROOT[@]}" "$SYNNET" --trust-network "$HOME_" >/dev/null 2>&1
n=$(grep -c "$HOME_" "$SYNNET_FW_NETWORKS_FILE")
[ "$n" = 1 ] && ok "trusting twice keeps one line" || bad "trusting twice wrote $n lines"

out=$("${AS_ROOT[@]}" "$SYNNET" --untrust-network Home 2>&1)
has "no longer trusting “Home”" "$out" "--untrust-network says so"
hasnt "$HOME_" "$(cat "$SYNNET_FW_NETWORKS_FILE")" "…and removes the line"
has "# NetworkManager connections synnet trusts" "$(cat "$SYNNET_FW_NETWORKS_FILE")" \
    "…keeping the file's comments"

out=$("${AS_ROOT[@]}" "$SYNNET" --trust-network Twin 2>&1); rc=$?
[ $rc != 0 ] && has "more than one connection" "$out" \
    "a name on two connections is refused, not guessed" || bad "an ambiguous name was accepted"
out=$("${AS_ROOT[@]}" "$SYNNET" --trust-network Nowhere 2>&1); rc=$?
[ $rc != 0 ] && has "no saved connection" "$out" \
    "an unknown name is refused" || bad "an unknown name was accepted"
if [ "$(id -u)" != 0 ]; then
    out=$("$SYNNET" --trust-network Home 2>&1); rc=$?
    [ $rc != 0 ] && has "needs root" "$out" "--trust-network needs root" \
        || bad "--trust-network ran without root"
fi

echo "=== when NetworkManager does not answer ==="
printf '#!/bin/sh\nexit 8\n' > "$T/bin/nmcli-dead"; chmod +x "$T/bin/nmcli-dead"
: > "$NFT_LOG"
SYNNET_NMCLI="$T/bin/nmcli-dead" "${AS_ROOT[@]}" "$SYNNET" --firewall >/dev/null 2>&1
rules=$(cat "$NFT_LOG")
hasnt "synnet-untrusted" "$rules" "no network is known, so nothing is dropped by interface"
has 'saddr { 10.0.0.0/8' "$rules" "…and the old private-source rule applies"
has "trust=lan" "$(cat "$SYNNET_FW_STATE_FILE")" "the state file says so"
has "# NetworkManager did not answer" "$(cat "$SYNNET_NETWORKS_STATE_FILE")" \
    "…and so does the published list"
"${AS_ROOT[@]}" "$SYNNET" --firewall >/dev/null 2>&1   # back to a known state

echo "=== --reapply, for the NetworkManager hook ==="
: > "$NFT_LOG"
if [ "$(id -u)" != 0 ]; then
    "$SYNNET" --reapply; rc=$?
    [ $rc = 0 ] && [ ! -s "$NFT_LOG" ] && ok "as a user it does nothing, quietly" \
        || bad "--reapply as a user rc=$rc or touched nft"
fi
echo off > "$SYNNET_FW_PREF_FILE"
"${AS_ROOT[@]}" "$SYNNET" --reapply; rc=$?
[ $rc = 0 ] && [ ! -s "$NFT_LOG" ] && ok "with the firewall switched off it does nothing" \
    || bad "--reapply with the firewall off rc=$rc or loaded a chain"
rm -f "$SYNNET_FW_PREF_FILE"
"${AS_ROOT[@]}" "$SYNNET" --reapply; rc=$?
[ $rc = 0 ] && grep -q synnet-untrusted "$NFT_LOG" \
    && ok "with it on, it re-applies" || bad "--reapply did not re-apply (rc=$rc)"

echo "=== asking ==="
: > "$ASK_LOG"
WAYLAND_DISPLAY=w ZENITY_RC=1 "$SYNNET" --ask
WAYLAND_DISPLAY=w ZENITY_RC=1 "$SYNNET" --ask
n=$(grep -c ZENITY "$ASK_LOG")
# enp7s0 (Home) and wlp6s0 (the café) are both untrusted now: two questions,
# each asked once however many times the unit fires.
[ "$n" = 2 ] && ok "each untrusted network is asked about once; \"Don't trust\" is remembered" \
    || bad "asked $n time(s) over two runs, wanted 2"
has "$CAFE" "$(cat "$T/state/synnet/declined")" "the answer is kept in the user's state"
hasnt "PKEXEC" "$(cat "$ASK_LOG")" "…and saying no needs no password"
has "Trust the network “Caf:e Wi-Fi”?" "$(cat "$ASK_LOG")" "the question names the network"

rm -f "$T/state/synnet/declined"; : > "$ASK_LOG"
WAYLAND_DISPLAY=w ZENITY_RC=0 "$SYNNET" --ask
has "PKEXEC /usr/bin/synnet --trust-network $CAFE" "$(cat "$ASK_LOG")" \
    "Trust goes through pkexec with the uuid"
[ ! -s "$T/state/synnet/declined" ] && ok "…and records no decline" \
    || bad "Trust was recorded as a decline"

: > "$ASK_LOG"
env -u WAYLAND_DISPLAY -u DISPLAY "$SYNNET" --ask
[ ! -s "$ASK_LOG" ] && ok "with no display nobody is asked" || bad "a dialog without a display"

"${AS_ROOT[@]}" "$SYNNET" --trust-network "$CAFE" >/dev/null 2>&1
"${AS_ROOT[@]}" "$SYNNET" --trust-network Home >/dev/null 2>&1
rm -f "$T/state/synnet/declined"; : > "$ASK_LOG"
WAYLAND_DISPLAY=w ZENITY_RC=1 "$SYNNET" --ask
[ ! -s "$ASK_LOG" ] && ok "a trusted network is never asked about" || bad "asked about a trusted network"

echo ""
echo "  $pass passed, $fails failed"
[ "$fails" -eq 0 ]
