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
# ⛔ PREPENDING TO PATH CANNOT HIDE A BINARY THAT IS REALLY THERE. This
# assertion used to read the ordinary environment and expect "no", which is
# true only on a machine where wayvnc happens not to be installed — so it
# passed in CI and FAILED on any developer box that has the server this package
# depends on. The absent case has to be BUILT: a PATH holding everything the
# script calls with wayvnc removed, which means a directory of links rather
# than a guess at which coreutils it uses. A guess that misses one turns this
# into a check of whether `sed` exists.
farm="$T/nowayvnc"; mkdir -p "$farm"
for f in /usr/bin/*; do ln -s "$f" "$farm/" 2>/dev/null; done
rm -f "$farm/wayvnc"
check "a machine with no wayvnc says so" "no" \
      "$(PATH="$stub:$farm" "$SR" status --rec | awk -F'\t' '$1=="wayvnc"{print $2}')"
# ...and it only means anything if it says yes when wayvnc IS there.
#
# ⛔ THE PRESENT CASE HAS TO BE BUILT TOO. This read the ordinary environment
# and expected "yes", which is true only on a machine that happens to have the
# server installed — so it passed on every developer box and FAILED IN CI,
# where nothing installs wayvnc. Both halves of a two-sided assertion have to
# be constructed, or the suite is testing the machine it is running on.
: > "$farm/wayvnc"; chmod +x "$farm/wayvnc"
check "...and says so the other way round too" "yes" \
      "$(PATH="$stub:$farm" "$SR" status --rec | awk -F'\t' '$1=="wayvnc"{print $2}')"

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

# ⛔ AND A STALE FILE NEVER CLAIMS A VIEWER. The bar reads this count; a state
# file outliving the server it describes would leave an indicator saying
# somebody is watching this screen over a server that is not there — worse than
# no indicator, and the failure synui's Recording.qml has a paragraph about.
printf 'connections=3\n' > "$XDG_RUNTIME_DIR/syn-remote.state"
check "a stale count reads as nobody while the server is stopped" "0" \
      "$("$SR" status --rec | awk -F'\t' '$1=="connections"{print $2}')"
rm -f "$XDG_RUNTIME_DIR/syn-remote.state"

# ── 7. the unit ───────────────────────────────────────────
unit="$here/../syn-remote.service"
grep -q '^ExecStart=/usr/bin/syn-remote run$' "$unit" &&
    ok "the unit runs the wrapper, not wayvnc directly" ||
    bad "the unit's ExecStart is not 'syn-remote run'"
grep -q '^WantedBy=default.target' "$unit" &&
    ok "...as a user unit" || bad "the unit is not WantedBy=default.target"

# ⛔ THE UNIT IS STARTED BEFORE THE COMPOSITOR EXISTS, so its first seconds are
# a race it is MEANT to lose. With systemd's default start limit — five starts
# in ten seconds — it burned every retry inside that race and gave up for the
# whole login: enabled, and dead, until somebody toggled it off and on. Both
# halves of that fix are load-bearing and neither is visible in the running
# system until a reboot, which is why they are asserted here.
grep -q '^StartLimitIntervalSec=0$' "$unit" &&
    ok "the unit has no start limit, so a lost race is not permanent" ||
    bad "the unit can still exhaust its start limit while synui is coming up"
grep -q '^Restart=always$' "$unit" &&
    ok "...and restarts even on a clean exit, so a new login is picked up" ||
    bad "Restart is not 'always'; a clean exit would strand the unit inactive"

# ⚠ AND THE WRAPPER WAITS RATHER THAN DYING. `run` used to die the instant
# $XDG_RUNTIME_DIR/synui-display was missing. Driven here with no compositor
# and a one-second budget: it must spend that second and then say what it was
# waiting for, not exit at once.
#
# ⛔ WAYLAND_DISPLAY IS UNSET FOR THIS CASE, and wayvnc is stubbed. wayland_socket
# falls back to $WAYLAND_DISPLAY when there is no synui-display file, so a suite
# run from inside a desktop would find the LIVE socket, sail past the wait and
# exec a REAL VNC server on the machine running the tests — a test that opens a
# port on the developer's desktop. The stub is the second lock: even if the
# wait were skipped, nothing that serves anything can start from here.
cat > "$stub/wayvnc" <<'EOF'
#!/bin/sh
echo "STUB WAYVNC SHOULD NOT HAVE RUN" >&2
exit 97
EOF
chmod +x "$stub/wayvnc"
( unset WAYLAND_DISPLAY
  SYN_REMOTE_SESSION_WAIT=1 "$SR" run >"$T/run.out" 2>"$T/run.err" )
check "...and no server was started to find that out" 0 \
      "$(grep -c 'STUB WAYVNC' "$T/run.err")"
check "run refuses without a session, by name" 1 \
      "$(grep -c 'no Wayland session after' "$T/run.err")"
check "...after waiting for one rather than dying at once" 1 \
      "$(grep -c 'waiting up to' "$T/run.err")"

# ── 8. saved connections ──────────────────────────────────
#
# The other half of this package: reaching somebody ELSE's desktop. What is
# checked here is what a connection manager can silently get wrong.

echo "=== the other end ==="

HOSTS_F="$XDG_CONFIG_HOME/syn-remote/hosts"
SECR_F="$XDG_CONFIG_HOME/syn-remote/secrets"

"$SR" add couch 192.168.1.40:5901 velle >/dev/null 2>&1
"$SR" add attic attic.local        >/dev/null 2>&1

check "add records host and port"      "192.168.1.40" "$("$SR" hosts --tsv | awk -F'\t' '$1=="couch"{print $2}')"
check "...and the port it was given"   "5901"         "$("$SR" hosts --tsv | awk -F'\t' '$1=="couch"{print $3}')"
check "...and the user"                "velle"        "$("$SR" hosts --tsv | awk -F'\t' '$1=="couch"{print $4}')"
check "a host with no port gets 5900"  "5900"         "$("$SR" hosts --tsv | awk -F'\t' '$1=="attic"{print $3}')"

# The header row is why an empty list can be told from a broken command.
#
# ⛔ PINNED IN FULL, AND mac IS LAST. The window and the TUI index these columns
# by position, so a column inserted anywhere but the end shifts every one after
# it — a saved password read as a port number, in a build where nothing failed.
check "the record names its columns" "name	host	port	user	secret	pinned	mac" \
      "$("$SR" hosts --tsv | head -1)"

# ⛔ A NAME IS A KEY IN THREE FILES. A tab would split a record and a newline
# would forge one, so the name is restricted rather than escaped.
"$SR" add "two words" host >/dev/null 2>&1
check "a name with a space is refused" "" "$("$SR" hosts --tsv | awk -F'\t' '$1 ~ / /{print $1}')"
"$SR" add "$(printf 'tab\there')" host >/dev/null 2>&1
check "a name with a tab is refused"   2 "$("$SR" hosts --tsv | tail -n +2 | wc -l)"
"$SR" add nope host:99999 >/dev/null 2>&1
check "a port over 65535 is refused"   2 "$("$SR" hosts --tsv | tail -n +2 | wc -l)"

check "the connection list is 0600" "600" "$(stat -c '%a' "$HOSTS_F" 2>/dev/null)"

# ── 9. ⛔ the secret-tool trap ─────────────────────────────
#
# THE ONE THAT MATTERS. With no keyring daemon running, secret-tool prints
# "The name is not activatable" to stderr and EXITS 0 — measured on this
# desktop, which has secret-tool installed and no daemon started. Anything that
# branches on its exit status therefore reports a password saved when there is
# no password anywhere, which for a credential store is the worst failure
# available. The write has to be read back and compared.

cat > "$stub/secret-tool" <<'EOF'
#!/bin/sh
# The measured failure, exactly: a complaint on stderr, nothing on stdout, 0.
echo "secret-tool: The name is not activatable" >&2
exit 0
EOF
chmod +x "$stub/secret-tool"

printf 'hunter2\n' | "$SR" saved couch set >/dev/null 2>&1
check "a keyring that answers nothing is NOT reported as the store" "file" \
      "$("$SR" saved couch)"
check "...and the password is actually retrievable afterwards" "hunter2" \
      "$( SYN_REMOTE_SOURCE_ONLY=1; . "$SR"; secret_get couch )"
check "the file holding it is 0600" "600" "$(stat -c '%a' "$SECR_F" 2>/dev/null)"
check "...and does not hold the password in the clear" "0" \
      "$(grep -c hunter2 "$SECR_F" 2>/dev/null)"

# A password carrying a tab would split the record it is stored in. base64 is
# what makes that survive — the reason it is there, and not a security claim.
odd=$(printf 'a\tb c')
( SYN_REMOTE_SOURCE_ONLY=1; . "$SR"; secret_file_put couch "$odd" )
check "a password containing a tab round-trips" "$odd" \
      "$( SYN_REMOTE_SOURCE_ONLY=1; . "$SR"; secret_get couch )"

# ── 10. a keyring that DOES work ──────────────────────────
#
# And when one answers, it wins — and the copy on disk is removed rather than
# left as a second, stale answer to the same question, which is the one that
# would be found first if the keyring were ever locked.
cat > "$stub/secret-tool" <<'EOF'
#!/bin/sh
# A keyring that actually stores, backed by files the suite can see.
#
# ⛔ KEYED BY THE `host` ATTRIBUTE, exactly as the real secret-tool is. The
# first version of this stub kept ONE file and ignored the attributes, so every
# lookup answered with the last password stored — which made a connection with
# no password of its own appear to have one, and would have let a genuine
# cross-contamination bug through green. A stub looser than the thing it stands
# in for tests nothing.
op=$1; shift
name=
while [ $# -gt 0 ]; do
  case "$1" in
    host) name=$2; shift ;;
  esac
  shift
done
f="$SR_TEST_KEYRING.$name"
case "$op" in
  store)  cat > "$f" ;;
  lookup) [ -s "$f" ] && cat "$f" ;;
  clear)  rm -f "$f" ;;
esac
exit 0
EOF
chmod +x "$stub/secret-tool"
export SR_TEST_KEYRING="$T/keyring"

printf 'sekrit\n' | "$SR" saved couch set >/dev/null 2>&1
check "a working keyring is used, and named"   "keyring" "$("$SR" saved couch)"
check "...and the copy on disk is taken away"  "0" \
      "$(grep -c '^couch	' "$SECR_F" 2>/dev/null)"
check "...and the password reads back"         "sekrit" \
      "$( SYN_REMOTE_SOURCE_ONLY=1; . "$SR"; secret_get couch )"

# ── 11. forgetting takes the password with it ─────────────
#
# A forgotten connection whose secret stays behind leaves a credential nothing
# can reach — and one that would come back attached to a host it was never
# meant for, if the same name were ever added again.
"$SR" forget couch >/dev/null 2>&1
check "forget drops the connection" "" \
      "$("$SR" hosts --tsv | awk -F'\t' '$1=="couch"{print $1}')"
check "...and its password with it" "" "$(cat "$SR_TEST_KEYRING.couch" 2>/dev/null)"

# ── 12. ⛔ the password never reaches argv ─────────────────
#
# An argument is world-visible in `ps` for as long as the viewer runs, and the
# environment is readable through /proc/PID/environ for the same window. The
# password goes down a pipe, which is read once and gone.
"$SR" add probe 10.0.0.9 someone >/dev/null 2>&1
printf 'topsecret\n' | "$SR" saved probe set >/dev/null 2>&1

# ⚠ A PIN IS NOW A PRECONDITION OF CONNECTING, so these have to provide one.
# A connection with no trusted certificate is refused before the viewer is ever
# reached — which is the whole point of section 12b — so without this the argv
# checks below would be testing the refusal, not the launch.
mkdir -p "$XDG_CONFIG_HOME/syn-remote/certs"
: > "$XDG_CONFIG_HOME/syn-remote/certs/probe.pem"
printf 'x\n' > "$XDG_CONFIG_HOME/syn-remote/certs/probe.pem"

cat > "$T/fakeviewer" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" > "$SR_TEST_ARGV"
cat > "$SR_TEST_STDIN"
EOF
chmod +x "$T/fakeviewer"
export SR_TEST_ARGV="$T/argv.txt" SR_TEST_STDIN="$T/stdin.txt"
(
    SYN_REMOTE_SOURCE_ONLY=1; . "$SR"
    VIEWER="$T/fakeviewer"
    cmd_connect probe
) >/dev/null 2>&1

check "the password arrives on the viewer's stdin" "topsecret" \
      "$(cat "$SR_TEST_STDIN" 2>/dev/null)"
check "...and NOT anywhere in its arguments" "0" \
      "$(grep -c topsecret "$SR_TEST_ARGV" 2>/dev/null)"
grep -q -- "--host 10.0.0.9" "$SR_TEST_ARGV" &&
    ok "the viewer is told which host" || bad "the viewer was not given --host"
grep -q -- "--user someone" "$SR_TEST_ARGV" &&
    ok "...and which user" || bad "the viewer was not given --user"

# An entry with NO saved password must still open — an empty pipe means
# "nothing stored", and the viewer draws its own prompt.
"$SR" add bare 10.0.0.10 >/dev/null 2>&1
printf 'x\n' > "$XDG_CONFIG_HOME/syn-remote/certs/bare.pem"
( SYN_REMOTE_SOURCE_ONLY=1; . "$SR"; VIEWER="$T/fakeviewer"; cmd_connect bare ) >/dev/null 2>&1
check "a connection with no password still launches the viewer" "" \
      "$(cat "$SR_TEST_STDIN" 2>/dev/null)"
grep -q -- "--host 10.0.0.10" "$SR_TEST_ARGV" &&
    ok "...with the host it was given" || bad "the viewer was not launched for it"

# ── 12b. ⛔ the TLS handshake, which is what actually broke ─
#
# Nothing could connect at all, and every layer looked healthy: the port was
# open, the firewall passed it, wayvnc answered RFB 003.008. Three faults, all
# in the handshake, all producing the SAME symptom and no client-side error —
# "Client handshake timed out" in the SERVER's journal, which reads like a
# network problem. Each one is pinned here.

view="$here/../syn-remote-view.c"

echo "=== the TLS handshake ==="

# ⛔ 1. THE CERTIFICATE HAS TO NAME THE ADDRESS A CLIENT DIALS. gtk-vnc calls
# gnutls_x509_crt_check_hostname, so a certificate with no subjectAltName can
# never validate against an IP. Measured against the old one: 127.0.0.1 and the
# LAN address both failed "IP address mismatch"; only the literal "synapseos"
# passed, and nothing resolves that.
grep -q 'addext "subjectAltName=' "$SR" &&
    ok "the certificate carries subjectAltNames" ||
    bad "the certificate is generated with no SAN — no client can validate it"
grep -q 'IP:127.0.0.1' "$SR" &&
    ok "...including the loopback address" ||
    bad "the SAN list omits 127.0.0.1"
grep -q "ip -4 -o addr show scope global" "$SR" &&
    ok "...and every address this machine actually has" ||
    bad "the SAN list does not include this machine's real addresses"

# ⛔ 2. `hostname` IS NOT INSTALLED ON SynapseOS. `$(hostname || echo ...)`
# therefore ALWAYS took the fallback, so every certificate ever issued named
# the wrong host. uname is coreutils and always answers.
! grep -q 'CN=$(hostname' "$SR" &&
    ok "the CN does not come from the absent \`hostname\` binary" ||
    bad "the CN still uses \`hostname\`, which is not installed here"
grep -q 'CN=$(uname -n' "$SR" &&
    ok "...it comes from uname -n" || bad "the CN does not come from uname -n"

# ⚠ 3. A MOVED ADDRESS INVALIDATES A CERTIFICATE THAT WAS CORRECT. Same
# symptom, and a DHCP lease is enough to cause it.
# ── the names the certificate vouches for ─────────────────
#
# ⛔ EVERY GENERATED SAN IS AN ADDRESS THE MACHINE HOLDS, which is exactly the
# set that is wrong when it is reached through anything else. A port forward
# presents a public IP the box has never seen and a DDNS name resolves to one;
# a viewer validates against what it DIALLED, so neither can ever match. These
# drive the escape hatch against a real certificate.
NT="$T/names"; mkdir -p "$NT"
names() { XDG_CONFIG_HOME="$NT" "$SR" "$@" 2>&1; }
ncert="$NT/syn-remote/cert.pem"
sanline() { openssl x509 -in "$ncert" -noout -ext subjectAltName 2>/dev/null | tail -1; }

names names add myhouse.duckdns.org >/dev/null
names names add 203.0.113.7 >/dev/null
case "$(sanline)" in
    *"DNS:myhouse.duckdns.org"*) ok "an added name reaches the certificate" ;;
    *) bad "the added name is not in the certificate" ;;
esac
# ⛔ CLASSIFIED, NOT GUESSED. An address written as a DNS name never matches:
# gnutls checks IP SANs for an IP and DNS SANs for a name, and the two are
# separate fields.
case "$(sanline)" in
    *"IP Address:203.0.113.7"*) ok "...and an address goes in as an IP, not a name" ;;
    *) bad "the added address was not classified as an IP" ;;
esac
# ⛔ THE ANCHORED LOOKUP. openssl prints the SANs on ONE comma-separated line,
# so an unanchored grep for "DNS:synapse" is satisfied by "DNS:synapse.local".
# Get that wrong and every run decides a present name is missing and re-issues
# the certificate — which silently breaks every client that pinned the last one.
before=$(openssl x509 -in "$ncert" -noout -fingerprint -sha256 2>/dev/null)
names names add 203.0.113.7 >/dev/null
check "adding a name twice does not re-issue the certificate" "$before" \
      "$(openssl x509 -in "$ncert" -noout -fingerprint -sha256 2>/dev/null)"
# And the local addresses survive the extras being added.
case "$(sanline)" in
    *"IP Address:127.0.0.1"*) ok "...and the generated addresses are still there" ;;
    *) bad "adding a name dropped the machine's own addresses" ;;
esac
# ⛔ REMOVING MUST FORCE A RE-ISSUE. The re-issue check asks whether every
# WANTED name is present; a name that is no longer wanted is still present, so
# nothing would notice and the certificate would go on vouching for it.
names names remove 203.0.113.7 >/dev/null
case "$(sanline)" in
    *"203.0.113.7"*) bad "a removed address is still in the certificate" ;;
    *) ok "removing a name re-issues without it" ;;
esac
# ⛔ THE VALUE REACHES openssl INSIDE -addext, so a comma forges a second SAN.
names names add 'evil,DNS:bank.example.com' >/dev/null 2>&1
case "$(sanline)" in
    *bank.example.com*) bad "a comma in a name forged a second SAN" ;;
    *) ok "a name that could forge a SAN is refused" ;;
esac

# ⚠ 3. A MOVED ADDRESS INVALIDATES A CERTIFICATE THAT WAS CORRECT. Same
# symptom, and a DHCP lease is enough to cause it.
grep -q "This machine's address changed" "$SR" &&
    ok "a certificate is re-issued when the address moves" ||
    bad "nothing re-issues the certificate when this machine's address changes"

# ⛔ 4. THE VIEWER'S RETURN CHECK. vnc_display_set_credential returns non-zero
# on FAILURE despite being declared gboolean — gtk-vnc's own gvncviewer writes
# `if (vnc_display_set_credential(...)) { failed }`. Inverted, every success
# became an abort, and because CLIENTNAME is asked for first the viewer bailed
# out before the certificate was ever sent.
! grep -q 'if (!vnc_display_set_credential' "$view" &&
    ok "the viewer does not treat a set_credential success as failure" ||
    bad "the viewer inverts vnc_display_set_credential — non-zero means FAILURE"

# ⛔ 5. AND IT MUST ANSWER CA_CERT_DATA, which is the only way a self-signed
# server can be validated at all.
grep -q 'VNC_DISPLAY_CREDENTIAL_CA_CERT_DATA' "$view" &&
    ok "the viewer answers the CA certificate credential" ||
    bad "the viewer never answers CA_CERT_DATA — the handshake stalls"
grep -q '"cacert"' "$view" &&
    ok "...from a pinned file given on the command line" ||
    bad "the viewer has no way to be given a pinned certificate"

# ── 12c. trust on first use ───────────────────────────────
"$SR" add tls 10.0.0.44 >/dev/null 2>&1
check "a new connection starts unpinned" "no" \
      "$("$SR" hosts --tsv | awk -F'\t' '$1=="tls"{print $6}')"
# ⛔ NEVER ACCEPT A CERTIFICATE NOBODY HAS SEEN. Without a terminal there is no
# one to compare the fingerprint against, and a pin taken on trust is a record
# of whoever answered the port rather than of the machine somebody meant.
# ⛔ NEVER ACCEPT A CERTIFICATE NOBODY HAS SEEN. Driven through the source seam
# so the REAL fetcher runs — pointing GETCERT at the installed path would test
# whether this machine happens to have syn-remote installed.
out=$(
    SYN_REMOTE_SOURCE_ONLY=1; . "$SR"
    GETCERT="$here/../syn-remote-getcert.py"
    cmd_trust tls 2>&1 </dev/null
)
case "$out" in
    *"not a terminal"*|*"could not fetch"*|*"sent no certificate"*)
        ok "trust refuses to pin without a human, or says it could not reach the host" ;;
    *)  bad "trust produced something unexpected: $out" ;;
esac
# And connect must SAY so rather than hand the viewer a doomed connection.
#
# ⛔ WITH A VIEWER IN PLACE, which is why this calls the function rather than
# the script. `connect` checks that the viewer exists before it asks about the
# certificate, so on a machine where the package is not INSTALLED — CI, every
# time — the refusal under test was replaced by "the viewer is missing" and
# this case failed for a reason that has nothing to do with pinning. The
# override is the same one section 6 uses for the idle inhibitor: a shell
# variable in a subshell, not an environment seam the shipped script would
# have to trust.
touch "$stub/fakeviewer"; chmod +x "$stub/fakeviewer"
out=$(VIEWER="$stub/fakeviewer" cmd_connect tls 2>&1 </dev/null)
case "$out" in
    *"no trusted certificate"*) ok "connect refuses an unpinned server, by name" ;;
    *) bad "connect did not refuse an unpinned server: $out" ;;
esac
grep -q -- '--cacert "$pin"' "$SR" &&
    ok "connect hands the viewer the pinned certificate" ||
    bad "connect does not pass --cacert"

# ⚠ A QUESTION MUST NOT MINT A KEY. `fingerprint` reads this machine's own
# certificate out loud; on a box that only ever connects OUT, generating a
# 2048-bit key and a ten-year certificate as a side effect of asking is wrong.
# ⚠ IN A FRESH CONFIG DIR. Earlier cases in this suite have already made a
# certificate here, and asking whether one EXISTS is meaningless in a directory
# that already has one.
fresh="$T/fresh"; mkdir -p "$fresh"
out=$(XDG_CONFIG_HOME="$fresh" "$SR" fingerprint 2>&1 </dev/null)
case "$out" in
    *"no certificate yet"*) ok "fingerprint does not create a certificate just to answer" ;;
    *) bad "fingerprint said: $out" ;;
esac
[ -e "$fresh/syn-remote/cert.pem" ] &&
    bad "fingerprint created a certificate as a side effect" ||
    ok "...and left none behind"
# ...and it does read a real one out when there is one.
[ -n "$("$SR" fingerprint 2>/dev/null)" ] &&
    ok "...but does read out the certificate this machine has" ||
    bad "fingerprint printed nothing for a machine that has a certificate"

# Forgetting takes the pin with it, like the password.
"$SR" forget tls >/dev/null 2>&1
[ -e "$XDG_CONFIG_HOME/syn-remote/certs/tls.pem" ] &&
    bad "forget left the pinned certificate behind" ||
    ok "forget drops the pinned certificate too"

# ── 13. the window and the launcher ───────────────────────
gui="$here/../syn-remote-gui.sh"
qml="$here/../shell.qml"
desktop="$here/../syn-remote.desktop"

# ⚠ ALL THREE MUST AGREE. synui's dock looks a pinned app up as
# "<app_id>.desktop" and never consults StartupWMClass — so an app_id that does
# not equal this file's basename is a pin that draws correctly and does nothing.
check "the launcher sets QS_APP_ID" 1 \
      "$(grep -c 'QS_APP_ID="${QS_APP_ID:-syn-remote}"' "$gui")"
check "...matching the .desktop basename" "syn-remote" \
      "$(basename "$desktop" .desktop)"
check "...and StartupWMClass" "syn-remote" \
      "$(sed -n 's/^StartupWMClass=//p' "$desktop")"
grep -q '^Exec=syn-remote-gui$' "$desktop" &&
    ok "the entry launches the wrapper, not qs directly" ||
    bad "the .desktop Exec is not syn-remote-gui"

# ⛔ A VIEW THAT SCROLLS SHOWS THAT IT SCROLLS. Also gated by
# tools/preflight.sh, and checked here so the suite fails where the file is.
grep -q 'ScrollBar.vertical: SynScrollBar' "$qml" &&
    ok "the connection list has a scrollbar" ||
    bad "the list in shell.qml scrolls with no scrollbar"

# The window must not own the credential decision — it shells out for every one
# of them, so there is exactly one answer to "where is my password".
check "the window reads the list through the CLI" 1 \
      "$(grep -c '"syn-remote", "hosts", "--tsv"' "$qml")"
grep -q 'TextField' "$qml" && ! grep -q 'echoMode: TextInput.Password' "$qml" &&
    ok "no password field in the window — it cannot reach the store safely" ||
    bad "shell.qml grew a password field; the password would cross argv"

# ⛔ CONNECT CANNOT BE A SILENT NO-OP. `syn-remote connect` asks about an
# unchecked certificate, and asking needs a tty; run straight from the window
# it printed its refusal on stderr and exited, and the window read neither. The
# button did nothing, and the only trace anywhere was "Client handshake timed
# out" in the SERVER's journal — a symptom that reads like a firewall.
grep -q 'function connectHost' "$qml" &&
    ok "the window has a connect path that knows about pinning" ||
    bad "shell.qml connects without checking whether the host is pinned"
grep -q '"syntty", "--hold", "-e", "syn-remote", "connect"' "$qml" &&
    ok "...and an unpinned host is connected through a terminal, so it can ask" ||
    bad "an unpinned Connect never reaches a tty; the button would do nothing"
# The general form of the same bug: any command that refuses must be readable.
awk '/id: actProc/,/^    }/' "$qml" | grep -q 'stderr: StdioCollector' &&
    ok "every button's failure is collected, not discarded" ||
    bad "actProc throws stderr away; a refused command looks like a dead button"

# ── 13b. the certificate fetcher's TLS ────────────────────
#
# ⛔ NOT VERIFYING IS THE POINT; NEGOTIATING ANYTHING IS NOT. This fetch exists
# to obtain the certificate that verification would need, so it cannot verify —
# and that makes the version it negotiates the only protection left. Python's
# default minimum has moved between releases, so it is stated in the file
# rather than inherited from whichever interpreter the machine has.
getcert="$here/../syn-remote-getcert.py"
grep -q 'minimum_version = ssl.TLSVersion.TLSv1_2' "$getcert" &&
    ok "the certificate fetcher will not go below TLS 1.2" ||
    bad "syn-remote-getcert does not state a minimum TLS version"

# ── 14. the viewer's own reasons ──────────────────────────
grep -q 'VNC_DISPLAY_CREDENTIAL_PASSWORD' "$view" &&
    ok "the viewer answers the password credential" ||
    bad "the viewer never answers VNC_DISPLAY_CREDENTIAL_PASSWORD"
# ⚠ A CREDENTIAL LEFT UNSET STALLS THE HANDSHAKE — the server waits for an
# answer that never comes and the window sits on "Connecting" with nothing
# visibly wrong.
grep -q 'VNC_DISPLAY_CREDENTIAL_CLIENTNAME' "$view" &&
    ok "...and the clientname one, which would otherwise stall it" ||
    bad "the viewer ignores CLIENTNAME, which stalls some servers"
# A wrong stored password must not become an infinite ask-answer-refuse loop.
grep -q 'creds_refused' "$view" &&
    ok "a refused password is not offered a second time" ||
    bad "the viewer would re-send a password the server already refused"

# ── 15. waking a machine that is asleep ───────────────────
#
# ⛔ NOT ONE PACKET LEAVES THIS MACHINE. A magic packet is a BROADCAST, so a
# suite that sent a real one would wake whatever is listening on the network of
# whoever is running the tests — including a build machine in somebody else's
# office. Everything below drives the wrapper's own logic against stand-ins,
# and the packet itself is asserted byte for byte against a fake socket.
#
# ⛔ AND NOT ONE LIVE SETTING IS TOUCHED. `wakeable on` writes two places — the
# card's flag and the NetworkManager profile — and both are seamed here. The
# last case in this section asserts the real `nmcli` was never called at all,
# because a suite that quietly reconfigured the developer's network while
# printing "ok" is the worst outcome available.

wolstate="$T/wol.state"; echo yes > "$wolstate"
cat > "$stub/wolhelper" <<'EOF'
#!/bin/sh
case "$1" in
  get) ;;
  set) [ "${WOL_SUPPORTED:-yes}" = no ] && [ "$3" = on ] && exit 2
       printf '%s' "$3" | sed 's/^on$/yes/;s/^off$/no/' > "$WOL_STATE" ;;
  *)   exit 1 ;;
esac
printf 'field\tvalue\ninterface\t%s\nsupported\t%s\nmagic\t%s\n' \
       "$2" "${WOL_SUPPORTED:-yes}" "$(cat "$WOL_STATE")"
EOF
chmod +x "$stub/wolhelper"

cat > "$stub/nmclistub" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$NM_LOG"
case "$*" in
  *"NAME,DEVICE connection show --active"*) echo "Wired connection 1:eth0" ;;
  *"802-3-ethernet.wake-on-lan connection show"*)
      echo "802-3-ethernet.wake-on-lan:$(cat "$NM_SETTING")" ;;
  *"connection modify"*)
      printf '%s' "$*" | sed -n 's/.*wake-on-lan \([a-z]*\).*/\1/p' > "$NM_SETTING" ;;
