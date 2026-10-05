#!/usr/bin/env bash
#
# cli_test.sh — the commands that write the bindings file, and what they refuse.
#
# ⛔ EVERYTHING IN A SCRATCH SYNMOUSE_HOME, with SYNMOUSE_SOCKET pointed at a
# socket nobody serves: a suite that rewrote the bindings of whoever ran it
# would change what their mouse does in their next game.
#
# ⚠ `devices` IS NOT ASSERTED ON. It reads this machine's /sys, and a gate that
# runs the program reads THIS MACHINE — one that passed here would fail on a
# box with no mouse plugged in. Only its record shape is checked.
#
# SynapseOS Project — GPL-2.0-or-later
set -uo pipefail
export LC_ALL=C.UTF-8
unset LANGUAGE

# ⚠ NEVER `cmd | grep -q` UNDER pipefail. grep -q exits at its first match, the
# writer takes SIGPIPE on its next line, and the pipeline fails with 141 on
# output that was right — a race, so it passes on a fast build and fails under ASan.
# q reads everything. The same goes for `| head`.
q() { grep "$@" >/dev/null; }

BIN=${1:?usage: cli_test.sh <syn-mouse binary>}
T=$(mktemp -d "${TMPDIR:-/tmp}/syn-mouse-cli.XXXXXX")
S=$(mktemp -d /tmp/smc.XXXXXX)
SPID=
cleanup() { [ -n "$SPID" ] && kill "$SPID" 2>/dev/null; wait 2>/dev/null; rm -rf "$T" "$S"; }
trap cleanup EXIT

export SYNMOUSE_HOME=$T/cfg
export SYNMOUSE_SOCKET=$S/nobody.sock
export SYNMOUSE_SYNUI_SOCKET=$S/synui.sock
F=$SYNMOUSE_HOME/bindings.conf

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
chk() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }
refuses() { ! "$BIN" "$@" >/dev/null 2>&1; }

echo "cli"

"$BIN" --rec profiles | q -x 'setting	notify	on'
chk "no file yet: an empty config, notify on" $?

"$BIN" add "Diablo IV" --app steam_app_2344520 >/dev/null 2>&1
grep -qx '\[Diablo IV\]' "$F" && grep -qx 'app = steam_app_2344520' "$F"
chk "add makes a profile for an app" $?

"$BIN" bind "Diablo IV" back toggle 1 every 5 >/dev/null 2>&1
"$BIN" bind "Diablo IV" forward repeat shift+2 every 250ms >/dev/null 2>&1
"$BIN" bind "Diablo IV" middle latch Shift >/dev/null 2>&1
"$BIN" bind "Diablo IV" wheelup key code:33 >/dev/null 2>&1
"$BIN" bind "Diablo IV" mouse4 key 3 >/dev/null 2>&1
grep -qx 'back = key 3' "$F"
chk "mouse4 is accepted as the back button, and the later bind wins" $?
grep -qx 'forward = repeat shift+2 every 0.25' "$F"
chk "250ms is written as 0.25 seconds" $?
grep -qx 'middle = latch shift' "$F"
chk "key names are written lower-case" $?
grep -qx 'wheelup = key f' "$F"
chk "code:33 is written as its name, f" $?
"$BIN" bind "Diablo IV" key:2 toggle e every 5 >/dev/null 2>&1
grep -qx 'key:2 = toggle e every 5' "$F"
chk "a key the mouse itself sends is bound as key:2" $?
"$BIN" bind "Diablo IV" key:code:30 key x >/dev/null 2>&1
grep -qx 'key:a = key x' "$F"
chk "key:code:30 is written as its name, key:a" $?

out=$("$BIN" --rec profiles)
printf '%s\n' "$out" | q -x 'profile	Diablo IV	steam_app_2344520		0'
chk "the profile record: name, app, title, everywhere" $?
printf '%s\n' "$out" | q -x 'bind	Diablo IV	forward	repeat	shift+2	250'
chk "a bind record carries the interval in milliseconds" $?
printf '%s\n' "$out" | q -x 'bind	Diablo IV	key:2	toggle	e	5000'
chk "a key the mouse sends has a bind record under its key: name" $?

"$BIN" bind "Diablo IV" left key 1 >/dev/null 2>&1
chk "the left button can be rebound in a profile for an app" $?

