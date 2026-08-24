#!/usr/bin/env bash
#
# plugin_host.sh — a plugin's PANEL and SERVICE, mounted and driven.
#
# ⛔ WHAT THIS GUARDS IS A BUTTON THAT DOES NOTHING AND SAYS NOTHING.
# plugin_load.sh proves a bar widget instantiates; it says nothing about the two
# entry points a widget delegates its actual behaviour to. For as long as
# nothing mounted them, a plugin declaring `panel` appeared on the bar, drew its
# glyph, and swallowed every click — because the call is
#
#     if (bar.shell && typeof bar.shell.toggle === "function") …
#
# and a host missing the member fails that guard in silence. Two of the five
# widgets installed on the machine this was written on failed exactly that way,
# and NOTHING appeared in any log. There is no error to grep for; the only way
# to know is to mount one and press it.
#
# So this starts a headless synui, starts the real quickshell tree against it,
# and drives PluginHost over the `plugin` IPC handler — which is the same call
# the click makes. ⚠ IPC AND NOT A CLICK because a headless session has nothing
# to click with, and synthetic input on a live seat is refused outright.
#
# ⚠ THE FIXTURE IS OURS HERE, WHICH plugin_load.sh's DELIBERATELY IS NOT. That
# one vendors Omarchy's own Spacer, because the claim it tests is "a file from
# their repository runs here unmodified" and a widget written to pass would
# prove nothing. The claim HERE is different: it is that the host offers the
# members their panels call, and testing that needs a plugin that calls every
# one of them — mounts a panel AND a service, takes the injected properties,
# opens on demand and writes a setting back. No single shipped widget does all
# four, and the ones that come closest are megabytes of chess engine.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: plugin_host.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: plugin_host.sh /path/to/synui /path/to/quickshell-tree}

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

# ⚠ A SCRATCH HOME, AND GSETTINGS_BACKEND WITH IT. A fake HOME has no dconf, and
# a QML tree that reads a gsetting through one that is not there stalls rather
# than failing — see reference_fake_home_misses_gsettings.
export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless GSETTINGS_BACKEND=memory
# ⛔ SYNUI_SOCKET WOULD POINT THIS AT THE LIVE DESKTOP. Inherited from the
# developer's own session, every synctl call below would act on the desk they
# are sitting at.
unset SYNUI_SOCKET

CFG="$TMP/.config/synui"
mkdir -p "$CFG"

# ── The fixture ─────────────────────────────────────────────────────────────
PLUG="$TMP/.config/synui/plugins/test.host"
mkdir -p "$PLUG"
cat > "$PLUG/manifest.json" <<'MANIFEST'
{
  "schemaVersion": 1,
  "id": "test.host",
  "name": "Host probe",
  "version": "1.0.0",
  "kinds": ["service", "panel", "bar-widget"],
  "entryPoints": {
    "service": "Service.qml",
    "panel": "Panel.qml",
    "barWidget": "BarWidget.qml"
  },
  "barWidget": { "displayName": "Host probe", "category": "Testing" }
}
MANIFEST

cat > "$PLUG/BarWidget.qml" <<'W'
import QtQuick
import Quickshell.Io
import qs.Ui

BarWidget {
  id: root
  moduleName: "test.host"
  implicitWidth: 8
  implicitHeight: root.barSize

  // ⚠ A HANDLER OF ITS OWN, which is what real widgets do — snake and calendar
  // both register one. It is how the test reads a widget-side value back: the
  // host cannot answer "did settings reach the widget", only the widget can.
  function panelPayload() {
    var p = root.bar && root.bar.shell ? root.bar.shell.panelFor("test.host") : null
    return p ? p.lastPayload : "no panel"
  }

  IpcHandler {
    target: "test.host"
    function probe(): string { return String(root.setting("probe", "unset")) }

    // What the panel was opened WITH, verbatim. A payload dropped on the way
    // in is a panel that opens and appears to ignore the request that opened
    // it — see the payload assertions below.
    function payload(): string { return String(root.panelPayload()) }

    // ⚠ THE MEMBERS A WIDGET READS OFF ITS HOST, asked for by name. Every one
    // of these was missing at some point and none of them said so: `foreground`
    // and `background` produced "Unable to assign [undefined] to QColor" in a
    // log on tty1 while the panel drew in the wrong colour, `shell` failed a
    // guard and swallowed the click, and `run` — the only unguarded one of the
    // four — threw a TypeError out of tetris's Play button.
    function hostMembers(): string {
      var b = root.bar
      if (!b) return "no bar"
      var out = []
      if (typeof b.run === "function") out.push("run")
      if (b.shell) out.push("shell")
      if (b.foreground !== undefined) out.push("foreground")
      if (b.background !== undefined) out.push("background")
      if (b.fontFamily !== undefined) out.push("fontFamily")
      return out.join(" ")
    }
  }
}
W

