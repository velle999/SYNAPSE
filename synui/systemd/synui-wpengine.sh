#!/bin/bash
# synui-wpengine — drive linux-wallpaperengine as a layer-shell wallpaper on synui.
#
# Wallpaper Engine itself is a Windows app; under Proton it renders into a
# Windows desktop window that does not exist here, so its own "Apply" is a
# no-op on Wayland. This runs the native reimplementation instead, which
# reads the same Steam Workshop assets and paints a wlr-layer-shell
# BACKGROUND surface. synui creates layer_tree[BACKGROUND] above its own
# wallpaper_tree, so that surface covers whatever wallpaper.c drew.
#
# The chosen wallpapers persist in ~/.config/synui/wpengine.state, one
# "<output> <workshop-id>" per line, and `restore` replays them at login.
#
# ONE ENGINE PROCESS PER OUTPUT
#
# The engine can drive several screens from a single process (--screen-root A
# --bg 1 --screen-root B --bg 2) and this script used to do exactly that. It
# cannot any more, because --set-property does not scope: every override lands
# in one flat map (ApplicationContext.cpp) and setupProperties() hands that same
# map to EVERY loaded background, so a preset's `color` or `size` would bleed
# onto whatever is painting the other monitor. Names that generic collide
# constantly. One process per screen is what makes the override map per-screen.
#
# It also makes a per-output change genuinely local: setting or clearing one
# monitor no longer has to stop and restart the wallpaper on all of them.
#
# PRESETS
#
# A subscription with no top-level "type", but with a "preset" object and a
# "dependency", is not a wallpaper of its own — it is a saved property set for
# the wallpaper "dependency" names (most of them are configs of audio
# visualisers). Handing its own id to the engine gets "Project type missing"
# and a screen that does not change. resolve_bg() turns it into the base
# wallpaper plus a --set-property per saved value, which is what Wallpaper
# Engine itself does with one.

set -uo pipefail

STEAM="${STEAM_ROOT:-$HOME/.local/share/Steam}"
WORKSHOP="$STEAM/steamapps/workshop/content/431960"
ASSETS="$STEAM/steamapps/common/wallpaper_engine/assets"
# The read-only tree SynapseOS ships its OWN wallpapers in, searched after the
# user's Workshop tree. This is what lets a wallpaper work on a box with no
# Steam at all: the Workshop "content" directory is just a directory, nothing
# about it requires Steam to have made it, and --bg takes a PATH as happily as
# an id. Steam's assets tree is only read by SCENE wallpapers — a video one
# renders fine with an empty --assets-dir. Both verified, not assumed.
SYSROOT="${SYNUI_WPENGINE_SYSROOT:-/usr/share/synapse/wallpapers/431960}"
WP_ROOTS=("$WORKSHOP" "$SYSROOT")
# Prefer the packaged wrapper (it cds into /usr/lib and sets the NVIDIA env
# itself); fall back to an uninstalled build tree for development.
if [ -n "${SYNUI_WPENGINE_BIN:-}" ]; then
    ENGINE="$SYNUI_WPENGINE_BIN"
elif [ -x /usr/bin/linux-wallpaperengine ]; then
    ENGINE=/usr/bin/linux-wallpaperengine
else
    ENGINE="$HOME/SYNAPSE/linux-wallpaperengine/build/output/linux-wallpaperengine"
fi
STATE="$HOME/.config/synui/wpengine.state"
RUNDIR="${XDG_RUNTIME_DIR:-/tmp}"
LOCKFILE="$RUNDIR/synui-wpengine.lock"
# What older versions of this script wrote, when one process covered every
# screen. Kept only so an upgrade in a live session can still reap it.
LEGACY_PIDFILE="$RUNDIR/synui-wpengine.pid"

# One pidfile and one log per output. Connector names ("DP-1", "HDMI-A-1") are
# already filename-safe, but sanitise rather than trust a name into a path.
slug()        { printf '%s' "$1" | tr -c 'A-Za-z0-9._-' '_'; }

# id -> the directory holding it, most specific root first. Fails if no root
# has it, so callers can keep saying "no such wallpaper".
wp_dir() {
    local root
    for root in "${WP_ROOTS[@]}"; do
        if [ -d "$root/$1" ]; then printf '%s' "$root/$1"; return 0; fi
    done
    return 1
}
pidfile_for() { printf '%s/synui-wpengine.%s.pid' "$RUNDIR" "$(slug "$1")"; }
logfile_for() { printf '%s/synui-wpengine.%s.log' "$RUNDIR" "$(slug "$1")"; }

