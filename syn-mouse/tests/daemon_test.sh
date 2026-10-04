#!/usr/bin/env bash
#
# daemon_test.sh — the whole daemon, end to end, with nothing real under it.
#
# ⛔ NO DEVICE IS OPENED. The mouse is a FIFO (SYNMOUSE_INPUT) the suite writes
# "type code value" lines into; what the daemon would have written to its twin
# and its keyboard lands as text in SYNMOUSE_OUTPUT; /dev/uinput is pointed at
# a path that does not exist, so a seam that failed would fail closed. synui is
# a socket served from here, and the config, the control socket and the
# notify-send it calls all live in a scratch directory. A suite that created a
# real input device would be typing into whatever the person running it has
# focused.
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

BIN=${1:?usage: daemon_test.sh <syn-mouse binary>}
command -v python3 >/dev/null || { echo "python3 is needed to serve the fake synui"; exit 77; }

T=$(mktemp -d "${TMPDIR:-/tmp}/syn-mouse-daemon.XXXXXX")
# ⚠ THE SOCKETS GET A SHORT DIRECTORY OF THEIR OWN. A unix socket path is
# limited to 108 bytes, and a TMPDIR deep in a build tree is past that before
# the file name — the daemon could not listen, exited, and the first write to
# its FIFO then waited forever for a reader.
S=$(mktemp -d /tmp/smd.XXXXXX)
DPID=; SPID=
cleanup() {
    [ -n "$DPID" ] && kill "$DPID" 2>/dev/null
    [ -n "$SPID" ] && kill "$SPID" 2>/dev/null
    wait 2>/dev/null
    rm -rf "$T" "$S"
}
trap cleanup EXIT

export SYNMOUSE_HOME=$T/cfg
export SYNMOUSE_SOCKET=$S/ctl.sock
export SYNMOUSE_INPUT=$T/mouse.fifo
export SYNMOUSE_OUTPUT=$T/out
export SYNMOUSE_SYNUI_SOCKET=$S/synui.sock
export SYNMOUSE_UINPUT=$T/no-such-uinput
mkdir -p "$SYNMOUSE_HOME" "$T/bin"
mkfifo "$SYNMOUSE_INPUT"
: > "$SYNMOUSE_OUTPUT"

# notify-send, faked: what the toast would have said.
cat > "$T/bin/notify-send" <<EOF
#!/bin/sh
for a; do last=\$a; done
printf '%s\n' "\$last" >> "$T/notified"
EOF
chmod +x "$T/bin/notify-send"
export PATH="$T/bin:$PATH"

# synui, faked: answers activewindow with whatever focus.json holds.
cat > "$T/synui.py" <<'EOF'
import os, socket, sys
path, reply = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.bind(path); s.listen(8)
while True:
    c, _ = s.accept()
    try:
        c.recv(512)
        with open(reply, "rb") as f: c.sendall(f.read())
    except OSError:
        pass
    c.close()
EOF
focus() { printf '%s\n' "$1" > "$T/focus.json"; }
focus '{"app_id":"other","title":"Other"}'
python3 "$T/synui.py" "$SYNMOUSE_SYNUI_SOCKET" "$T/focus.json" & SPID=$!

pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok    $1"; }
bad() { fail=$((fail+1)); echo "  FAIL  $1"; }
chk() { if [ "$2" = 0 ]; then ok "$1"; else bad "$1"; fi; }

# Wait up to $2 seconds for a fixed string to appear in the output.
wait_out() {
    local i=0
    while [ $i -lt $(( ${2:-3} * 20 )) ]; do
        grep -qxF "$1" "$SYNMOUSE_OUTPUT" && return 0
        sleep 0.05; i=$((i+1))
    done
    return 1
}
count() { grep -cxF "$1" "$SYNMOUSE_OUTPUT"; }
# ⚠ NEVER BLOCKS: a FIFO write with no reader waits forever, and a daemon
# that died is exactly the case this suite exists to report.
ev() { printf '%s\n' "$@" | timeout 2 tee "$SYNMOUSE_INPUT" >/dev/null || bad "write to the mouse FIFO (daemon gone?)"; }
SYN="0 0 0"

cat > "$SYNMOUSE_HOME/bindings.conf" <<'EOF'
[Game]
app = game
back = toggle 1 every 0.3
forward = key shift+2
wheelup = key f
EOF

