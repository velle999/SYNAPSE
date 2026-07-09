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

# Emit "1" when audio is playing, "0" when not — only on change — on stdout.
audio_monitor() {
    prev=""
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

        if [ "$cur" != "$prev" ]; then
            printf '%s\n' "$cur"
            prev="$cur"
        fi
        sleep 2
    done
}

# If synui-idle-inhibit exits (e.g. synui restarted), the pipe closes, the
# monitor gets SIGPIPE, this script exits, and systemd (Restart=always) brings
# it back for the next session.
audio_monitor | /usr/lib/synui/synui-idle-inhibit