# Every mutating command is read-modify-write across three things that must
# agree: the state file, the pidfiles, and the running engines. synui fires this
# script from the Super+W picker without waiting for it, so two picks in quick
# succession overlap — and they did: one invocation rewrote the state while
# another was between its resolve and its launch, leaving an engine painting
# ids that appear nowhere in the state file, and the next pick reasoning from a
# state that did not describe the screen. Serialize them instead. The lock is
# held until the process exits, which is what makes stop-then-start atomic.
#
# "until the process exits" means THIS process and no other: an flock belongs to
# the open file description, not to the fd, so a child that inherits fd 9 keeps
# the lock alive for as long as IT runs. The engine we launch is a daemon, so
# that made every later pick — a Workshop id or a plain image, since leaving the
# engine also has to take the lock — sit out the full `-w 30` before doing
# anything at all, and then proceed unserialized anyway, which is the very race
# the lock exists to prevent. The engine launch below closes fd 9 explicitly
# (`9>&-`); anything else spawned from here is short-lived and finishes while we
# still hold the lock, so it costs nothing to let those inherit it.
take_lock() {
    exec 9>"$LOCKFILE" || return 0
    flock -w 30 9 ||
        echo "synui-wpengine: timed out waiting for the lock, continuing" >&2
}

# Linux truncates /proc/PID/comm to 15 chars, so "linux-wallpaperengine" shows up
# as "linux-wallpaper" and `pgrep -x linux-wallpaperengine` never matches.
COMM=linux-wallpaper

# linux-wallpaperengine resolves its CEF/asset siblings relative to the
# binary, so an uninstalled build has to be started from its own output
# directory. The packaged wrapper already does that for us.
ENGINE_DIR="$(dirname "$ENGINE")"
[ "$ENGINE" = /usr/bin/linux-wallpaperengine ] && ENGINE_DIR=/

die() { echo "synui-wpengine: $*" >&2; exit 1; }

need_engine() {
    [ -x "$ENGINE" ] || die "engine not found at $ENGINE (set SYNUI_WPENGINE_BIN)"
}

# Workshop DIRECTORY -> "type<TAB>title", read straight from project.json.
#
# A preset has no "type" of its own; report it as one and name the wallpaper it
# re-configures, because a bare "?" said nothing about why it behaves
# differently. An asset pack is called out for the same reason.
meta() {
    python3 - "$1" "$WORKSHOP" <<'PY' 2>/dev/null
import json, os, sys

def load(path):
    with open(os.path.join(path, "project.json"), encoding="utf-8-sig") as f:
        return json.load(f)

d, root = sys.argv[1], sys.argv[2]
try:
    j = load(d)
except Exception:
    sys.exit(1)

t = str(j.get("type", "") or "")
title = j.get("title", "?")

if not t:
    if isinstance(j.get("preset"), dict) and j.get("dependency") is not None:
        dep = str(j["dependency"])
        try:
            base = load(os.path.join(root, dep)).get("title", dep)
            t, title = "preset", "%s  [preset of %s]" % (title, base)
        except Exception:
            t, title = "preset!", "%s  [base %s not subscribed]" % (title, dep)
    elif str(j.get("category", "")).lower() == "asset":
        t = "asset"
    else:
        t = "?"

print("%s\t%s" % (t.lower(), title))
PY
}

