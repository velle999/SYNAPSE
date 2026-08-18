#!/usr/bin/env bash
# wpengine_assets_dir.sh — the engine must always be told where the assets are.
#
# THE BUG THIS EXISTS FOR (synui 0.1.0-392): synui-wpengine passed
# `--assets-dir` only when Steam's Wallpaper Engine assets tree was on disk, and
# otherwise omitted the flag — on the correct observation that a VIDEO wallpaper
# never reads that tree. The wallpaper does not; the ENGINE does. With no
# `--assets-dir` upstream goes looking for Steam's copy of the app, fails, and
# EXITS:
#
#     Cannot find directory for steam app wallpaper_engine: assets
#     Cannot find a valid assets folder, resolved to
#         "/usr/lib/linux-wallpaperengine/assets"
#
# That is every machine that does not own Wallpaper Engine on Steam — the live
# ISO and every fresh install, i.e. exactly the machines the four wallpapers in
# /usr/share/synapse/wallpapers/431960 were packaged for.
#
# ⚠ THE ASSERTION IS "THE FLAG IS THERE AND ITS PATH EXISTS", NOT "AN ASSETS
# TREE EXISTS". An empty directory is enough for a video wallpaper and is what
# the fix passes; asserting on the CONTENTS would fail the fix, and asserting
# only that the flag appears would pass a fix that named a path that isn't there
# — which is the failure being repaired.
#
# ⚠ NOTHING HERE MAY DISPATCH A VERB. Every verb calls `outputs()`, which runs
# `synctl outputs` against whatever compositor WAYLAND_DISPLAY names — the LIVE
# seat. The script is SOURCED (SYNUI_WPENGINE_SOURCE_ONLY=1) and start_output()
# called directly, so no IPC happens at all.
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

# A wallpaper tree with one video and one scene wallpaper, standing in for
# /usr/share/synapse/wallpapers/431960.
mkdir -p "$T/sysroot/900000001" "$T/sysroot/900000002"
printf '{"type":"video","title":"stub video","file":"v.mp4"}\n' \
    > "$T/sysroot/900000001/project.json"
printf '{"type":"scene","title":"stub scene"}\n' \
    > "$T/sysroot/900000002/project.json"

# The stub engine records its argv and exits. It does NOT have to survive:
# start_output's settle loop gives up on its own and returns 0 either way, and a
# stub that lingered would only make the test slower.
mkdir -p "$T/bin"
cat > "$T/bin/engine" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$ARGV_OUT"
STUB
chmod +x "$T/bin/engine"

# Everything the script reads from the environment, so it never touches the real
# home, the real runtime dir or the real Steam install.
run_start() {
    local steam="$1" out="$2" id="$3"
    : > "$T/argv"
    rm -rf "$T/run"; mkdir -p "$T/run" "$T/home/.config/synui"
    ( set +u
      export HOME="$T/home" \
             XDG_RUNTIME_DIR="$T/run" \
             STEAM_ROOT="$steam" \
             SYNUI_WPENGINE_SYSROOT="$T/sysroot" \
             SYNUI_WPENGINE_BIN="$T/bin/engine" \
             SYNUI_WPENGINE_SOURCE_ONLY=1 \
             ARGV_OUT="$T/argv"
      # shellcheck disable=SC1090
      . "$SCRIPT"
      start_output "$out" "$id" )
    return $?
}

# The value that followed --assets-dir, or nothing.
assets_arg() {
    grep -A1 -x -e '--assets-dir' "$T/argv" 2>/dev/null | tail -n +2 | head -1
}

echo "synui-wpengine --assets-dir"
echo "  script: $SCRIPT"

# ── 1. No Steam at all: the ISO and every fresh install ──────────────────
# This is the case the bug lived in. It must still launch (a video wallpaper
# needs no assets tree) AND it must still be told an assets directory.
run_start "$T/nosuchsteam" DP-1 900000001
check $? "video wallpaper starts with no Steam install"

a="$(assets_arg)"
[ -n "$a" ]
check $? "--assets-dir is passed when Steam's assets tree is absent"

[ -n "$a" ] && [ -d "$a" ]
check $? "the assets directory it names EXISTS (\"${a:-<none>}\")"

# ── 2. Steam present: unchanged, and it must win ─────────────────────────
mkdir -p "$T/steam/steamapps/common/wallpaper_engine/assets"
run_start "$T/steam" DP-1 900000001 >/dev/null
[ "$(assets_arg)" = "$T/steam/steamapps/common/wallpaper_engine/assets" ]
check $? "Steam's own assets tree is preferred when it is there"

# ── 3. A SCENE wallpaper with no assets tree still declines ──────────────
# The fallback is an EMPTY directory, which a scene wallpaper cannot use — so
# this must keep refusing rather than launching an engine that renders nothing.
run_start "$T/nosuchsteam" DP-1 900000002
rc=$?
[ "$rc" != 0 ]
check $? "a scene wallpaper with no assets tree is still refused"

[ ! -s "$T/argv" ]
check $? "...and the engine is never launched for it"

echo
printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
