#!/usr/bin/env bash
# syn_remote_test.sh — the wrapper, without a network and without a desktop.
#
# syn-remote is thin on purpose: wayvnc does the VNC and synui already hands it
# capture and input. What is HERE is the part a wrapper has to get right, and
# every one of these has a silent failure behind it:
#
#   - WHERE IT LISTENS. synnet's default-drop input chain accepts everything
#     from 10/8, 172.16/12 and 192.168/16, so a bind address that quietly moved
#     to 0.0.0.0 would put a desktop on the LAN with nothing saying so.
#   - WHO CAN READ THE PASSWORD. The settings file holds it, and it is written
#     through a temporary on every change — chmod'ing once at creation left it
#     world-readable after the next `listen`.
#   - WHAT COMES OUT OF `password`. It is read as `PW=$(syn-remote password)`,
#     and the first run of that also makes the TLS certificate; a progress line
#     on stdout ends up inside the password.
#   - WAKING THE SCREEN. A blanked output cannot be captured at all, so without
#     the wake-on-connect a viewer gets a black rectangle it cannot click out
#     of. Driven here against stubs, because the real thing needs a compositor.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
SR="$here/../syn-remote.sh"
[ -x "$SR" ] || { echo "ABORT no syn-remote.sh beside the suite"; exit 2; }

pass=0 fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 — expected [$2], got [$3]"; fi; }

T=$(mktemp -d /tmp/synremote.XXXXXX)
trap 'rm -rf "$T"' INT TERM EXIT
export XDG_CONFIG_HOME="$T/config" XDG_RUNTIME_DIR="$T/run"
mkdir -p "$XDG_RUNTIME_DIR"

# ⚠ NO systemctl, NO wayvnc, NO wlopm on PATH unless a case puts one there.
# This suite must not enable a unit or open a port on the machine running it.
stub="$T/bin"; mkdir -p "$stub"
cat > "$stub/systemctl" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$SR_TEST_LOG"
case "$*" in *is-active*) exit 3 ;; *is-enabled*) echo disabled; exit 1 ;; esac
exit 0
EOF
chmod +x "$stub/systemctl"
export SR_TEST_LOG="$T/systemctl.log"
PATH="$stub:$PATH"

echo "syn-remote"

# ── 1. where it listens ───────────────────────────────────
check "the default is this machine only" "127.0.0.1" "$("$SR" status --rec | awk -F'\t' '$1=="address"{print $2}')"
check "...which the record calls local"  "local"     "$("$SR" status --rec | awk -F'\t' '$1=="scope"{print $2}')"

"$SR" listen lan >/dev/null 2>&1
check "listen lan opens it to the network" "0.0.0.0" "$("$SR" status --rec | awk -F'\t' '$1=="address"{print $2}')"
check "...and the record says so"          "lan"     "$("$SR" status --rec | awk -F'\t' '$1=="scope"{print $2}')"
"$SR" listen local >/dev/null 2>&1
check "...and listen local closes it again" "127.0.0.1" "$("$SR" status --rec | awk -F'\t' '$1=="address"{print $2}')"

# ── 2. the password, and who can read it ──────────────────
#
# ⛔ CAPTURED THE WAY A SCRIPT CAPTURES IT, stderr discarded. Anything this
# prints that is not the password lands inside somebody's password.
pw=$("$SR" password 2>/dev/null)
check "password prints the password and nothing else" 20 "${#pw}"
case "$pw" in *[!A-Za-z0-9]*) bad "the generated password has something odd in it: [$pw]" ;;
              *) ok "...twenty characters of it" ;; esac

conf="$XDG_CONFIG_HOME/syn-remote/syn-remote.conf"
check "the file holding it is 0600" "600" "$(stat -c '%a' "$conf" 2>/dev/null)"
# ⛔ AFTER ANOTHER WRITE, which is where this actually broke: the settings file
# is replaced through a temporary, so a chmod at creation does not survive.
"$SR" listen lan >/dev/null 2>&1; "$SR" listen local >/dev/null 2>&1
check "...and still 0600 after a later change" "600" "$(stat -c '%a' "$conf" 2>/dev/null)"