esac
exit 0
EOF
chmod +x "$stub/nmclistub"

# A fixture of network interfaces, because the real /sys/class/net on the
# machine running this is whatever that machine happens to have — and the thing
# under test is precisely which one gets picked out of a mixed bag.
net="$T/net"; mkdir -p "$net"
mkdir -p "$net/lo"                                        # no device link at all
mkdir -p "$net/docker0"; echo 1 > "$net/docker0/carrier"  # a bridge: DEVTYPE, no device
printf 'DEVTYPE=bridge\n' > "$net/docker0/uevent"
mkdir -p "$net/wlan0/device" "$net/wlan0/phy80211"        # wireless: WoWLAN is not this
echo 1 > "$net/wlan0/carrier"
mkdir -p "$net/eth1/device"; echo 0 > "$net/eth1/carrier" # wired, but no cable
mkdir -p "$net/eth0/device"; echo 1 > "$net/eth0/carrier" # the answer
printf 'INTERFACE=eth0\n' > "$net/eth0/uevent"
echo "aa:bb:cc:dd:ee:ff" > "$net/eth0/address"

export NM_LOG="$T/nmcli.log" NM_SETTING="$T/nm.setting" WOL_STATE="$wolstate"
echo default > "$NM_SETTING"
wk() { SYN_REMOTE_NET_DIR="$net" SYN_REMOTE_WOL_HELPER="$stub/wolhelper" \
       SYN_REMOTE_NMCLI="$stub/nmclistub" "$SR" wakeable "$@"; }