echo "daemon"

"$BIN" daemon 2> "$T/log" & DPID=$!
for _ in $(seq 60); do "$BIN" --rec status | q '^daemon	[1-9]' && break; sleep 0.05; done
"$BIN" --rec status | q '^daemon	[1-9]'
chk "it starts and answers on its socket" $?
if ! kill -0 "$DPID" 2>/dev/null; then
    echo "--- daemon log"; cat "$T/log"; exit 1
fi

"$BIN" daemon 2>&1 | q 'already running'
chk "a second one sees the first and leaves" $?

# ── not focused: hands off ──────────────────────────────────────────────────
sleep 0.4
! grep -q '^grab' "$SYNMOUSE_OUTPUT"
chk "another app has focus: the mouse is not taken" $?
ev "1 275 1" "$SYN" "1 275 0" "$SYN"
sleep 0.2
! grep -q '^key' "$SYNMOUSE_OUTPUT"
chk "…and a bound button does nothing it would not normally do" $?

# ── the game has focus ──────────────────────────────────────────────────────
focus '{"app_id":"game","title":"The Game"}'
wait_out "grab 1 1" 3
chk "the game has focus: the mouse is taken" $?
"$BIN" --rec status | q '^profile	Game$'
chk "…and status names the profile in force" $?

# Forwarding: an unbound button and motion reach the twin as they are.
ev "1 272 1" "$SYN" "2 0 5" "2 1 -3" "$SYN" "1 272 0" "$SYN"
wait_out "twin 1 1 272 0" 2
chk "an unbound button is forwarded, press and release" $?
grep -qxF "twin 1 2 0 5" "$SYNMOUSE_OUTPUT" && grep -qxF "twin 1 2 1 -3" "$SYNMOUSE_OUTPUT"
chk "motion is forwarded unchanged" $?

# key: shift+2, in order, held while held.
ev "1 276 1" "$SYN"
wait_out "key 3 1" 2
chk "forward presses shift+2" $?
grep -A1 -xF "key 42 1" "$SYNMOUSE_OUTPUT" | q -xF "key 3 1"
chk "…shift first" $?
! grep -q '^twin 1 1 276' "$SYNMOUSE_OUTPUT"
chk "…and the button itself never reaches the twin" $?
ev "1 276 0" "$SYN"
wait_out "key 42 0" 2
grep -A1 -xF "key 3 0" "$SYNMOUSE_OUTPUT" | q -xF "key 42 0"
chk "released in reverse when the button comes up" $?

# wheel: a bound notch is a tap; its hi-res copy is swallowed; the other way passes.
ev "2 11 120" "2 8 1" "$SYN"
wait_out "key 33 0" 2
chk "a wheel notch up taps f" $?
! grep -q '^twin 1 2 11 120' "$SYNMOUSE_OUTPUT"
chk "…and its high-resolution copy is not forwarded" $?
ev "2 11 -120" "2 8 -1" "$SYN"
wait_out "twin 1 2 8 -1" 2
chk "a notch down, unbound, is forwarded" $?

# toggle: every 0.3 s until clicked again.
ev "1 275 1" "$SYN" "1 275 0" "$SYN"
wait_out "key 2 1" 2
chk "back switches the toggle on and presses 1 at once" $?
sleep 1.0
n=$(count "key 2 1")
[ "$n" -ge 3 ] && [ "$n" -le 6 ]
chk "…and again every 0.3 s ($n presses in ~1 s)" $?
"$BIN" --rec status | q -P '^active\tGame\tback\ttoggle\t1\t300\t1$'
chk "status shows it running" $?
grep -qF 'Pressing 1 every 0.3 s' "$T/notified" 2>/dev/null
chk "a notice said so" $?

# ── focus leaves: the toggle pauses ─────────────────────────────────────────
focus '{"app_id":"browser","title":"A Browser"}'
for _ in $(seq 40); do "$BIN" --rec status | q '^profile	$' && break; sleep 0.05; done
sleep 0.1
n1=$(count "key 2 1")
sleep 0.9
n2=$(count "key 2 1")
[ "$n1" = "$n2" ]
chk "focus moved to a browser: no more presses ($n1 -> $n2)" $?
[ "$(count 'key 2 1')" = "$(count 'key 2 0')" ]
chk "…and nothing was left held down" $?
"$BIN" --rec status | q -P '^active\tGame\tback\ttoggle\t1\t300\t0$'
chk "status shows it paused, still on" $?
# A bound button is a plain button again straight away, before the ungrab.
ev "1 275 1" "$SYN" "1 275 0" "$SYN"
wait_out "twin 1 1 275 0" 2
chk "while focus is away, back is an ordinary button" $?
wait_out "grab 1 0" 5
chk "a few seconds later the mouse is let go" $?

