#!/bin/sh
# wallhaven_window.sh — the browser window answers its own keyboard
#
# ⛔ THIS RIG EXISTS BECAUSE EVERY KEY IT ADVERTISED WAS DEAD. wallhaven.qml
# shipped in 590 with `Keys.onPressed` attached to the PanelWindow, and Qt only
# delivers keys to an ITEM with active focus inside the surface — layer-shell
# handing the surface keyboard focus is not enough (Ui/KeyboardPanel.qml says so
# in as many words). The footer listed Enter, the arrows, 1 2 3, S and Esc; the
# window answered none of them, and nothing anywhere reported a problem. A dead
# key handler is not a crash, not a warning and not a lint error.
#
# So the keys are PRESSED here, against a real headless compositor, and what is
# asserted is what the key was supposed to reach:
#
#   1. off: the window opens on its own switch and asks wallhaven.cc NOTHING
#   2. Enter on that pane turns the switch on — and only then does a search go
#   3. `w` opens the Super+W picker and this window goes away
#   4. Esc closes it, which is the 590 regression's own assertion
#   5. ⛔ ONE window on a TWO-screen desk, not one per screen — 590 and 591 put
#      the browser on every monitor at once, because an unnamed output meant
#      "all of them" rather than "nobody said"
#   6. …and on the monitor it was NAMED, which is how synui puts it where the
#      key was pressed
#
# The ledger is a fake `synui-wallhaven` and a fake `synctl` on PATH: the window
# reaches both through Quickshell's Process, so being called is the whole of
# "the key arrived".
#
# ⚠ ONE PRESS PER RUN, each with its own compositor. On a headless seat wtype's
# virtual keyboard is the only keyboard the seat has, so it appears and
# disappears with each invocation and Qt tears the wl_keyboard down in between —
# which reads exactly like the window losing focus.
#
# Usage: wallhaven_window.sh /path/to/synui /path/to/quickshell/wallhaven.qml
# Skips (77) without a DRM render node, wtype or quickshell.
set -u

SYNUI=${1:?usage: wallhaven_window.sh /path/to/synui /path/to/wallhaven.qml}
QML=${2:?usage: wallhaven_window.sh /path/to/synui /path/to/wallhaven.qml}

if ! ls /dev/dri/renderD* >/dev/null 2>&1; then
    echo "SKIP: no DRM render node — synui's fx_renderer is GLES2/DMA-BUF only."
    exit 77
fi
for t in wtype quickshell; do
    command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t is not installed."; exit 77; }
done

# ⚠ Before anything else, or the synthetic keys land on the real desktop.
unset SYNUI_SOCKET WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY

fail() { echo "FAIL: $*" >&2; exit 1; }

# Run the window once under a fresh headless compositor.
#   $1 the switch's answer (on|off), $2 a key for wtype ("" for none),
#   $3 the output to name ("" for none — the browser's own fallback)
# Leaves LEDGER_TEXT, LOG_TEXT, RUN_ALIVE and OUTPUTS set.
LEDGER=
run() {
    _state=$1; _key=$2; _out=${3:-}
    TMP=$(mktemp -d /tmp/synui-whwin.XXXXXX) || exit 1
    chmod 700 "$TMP"
    LEDGER="$TMP/calls"
    : > "$LEDGER"

    cat > "$TMP/synuirc" <<'RC'
welcome_at_startup = off
start_overlay = off
desktop_icons = 0
power_enabled = 0
power_dim_timeout = 86400
power_blank_timeout = 86400
power_lock_timeout = 86400
power_suspend_timeout = 86400
RC

    mkdir -p "$TMP/bin"
    cat > "$TMP/bin/synui-wallhaven" <<EOS
#!/bin/sh
echo "wallhaven \$*" >> "$TMP/calls"
case "\$1" in
  status) echo "$_state" ;;
  on)     echo "wallhaven: on" ;;
  search) printf 'id\tthumb\tresolution\tcategory\n' ;;
esac
exit 0
EOS
    cat > "$TMP/bin/synctl" <<EOS
