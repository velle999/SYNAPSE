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
check "...and says so the other way round too" "yes" \
      "$(awk -F'\t' '$1=="wayvnc"{print $2}' <<<"$rec")"

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
check "the record names its columns" "name	host	port	user	secret	pinned" \
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
out=$("$SR" connect tls 2>&1 </dev/null)
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

echo ""
if [ "$fail" -eq 0 ]; then echo "all $pass syn-remote checks passed"; else echo "$fail of $((pass+fail)) failed"; fi
exit $(( fail > 0 ))
