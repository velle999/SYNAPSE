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
# ⚠ AND IT IS NOW THE ONLY THING THAT TURNS THE HUD ON. The session used to
# export MANGOHUD=1 for every process in it, which loaded MangoHud's Vulkan
# layer into every Vulkan client whether or not it was a game — and segfaulted
# three of them on AMD. That default is off; this wrapper (and `syn game`,
# which is a front door onto it) is where the overlay comes from, and anything
# it launches inherits it, so `syn game steam` covers a whole library.
#
# `--hud-everywhere on` puts the old session-wide behaviour back for whoever
# asks for it; see /etc/synapseos/mangohud.conf.
#
# Usage: synui-game-run [--gamescope[=WxH[@R]]] [--no-hud] [--no-gamemode]
#                       [--ensure-config] -- COMMAND [ARGS...]
#        synui-game-run --hud-everywhere [on|off|status]
set -u

hud=1 gm=1 gamescope='' cfg_only=0

# ── The session-wide switch ─────────────────────────────────────────────────
#
# Per USER, deliberately: /etc/synapseos/mangohud.conf is the machine's default
# and needs root to change, and one person wanting an overlay in everything is
# not a reason to change the machine for everybody. The session prefers the
# user file when it exists.
hud_everywhere() {  # hud_everywhere <on|off|status>
    local f="${XDG_CONFIG_HOME:-$HOME/.config}/synapseos/mangohud.conf"
    local cur=0
    for c in "$f" /etc/synapseos/mangohud.conf; do
        [ -r "$c" ] && { cur=$(sed -n 's/^[[:space:]]*MANGOHUD_EVERYWHERE=\([01]\).*/\1/p' "$c" | tail -1); break; }
    done
    [ -n "$cur" ] || cur=0

    case "${1:-status}" in
        status)
            if [ "$cur" = 1 ]; then
                printf 'MangoHud loads in every Vulkan client this session starts.\n'
            else
                printf 'MangoHud loads only in what `syn game` / synui-game-run launches.\n'
            fi
            printf '  change it with: syn game hud on|off\n'
            return 0 ;;
        on)  want=1 ;;
        off) want=0 ;;
        *)   printf 'synui-game-run: --hud-everywhere takes on, off or status\n' >&2
             return 2 ;;
    esac

    mkdir -p "$(dirname "$f")" || return 1
    # Written whole, not appended: this file has exactly one setting in it, and
    # a second MANGOHUD_EVERYWHERE line below the first would make the answer
    # depend on which one a reader stops at.
    cat > "$f" <<EOF
# Written by \`syn game hud\`. See /etc/synapseos/mangohud.conf for what this
# does and why the default is 0.
MANGOHUD_EVERYWHERE=$want
EOF
    if [ "$want" = 1 ]; then
        printf 'MangoHud will load in every Vulkan client — from the next login.\n'
        printf '  ⚠ on AMD that has segfaulted mpv, QtMultimedia and a test suite\n'
    else
        printf 'MangoHud will load only in what a game launcher starts — from the next login.\n'
    fi
    return 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --no-hud)        hud=0 ;;
        --no-gamemode)   gm=0 ;;
        --gamescope)     gamescope='default' ;;
        --gamescope=*)   gamescope="${1#*=}" ;;
        --ensure-config) cfg_only=1 ;;
        --hud-everywhere)   shift; hud_everywhere "${1:-status}"; exit $? ;;
        --hud-everywhere=*) hud_everywhere "${1#*=}"; exit $? ;;
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
mangohud_default() {
    cat <<'EOF'
# SynapseOS default MangoHud overlay (synui-game-run). Edit freely — it is only
# written when absent. See `man mangohud` for every option.
#
# This file SHADOWS /etc/MangoHud.conf completely, so it repeats the system
# default's no_display: the overlay is meant to stay hidden until Shift_R+F12
# asks for it, inside the game, which is the only place that toggle can work.
no_display
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

ensure_config() {
    local d="$HOME/.config/MangoHud" f
    f="$d/MangoHud.conf"
    mkdir -p "$d" 2>/dev/null || return 0

    if [ ! -f "$f" ]; then
        mangohud_default > "$f"
        return 0
    fi

    # ── Heal a config from a synui-game-run older than the no_display default ──
    #
    # This file SHADOWS /etc/MangoHud.conf entirely (documented MangoHud
    # behaviour), so a copy written before no_display was added takes the
    # system default's "hidden until Shift_R+F12" away and gives no way to get
    # it back — and because the early return above skipped any existing file,
    # it could never self-heal. The overlay came up covering the game on every
    # launch instead of on request.
    #
    # Strictly guarded, because silently rewriting a config someone tuned is
    # worse than the bug: heal ONLY when the file still carries our header AND
    # every setting in it is byte-identical to today's default apart from the
    # missing no_display. Change one value and this leaves it alone forever.
    if grep -q 'SynapseOS default MangoHud overlay' "$f" \
       && ! grep -qx 'no_display' "$f" \
       && diff -q <(mangohud_default | grep -vE '^[[:space:]]*(#|$)' | grep -vx 'no_display') \
                  <(grep -vE '^[[:space:]]*(#|$)' "$f") >/dev/null 2>&1; then
        mangohud_default > "$f"
    fi
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
