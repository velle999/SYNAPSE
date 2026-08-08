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
#
# ...with one exception, see IGNORE below: a *wallpaper* must never be able to
# hold the screen awake.
set -u

# Wait for synui to publish its Wayland display.
#
# synui writes this into XDG_RUNTIME_DIR now (see synui_main.c — /tmp was a
# privilege hazard for the *root* synui-foot service). This runs as the user, so
# /tmp is kept purely as a fallback, and only matters while an older synui that
# still writes there is the one running.
DISPLAY_FILE="${XDG_RUNTIME_DIR:-}/synui-display"
[ -n "${XDG_RUNTIME_DIR:-}" ] || DISPLAY_FILE=/tmp/synui-display

n=0
while [ ! -f "$DISPLAY_FILE" ]; do
    if [ -f /tmp/synui-display ]; then
        DISPLAY_FILE=/tmp/synui-display
        break
    fi
    sleep 0.5
    n=$((n + 1))
    if [ "$n" -ge 30 ]; then
        echo "synui-media-inhibit: timed out waiting for synui" >&2
        exit 1
    fi
done
WAYLAND_DISPLAY="$(tr -d '[:space:]' < "$DISPLAY_FILE")"
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
# Streams that never count as "media playing", however loud PipeWire thinks they
# are. linux-wallpaperengine opens an SDL playback device even when synui-wpengine
# starts it with --silent (that flag only mutes the engines own mixer, so from
# out here the node reads volume 1.0, mute false) and pins it at state "running"
# for its whole lifetime via node.always-process. That matched the test below on
# every boot a Workshop wallpaper was set, held a real idle inhibitor forever, and
# so silently disabled every power.c stage — dim, blank and lock all stopped
# firing. A wallpaper is background decoration; it does not get a vote here.
#
# NB: no apostrophes anywhere below — this whole block is a single-quoted shell
# argument, so one closes it and bash then tries to run the Python.
#
# Matching the names on the stream node itself is necessary but NOT sufficient.
# For a
# scene wallpaper the node really is called linux-wallpaperengine, but for a
# VIDEO wallpaper the engine plays it through an embedded mpv, and that stream
# node identifies itself as mpv, with media.name "<video file> - mpv". Nothing
# on the node names the wallpaper at all, so the name test above missed it and
# the inhibitor was held forever again — the exact pkgrel-195 symptom, one layer
# deeper: dim, blank and lock all dead for as long as a video wallpaper was set.
#
# The owning process is the reliable answer, and it IS recorded — just not on
# the stream node. The node carries client.id, and THAT Client object has
# application.process.binary (this is the "sibling object" the pkgrel-195 note
# describes; it is reachable, not merely absent). So resolve the node to its
# client and judge the process, which covers the engine however many players it
# embeds and whatever they choose to call themselves.
# Third instance of the same trap, 2026-08-08: the EQUALIZER, held running by
# the VISUALISER widget. cava reads effect_input.synui_eq:monitor_*, and a
# monitor that is being read keeps the chain processing, so effect_output.synui_eq
# sits at state "running" for as long as the visualiser is on — with nothing
# playing at all. velle left the machine overnight and it never slept: cava
# started at 02:44:09, this went "audio active" at 02:44:11, and it did not
# release once in the following eight hours (108 releases in the day before it).
# dim, blank, lock and suspend were all dead for the whole night, exactly as in
# the two wallpaper cases above.
#
# Ignoring the equalizer sounds like it should break the feature outright, since
# EVERYTHING routes through it — Firefox -> effect_input.synui_eq ->
# effect_output.synui_eq -> alsa. It does not, and this is the point worth
# keeping: the EQ node is a DERIVED one. Every real source still carries its own
# Stream/Output/Audio node (Firefox appears as node.name "Firefox" alongside the
# EQ, not instead of it), so genuine playback is still counted on the node that
# actually describes it, and only the double-count goes away.
#
# Matched on node.name, not on the "SynapseOS Equalizer" description: the
# description is a user-facing string that a rename or a translation could move,
# and node.name is what eq.c actually creates the chain with.
IGNORE = ("linux-wallpaperengine", "effect_output.synui_eq")
NAME_KEYS = ("application.name", "node.name", "node.description", "media.name")
CLIENT_KEYS = ("application.process.binary", "application.name")

clients = {}
for o in objs:
    if str(o.get("type", "")).endswith(":Client"):
        clients[o.get("id")] = ((o.get("info") or {}).get("props") or {})

def matches(props, keys):
    for key in keys:
        if str(props.get(key, "")).strip().lower() in IGNORE:
            return True
    return False

def ignored(props):
    if matches(props, NAME_KEYS):
        return True
    return matches(clients.get(props.get("client.id"), {}), CLIENT_KEYS)

for o in objs:
    info = o.get("info") or {}
    props = info.get("props") or {}
    mclass = str(props.get("media.class", ""))
    if mclass.startswith("Stream/Output/Audio") and info.get("state") == "running":
        if ignored(props):
            continue
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