#!/bin/sh
echo "synctl \$*" >> "$TMP/calls"
exit 0
EOS
    chmod +x "$TMP/bin/synui-wallhaven" "$TMP/bin/synctl"

    # ⛔ THE SEATBELT. synui-apply-theme hardcodes $HOME, so a nested compositor
    # started with a scratch XDG_CONFIG_HOME still re-themes the LIVE desktop.
    for h in synui-apply-theme synui-glass synui-apply-font synui-wpengine; do
        printf '#!/bin/sh\nexit 0\n' > "$TMP/bin/$h"
        chmod +x "$TMP/bin/$h"
    done

    export PATH="$TMP/bin:$PATH"
    export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP"
    export SYNUI_CONFIG="$TMP/synuirc" SYNUI_WINDOWS="$TMP/windows.conf"
    export GSETTINGS_BACKEND=memory
    # ⛔ TWO SCREENS. With one, "on the focused monitor" and "on every monitor"
    # draw exactly the same picture and the bug this rig closes is invisible.
    export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_HEADLESS_OUTPUTS=2

    "$SYNUI" -d > "$TMP/synui.log" 2>&1 &
    SYNUI_PID=$!

    SOCK=; i=0
    while [ $i -lt 100 ]; do
        for c in "$TMP"/wayland-*; do
            case "$c" in *.lock) continue;; esac
            [ -S "$c" ] && SOCK=$(basename "$c") && break
        done
        [ -n "$SOCK" ] && break
        kill -0 "$SYNUI_PID" 2>/dev/null ||
            fail "synui exited during startup: $(tail -5 "$TMP/synui.log")"
        i=$((i + 1)); sleep 0.1
    done
    [ -n "$SOCK" ] || fail "no wayland socket after 10s"
    export WAYLAND_DISPLAY="$SOCK"
    # ⚠ ITS OWN. A quickshell started from a rig inherits the SYNUI_SOCKET of
    # whatever session launched the rig, and would drive the LIVE desktop.
    export SYNUI_SOCKET="$TMP/synui-$SOCK.sock"

    # The launcher's job in miniature: the FIRST window is told which monitor by
    # the environment, because `quickshell -p file.qml` takes no argument.
    SYNUI_WALLHAVEN_OUTPUT="$_out" quickshell -p "$QML" > "$TMP/qs.log" 2>&1 &
    QS_PID=$!
    sleep 3
    [ -n "$_key" ] && { wtype -s 200 -k "$_key" || fail "wtype could not send $_key"; }
    sleep 2

    if kill -0 "$QS_PID" 2>/dev/null; then RUN_ALIVE=yes; else RUN_ALIVE=no; fi
    # ⚠ The wait is inside the redirect on purpose: the "Killed" line is the
    # SHELL's job notice, printed when it reaps them, not the kill's output.
    { kill -9 "$QS_PID" "$SYNUI_PID"; wait "$QS_PID" "$SYNUI_PID"; } 2>/dev/null
    LEDGER_TEXT=$(cat "$LEDGER")
    # Every layer surface synui accepted, one per line: "'<namespace>' on <out>".
    LOG_TEXT=$(grep "layer surface" "$TMP/synui.log" 2>/dev/null)
    OUTPUTS=$(sed -n "s/.*layer surface '[^']*' on \([^ ]*\).*/\1/p" \
                  "$TMP/synui.log" 2>/dev/null)
    rm -rf "$TMP"
    unset SYNUI_SOCKET WAYLAND_DISPLAY
}

pass=0
ok() { echo "  ok    $1"; pass=$((pass + 1)); }

# ── 1. off: it asks the switch, and nothing else ────────────────────────
run off ""
echo "$LEDGER_TEXT" | grep -q '^wallhaven status$' ||
    fail "the window never asked whether the switch is on.
       Ledger: $(echo "$LEDGER_TEXT" | tr '\n' '|')"
echo "$LEDGER_TEXT" | grep -q '^wallhaven search' &&
    fail "⛔ the window searched wallhaven.cc while the switch was OFF. That is
       the one thing this feature promises not to do.
       Ledger: $(echo "$LEDGER_TEXT" | tr '\n' '|')"
[ "$RUN_ALIVE" = yes ] || fail "the window did not stay up with the switch off"
ok "off: it asks the switch and reaches the network for nothing"

# ── 2. Enter turns it on, and only then is anything asked for ───────────
run off Return
echo "$LEDGER_TEXT" | grep -q '^wallhaven on$' ||
    fail "Enter on the consent pane did not turn the switch on — which is how
       591 found that NONE of this window's keys were delivered.
       Ledger: $(echo "$LEDGER_TEXT" | tr '\n' '|')"
echo "$LEDGER_TEXT" | grep -q '^wallhaven search' ||
    fail "the switch went on and no search followed: the window is on but blank.
       Ledger: $(echo "$LEDGER_TEXT" | tr '\n' '|')"
ok "Enter turns the switch on, and the first search follows it"

# ── 3. w goes back to the Super+W picker ────────────────────────────────
run on w
echo "$LEDGER_TEXT" | grep -q '^synctl dispatch wallpaper$' ||
    fail "w did not open the wallpaper picker.
       Ledger: $(echo "$LEDGER_TEXT" | tr '\n' '|')"
[ "$RUN_ALIVE" = no ] ||
    fail "w opened the picker and left this window up. Both are full-screen and
       both want the keyboard, which is a picker nobody can drive."
ok "w opens the picker, and this window gets out of the way"

# ── 4. Esc closes it ────────────────────────────────────────────────────
run on Escape
[ "$RUN_ALIVE" = no ] ||
    fail "Escape did not close the window. This is exactly the shape 590
       shipped: a footer advertising keys that reach nothing."
ok "Esc closes the window"

# ── 5. one window on a two-screen desk, with nobody naming a monitor ────
run on ""
[ "$(echo "$LOG_TEXT" | grep -c "layer surface")" = 1 ] ||
    fail "⛔ the browser mapped $(echo "$LOG_TEXT" | grep -c 'layer surface')
       surfaces on a two-screen desk. An output nobody named means ONE screen's
       worth of not knowing — it is not a request for a window on each.
       Surfaces: $(echo "$LOG_TEXT" | tr '\n' '|')"
ok "no output named: one window, not one per screen"

# ── 6. …and on the monitor it was named ─────────────────────────────────
run on "" HEADLESS-2
[ "$OUTPUTS" = "HEADLESS-2" ] ||
    fail "named HEADLESS-2, the browser mapped on '$OUTPUTS'. This is how synui
       puts it where the key was pressed; on the wrong screen it is the same bug
       wearing a different hat.
       Surfaces: $(echo "$LOG_TEXT" | tr '\n' '|')"
ok "named output: exactly that monitor, and only it"

echo "PASS ($pass checks)"
exit 0