check "the wired interface with a link is the one picked" "eth0" \
      "$(wk --rec | awk -F'\t' '$1=="interface"{print $2}')"
# Each of these was a real candidate for being picked by accident.
check "...and its address is read from it" "aa:bb:cc:dd:ee:ff" \
      "$(wk --rec | awk -F'\t' '$1=="mac"{print $2}')"

# ⛔ THE STATE IS ASKED OF THE CARD, NOT OF THE SETTING. These two disagree in
# exactly the case that matters — armed now, forgotten at the next boot — and a
# switch that reports its own setting back to itself is how a feature says "on"
# for four releases with nothing behind it.
echo no > "$wolstate"; echo magic > "$NM_SETTING"
check "a disarmed card reads as disarmed even when the profile says magic" "no" \
      "$(wk --rec | awk -F'\t' '$1=="armed"{print $2}')"
check "...and the profile is reported separately, not instead" "magic" \
      "$(wk --rec | awk -F'\t' '$1=="remembered"{print $2}')"

# ── on and off write BOTH places ──────────────────────────
echo no > "$wolstate"; echo none > "$NM_SETTING"
wk on >/dev/null 2>&1
check "wakeable on arms the card now" "yes" "$(cat "$wolstate")"
check "...and tells NetworkManager to do it again at every activation" "magic" \
      "$(cat "$NM_SETTING")"