# ── focus returns: it carries on ────────────────────────────────────────────
focus '{"app_id":"game","title":"The Game"}'
wait_out "grab 1 1" 3
n1=$(count "key 2 1")
sleep 0.8
n2=$(count "key 2 1")
[ "$n2" -gt "$n1" ]
chk "back in the game: the toggle carries on ($n1 -> $n2)" $?

# ── a press that started forwarded ends forwarded ───────────────────────────
ev "1 274 1" "$SYN"
wait_out "twin 1 1 274 1" 2
"$BIN" bind Game middle key x >/dev/null
sleep 0.2
ev "1 274 0" "$SYN"
wait_out "twin 1 1 274 0" 2
chk "middle was pressed unbound, bound mid-press: its release still reaches the twin" $?
ev "1 274 1" "$SYN" "1 274 0" "$SYN"
wait_out "key 45 0" 2
chk "…and the next press uses the new binding (x)" $?

# ── an edit leaves alone what it did not touch ──────────────────────────────
n1=$(count "key 2 1")
sleep 0.8
n2=$(count "key 2 1")
[ "$n2" -gt "$n1" ]
chk "binding middle did not switch off the toggle on back ($n1 -> $n2)" $?
[ "$(grep -c 'loaded ' "$T/log")" = 2 ]
chk "one save is one reload, not one per signal ($(grep -c 'loaded ' "$T/log") loads)" $?

# ── stop ────────────────────────────────────────────────────────────────────
"$BIN" stop >/dev/null
sleep 0.1
n1=$(count "key 2 1")
sleep 0.8
n2=$(count "key 2 1")
[ "$n1" = "$n2" ]
chk "syn-mouse stop switches the toggle off ($n1 -> $n2)" $?
grep -qF 'Stopped pressing 1' "$T/notified"
chk "…with a notice" $?
! "$BIN" --rec status | q '^active'
chk "…and status shows nothing on" $?

# ── an edit of the toggle itself does switch it off ─────────────────────────
ev "1 275 1" "$SYN" "1 275 0" "$SYN"
sleep 0.3
: > "$T/notified"
"$BIN" bind Game back toggle 1 every 0.4 >/dev/null
sleep 0.2
n1=$(count "key 2 1")
sleep 0.9
n2=$(count "key 2 1")
[ "$n1" = "$n2" ] && grep -qF 'Stopped pressing 1' "$T/notified"
chk "rebinding the toggle's own button switches it off, and says so ($n1 -> $n2)" $?

# ── shutdown ────────────────────────────────────────────────────────────────
ev "1 276 1" "$SYN"
wait_out "key 3 1" 2
kill -TERM "$DPID"; wait "$DPID" 2>/dev/null; DPID=
[ "$(count 'key 42 1')" = "$(count 'key 42 0')" ] && [ "$(count 'key 3 1')" = "$(count 'key 3 0')" ]
chk "stopped with forward held down: shift and 2 are let go on the way out" $?
tail -n 5 "$SYNMOUSE_OUTPUT" | q -xF "grab 1 0"
chk "…and the mouse is let go" $?
[ ! -e "$SYNMOUSE_SOCKET" ]
chk "…and its socket is gone" $?

# Every code pressed was released, over the whole run.
unbalanced=$(awk '$1=="key"{k[$2]+= ($3?1:-1)} $1=="twin"&&$3==1{t[$4]+=($5?1:-1)}
     END{for(c in k) if(k[c]) print "key " c; for(c in t) if(t[c]) print "twin " c}' "$SYNMOUSE_OUTPUT")
[ -z "$unbalanced" ]
chk "over the whole run, every press has its release${unbalanced:+ (not: $unbalanced)}" $?

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
if [ "$fail" != 0 ]; then echo "--- daemon log"; cat "$T/log"; fi
[ "$fail" = 0 ]
