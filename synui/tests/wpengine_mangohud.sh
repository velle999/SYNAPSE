#!/usr/bin/env bash
# wpengine_mangohud.sh — MangoHud must never be loaded into the wallpaper engine.
#
# THE BUG THIS EXISTS FOR (synui 0.1.0-409). The session exports MANGOHUD=1 so
# games get the overlay without a per-game wrapper — see the synui-session
# heredoc in syn-install and the matching ~/.bash_profile block. MangoHud's
# Vulkan manifest declares:
#
#     "enable_environment":  { "MANGOHUD": "1" }
#     "disable_environment": { "DISABLE_MANGOHUD": "1" }
#
# so that ONE variable loads VK_LAYER_MANGOHUD_overlay into every Vulkan client
# in the session. The wallpaper engine is one of them, because mpv asks
# libavutil for a Vulkan hwdec device — and on an AMD Renoir laptop that
# segfaults the engine before it paints a frame, inside MangoHud's own
# vkCreateDevice hook:
#
#     #0  libvulkan.so.1
#     #1  libMangoHud.so + 0x41eca
#     #4  vkCreateDevice
#     #5  libavutil.so.61
#     #7  av_hwdevice_ctx_create
#     #8+ libmpv.so.2
#
# ⚠ NVIDIA NEVER SEES IT — mpv picks a different hwdec there and vkCreateDevice
# is never called, so three engines run happily on the development desktop
# while every AMD laptop gets a still picture and a core dump. The wallpaper
# looks applied: the picker reports success, wpengine.state records it, and the
# only evidence is a core dump and a runtime log /run erases at reboot.
#
# ⚠ THE ASSERTION IS ON DISABLE_MANGOHUD, NOT ON UNSETTING MANGOHUD. The
# manifest's disable_environment beats its enable_environment, so
# DISABLE_MANGOHUD=1 is the knob that actually holds; clearing MANGOHUD alone
# would depend on nothing else in the session setting it again.
#
# ⚠ NOTHING HERE MAY DISPATCH A VERB — every verb runs `synctl outputs` against
# the LIVE seat. The script is SOURCED and start_output() called directly. Same
# rule as wpengine_assets_dir.sh.
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

mkdir -p "$T/sysroot/900000001"
printf '{"type":"video","title":"stub video","file":"v.mp4"}\n' \
    > "$T/sysroot/900000001/project.json"

# The stub engine records the ENVIRONMENT it was handed, which is the whole
# point here — the assets test records argv instead.
mkdir -p "$T/bin"
cat > "$T/bin/engine" <<'STUB'
#!/usr/bin/env bash
{
    printf 'MANGOHUD=%s\n'         "${MANGOHUD-<unset>}"
    printf 'DISABLE_MANGOHUD=%s\n' "${DISABLE_MANGOHUD-<unset>}"
} > "$ENV_OUT"
STUB
chmod +x "$T/bin/engine"

# MANGOHUD=1 in the caller's environment is not incidental — it is exactly what
# the session does, and it is the condition the bug needs.
run_start() {
    : > "$T/env"
    rm -rf "$T/run"; mkdir -p "$T/run" "$T/home/.config/synui"
    ( set +u
      export HOME="$T/home" \
             XDG_RUNTIME_DIR="$T/run" \
             STEAM_ROOT="$T/nosuchsteam" \
             SYNUI_WPENGINE_SYSROOT="$T/sysroot" \
             SYNUI_WPENGINE_BIN="$T/bin/engine" \
             SYNUI_WPENGINE_SOURCE_ONLY=1 \
             ENV_OUT="$T/env" \
             MANGOHUD=1 \
             MANGOHUD_CONFIGFILE="$T/home/.config/MangoHud/MangoHud.conf"
      # shellcheck disable=SC1090
      . "$SCRIPT"
      start_output DP-1 900000001
      # The fix has to be a per-command prefix, not an export: anything else
      # the script launches later is none of its business.
      printf 'SCRIPT_MANGOHUD=%s\n' "${MANGOHUD-<unset>}" >> "$ENV_OUT" )
    return $?
}

val() { grep -m1 "^$1=" "$T/env" 2>/dev/null | cut -d= -f2-; }

echo "synui-wpengine MangoHud"
echo "  script: $SCRIPT"

run_start
check $? "the engine still starts with MANGOHUD=1 in the session"

[ "$(val DISABLE_MANGOHUD)" = 1 ]
check $? "DISABLE_MANGOHUD=1 reaches the engine (got \"$(val DISABLE_MANGOHUD)\")"

[ "$(val MANGOHUD)" != 1 ]
check $? "MANGOHUD is not 1 for the engine (got \"$(val MANGOHUD)\")"

# ⚠ Both, and in that order of importance. DISABLE_MANGOHUD is what the Vulkan
# manifest honours; MANGOHUD=0 only covers the OpenGL side. A fix that set just
# the second would still load the layer whenever anything re-exported MANGOHUD.
[ "$(val SCRIPT_MANGOHUD)" = 1 ]
check $? "the script's own environment is untouched (got \"$(val SCRIPT_MANGOHUD)\")"

echo
printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