wk off >/dev/null 2>&1
check "wakeable off disarms the card"  "no"   "$(cat "$wolstate")"
check "...and stops NetworkManager re-arming it"  "none" "$(cat "$NM_SETTING")"

# ⛔ THE CARD FIRST, THE PROFILE ONLY IF IT TOOK. A card that cannot do magic
# packets must not leave a NetworkManager profile behind claiming it can — that
# profile outlives the machine's memory of the failure.
echo none > "$NM_SETTING"
out=$(WOL_SUPPORTED=no wk on 2>&1)
grep -q 'cannot be woken' <<<"$out" &&
    ok "a card that cannot do it says so" ||
    bad "wakeable on was silent about a card with no magic-packet support"
check "...and wrote nothing to NetworkManager" "none" "$(cat "$NM_SETTING")"

# ⛔ "COULD NOT ASK" IS NOT "NO". With the helper missing — a half-installed
# package, or a kernel that refused — an answer of "this card cannot be woken"
# would be a hardware verdict reached from a missing file.
out=$(SYN_REMOTE_NET_DIR="$net" SYN_REMOTE_WOL_HELPER="$T/not-installed" \
      SYN_REMOTE_NMCLI="$stub/nmclistub" "$SR" wakeable 2>&1)
grep -q 'not known' <<<"$out" &&
    ok "a card that could not be asked is reported as unknown" ||
    bad "wakeable turned a missing helper into a verdict about the card: $out"