# Workshop id -> what the engine should actually be told to render: the id to
# pass to --bg, then any override arguments, NUL-separated on stdout.
#
# For an ordinary wallpaper that is just the id back. For a PRESET it is the
# "dependency" wallpaper plus one --set-property per saved value. The engine
# parses `name=value` into a flat map and setupPropertiesForProject() looks each
# name up against the project's OWN properties, so names the base does not have
# are never consulted and cost nothing — which is what lets the editor's
# bookkeeping keys through harmlessly. JSON booleans become 1/0, the only
# spelling PropertyBool accepts besides "true"; nulls are dropped.
#
# Prints nothing and fails when it IS a preset whose dependency is not
# subscribed: there is no wallpaper to render, and the caller skips that output
# rather than starting an engine that would draw nothing.
resolve_bg() {
    python3 - "$1" "${WP_ROOTS[@]}" <<'PY'
import json, os, sys

wid, roots = sys.argv[1], sys.argv[2:]

def find(i):
    for r in roots:
        p = os.path.join(r, i)
        if os.path.isdir(p):
            return p
    return None

def load(i):
    p = find(i)
    if p is None:
        raise IOError(i)
    with open(os.path.join(p, "project.json"), encoding="utf-8-sig") as f:
        return json.load(f)

def bg(i):
    """What to hand --bg for id `i`.

    An id in the USER's Workshop tree stays an id, which is exactly what the
    engine has always been given — no existing install changes behaviour.
    Something found only in a system root has to become a path, because the
    engine's own id lookup searches Steam and nowhere else.
    """
    return i if os.path.isdir(os.path.join(roots[0], i)) else (find(i) or i)

try:
    j = load(wid)
except Exception:
    # Unreadable project.json: hand the id over as-is and let the engine be the
    # judge, which is what happened before presets were understood at all.
    sys.stdout.write(bg(wid) + "\0")
    sys.exit(0)

preset = j.get("preset")
args = []

if not j.get("type") and isinstance(preset, dict) and j.get("dependency") is not None:
    dep = str(j["dependency"])
    try:
        load(dep)
    except Exception:
        sys.exit(1)
    args.append(bg(dep))
    for k, v in preset.items():
        if v is None or isinstance(v, (dict, list)):
            continue
        if isinstance(v, bool):
            v = "1" if v else "0"
        args.append("--set-property")
        args.append("%s=%s" % (k, v))
else:
    args.append(bg(wid))

sys.stdout.write("\0".join(args) + "\0")
PY
}