cat > "$PLUG/Service.qml" <<'S'
import QtQuick

Item {
  property string omarchyPath: ""
  property var shell: null
  property var manifest: null
  property bool ready: true
  property string barText: "probe"
}
S

# The panel deliberately declares NO omarchyPath: flappy-pipes does not either,
# and injecting a property a plugin never declared throws out of onLoaded and
# takes the registration with it. This is the shape that caught that.
cat > "$PLUG/Panel.qml" <<'P'
import QtQuick

Item {
  id: panel
  property var shell: null
  property var manifest: null
  property var service: null
  property bool opened: false
  property string lastPayload: ""

  function open(payloadJson) {
    panel.opened = true
    panel.lastPayload = String(payloadJson)
    // ⚠ THE WRITE-BACK, ON OPEN, because that is the only moment this test can
    // reach. flappy writes its best score the same way — through the shell,
    // from inside the panel — and the whole path is guarded, so a host missing
    // updateEntryInline loses the write in silence.
    panel.record("written-by-panel")
  }
  function close() { panel.opened = false }

  function record(value) {
    if (!panel.shell || typeof panel.shell.updateEntryInline !== "function") return
    panel.shell.updateEntryInline("test.host", { id: "test.host", probe: value })
  }
}
P

printf 'test.host=on\n' > "$CFG/plugins.state"

python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (48, 48, 48)).save('$TMP/wp.png')" 2>/dev/null \
    || { echo "SKIP: python3 PIL not installed."; exit 77; }
printf 'wallpaper = %s\n' "$TMP/wp.png" > "$CFG/synuirc"

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

mkdir -p "$TMP/bin"
cp "$(dirname "$0")/../systemd/synui-plugins.sh" "$TMP/bin/synui-plugins"
chmod +x "$TMP/bin/synui-plugins"
export PATH="$TMP/bin:$PATH"
export SYNUI_BAR="$TREE"

quickshell -p "$TREE/shell.qml" >"$QSLOG" 2>&1 &
QS_PID=$!
sleep 5
kill -0 "$QS_PID" 2>/dev/null || { sed -n '$-25,$p' "$QSLOG" >&2; fail "the bar died on startup"; }

pass=0 fail_n=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail_n=$((fail_n + 1)); }

ipc() { quickshell -p "$TREE/shell.qml" ipc call plugin "$@" 2>&1; }
# The FIXTURE's own handler, which answers about itself. A different target
# from `plugin` above: that one is the host's, this one is the widget's.
hostipc() { quickshell -p "$TREE/shell.qml" ipc call test.host "$@" 2>&1; }

# ⚠ THE SCAN HAS TO SEE THE ENTRY POINTS FIRST. Everything below is downstream
# of two columns in one TSV row, and a scan that lost them would fail every
# assertion here with no hint as to which end was wrong.
row=$("$TMP/bin/synui-plugins" scan | awk -F'\t' '$1=="test.host"')
[ "$(printf '%s' "$row" | cut -f8)" = "Panel.qml" ] \
    && [ "$(printf '%s' "$row" | cut -f9)" = "Service.qml" ] \
    && ok "the scan reports the panel and service entry points" \
    || bad "the scan lost the session-scoped entry points: $row"

# ⛔ THE NEEDLE IS `ERROR`, NOT `Error:` — quickshell prints its level in
# capitals. The pipewire line is filtered by name and only by name; there is no
# daemon in a headless session and filtering the whole service would hide a real
# audio fault.
errs=$(grep "ERROR" "$QSLOG" | grep -v "Failed to connect pipewire context")
[ -z "$errs" ] && ok "the bar loaded with no errors" \
               || bad "the bar logged an ERROR:
$errs"

grep -q "Configuration Loaded" "$QSLOG" \
    && ok "…and the configuration finished loading" \
    || bad "the bar never reported the configuration loaded"

grep -qi "plugin \(panel\|service\) failed to load" "$QSLOG" \
    && bad "a session-scoped entry point failed to load" \
    || ok "…and neither entry point failed to load"

# Both mounted, once. This is the whole gap: for as long as nothing mounted
# them, `mounted` was empty and every assertion after it was unreachable.
[ "$(ipc mounted test.host)" = "panel service" ] \
    && ok "the panel and the service are both mounted" \
    || bad "mounted reported '$(ipc mounted test.host)', wanted 'panel service'"