grep -q 'could not be run' <<<"$out" &&
    ok "...and says which helper it could not run" ||
    bad "nothing said why the card could not be asked"
check "...and the record says unknown too" "unknown" \
      "$(SYN_REMOTE_NET_DIR="$net" SYN_REMOTE_WOL_HELPER="$T/not-installed" \
         SYN_REMOTE_NMCLI="$stub/nmclistub" "$SR" wakeable --rec |
         awk -F'\t' '$1=="armed"{print $2}')"

# ── the packet ────────────────────────────────────────────
#
# ⚠ THE REAL FUNCTION, EXTRACTED FROM THE SCRIPT. The python is lifted out of
# syn-remote.sh verbatim and run against a fake socket module, so this cannot
# drift from what actually ships — a copy of the packet-building code here
# would pass forever after the original was changed.
fake="$T/fakesock"; mkdir -p "$fake"
cat > "$fake/socket.py" <<'EOF'
import json, os
AF_INET = 2; SOCK_DGRAM = 2; SOL_SOCKET = 1; SO_BROADCAST = 6
class socket:
    def __init__(self, *a): self.opts = []
    def setsockopt(self, lvl, opt, val): self.opts.append((lvl, opt, val))
    def sendto(self, data, addr):
        with open(os.environ["SENT"], "a") as f:
            f.write(json.dumps({"len": len(data), "hex": data.hex(),
                                "to": addr[0], "port": addr[1],
                                "bcast": (SOL_SOCKET, SO_BROADCAST, 1) in self.opts}) + "\n")
