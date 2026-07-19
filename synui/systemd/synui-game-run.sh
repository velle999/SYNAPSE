#!/usr/bin/env bash
# synui-game-run — launch a game the SteamOS way: gamemoderun for the CPU/GPU
# governor, MangoHud for the FPS/temp/frametime overlay, and (opt-in) gamescope
# as the nesting micro-compositor for clean scaling / a frame cap.
#
# WHY a launcher and not "game mode": synui's Super+G game mode is about a game
# that is ALREADY running — it frees the GPU (stops synapd) and holds off idle.
# MangoHud and gamemode have to be in place at *launch*; you cannot inject an
# overlay into a process that is already up. So this is the front half: point a
# .desktop Exec, a dock pin, or a shell at it.
#
#   Exec=synui-game-run -- %command%           # Steam launch option style
#   synui-game-run -- ./game.x86_64            # a bare binary
#   synui-game-run --gamescope=2560x1440@60 -- steam -gamepadui
#
# Every wrapper is optional and guarded: a missing tool is dropped, not fatal, so
# the command still launches (just without that layer).
#
# Usage: synui-game-run [--gamescope[=WxH[@R]]] [--no-hud] [--no-gamemode]
#                       [--ensure-config] -- COMMAND [ARGS...]
set -u

hud=1 gm=1 gamescope='' cfg_only=0

while [ $# -gt 0 ]; do
    case "$1" in
        --no-hud)        hud=0 ;;
        --no-gamemode)   gm=0 ;;
        --gamescope)     gamescope='default' ;;
        --gamescope=*)   gamescope="${1#*=}" ;;
        --ensure-config) cfg_only=1 ;;
        --)              shift; break ;;
        -h|--help)       sed -n '2,20p' "$0"; exit 0 ;;
        *)               echo "synui-game-run: unknown option '$1'" >&2; exit 2 ;;
    esac
    shift
done

# ── Default MangoHud overlay config ─────────────────────────────────────────
# Written once, never clobbered — a user who tunes it keeps their version. The
# defaults are the metrics worth seeing at a glance: FPS + frametime graph, GPU
# and CPU load/temp. Positioned top-left so it does not sit under a crosshair.
ensure_config() {
    local d="$HOME/.config/MangoHud" f
    f="$d/MangoHud.conf"
    [ -f "$f" ] && return 0
    mkdir -p "$d" 2>/dev/null || return 0
    cat > "$f" <<'EOF'
# SynapseOS default MangoHud overlay (synui-game-run). Edit freely — it is only
# written when absent. See `man mangohud` for every option.
legacy_layout=false
fps
frametime=1
frame_timing=1
gpu_stats
gpu_temp
gpu_load_change
cpu_stats
cpu_temp
cpu_load_change
ram
vram
position=top-left
font_size=20
background_alpha=0.4
toggle_hud=Shift_R+F12
EOF
}
ensure_config

if [ "$cfg_only" = 1 ]; then
    exit 0
fi

if [ $# -eq 0 ]; then
    echo "synui-game-run: no command after -- (see --help)" >&2
    exit 2
fi

# ── Build the wrapper stack, inside-out: gamemoderun → mangohud → CMD ────────
cmd=("$@")

if [ "$hud" = 1 ] && command -v mangohud >/dev/null 2>&1; then
    cmd=(mangohud "${cmd[@]}")
fi
if [ "$gm" = 1 ] && command -v gamemoderun >/dev/null 2>&1; then
    cmd=(gamemoderun "${cmd[@]}")
fi

# gamescope wraps the lot: it is the outer compositor the game renders into.
if [ -n "$gamescope" ]; then
    if command -v gamescope >/dev/null 2>&1; then
        gs=(gamescope -f)                 # -f: start fullscreen
        if [ "$gamescope" != default ]; then
            # WxH or WxH@R — width/height feed -W/-H, refresh feeds -r.
            res="${gamescope%@*}"; rate=''
            case "$gamescope" in *@*) rate="${gamescope#*@}";; esac
            case "$res" in
                *x*) gs+=(-W "${res%x*}" -H "${res#*x}") ;;
            esac
            [ -n "$rate" ] && gs+=(-r "$rate")
        fi
        cmd=("${gs[@]}" -- "${cmd[@]}")
    else
        echo "synui-game-run: gamescope not installed — running without it" >&2
    fi
fi

exec "${cmd[@]}"
