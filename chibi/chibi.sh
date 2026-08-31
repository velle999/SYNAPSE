#!/usr/bin/env bash
#
# chibi — launch the Chibi AI companion (SynapseOS package build).
#
# The upstream repo runs out of a git checkout with a .venv; the packaged build
# instead ships the app read-only in /usr/lib/chibi/app and its python deps in
# /usr/lib/chibi/pydeps (see PKGBUILD). All mutable state — memory, journal,
# alarms, soul — lives in $HOME, so the app directory never needs to be writable.

set -euo pipefail

APP_DIR=/usr/lib/chibi/app
PYDEPS=/usr/lib/chibi/pydeps
BUILD_PYVER=@PYVER@

# This box runs no notification daemon, so when the launcher is started from the
# dock or the waybar menu a terminal is the only way a failure is visible at all.
die() {
    local msg="chibi: $*"
    if [ -t 2 ]; then
        printf '%s\n' "$msg" >&2
    else
        # Launched from a .desktop with no tty, so the error has nowhere to go
        # but a terminal window. syntty is the default and the one terminal
        # every install profile has; kitty and foot are what older installs
        # have, and foot still works without OpenGL.
        #
        # ⚠ `command -v`, NOT `a || b`. The old chain asked the wrong question:
        # a non-zero exit from the first terminal means the COMMAND failed, not
        # that the terminal is missing — so a kitty that opened and printed the
        # error and was then closed with a non-zero status opened a second
        # window in foot saying the same thing. Asking whether the binary
        # exists is the question that was meant.
        #
        # ⚠ syntty needs --hold before -e; kitty and foot accept it either way.
        # A syntty older than 0.1.0-27 has no --hold at all and would die at
        # parse, so the message would be lost — which is why the error is still
        # written to stderr above it, where a journal can catch it.
        printf '%s\n' "$msg" >&2
        for t in syntty kitty foot; do
            command -v "$t" >/dev/null 2>&1 || continue
            # ⚠ FOREGROUND, as it always was. Backgrounding it and exiting
            # orphans the window onto init with this script's inherited
            # descriptors still open — and when the launcher was started by
            # quickshell those are pipes quickshell closes the moment its
            # direct child exits, which is the SIGPIPE that kills detached
            # children all over this system.
            MSG="$msg" "$t" --hold -e sh -c 'printf "%s\n" "$MSG"' >/dev/null 2>&1
            break
        done
    fi
    exit 1
}

[ -d "$APP_DIR" ] || die "app directory not found: $APP_DIR (reinstall the chibi package)"

# The vendored wheels (ctranslate2, onnxruntime, opencv) are compiled against
# the ABI of the python chibi was built with. After a python minor-version
# upgrade they are simply invisible, and Chibi would otherwise start up silently
# stripped of voice — the exact "she runs but can't hear or speak" failure this
# package exists to prevent. Fail loudly instead.
pyver="$(python -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
if [ "$pyver" != "$BUILD_PYVER" ]; then
    die "python $pyver but chibi's bundled deps were built for python $BUILD_PYVER — rebuild the chibi package"
fi

export PYTHONPATH="$PYDEPS${PYTHONPATH:+:$PYTHONPATH}"
# The app dir is read-only; don't let python try (and fail) to write .pyc.
# The package precompiled them already.
export PYTHONDONTWRITEBYTECODE=1

# Refuse to start a second instance. Two Chibis contend for the microphone and
# for the DreamSync port (8077); the loser only prints a bind error and then
# runs on without dream sync. Match on the process's cwd so a Chibi started by
# hand is found too, not just one we launched.
running_pid() {
    local pid
    for pid in $(pgrep -u "$(id -u)" -f 'main\.py' 2>/dev/null || true); do
        [ -r "/proc/$pid/cwd" ] || continue
        if [ "$(readlink -f "/proc/$pid/cwd" 2>/dev/null)" = "$APP_DIR" ]; then
            printf '%s' "$pid"
            return 0
        fi
    done
    return 1
}

if pid=$(running_pid); then
    die "already running (pid $pid) — close that window first"
fi

# SDL2 takes the Wayland app_id from these. synui's dock resolves a window's
# .desktop entry and icon by an exact <app_id>.desktop basename match
# (synui/src/icons.c), so without them Chibi shows up as a bare monogram.
export SDL_VIDEO_WAYLAND_WMCLASS=chibi
export SDL_VIDEO_X11_WMCLASS=chibi
export SDL_APP_ID=chibi

# ── SynapseOS defaults ───────────────────────────────────────────────────
# Each is overridable: a value already in the environment wins, so a user can
# point Chibi at Ollama or their own models without editing this launcher.
#
# Talk to synapd, the OS's own AI daemon, over its unix socket — so a fresh
# install has a working brain with no Ollama and no network.
export CHIBI_LLM_BACKEND="${CHIBI_LLM_BACKEND:-synapd}"
export CHIBI_SYNAPD_SOCKET="${CHIBI_SYNAPD_SOCKET:-/run/synapd/synapd.sock}"
# Load the packaged STT model instead of downloading ~75MB from HuggingFace on
# first run — without this a freshly imaged, offline machine boots up deaf.
export CHIBI_STT_MODEL_DIR="${CHIBI_STT_MODEL_DIR:-/usr/share/faster-whisper/small}"
# Come up windowed. Chibi's own default is the Pi kiosk's fullscreen, which on
# a desktop means no titlebar, so no close button — and under Wayland there is
# no window manager keybind to escape it either. The app draws its own quit
# button now, but a window the compositor can decorate is the better default
# here. Set CHIBI_FULLSCREEN=1 to get kiosk behaviour back.
export CHIBI_FULLSCREEN="${CHIBI_FULLSCREEN:-0}"

cd "$APP_DIR"
exec python main.py "$@"
