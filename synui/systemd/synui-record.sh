#!/bin/sh
# synui-record — start/stop a screen recording (Super+Shift+R).
#
# One key does both, because a recorder you have to stop from somewhere else is
# a recorder that runs for an hour by accident. State is the wf-recorder process
# itself — there is no state file to go stale if it dies.
#
# wf-recorder rather than something synui-native: recording means encoding, and
# ffmpeg is a dependency the compositor should not grow. It captures through
# wlr-screencopy, which synui already exports for grim/slurp, so this needs
# nothing new from the compositor.
#
# Feedback comes from notifications, which synui now serves itself (src/notif.c)
# — before that this tool could not have told you anything, which is most of why
# a recorder that starts silently and stops silently is a bad idea.
#
# SynapseOS Project — GPLv2
# https://github.com/velle999/SYNAPSE
set -u

note() { command -v notify-send >/dev/null 2>&1 && notify-send -a synui "$@" || true; }

# Already recording? Stop, and let wf-recorder finalise the file. SIGKILL would
# leave an unplayable container — the whole recording lost with nothing to show.
#
# SIGTERM, deliberately, NOT SIGINT — this is measured, not stylistic. A command
# backgrounded with `&` from a non-interactive shell inherits SIGINT and SIGQUIT
# set to **SIG_IGN**, and SIG_IGN survives exec. So the recorder started below
# ignores SIGINT for its whole life: `pkill -INT` returns success, the process
# lives on, and the only way to end it is SIGKILL and a broken file. Verified on
# the child: `SigIgn: 0000000000000006` (bits 2,3 = INT, QUIT), alive after
# kill -INT.
#
# It cannot be fixed in the shell either: POSIX says a shell that inherits a
# signal as ignored cannot trap it, so `trap ":" INT` before an exec is a silent
# no-op. `&` does NOT touch SIGTERM, and wf-recorder catches it — `SigCgt` has
# bit 14 set — so TERM is both deliverable and clean. Verified end to end: exits
# on TERM, and ffprobe reports a real duration on the file.
if pkill -TERM -x wf-recorder 2>/dev/null; then
    # Wait for it to actually finish writing before claiming it saved. A bounded
    # wait: a long recording takes a moment to flush, but this must never hang
    # the keybind forever if wf-recorder wedges.
    n=0
    while pgrep -x wf-recorder >/dev/null 2>&1; do
        n=$((n + 1))
        [ "$n" -ge 100 ] && break     # 10s
        sleep 0.1
    done
    note "Recording stopped" "Saved to ~/Videos"
    exit 0
fi

command -v wf-recorder >/dev/null 2>&1 || {
    note "Cannot record" "wf-recorder is not installed"
    exit 1
}

dir="${XDG_VIDEOS_DIR:-$HOME/Videos}"
mkdir -p "$dir" || { note "Cannot record" "Could not create $dir"; exit 1; }
file="$dir/synui-$(date +%Y%m%d-%H%M%S).mp4"

# -a records audio too; deliberately NOT default. A recorder that silently picks
# up the microphone is a privacy problem, and someone who wants audio can say so.
audio=""
[ "${1:-}" = "--audio" ] && audio="-a"

note "Recording" "Super+Shift+R again to stop"

# Detached: the keybind's own process must return immediately, or synui's
# spawn() is left holding a child for the length of the recording.
#
# This child inherits SIGINT/SIGQUIT as SIG_IGN from the `&` and cannot be told
# otherwise from a shell (see the SIGTERM note above) — which is exactly why the
# stop path signals TERM. Nothing to work around here as long as that holds.
#
# If a child ever ignores a signal it visibly handles, this is why: inherited
# dispositions survive exec, and it has bitten this project repeatedly.
setsid wf-recorder $audio -f "$file" >/dev/null 2>&1 &

# Give it long enough to fail loudly (no screencopy, disk full, bad codec)
# rather than leaving a "Recording" toast up over a recorder that already died.
sleep 1
if ! pgrep -x wf-recorder >/dev/null 2>&1; then
    note "Recording failed" "wf-recorder exited immediately"
    exit 1
fi
