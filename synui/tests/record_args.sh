#!/bin/sh
# record_args.sh — what synui-record puts on wf-recorder's command line
#
# wf-recorder's default is damage-driven capture: a frame per compositor
# repaint, only when something changed. That produces a VARIABLE-rate file that
# declares its r_frame_rate as the 90 kHz timebase rather than a rate, and three
# separate-looking bugs fall out of it:
#
#   - the capture stamps itself H.264 level 6.2 (a level meant for 8K), because
#     the encoder reads 90000 as the frame rate;
#   - no timeline runs at the ~67 fps such a capture averages, so every editor
#     conforms it on import; and
#   - conforming DROPS frames. A real 313-frame capture reached DNxHR as 226.
#
# `-r` fixes all three at the source and costs nothing — measured on a live
# 4-second capture of the same desktop, variable was 1.6 MB / 422 frames and
# constant 60 was 1.3 MB / 237. It is SMALLER, because a high-refresh screen
# under damage capture emits frames faster than 60 whenever anything moves.
#
# So the flag is not a preference and must not quietly go missing. Neither must
# the .mov/DNxHR path, which is the only way a capture opens in the free
# DaVinci Resolve on Linux at all — it decodes neither H.264 nor AAC.
#
# Argument CONSTRUCTION only. Actually recording needs a compositor, a seat and
# an output, none of which a build machine has.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

rec=${1:?usage: record_args.sh <synui-record.sh> <synui-record-status.sh>}
status=${2:?usage: record_args.sh <synui-record.sh> <synui-record-status.sh>}

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

echo "=== syntax ==="
if bash -n "$rec" 2>/dev/null; then
    echo "  ok    synui-record.sh parses"
else
    echo "  FAIL  synui-record.sh does not parse"; exit 1
fi

echo ""
echo "=== constant frame rate is not optional ==="
check "-r reaches wf-recorder" "1" \
      "$(grep -c 'set -- "\$@" -r "\$fps"' "$rec")"
check "defaults to 60" "1" \
      "$(grep -c 'fps="\${SYNUI_RECORD_FPS:-60}"' "$rec")"

echo ""
echo "=== --edit records a mezzanine ==="
check "--edit is accepted" "1" "$(grep -c '^    --edit)' "$rec")"
# All four matter. dnxhd alone with no profile does not encode 1440p at all
# (plain DNxHD is fixed-resolution); yuv422p is what the profile requires; and
# PCM is there because free Resolve reads no AAC either, so an AAC track would
# import silent even once the video decoded.
for flag in '-c dnxhd' 'profile=dnxhr_lb' '-x yuv422p' '-C pcm_s16le'; do
    check "edit mode passes $flag" "1" \
          "$(grep -c -- "$flag" "$rec")"
done
check "edit mode writes .mov, not .mp4" "1" \
      "$(grep -c 'synui-\$(date +%Y%m%d-%H%M%S).mov' "$rec")"
check "the default stays .mp4" "1" \
      "$(grep -c 'synui-\$(date +%Y%m%d-%H%M%S).mp4' "$rec")"
# 1.1 GB/min is a disk filled inside an hour. The toast is the last point where
# stopping is still cheap, so it says the rate rather than just "Recording".
check "edit mode warns about the rate" "1" \
      "$(grep -c 'note "Recording for editing".*GB/min' "$rec")"

echo ""
echo "=== the bar still recognises the recording ==="
# synui-record-status recovers -f/-o/-a by walking wf-recorder's argv, and the
# new flags interleave with those. A parser that mistook `-r 60` for a filename,
# or stopped at the first thing it did not know, would blank the pill's tooltip
# while the recording ran perfectly — a failure with no symptom at the point it
# happens.
if command -v python3 >/dev/null 2>&1; then
    out=$(python3 - "$status" <<'PY'
import sys
from importlib.machinery import SourceFileLoader
m = SourceFileLoader("st", sys.argv[1]).load_module()

# exactly what synui-record now emits, edit mode with audio: the longest form
argv = ["wf-recorder", "-aalsa_output.monitor", "-o", "DP-3", "-r", "60",
        "-c", "dnxhd", "-p", "profile=dnxhr_lb", "-x", "yuv422p",
        "-C", "pcm_s16le", "-f", "/home/u/Videos/synui-1.mov"]
i = m.parse_args(argv[1:])
print(i["file"], i["output"], i["audio"], sep="|")

# and the default form
argv = ["wf-recorder", "-o", "DP-3", "-r", "60", "-f", "/home/u/Videos/s.mp4"]
d = m.parse_args(argv[1:])
print(d["file"], d["output"], d["audio"], sep="|")
PY
)
    check "edit-mode argv: file/output/audio recovered" \
          "/home/u/Videos/synui-1.mov|DP-3|alsa_output.monitor" \
          "$(printf '%s\n' "$out" | sed -n 1p)"
    check "default argv: file/output recovered" \
          "/home/u/Videos/s.mp4|DP-3|" \
          "$(printf '%s\n' "$out" | sed -n 2p)"
else
    echo "  skip  parser round-trip (no python3)"
fi

echo ""
if [ "$fails" -eq 0 ]; then
    echo "all synui-record argument checks passed"
else
    echo "$fails check(s) failed"
fi
exit $([ "$fails" -eq 0 ] && echo 0 || echo 1)
