#!/usr/bin/env bash
# wpengine_preset_files.sh — a preset's own files reach the engine as real paths.
#
# THE BUG THIS EXISTS FOR (synui 0.1.0-642): a preset keeps the images and
# videos it was saved with in its OWN folder and names them relative to it
# ("background_image": "files/Nezuko1.jpg"). resolve_bg() handed those values
# over verbatim, the engine resolved them against the BASE wallpaper it renders,
# found nothing, and the preset came up as its base's default look — the Nezuko
# presets of the Customizable Module Visualizer showed the visualizer on black.
#
# ⚠ ONLY A FILE THAT IS REALLY INSIDE THE PRESET'S FOLDER IS REWRITTEN. Workshop
# content is third-party: a value that climbs out with "..", or a symlink in the
# preset that points elsewhere, must reach the engine exactly as written, so the
# engine's own lookup is all it ever gets.
#
# ⚠ NOTHING HERE MAY DISPATCH A VERB (see wpengine_assets_dir.sh): the script is
# SOURCED and resolve_bg() called directly, so no IPC happens at all.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

SCRIPT="${SYNUI_WPENGINE_SCRIPT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/systemd/synui-wpengine.sh}"
[ -r "$SCRIPT" ] || { echo "no script at $SCRIPT"; exit 1; }

T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

pass=0 fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  FAIL  %s\n' "$1"; fail=$((fail+1)); }
check(){ if [ "$1" = 0 ]; then ok "$2"; else bad "$2"; fi; }

# A Workshop tree with a web wallpaper and a preset of it that carries files.
W="$T/steam/steamapps/workshop/content/431960"
mkdir -p "$W/800000001" "$W/800000002/files" "$T/elsewhere"
printf '{"type":"web","title":"stub base","file":"index.html"}\n' > "$W/800000001/project.json"
: > "$W/800000002/files/picture.jpg"
: > "$W/800000002/files/clip.mp4"
: > "$T/elsewhere/secret.jpg"
ln -s "$T/elsewhere/secret.jpg" "$W/800000002/files/link.jpg"
cat > "$W/800000002/project.json" <<'JSON'
{
  "title": "stub preset",
  "dependency": "800000001",
  "preset": {
    "background_image": "files/picture.jpg",
    "background_video": "files/clip.mp4",
    "slides": "files",
    "missing": "files/gone.jpg",
    "climbs": "../800000001/project.json",
    "link": "files/link.jpg",
    "customface": "http://example.com/clock.png",
    "clock_font": "impact",
    "clock_color": "1 1 1",
    "enabled": true
  }
}
JSON

# resolve_bg's NUL-separated words, one per line
args="$( ( set +u
           export HOME="$T/home" XDG_RUNTIME_DIR="$T/run" STEAM_ROOT="$T/steam" \
                  SYNUI_WPENGINE_SYSROOT="$T/sysroot" SYNUI_WPENGINE_SOURCE_ONLY=1
           # shellcheck disable=SC1090
           . "$SCRIPT"
           resolve_bg 800000002 ) | tr '\0' '\n' )"

# The value --set-property carried for one name, or <none>.
value() {
    printf '%s\n' "$args" | sed -n "s/^$1=//p" | head -1 | grep . || echo '<none>'
}

here="$(realpath "$W/800000002")"

echo "synui-wpengine preset files"
echo "  script: $SCRIPT"

[ "$(printf '%s\n' "$args" | head -1)" = 800000001 ]
check $? "the preset renders its base wallpaper"

[ "$(value background_image)" = "$here/files/picture.jpg" ]
check $? "an image in the preset's folder goes out as its real path ($(value background_image))"

[ "$(value background_video)" = "$here/files/clip.mp4" ]
check $? "so does a video"

[ "$(value slides)" = "$here/files" ]
check $? "so does a folder in it"

[ "$(value missing)" = files/gone.jpg ]
check $? "a file the preset does not have is passed as written"

[ "$(value climbs)" = ../800000001/project.json ]
check $? "a value that climbs out of the preset is passed as written"

[ "$(value link)" = files/link.jpg ]
check $? "a symlink that points out of the preset is passed as written"

[ "$(value customface)" = http://example.com/clock.png ]
check $? "a URL is untouched"

[ "$(value clock_font)" = impact ] && [ "$(value clock_color)" = "1 1 1" ]
check $? "plain values are untouched"

[ "$(value enabled)" = 1 ]
check $? "a boolean is still 1/0"

echo
printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