"$SR" password "hunter2" >/dev/null 2>&1
check "a password can be set by hand" "hunter2" "$("$SR" password 2>/dev/null)"
new=$("$SR" password new 2>/dev/null)
[ "$new" != "hunter2" ] && [ ${#new} -eq 20 ] && ok "...and rolled" || bad "password new did not roll it"

# ⛔ ONE LINE PER SETTING. A pasted value carrying a newline would become a
# second setting silently, and the value people paste is the password.
out=$("$SR" password "$(printf 'a\nb')" 2>&1)
grep -q newline <<<"$out" && ok "a value with a newline in it is refused" \
                          || bad "a newline was accepted into the settings file"

# ── 3. the config wayvnc is actually given ────────────────
SYN_REMOTE_SOURCE_ONLY=1 . "$SR"
ensure_credentials >/dev/null 2>&1
write_wayvnc_config
wv="$XDG_CONFIG_HOME/wayvnc/config"
check "wayvnc's config is written 0600" "600" "$(stat -c '%a' "$wv" 2>/dev/null)"
# enable_auth REQUIRES all three (wayvnc(1)); two of them is a server that
# refuses to start, and none of them is a desktop on the LAN with no password.
for k in enable_auth certificate_file private_key_file password address port; do
    grep -q "^$k=" "$wv" && ok "...and carries $k" || bad "the generated config has no $k"
done
check "auth is on" "enable_auth=true" "$(grep '^enable_auth=' "$wv")"
[ -s "$CERT" ] && [ -s "$KEY" ] && ok "the TLS certificate and key exist" \
                                || bad "no certificate was made"
check "the private key is 0600" "600" "$(stat -c '%a' "$KEY" 2>/dev/null)"

# ── 4. PAM instead of a second secret ─────────────────────
set_setting pam on
write_wayvnc_config
grep -q '^enable_pam=true' "$wv" && ok "auth pam asks PAM instead" || bad "enable_pam not written"
grep -q '^password=' "$wv" && bad "the password is still in the config under PAM" \
                           || ok "...and the password is left out of the config"
set_setting pam off
write_wayvnc_config
grep -q '^password=' "$wv" && ok "...and comes back when PAM is off" || bad "password not restored"

# ── 5. the record ─────────────────────────────────────────
#
# ⚠ NEVER TRANSLATED, and matched on by the window: the first row names the
# columns and the values are identifiers.
rec=$("$SR" status --rec)
check "the record's first row names the columns" "field	value" "$(head -1 <<<"$rec")"
for f in running atlogin connections address port scope auth session wayvnc; do
    grep -q "^$f	" <<<"$rec" || bad "the record has no $f row"
done
ok "...and carries every field the window reads"
check "a machine with no wayvnc says so" "no" "$(awk -F'\t' '$1=="wayvnc"{print $2}' <<<"$rec")"

# ── 6. THE WHOLE POINT: waking a blanked screen ───────────
#
# ⛔ A BLANKED OUTPUT CANNOT BE CAPTURED — measured on synui, where `grim`
# answers "failed to copy output" once the idle blank stage has fired. So a
# connection has to turn the outputs back on and hold an inhibitor, or an
# unattended machine is a black rectangle ten minutes after the last keypress.
#
# Driven against a fake event stream, because the real one needs a compositor,
# a server and somebody connecting to it.
cat > "$stub/wlopm" <<EOF
#!/bin/sh
printf 'wlopm %s\n' "\$*" >> "$T/actions.log"
EOF
cat > "$stub/wayvncctl" <<'EOF'
#!/bin/sh
printf '%s\n' '{"method":"client-connected","params":{"connection_count":1}}'
printf '%s\n' '{"method":"client-connected","params":{"connection_count":2}}'
printf '%s\n' '{"method":"client-disconnected","params":{"connection_count":1}}'
printf '%s\n' '{"method":"client-disconnected","params":{"connection_count":0}}'
EOF
cat > "$stub/fake-inhibit" <<EOF
#!/bin/sh
while IFS= read -r -n1 c; do printf 'inhibit %s\n' "\$c" >> "$T/actions.log"; done
EOF
chmod +x "$stub/wlopm" "$stub/wayvncctl" "$stub/fake-inhibit"
IDLE_INHIBIT="$stub/fake-inhibit"
STATE="$T/state"
watch_clients
sleep 0.5
acts=$(tr '\n' ' ' < "$T/actions.log" 2>/dev/null)

case "$acts" in *"wlopm --on"*) ok "a connection turns the outputs back on" ;;
                *) bad "nothing woke the screen: [$acts]" ;; esac
case "$acts" in *"inhibit 1"*) ok "...and holds the machine awake" ;;
                *) bad "no idle inhibitor was taken: [$acts]" ;; esac
case "$acts" in *"inhibit 0"*) ok "...and releases it when the last one leaves" ;;
                *) bad "the inhibitor was never released: [$acts]" ;; esac
# ⚠ ONCE, NOT PER CLIENT. Two viewers is one screen: waking and inhibiting
# again on the second is harmless, but releasing on the FIRST disconnect while
# somebody is still watching is not.
check "the screen is woken once, not per viewer" 1 "$(grep -c 'wlopm --on' "$T/actions.log")"
check "...and released once, when the count reaches zero" 1 "$(grep -c 'inhibit 0' "$T/actions.log")"
check "the state file follows the count" "connections=0" "$(cat "$STATE" 2>/dev/null)"

# ── 7. the unit ───────────────────────────────────────────
unit="$here/../syn-remote.service"
grep -q '^ExecStart=/usr/bin/syn-remote run$' "$unit" &&
    ok "the unit runs the wrapper, not wayvnc directly" ||
    bad "the unit's ExecStart is not 'syn-remote run'"
grep -q '^WantedBy=default.target' "$unit" &&
    ok "...as a user unit" || bad "the unit is not WantedBy=default.target"

echo ""
if [ "$fail" -eq 0 ]; then echo "all $pass syn-remote checks passed"; else echo "$fail of $((pass+fail)) failed"; fi
exit $(( fail > 0 ))
