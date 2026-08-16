#!/bin/bash
# fit_rig.sh — render the Fit to screen tab in a HEADLESS nested synui and
# screenshot every state of it.
#
# ⚠ NOT run by `meson test`, and it must not be: it needs a DRM render node and
# it boots a compositor. syn_arcade_test.sh is the suite; this answers the one
# question a suite of greps cannot — does the panel actually DRAW, and is it
# filled with what the binary said.
#
# Usage:
#   tests/fit_rig.sh build/syn-arcade ../synui/_build/synui data/syn-arcade.qml
#
# ⚠ THE TAB IS SELECTED BY EDITING A COPY OF THE QML, not by clicking. There is
# no pointer on a headless seat, and synthesising one into the LIVE session is
# the one thing this project will not do — so the rig copies the file, changes
# the initial `tab` (and, for the editor shots, `fitEditing`), and renders that.
# The copy is byte-identical otherwise, and the sed is checked: a pattern that
# stopped matching would leave every screenshot on the Overlay tab and look
# exactly like a tab that failed to draw.
#
# Every seatbelt from bigscreen_rig.sh, for the same reason — this rig shares a
# machine with a live desktop, and three of the files syn-arcade writes are
# files that desktop reads.
set -u

REAL=${1:?usage: fit_rig.sh /path/to/syn-arcade /path/to/synui /path/to/qml}
SYNUI=${2:?}
QML=${3:?}

REAL=$(readlink -f "$REAL")
QML=$(readlink -f "$QML")

TMP=$(mktemp -d /tmp/fitrig.XXXXXX) || exit 1
chmod 700 "$TMP"
OUT=$TMP/out
mkdir -p "$OUT" "$TMP/bin" "$TMP/apps"

cleanup() {
    [ -n "${QS_PID:-}" ] && kill -9 "$QS_PID" 2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill -9 "$SYNUI_PID" 2>/dev/null
    echo "TMP kept: $TMP"
}
trap cleanup EXIT INT TERM

# ── the sandbox ─────────────────────────────────────────────────────────────
export HOME="$TMP" XDG_CONFIG_HOME="$TMP/config" XDG_CACHE_HOME="$TMP/cache"
export XDG_DATA_HOME="$TMP/data" XDG_RUNTIME_DIR="$TMP" XDG_STATE_HOME="$TMP/state"
export PATH="$TMP/bin:$PATH"
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
export SYNUI_RUNNING=1
unset DISPLAY WAYLAND_DISPLAY

# ⚠ synui exports SYNUI_SOCKET into every process it starts, and synctl PREFERS
# it over XDG_RUNTIME_DIR — so without this `fit screens` would be asking the
# LIVE compositor which monitors are attached, however carefully everything
# else is redirected.
unset SYNUI_SOCKET

mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME/applications" "$TMP/Desktop"

# ── fixtures ────────────────────────────────────────────────────────────────
#
# Two wrappers, so the list has rows and a row with a desktop icon is drawn
# differently from one without. An empty list screenshots perfectly and proves
# nothing about the delegate.
"$REAL" fit new --name="Gangsters (Fullscreen)" \
    --exec="wine gangsters.exe" --game=800x600 --screen=2560x1440 \
    --sharpness=2 --env=WINEPREFIX=/home/you/Games/gangsters \
    --workdir=/home/you/Games/gangsters >/dev/null 2>&1
"$REAL" fit new --name="The Sims" --exec="wine Sims.exe" \
    --game=1024x768 --screen=2560x1440 --desktop=yes >/dev/null 2>&1

# An application to wrap, in the picker's own search path — including one that
# ALREADY carries a gamescope line, which is the case `fit inspect` has to take
# apart rather than wrap again.
cat > "$XDG_DATA_HOME/applications/quake.desktop" <<'APP'
[Desktop Entry]
Type=Application
Name=Quake
Exec=wine Quake.exe %U
Path=/home/you/Games/quake
Icon=quake
Categories=Game;
APP
cat > "$XDG_DATA_HOME/applications/already-scoped.desktop" <<'APP'
[Desktop Entry]
Type=Application
Name=Descent
Exec=env WINEPREFIX=/home/you/.wine gamescope -w 640 -h 480 -W 1920 -H 1080 -f -F nis -- wine descent.exe
Path=/home/you/Games/descent
Categories=Game;
APP

# ── the compositor ──────────────────────────────────────────────────────────
cat > "$TMP/synuirc" <<'RC'
welcome_at_startup = off
dpms_timeout = 86400
lock_timeout = 86400
suspend_timeout = 86400
idle_timeout = 86400
RC
export SYNUI_CONFIG="$TMP/synuirc"

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui renders through fx_renderer (GLES2)"
    exit 77
fi

"$SYNUI" > "$TMP/synui.log" 2>&1 &
SYNUI_PID=$!

SOCK=
for _ in $(seq 1 100); do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$TMP/synui.log" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || { echo "synui died:"; tail -20 "$TMP/synui.log"; exit 1; }
    sleep 0.1
done
[ -n "$SOCK" ] || { echo "no socket in 10s"; tail -20 "$TMP/synui.log"; exit 1; }
export WAYLAND_DISPLAY="$SOCK"
echo "compositor up on $SOCK"

# ── one shot of one state ───────────────────────────────────────────────────
#
# $1 the screenshot name, $2 a sed script applied to the QML copy.
shot_state() {
    local name=$1 edit=$2 copy="$TMP/$1.qml"

    sed "$edit" "$QML" > "$copy"
    if cmp -s "$copy" "$QML"; then
        echo "FAIL: the sed for '$name' changed nothing — the pattern has moved"
        return 1
    fi

    QT_QPA_PLATFORM=wayland QS_APP_ID=syn-arcade SYNARCADE_BIN="$REAL" \
        quickshell -p "$copy" > "$TMP/$name.log" 2>&1 &
    QS_PID=$!
    sleep 4

    if ! kill -0 "$QS_PID" 2>/dev/null; then
        echo "FAIL: quickshell died on '$name'"
        tail -20 "$TMP/$name.log"
        return 1
    fi
    grep -aE "ERROR|Error|is not a type|Cannot assign|undefined" "$TMP/$name.log" \
        | head -10

    grim -o HEADLESS-1 "$OUT/$name.png" 2>/dev/null || grim "$OUT/$name.png"
    kill "$QS_PID" 2>/dev/null
    wait "$QS_PID" 2>/dev/null
    QS_PID=
    echo "  shot $name"
}

# The list, the editor and the picker. `property int tab: 0` is the one line
# that decides which panel is on screen.
shot_state 01-list   's/property int tab: 0/property int tab: 3/'
shot_state 02-editor 's/property int tab: 0/property int tab: 3/;
                      s/property bool   fitEditing: false/property bool   fitEditing: true/'
shot_state 03-picker 's/property int tab: 0/property int tab: 3/;
                      s/property bool   fitPicking: false/property bool   fitPicking: true/'

echo
echo "screenshots in $OUT"
ls -1 "$OUT"