EOF
awk '/^magic_packet\(\)/,/^}/' "$SR" | awk '/<<.PY.$/{p=1;next} /^PY$/{p=0} p' > "$T/packet.py"
[ -s "$T/packet.py" ] && ok "the packet builder was found in the script" \
                      || bad "could not extract magic_packet's python from syn-remote.sh"
export SENT="$T/sent.jsonl"; : > "$SENT"
PYTHONPATH="$fake" python3 "$T/packet.py" "aa:bb:cc:dd:ee:ff" "192.0.2.9" 2>/dev/null
# 6 bytes of 0xFF and the address sixteen times over: 6 + 96 = 102.
check "the packet is a magic packet" "102" \
      "$(awk -F'"len": ' 'NR==1{print $2+0}' "$SENT")"
check "...six 0xFF and the address sixteen times" \
      "ffffffffffff$(for i in $(seq 16); do printf 'aabbccddeeff'; done)" \
      "$(python3 -c 'import json,sys;print(json.loads(open(sys.argv[1]).readline())["hex"])' "$SENT")"
# ⛔ WITHOUT SO_BROADCAST THE KERNEL REFUSES THE SEND, and it refuses it
# silently enough that the wrapper would report a packet it never put on
# the wire.
check "...sent with SO_BROADCAST set" "true" \
      "$(python3 -c 'import json,sys;print(str(json.loads(open(sys.argv[1]).readline())["bcast"]).lower())' "$SENT")"
