#!/usr/bin/env bash
#
# plugin_reorder.sh — moving a plugin along the bar, and what it must NOT cost.
#
# ⛔ THE INTERESTING HALF OF THIS IS WHAT DOES NOT HAPPEN. A click on ▴ used to
# be: run synui-plugins to work out the current order (a walk of every plugin
# directory, a read of every manifest, a grep of every entry point), write the
# file, wait for the watch to fire, and then run the WHOLE SCAN AGAIN to find
# out what the write had done — after which `all` was a new array and every
# plugin widget on every bar was torn down and reloaded. Half a second and a
# flicker of the whole row, to swap two rows the bar was already holding.
#
# None of that is visible from outside the process, and none of it is an error,
# so nothing in the suite could ever have caught it coming back. What this
# pins down:
#
#   * the row order changes, and reaches plugins-order.state
#   * with ONE call to synui-plugins, which is `order` and not a scan
#   * and no plugin widget is reloaded by it — the bar's model moves an item
#     rather than rebuilding the row (Plugins.syncModel)
#   * a reorder from a TERMINAL still reaches the bar, which is the path the
#     rescan-skipping must not break
#
# Every synui-plugins invocation is logged by a wrapper on PATH, and each
# fixture widget logs its own load, so both are countable.
#
# ⚠ IPC AND NOT A CLICK, for the reason tests/plugin_host.sh gives at length: a
# headless session has nothing to click with, and synthetic input on a live
# seat is refused outright. `ipc call plugin down` calls Plugins.moveDown, the
# same function the arrow's MouseArea calls.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: plugin_reorder.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: plugin_reorder.sh /path/to/synui /path/to/quickshell-tree}

command -v quickshell >/dev/null 2>&1 || { echo "SKIP: quickshell not installed."; exit 77; }

TMP=$(mktemp -d)
cleanup() {
    [ -n "${QS_PID:-}" ] && kill "$QS_PID" 2>/dev/null
    [ -n "${SYNUI_PID:-}" ] && kill "$SYNUI_PID" 2>/dev/null
    wait 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

# A scratch HOME, and a memory gsettings backend with it — see plugin_host.sh's
# note: a fake HOME has no dconf, and a read through one that is not there
# stalls rather than failing.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless GSETTINGS_BACKEND=memory
unset SYNUI_SOCKET   # ⛔ inherited, it would point all of this at the live desk
CFG="$TMP/.config/synui"
ORDER="$CFG/plugins-order.state"
mkdir -p "$CFG"

# ── Three fixtures that say when they load ──────────────────────────────────
#
# Rooted at BarWidget because the host assigns `bar`, `moduleName` and
# `settings` into whatever it loads, and assigning to a property a plain Item
# has not got throws out of onLoaded — see reference_omarchy_panel_service_host.
# Each one prints a line the moment it is instantiated, which is the only way
# to tell a row that MOVED from a row that was built again.
for id in a.id b.id c.id; do
    mkdir -p "$TMP/.config/synui/plugins/$id"
    cat > "$TMP/.config/synui/plugins/$id/manifest.json" <<MANIFEST
{ "schemaVersion": 1, "id": "$id", "name": "$id", "version": "1.0.0",
  "kinds": ["bar-widget"], "entryPoints": { "barWidget": "W.qml" } }
MANIFEST
    cat > "$TMP/.config/synui/plugins/$id/W.qml" <<WIDGET
import QtQuick
import qs.Ui

BarWidget {
    id: root
    moduleName: "$id"
    implicitWidth: 8
    implicitHeight: root.barSize
    Component.onCompleted: console.log("PLUGINLOADED $id")

    // How many live instances of itself this widget can reach through its
    // host — BarWidget.broadcast()'s whole mechanism, and the one thing the
    // model change below could break silently: the fold matches on the slot's
    // id, which is no longer the row it was handed but the model's own.
    Timer {
        interval: 400; running: true; repeat: true
        onTriggered: if (root.bar) console.log("PLUGINPEERS", root.moduleName,
                                               root.bar.moduleWidgets(root.moduleName).length)
    }
}
WIDGET
    printf '%s=on\n' "$id" >> "$CFG/plugins.state"
done

python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (128, 128, 128)).save('$TMP/wp.png')" 2>/dev/null \
    || { echo "SKIP: python3 PIL not installed."; exit 77; }
printf 'wallpaper = %s\n' "$TMP/wp.png" > "$CFG/synuirc"

# ── synui-plugins, wrapped so every call it makes is countable ──────────────
mkdir -p "$TMP/bin"
REAL="$(cd "$(dirname "$0")/.." && pwd)/systemd/synui-plugins.sh"
CALLS="$TMP/calls.log"
cat > "$TMP/bin/synui-plugins" <<WRAP
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$CALLS"
exec bash "$REAL" "\$@"
WRAP
chmod +x "$TMP/bin/synui-plugins"
export PATH="$TMP/bin:$PATH"
export SYNUI_BAR="$TREE"
# ⚠ THE FIXTURE TREE AND NOTHING ELSE. The default search path ends at
# /usr/share/synui/plugins, so on a machine with synui installed the shipped
# widgets join the row and every position below is off by however many are
# installed — a test that passes or fails depending on what is on the box
# running it. The bar inherits this, and so does the wrapper it spawns.
export SYNUI_PLUGIN_DIRS="$TMP/.config/synui/plugins"

# ── Run it ──────────────────────────────────────────────────────────────────
LOG="$TMP/synui.log"; QSLOG="$TMP/qs.log"
"$SYNUI" >"$LOG" 2>&1 &
SYNUI_PID=$!