# ── refusals ────────────────────────────────────────────────────────────────
"$BIN" add Everywhere >/dev/null 2>&1
refuses bind Everywhere left key 1
chk "…but not in a profile for everywhere" $?
refuses add "Diablo IV" --app ""
chk "…and its app cannot be taken away while left is bound" $?
grep -qx 'app = steam_app_2344520' "$F"
chk "…and the refused change did not reach the file" $?
refuses bind "Diablo IV" back toggle 1 every 0.01
chk "an interval under 0.05 s is refused" $?
refuses bind "Diablo IV" back key nosuchkey
chk "an unknown key name is refused" $?
{ "$BIN" bind "Diablo IV" back key nosuchkey 2>&1 || true; } | q "'nosuchkey'"
chk "…naming the word it did not know" $?
refuses bind "Diablo IV" thumb key 1
chk "an unknown button is refused" $?
refuses bind "Diablo IV" key:mouse1 key 1
chk "key:mouse1 is refused: the mouse's own buttons have names" $?
refuses bind "Diablo IV" key:shift+2 key 1
chk "key:shift+2 is refused: a button sends one key" $?
refuses bind Nope back key 1
chk "a profile that does not exist is refused" $?
refuses add "Bad [name]"
chk "brackets in a profile name are refused" $?
refuses bind "Diablo IV" back key
chk "key with no key is refused" $?
refuses bind "Diablo IV" back off now
chk "off with words after it is refused" $?

# A refused bind writes nothing.
before=$(md5sum < "$F")
"$BIN" bind "Diablo IV" back key nosuchkey >/dev/null 2>&1
[ "$before" = "$(md5sum < "$F")" ]
chk "a refused bind leaves the file exactly as it was" $?

# ── unbind, remove, settings ────────────────────────────────────────────────
"$BIN" unbind "Diablo IV" wheelup >/dev/null 2>&1
! grep -q '^wheelup' "$F"
chk "unbind takes the line out" $?
"$BIN" unbind "Diablo IV" key:code:30 >/dev/null 2>&1
! grep -q '^key:a' "$F" && grep -qx 'key:2 = toggle e every 5' "$F"
chk "unbind a key: input by either spelling, and only that one" $?
"$BIN" set notify off >/dev/null 2>&1 && grep -qx 'notify = off' "$F"
chk "set notify off" $?
"$BIN" set device "Viper" >/dev/null 2>&1 && grep -qx 'device = Viper' "$F"
chk "set device" $?
"$BIN" set device >/dev/null 2>&1; ! grep -q '^device' "$F"
chk "set device with nothing clears it" $?
"$BIN" remove Everywhere >/dev/null 2>&1; ! grep -q 'Everywhere' "$F"
chk "remove takes the whole section out" $?

# A hand edit survives being read and rewritten.
printf '\n[Hand]\ntitle = *Game*\nright = toggle e every 2.5\n' >> "$F"
"$BIN" bind Hand forward off >/dev/null 2>&1
grep -qx 'title = \*Game\*' "$F" && grep -qx 'right = toggle e every 2.5' "$F" && grep -qx 'forward = off' "$F"
chk "a hand-written section is read, kept and added to" $?

# ── without a daemon ────────────────────────────────────────────────────────
"$BIN" bind Hand back key 1 2>&1 | q 'not running'
chk "a bind with no service running says the mouse does not know yet" $?
"$BIN" --rec status | q -x 'daemon	0	'
chk "status says not running, as a record" $?
"$BIN" stop >/dev/null 2>&1
chk "stop with nothing running is not an error" $?

# ── the lists ───────────────────────────────────────────────────────────────
"$BIN" --rec buttons | q -x 'button	back	Thumb button (back)'
chk "buttons: name and label" $?
"$BIN" --rec keys | q -x 'key	shift	42'
chk "keys: name and code" $?
"$BIN" --rec devices | sed -n 1p | q -x 'kind	name	node	buttons	readable'
chk "devices: its header (its rows are this machine's)" $?

cat > "$T/synui.py" <<'EOF'
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.bind(sys.argv[1]); s.listen(4)
reply = b'[{"app_id":"steam_app_2344520","title":"Diablo IV","at":[0,0]},' \
        b'{"app_id":"vivaldi","title":"Tab\\twith a tab","nested":{"app_id":"x"}}]\n'
while True:
    c, _ = s.accept(); c.recv(256); c.sendall(reply); c.close()
EOF
if command -v python3 >/dev/null; then
    python3 "$T/synui.py" "$SYNMOUSE_SYNUI_SOCKET" & SPID=$!
    for _ in $(seq 40); do [ -S "$SYNMOUSE_SYNUI_SOCKET" ] && break; sleep 0.05; done
    out=$("$BIN" --rec apps)
    printf '%s\n' "$out" | q -x 'app	steam_app_2344520	Diablo IV'
    chk "apps: each open window's app id and title" $?
    printf '%s\n' "$out" | q -x 'app	vivaldi	Tab%09with a tab'
    chk "…a tab inside a title is encoded, so the row stays one row" $?
    [ "$(printf '%s\n' "$out" | grep -c '^app')" = 2 ]
    chk "…and a nested object is not a window" $?
else
    echo "  --    python3 missing: apps not checked"
fi

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ]