check "...to the broadcast address and to the host itself" "255.255.255.255 192.0.2.9" \
      "$(python3 -c '
import json,sys
seen=[]
for l in open(sys.argv[1]):
    d=json.loads(l)
    if d["to"] not in seen: seen.append(d["to"])
print(" ".join(seen))' "$SENT")"

# ── what is saved, and what is refused ────────────────────
"$SR" add wakeme 192.0.2.50 --mac AA-BB-CC-DD-EE-01 >/dev/null 2>&1
check "a hardware address is saved with the connection" "AA-BB-CC-DD-EE-01" \
      "$("$SR" hosts --tsv | awk -F'\t' '$1=="wakeme"{print $7}')"
# ⛔ AND THE EMPTY COLUMN BEFORE IT IS STILL A COLUMN. `wakeme` was saved with
# no user name, and tab is an IFS *whitespace* character — so a run of two tabs
# read as one, the empty user field vanished, and every column after it moved
# one to the left: the hardware address arrived in the user's place. It is read
# on a non-whitespace separator for that reason, and this is the case that
# says so. The same collapse was already giving the TUI the wrong password
# state for any host saved without a user name.
check "...and the empty user column before it is still empty" "" \
      "$("$SR" hosts --tsv | awk -F'\t' '$1=="wakeme"{print $4}')"