# ⚠ AND THE INJECTION HAS TO HAVE SURVIVED. A panel that declares no
# omarchyPath used to throw out of onLoaded on the assignment and never reach
# registerPanel — which looks exactly like the bug this file exists to catch,
# because the symptom is again a panel that will not open.
[ "$(ipc opened test.host)" = "closed" ] \
    && ok "a freshly mounted panel is closed" \
    || bad "a freshly mounted panel reported '$(ipc opened test.host)'"

ipc toggle test.host >/dev/null; sleep 1
[ "$(ipc opened test.host)" = "open" ] \
    && ok "toggle opens it — the call a bar widget's click makes" \
    || bad "toggle did not open the panel"

ipc toggle test.host >/dev/null; sleep 1
[ "$(ipc opened test.host)" = "closed" ] \
    && ok "…and toggle closes it again" \
    || bad "toggle did not close the panel"

# ⚠ A BAR-WIDGET-ONLY PLUGIN HAS NOTHING TO OPEN, and must not claim otherwise.
# The intent map answers while a panel is still loading, and on its own it
# reported "open" for a plugin that has no panel at all.
# ── The payload ─────────────────────────────────────────────────────────────
#
# ⛔ AN OPEN THAT LOSES ITS PAYLOAD IS A BUTTON THAT IGNORES YOU. YT Mini's bar
# button asks for `{"clipboard":true}`, which is the difference between a window
# that plays the link you just copied and an empty one; its panel also reads
# `url`, `grab`, `radio`, `corner` and `move` out of the same object. The bare
# `toggle`/`open` above hand the panel an empty string by design, so the
# payload-carrying spellings are a separate contract and need their own check.
#
# ⚠ THEY EXIST BECAUSE quickshell MATCHES ON ARITY. Widening `toggle` would
# refuse every one-argument call already written, and a default value is worse:
# an untyped parameter drops the function out of the handler altogether and the
# call answers "Function not found." for a function plainly there in the file.
ipc openWith test.host '{"clipboard":true}' >/dev/null; sleep 1
[ "$(ipc opened test.host)" = "open" ] \
    && ok "openWith opens the panel" \
    || bad "openWith did not open the panel"
[ "$(hostipc payload)" = '{"clipboard":true}' ] \
    && ok "…and the payload arrived at open() untouched" \
    || bad "the panel was opened with '$(hostipc payload)'"

ipc close test.host >/dev/null; sleep 1
ipc toggleWith test.host '{"url":"u"}' >/dev/null; sleep 1
[ "$(hostipc payload)" = '{"url":"u"}' ] \
    && ok "toggleWith carries one too" \
    || bad "toggleWith delivered '$(hostipc payload)'"
ipc close test.host >/dev/null; sleep 1

ipc toggle synapse.uptime >/dev/null; sleep 1
[ "$(ipc opened synapse.uptime)" = "closed" ] \
    && ok "a plugin with no panel never reports one open" \
    || bad "a panel-less plugin reported '$(ipc opened synapse.uptime)'"

# ── The settings round trip ─────────────────────────────────────────────────
#
# ⛔ THE FILE HAS TO EXIST BEFORE A FileView WILL WRITE TO IT — no `saved`, no
# `saveFailed`, nothing — which is what made the desktop post-it lose the first
# thing ever typed into it. plugins.json does not exist until something writes,
# so the FIRST write is the one at risk and the only one worth asserting.
JSON="$TMP/.config/synui/plugins.json"
[ -s "$JSON" ] && grep -q '"probe"' "$JSON" \
    && ok "the panel's write reached plugins.json through shell.updateEntryInline" \
    || bad "plugins.json holds no write from the panel: $(cat "$JSON" 2>&1)"

# …and came back the other way. A widget handed its settings once at load would
# still be holding the empty object it started with.
probe=$(quickshell -p "$TREE/shell.qml" ipc call test.host probe 2>&1)
[ "$probe" = "written-by-panel" ] \
    && ok "…and the bar widget can read it back as a setting" \
    || bad "the widget's setting reads '$probe', wanted 'written-by-panel'"

members=$(quickshell -p "$TREE/shell.qml" ipc call test.host hostMembers 2>&1)
[ "$members" = "run shell foreground background fontFamily" ] \
    && ok "the bar offers every member a widget reads off its host" \
    || bad "the host is missing a member: got '$members'"

printf '\n  %d passed, %d failed\n' "$pass" "$fail_n"
[ "$fail_n" = 0 ] || exit 1
echo "plugin_host: PASS"