SOCK=
i=0
while [ $i -lt 100 ]; do
    SOCK=$(sed -n 's/.*running on WAYLAND_DISPLAY=\(wayland-[0-9]*\).*/\1/p' "$LOG" | head -1)
    [ -n "$SOCK" ] && break
    kill -0 "$SYNUI_PID" 2>/dev/null || fail "synui died on startup"
    sleep 0.1; i=$((i + 1))
done
[ -n "$SOCK" ] || fail "no Wayland socket within 10s"
export WAYLAND_DISPLAY="$SOCK"

quickshell -p "$TREE/shell.qml" >"$QSLOG" 2>&1 &
QS_PID=$!
sleep 5
kill -0 "$QS_PID" 2>/dev/null || { sed -n '$-25,$p' "$QSLOG" >&2; fail "the bar died on startup"; }

pass=0 fail_n=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail_n=$((fail_n + 1)); }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (want '$2', got '$3')"; fi; }

ipc()    { quickshell -p "$TREE/shell.qml" ipc call plugin "$@" 2>&1; }
loads()  { grep -c "PLUGINLOADED" "$QSLOG"; }
scans()  { grep -c '^scan$' "$CALLS"; }
orders() { grep -c '^order ' "$CALLS"; }
# The written order, waited for rather than slept on: the write is a detached
# child and the bar coalesces a run of clicks before it spawns one.
awaitorder() {  # awaitorder <want>
    local i=0
    while [ $i -lt 40 ]; do
        [ -r "$ORDER" ] && [ "$(tr '\n' ' ' < "$ORDER")" = "$1 " ] && return 0
        sleep 0.1; i=$((i + 1))
    done
    return 1
}

errs=$(grep "ERROR" "$QSLOG" | grep -v "Failed to connect pipewire context")
[ -z "$errs" ] && ok "the bar loaded with no errors" \
               || bad "the bar logged an ERROR:
$errs"

check "all three fixtures are on the bar" 3 "$(loads)"
[ -e "$ORDER" ] && bad "plugins-order.state exists before anything was moved" \
                || ok "nothing is written until something is moved"

# ── One arrow ───────────────────────────────────────────────────────────────
was_scans=$(scans)
ipc down a.id >/dev/null
awaitorder "b.id a.id c.id" \
    && ok "▾ on the first row writes the new order" \
    || bad "plugins-order.state is '$(tr '\n' ' ' < "$ORDER" 2>/dev/null)', wanted 'b.id a.id c.id'"

# Give a rescan, if the bar were going to run one, time to appear.
sleep 1
check "…with exactly one call to synui-plugins" 1 "$(orders)"
check "…and no scan: the bar knew the answer before it asked" \
      "$was_scans" "$(scans)"
check "…and not one plugin widget was reloaded by it" 3 "$(loads)"

# ── A run of them, from wherever the last one left off ──────────────────────
ipc down a.id >/dev/null
ipc down a.id >/dev/null
awaitorder "b.id c.id a.id" \
    && ok "two more ▾ take it to the end" \
    || bad "plugins-order.state is '$(tr '\n' ' ' < "$ORDER" 2>/dev/null)', wanted 'b.id c.id a.id'"
sleep 1
check "…still with no scan" "$was_scans" "$(scans)"
check "…and still no widget reloaded" 3 "$(loads)"

ipc down a.id >/dev/null
sleep 1
check "the last row has no further ▾ — nothing is written" \
      "b.id c.id a.id " "$(tr '\n' ' ' < "$ORDER")"

ipc up b.id >/dev/null
sleep 1
check "the first row has no further ▴ either" \
      "b.id c.id a.id " "$(tr '\n' ' ' < "$ORDER")"

# ── …and the same file, written from a terminal, still reaches the bar ──────
#
# This is the path the rescan-skipping above must not break: the bar skips a
# rescan when the file already says what it is showing, and a reorder it did
# not make says something else.
was_scans=$(scans)
"$TMP/bin/synui-plugins" order a.id b.id c.id >/dev/null
i=0
while [ $i -lt 40 ]; do
    [ "$(scans)" -gt "$was_scans" ] && break
    sleep 0.1; i=$((i + 1))
done
[ "$(scans)" -gt "$was_scans" ] \
    && ok "an order written from a terminal makes the bar rescan" \
    || bad "the bar never rescanned after an outside reorder"
sleep 1
check "…and even that reloaded nothing: the row moved, it was not rebuilt" \
      3 "$(loads)"

# ⚠ AND THE FOLD STILL FINDS THEM. BarWidget.broadcast() reaches a widget's
# peers through the bar's own slot list, matching on the slot's id — which is
# now the model's rather than a row handed to the delegate. A miss here is a
# widget that can no longer talk to itself on another monitor, and nothing
# whatever in any log.
# grep -o, because quickshell prefixes every line with its own level tag and
# wraps it in ANSI colour — an awk on $2 reads the escape and not the id. Same
# trap plugin_load.sh's note about `ERROR` vs `Error:` describes.
peers=$(grep -o 'PLUGINPEERS [a-z.]* [0-9]*' "$QSLOG" | tail -3 \
        | sed 's/PLUGINPEERS //' | sort | tr '\n' ' ')
check "each plugin still reaches its own instance through the host" \
      "a.id 1 b.id 1 c.id 1 " "$peers"

errs=$(grep "ERROR" "$QSLOG" | grep -v "Failed to connect pipewire context")
[ -z "$errs" ] && ok "nothing threw while any of it ran" \
               || bad "the bar logged an ERROR:
$errs"

printf '\n  %d passed, %d failed\n' "$pass" "$fail_n"
[ "$fail_n" = 0 ] || exit 1
echo "plugin_reorder: PASS"
