#!/bin/sh
# secfeed_shutdown.sh — synui still exits on SIGTERM while its synguard feed
# is backed up.
#
# secfeed.c's thread hands each verdict to the main thread over a pipe, and
# the main thread drains that pipe only on frames, 32 records a frame. An idle
# compositor draws none, so a burst of verdicts fills the pipe and the thread
# blocks in write() — where shutdown() of the feed socket does not reach it.
# synui_destroy() then joined that thread for ever: the smoke suite's idle
# section-13 instance, subscribed to the build machine's own synguard while the
# rest of the suite set it off, hung until meson killed it at 540 s in three
# of three full ASan runs.
#
# A stand-in synguard (SYNUI_SECFEED_SOCKET) waits until this synui is idle,
# sends it a burst, and the test waits until the feed thread is blocked on the
# pipe. Then SIGTERM must end synui within 10 s, exit 0.
#
# Usage: secfeed_shutdown.sh /path/to/synui

set -u

SYNUI=${1:?usage: secfeed_shutdown.sh /path/to/synui}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node (/dev/dri/renderD*); see smoke.sh."
    exit 77
fi

TMP=$(mktemp -d /tmp/synui-secfeed.XXXXXX) || exit 1
chmod 700 "$TMP"
LOG=$TMP/synui.log
SYNUI_PID=
FAKE_PID=

cleanup() {
    [ -n "$SYNUI_PID" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    [ -n "$FAKE_PID" ] && kill -9 "$FAKE_PID" 2>/dev/null
    rm -rf "$TMP"
}
fail() { echo "FAIL: $*" >&2; tail -20 "$LOG" >&2; cleanup; exit 1; }
trap 'cleanup; exit 1' INT TERM

printf 'welcome_at_startup = off\n' > "$TMP/synuirc"
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
export SYNUI_CONFIG="$TMP/synuirc"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export SYNUI_SECFEED_SOCKET="$TMP/synguard.sock"

# sg_secfeed_msg_t: magic "SGFV", version 1, pid, uid, verdict (2 = ALERT),
# threat, comm[16], reason[112]. pid 1 is no window's, so no border changes.
python3 - "$SYNUI_SECFEED_SOCKET" "$TMP/go" <<'PY' &
import os, socket, struct, sys, time
srv = socket.socket(socket.AF_UNIX)
srv.bind(sys.argv[1])
srv.listen(1)
c, _ = srv.accept()
while not os.path.exists(sys.argv[2]):
    time.sleep(0.05)
rec = struct.pack("<IIIIii16s112s", 0x53474656, 1, 1, 0, 2, 1,
                  b"fake", b"secfeed_shutdown.sh")
c.settimeout(5)
try:
    for _ in range(4000):
        c.sendall(rec)
except OSError:
    pass            # synui stopped reading: the buffers are full
c.settimeout(None)
try:
    c.recv(1)       # hold the connection until synui closes it
except OSError:
    pass
PY
FAKE_PID=$!

"$SYNUI" >"$LOG" 2>&1 &
SYNUI_PID=$!

i=0
while ! grep -q "subscribed to synguard verdict feed" "$LOG"; do
    [ $i -ge 150 ] && fail "synui never subscribed to the stand-in feed"
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died during startup"
    sleep 0.1
    i=$((i + 1))
done

# Idle first: the burst has to arrive when no frames are being drawn.
sleep 2
: > "$TMP/go"

# Wait for the feed thread to block writing the hand-off pipe.
blocked=
i=0
while [ $i -lt 100 ]; do
    if grep -qs pipe_write /proc/"$SYNUI_PID"/task/*/wchan; then
        blocked=1
        break
    fi
    sleep 0.1
    i=$((i + 1))
done
[ -n "$blocked" ] || fail "the feed never backed up (no thread blocked on the pipe), so this proves nothing"
echo "ok 1 - a burst of verdicts at an idle synui blocks its feed thread on the pipe"

kill -TERM "$SYNUI_PID"
i=0
while kill -0 "$SYNUI_PID" 2>/dev/null; do
    [ $i -ge 100 ] && fail "synui did not exit within 10s of SIGTERM with its feed backed up"
    sleep 0.1
    i=$((i + 1))
done
wait "$SYNUI_PID"
rc=$?
SYNUI_PID=
[ "$rc" -eq 0 ] || fail "synui exited $rc shutting down with its feed backed up (expected 0)"
if grep -qE "(ERROR|SUMMARY): (Address|Leak)Sanitizer" "$LOG"; then
    fail "sanitizer error shutting down with the feed backed up"
fi
echo "ok 2 - SIGTERM still ends it, exit 0"

cleanup
exit 0
