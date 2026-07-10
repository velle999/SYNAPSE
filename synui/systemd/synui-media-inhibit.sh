#!/bin/sh
# synui-media-inhibit — hold off idle standby (lock/DPMS) while media plays.
#
# Watches PipeWire for an actively *running* audio output stream and drives
# synui-idle-inhibit, which holds a real Wayland idle inhibitor. synui then tells
# synui's own idle stages (power.c) to hold off, so
# the screen won't lock or blank while sound is playing — and re-arms the instant
# playback stops.
#
# Detection is audio-output based (any app making sound), not MPRIS: a paused
# player drops its stream to "idle"/"suspended"; only "running" counts.
set -u

# Wait for synui to publish its Wayland display.
n=0
while [ ! -f /tmp/synui-display ]; do
    sleep 0.5
    n=$((n + 1))
    if [ "$n" -ge 30 ]; then
        echo "synui-media-inhibit: timed out waiting for synui" >&2
        exit 1
    fi
done
WAYLAND_DISPLAY="$(tr -d '[:space:]' < /tmp/synui-display)"
export WAYLAND_DISPLAY

# Emit "1" when audio is playing, "0" when not, on stdout — every poll, not just
# on change. synui-idle-inhibit's set_inhibit() is idempotent (it only acts, and
# only logs, on a real transition), so repeating the current state is free. The
# steady write is what makes a dead reader observable: without it a stuck state
# means nothing is ever written to the pipe and the client's death goes unnoticed
# indefinitely. Now it surfaces within one poll interval.
audio_monitor() {
    while :; do
        cur=0
        if pw-dump 2>/dev/null | python3 -c '
import json, sys
try:
    objs = json.load(sys.stdin)
except Exception:
    sys.exit(2)   # transient pw-dump/parse failure: leave state unchanged
for o in objs:
    info = o.get("info") or {}
    mclass = str((info.get("props") or {}).get("media.class", ""))
    if mclass.startswith("Stream/Output/Audio") and info.get("state") == "running":
        sys.exit(0)
sys.exit(1)
'; then
            cur=1
        elif [ $? -eq 2 ]; then
            sleep 2
            continue
        fi

        # A failed write means synui-idle-inhibit is gone. Don't rely on SIGPIPE
        # to end us: systemd's IgnoreSIGPIPE= defaults to yes, so the signal is
        # SIG_IGN'd and inherited here, turning the would-be kill into a plain
        # EPIPE that a bare `printf` would discard while the loop spins on.
        # The unit sets IgnoreSIGPIPE=no; this check stands on its own anyway.
        if ! printf '%s\n' "$cur" 2>/dev/null; then
            echo "synui-media-inhibit: idle-inhibit client gone — restarting" >&2
            exit 1
        fi
        sleep 2
    done
}

# When synui restarts, its clients die; synui-idle-inhibit exits, the pipe
# breaks, the monitor above notices on its next write and exits, and systemd
# (Restart=always) brings the pair back against the new compositor.
audio_monitor | /usr/lib/synui/synui-idle-inhibit
echo "synui-media-inhibit: inhibit pipeline exited — restarting" >&2
exit 1