check "...with the columns still in their places" "none" \
      "$("$SR" hosts --tsv | awk -F'\t' '$1=="wakeme"{print $5}')"
out=$("$SR" add nope 192.0.2.51 --mac "not-a-mac" 2>&1)
grep -q 'six pairs of hex' <<<"$out" && ok "...and nonsense in that column is refused" \
                                     || bad "an invalid hardware address was accepted"
"$SR" hosts --tsv | grep -q '^nope	' && bad "the refused connection was saved anyway" \
                                      || ok "...with nothing saved for it"

# ⚠ A hosts file written by an older syn-remote has four columns. It must read
# back as "no address saved", not as a short record the window drops.
printf 'old\t192.0.2.60\t5900\tvelle\n' >> "$XDG_CONFIG_HOME/syn-remote/hosts"
check "a connection saved before there was a mac column still reads" "192.0.2.60" \
      "$("$SR" hosts --tsv | awk -F'\t' '$1=="old"{print $2}')"
check "...with an empty address rather than a missing column" "6" \
      "$("$SR" hosts --tsv | awk -F'\t' '$1=="old"{print NF-1}')"

# ⛔ NOTHING TO SEND TO IS AN ERROR WITH THE FIX IN IT, not a packet into the
# void addressed to nothing.
out=$(SYN_REMOTE_WAKE_WAIT=1 "$SR" wake old 2>&1)
grep -q 'no hardware address' <<<"$out" && ok "waking a machine with no address saved refuses" \
                                        || bad "wake was silent about having no address to send to"
grep -q -- '--mac' <<<"$out" && ok "...and says how to give it one" \
                             || bad "the refusal does not say how to fix it"

# ⛔ A MACHINE THAT IS ANSWERING IS NOT WOKEN. The packet is harmless but the
# broadcast is not free, and "it says it woke something that was never asleep"
# is a report nobody can act on.
python3 -c '
import socket, sys, time
s = socket.socket(); s.bind(("127.0.0.1", 0)); s.listen(1)
open(sys.argv[1], "w").write(str(s.getsockname()[1]))
time.sleep(60)' "$T/port" &
listener=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do [ -s "$T/port" ] && break; sleep 0.2; done
lport=$(cat "$T/port" 2>/dev/null)
if [ -n "$lport" ]; then
    "$SR" add awake "127.0.0.1:$lport" --mac aa:bb:cc:dd:ee:02 >/dev/null 2>&1
    : > "$SENT"
    out=$(SENT="$SENT" PYTHONPATH="$fake" "$SR" wake awake 2>&1)
    grep -q 'already awake' <<<"$out" && ok "a machine that is answering is not sent a packet" \
                                      || bad "wake fired at a machine that was already awake"
else
    bad "could not start a local listener for the already-awake case"
fi
kill "$listener" 2>/dev/null

# ⛔ AND THE LIVE MACHINE WAS NEVER TOUCHED. Every case above went through the
# seams; if any of them fell through to the real tools, this is where it shows.
[ -s "$NM_LOG" ] && ok "the nmcli stand-in was the one that was called" \
                 || bad "no call reached the nmcli stand-in — something used the real one"
grep -q 'connection modify' "$NM_LOG" && ok "...including the write" \
                                      || bad "wakeable on never wrote a profile at all"


echo ""
if [ "$fail" -eq 0 ]; then echo "all $pass syn-remote checks passed"; else echo "$fail of $((pass+fail)) failed"; fi
exit $(( fail > 0 ))