cmd_list() {
    local root d id m seen=()
    for root in "${WP_ROOTS[@]}"; do
        [ -d "$root" ] || continue
        for d in "$root"/*/; do
            [ -d "$d" ] || continue
            id="$(basename "$d")"
            # A user subscription shadows a shipped wallpaper of the same id.
            case " ${seen[*]-} " in *" $id "*) continue ;; esac
            m="$(meta "$d")" || continue
            seen+=("$id")
            printf '%-12s %-8s %s\n' "$id" "${m%%	*}" "${m##*	}"
        done
    done
    [ ${#seen[@]} -gt 0 ] || die "no wallpapers in ${WP_ROOTS[*]}"
}

outputs() {
    synctl outputs 2>/dev/null |
        python3 -c 'import json,sys; print("\n".join(o["name"] for o in json.load(sys.stdin)))' 2>/dev/null
}

# Is this pid one of our engines, and is it the one painting $out?
#
# Every engine we start names its output on the command line, which is a better
# identifier than the pidfile: it survives a stale or deleted pidfile, and it is
# what keeps a per-output stop from reaching another screen's engine. Only
# --screen-root takes a connector name (--bg takes an id, --set-property takes
# k=v), so matching the line after it cannot collide with another argument.
#
# It also reaps the single all-screens process older versions of this script
# ran: that one lists every output, so it matches — and it should, since it
# really is what is painting that screen.
pid_is_ours_for() {
    local pid="$1" out="$2"
    [ -n "$pid" ] || return 1
    [ "$(cat "/proc/$pid/comm" 2>/dev/null)" = "$COMM" ] || return 1
    # 2>/dev/null BEFORE the input redirection, not after. Redirections are
    # applied left to right, so with the usual ordering the shell tries to open
    # /proc/<pid>/cmdline first and prints "No such file or directory" on the
    # still-live stderr when the engine exited between the comm check above and
    # this line -- which is exactly what a stale pidfile looks like on restore.
    tr '\0' '\n' 2>/dev/null < "/proc/$pid/cmdline" |
        grep -A1 -x -e '--screen-root' | grep -qxF -e "$out"
}

# The engine painting one output. Pidfile first, then a scan by cmdline.
pids_for_output() {
    local out="$1" pid pf
    pf="$(pidfile_for "$out")"
    if [ -r "$pf" ] && read -r pid < "$pf" && pid_is_ours_for "$pid" "$out"; then
        echo "$pid"
        return 0
    fi
    for pid in $(pgrep -x "$COMM" 2>/dev/null); do
        pid_is_ours_for "$pid" "$out" && echo "$pid"
    done
    return 0
}

all_engine_pids() { pgrep -x "$COMM" 2>/dev/null; }

# Kill whatever the given lister command reports, politely then not. The list is
# re-queried each pass rather than `kill -0`-ing a saved copy, which cannot tell
# "one of these is gone" from "all of them are".
reap() {
    local pids _
    pids="$("$@")"
    [ -n "$pids" ] && kill $pids 2>/dev/null
    for _ in $(seq 30); do
        [ -n "$("$@")" ] || break
        sleep 0.1
    done
    pids="$("$@")"
    [ -n "$pids" ] && kill -9 $pids 2>/dev/null
    return 0
}

# Stop the engine on ONE output and leave every other screen painting.
stop_output() {
    local out="$1"
    reap pids_for_output "$out"
    rm -f "$(pidfile_for "$out")"
    return 0
}

# Stop everything, including engines this script did not start: a leftover from
# a previous session, or the old single all-screens process, whose pidfile name
# is not in the per-output scheme and which would otherwise survive every stop
# and keep painting over the wallpaper synui just restored.
stop_all() {
    reap all_engine_pids
    rm -f "$RUNDIR"/synui-wpengine.*.pid "$LEGACY_PIDFILE"
    return 0
}

# The state lines that apply to THIS session, "<output> <id>" per line.
#
# Entries are dropped when the compositor we are talking to has no such output.
# That is what keeps a NESTED synui harmless: synui autostarts this by absolute
# path, so a nested instance runs the real script, and without this filter its
# `restore` would kill the engine painting the real seat and relaunch it on a
# 1280x720 HEADLESS-1. An empty `synctl outputs` means we could not ask (IPC not
# up yet at login), which is NOT the same as "no match" — trust the state file
# then rather than silently never starting.
live_entries() {
    [ -s "$STATE" ] || return 0

    local live out id
    live="$(outputs)"

    while read -r out id; do
        case "$out" in ''|'#'*) continue ;; esac
        [ -n "$id" ] || continue
        if [ -n "$live" ] && ! printf '%s\n' "$live" | grep -qxF "$out"; then
            continue
        fi
        wp_dir "$id" >/dev/null || { echo "synui-wpengine: skipping unknown id $id" >&2; continue; }
        printf '%s %s\n' "$out" "$id"
    done < "$STATE"
}

# Launch one engine for one output. Caller has already cd'd to $ENGINE_DIR.
start_output() {
    local out="$1" id="$2"
    local args=() props=() bg pid

    mapfile -t -d '' args < <(resolve_bg "$id")
    if [ ${#args[@]} -eq 0 ]; then
        echo "synui-wpengine: cannot resolve $id for $out" \
             "(a preset whose base wallpaper is not subscribed?), skipping" >&2
        return 1
    fi
    bg="${args[0]}"
    [ ${#args[@]} -gt 1 ] && props=("${args[@]:1}")

    # Steam's assets tree is read by SCENE wallpapers only; video and web
    # render without it. Requiring it unconditionally is what used to stop a
    # box with no Steam from showing a wallpaper it already had on disk.
    # `wtype`, not `type`: a local named `type` shadows the shell builtin for
    # the rest of the function.
    #
    # ⚠ BUT `--assets-dir` MUST STILL BE PASSED, AND MUST NAME A DIRECTORY THAT
    # EXISTS. "Renders without the assets tree" is true of the wallpaper; it is
    # NOT true of the flag. Omit it and the engine does not shrug and carry on —
    # it goes looking for Steam's copy of Wallpaper Engine, fails, and EXITS:
    #
    #     Cannot find directory for steam app wallpaper_engine: assets
    #     Cannot find a valid assets folder, resolved to
    #         "/usr/lib/linux-wallpaperengine/assets"
    #
    # That is every machine without Wallpaper Engine bought on Steam — which is
    # the live ISO and every fresh install, i.e. exactly the machines the four
    # wallpapers in $SYSROOT exist for. An EMPTY directory is enough: upstream
    # only requires the path to resolve, and a video wallpaper never reads it.
    #
    # ⚠ AND IT FAILS LOOKING LIKE IT WORKED. The engine commits one buffer
    # before it dies, so its dead layer surface keeps that frame on the
    # background layer; synui is not watching the process and nothing repaints.
    # The desktop shows a still picture that never moves, the picker says the
    # wallpaper was applied, and the only record is the runtime log — which
    # /run clears at the next boot. Verified on the 0.2.9 ISO (synui 390,
    # linux-wallpaperengine 0.1.0-14): with the flag omitted `pgrep
    # linux-wallpaperengine` finds nothing seconds after "applying"; with it
    # pointed at an empty directory the process is still there.
    local assets=() dir wtype
    if [ -d "$ASSETS" ]; then
        assets=(--assets-dir "$ASSETS")
    else
        dir="$(wp_dir "$id")" && wtype="$(meta "$dir")" && wtype="${wtype%%	*}"
        if [ "${wtype:-}" = scene ]; then
            echo "synui-wpengine: $id is a scene wallpaper and needs Wallpaper" \
                 "Engine's assets at $ASSETS — skipping $out" >&2
            return 1
        fi
        # In $RUNDIR rather than $SYSROOT: the wallpaper tree is read-only on
        # the live image, and this wants to be gone at the next boot in case a
        # real assets tree has turned up by then.
        if mkdir -p "$RUNDIR/synui-wpengine-assets" 2>/dev/null; then
            assets=(--assets-dir "$RUNDIR/synui-wpengine-assets")
        fi
    fi

    # THE WALLPAPER MUST NOT TAKE THE MOUSE, or the desktop stops responding.
    #
    # The engine's input region is empty only while mouse support is off. With
    # it on -- which is the default, there is only a --disable-mouse flag --
    # WaylandOutputViewport.cpp does
    #
    #     wl_region_add (region, 0, 0, INT32_MAX, INT32_MAX);
    #
    # and claims the whole surface. synui reads "clicked the desktop" as a click
    # that hit no view and no surface at all (input.c, the deskmenu_open path),
    # so a wallpaper holding a full-surface input region silently swallows every
    # desktop right-click on every output it covers. The menu never opens and
    # nothing logs a reason.
    #
    # Set SYNUI_WPENGINE_MOUSE=1 to hand the mouse back for a wallpaper that
    # reacts to the cursor, knowing the desktop menu goes with it.
    local mouse=(--disable-mouse)
    [ "${SYNUI_WPENGINE_MOUSE:-0}" = 1 ] && mouse=()

    # __GL_THREADED_OPTIMIZATIONS=0 is the upstream-documented NVIDIA workaround.
    # 9>&- drops the lock fd: the engine outlives this script, and an inherited
    # fd 9 would hold take_lock's flock for the whole session (see take_lock).
    __GL_THREADED_OPTIMIZATIONS=0 \
    setsid "$ENGINE" "${assets[@]}" --layer background "${mouse[@]}" \
        --fps "${SYNUI_WPENGINE_FPS:-30}" --silent \
        --screen-root "$out" --bg "$bg" --scaling fill "${props[@]}" \
        >"$(logfile_for "$out")" 2>&1 </dev/null 9>&- &
    echo $! > "$(pidfile_for "$out")"
    disown

    # setsid execs when it can and forks when it cannot, so $! is only
    # sometimes the engine. Let it settle, then record the pid we can see —
    # identified by the output it names, since several are running now.
    for _ in $(seq 30); do
        pid="$(pids_for_output "$out" | head -1)"
        [ -n "$pid" ] && { echo "$pid" > "$(pidfile_for "$out")"; return 0; }
        sleep 0.1
    done
    return 0
}

# One engine per line of the saved state. Outputs with no entry keep synui's own
# wallpaper, since nothing is started for them.
start_from_state() {
    need_engine

    local entries
    entries="$(live_entries)"
    [ -n "$entries" ] || return 0

    cd "$ENGINE_DIR" || die "cannot enter $ENGINE_DIR"

    local out id
    while read -r out id; do
        [ -n "$out" ] || continue
        start_output "$out" "$id"
    done <<< "$entries"
}

cmd_set() {
    local id="${1:-}" out="${2:-}"
    [ -n "$id" ] || die "usage: synui-wpengine set <workshop-id> [output|all]"
    wp_dir "$id" >/dev/null || die "no such wallpaper: $id"

    take_lock

    mkdir -p "$(dirname "$STATE")"
    touch "$STATE"

    if [ -z "$out" ] || [ "$out" = all ]; then
        : > "$STATE"
        local o
        for o in $(outputs); do printf '%s %s\n' "$o" "$id" >> "$STATE"; done
        stop_all
        start_from_state
    else
        # replace this output's line, keep the others
        local tmp; tmp="$(mktemp)"
        grep -v "^$out " "$STATE" > "$tmp" 2>/dev/null || true
        printf '%s %s\n' "$out" "$id" >> "$tmp"
        mv "$tmp" "$STATE"

        # Only this screen's engine is touched. The others keep painting and
        # never even flicker — with one process for everything they all had to
        # come down and back up for a change to a single monitor.
        stop_output "$out"
        need_engine
        cd "$ENGINE_DIR" || die "cannot enter $ENGINE_DIR"
        start_output "$out" "$id"
    fi

    echo "synui-wpengine: applied $id"
}

cmd_off() {
    local out="${1:-}"

    take_lock
    mkdir -p "$(dirname "$STATE")"

    if [ -n "$out" ] && [ "$out" != all ]; then
        local tmp; tmp="$(mktemp)"
        grep -v "^$out " "$STATE" > "$tmp" 2>/dev/null || true
        mv "$tmp" "$STATE"
        stop_output "$out"
        echo "synui-wpengine: stopped on $out, synui wallpaper restored there"
        return 0
    fi

    stop_all
    : > "$STATE"
    echo "synui-wpengine: stopped, synui wallpaper restored"
}

cmd_status() {
    local stray
    if [ ! -s "$STATE" ]; then
        stray="$(all_engine_pids)"
        if [ -n "$stray" ]; then
            echo "not running by us, but an engine is alive (pid $(echo $stray))"
        else
            echo "not running"
        fi
        return 0
    fi

    local out id pids m
    printf '%-10s %-8s %-12s %-8s %s\n' OUTPUT PID ID TYPE TITLE
    while read -r out id; do
        [ -n "$out" ] || continue
        pids="$(pids_for_output "$out")"
        [ -n "$pids" ] || pids='-'
        if m="$(meta "$(wp_dir "$id")")"; then
            printf '%-10s %-8s %-12s %-8s %s\n' \
                   "$out" "$(echo $pids)" "$id" "${m%%	*}" "${m##*	}"
        else
            printf '%-10s %-8s %-12s %-8s %s\n' "$out" "$(echo $pids)" "$id" '?' '(missing)'
        fi
    done < "$STATE"
}

# Test seam. Sourcing the script gets at the functions above without running a
# command — which matters more here than usual: every verb goes through
# `outputs()`, and `synctl outputs` talks to whatever compositor the ambient
# WAYLAND_DISPLAY names. That is the LIVE seat, so a test that dispatched a verb
# would start engines on the developer's real screens.
if [ -n "${SYNUI_WPENGINE_SOURCE_ONLY:-}" ]; then
    return 0 2>/dev/null || exit 0
fi

case "${1:-}" in
    list)    cmd_list ;;
    set)     shift; cmd_set "$@" ;;
    off)     shift; cmd_off "$@" ;;
    restore)
        take_lock
        # Decide BEFORE stopping anything: with nothing to run here (a nested
        # or headless synui), leave whatever is on the real seat alone.
        entries="$(live_entries)"
        if [ -z "$entries" ]; then
            echo "synui-wpengine: no saved wallpaper for this session's outputs" >&2
            exit 0
        fi
        # Stop only the outputs about to be repainted — NOT stop_all. A nested
        # synui that does have a matching output must not take down the real
        # seat's other screens, and this is also what reaps the old single
        # all-screens process on the first restore after an upgrade.
        while read -r out _; do stop_output "$out"; done <<< "$entries"
        start_from_state ;;
    status)  cmd_status ;;
    *)
        cat <<EOF
usage: synui-wpengine <command>

  list                     list subscribed Workshop wallpapers (id, type, title)
  set <id> [output|all]    apply a wallpaper (default: every output) and persist
  off [output|all]         stop the engine (on one output, or everywhere) and
                           hand the background back to synui
  restore                  re-apply the saved state (for autostart)
  status                   show what is running and what is saved

One engine process runs per output, so a preset's property overrides stay on
the screen they were picked for. A preset (a saved property set for some other
wallpaper) is resolved to that wallpaper plus its overrides automatically.
EOF
        exit 1 ;;
esac
