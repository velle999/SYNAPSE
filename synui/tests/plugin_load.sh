#!/usr/bin/env bash
#
# plugin_load.sh — a real Omarchy bar widget, loaded into a real bar.
#
# tests/plugins.sh checks the manifest, the search path and the refusals. All of
# that is a shell script reading files: it proves synui will OFFER a plugin, and
# nothing whatever about whether one runs.
#
# ⛔ AND "THE TYPES RESOLVE" IS NOT "IT RUNS" EITHER. qmllint against the Ui and
# Commons modules says every name a widget uses exists; it does not say the
# widget instantiates, and it cannot — a binding loop, a missing host property or
# a signal with the wrong shape are all runtime. So this starts a headless synui,
# starts the real quickshell tree against it with a plugin enabled, and reads the
# bar's own log for the error the Loader would print.
#
# ⚠ THE FIXTURE IS OMARCHY'S OWN WIDGET, NOT ONE WRITTEN TO PASS. A hand-made
# plugin proves the loader works and nothing about the compatibility claim; the
# whole point is that a file from their repository runs here unmodified. It is
# vendored into this file rather than fetched, so the suite does not need the
# network — MIT, © David Heinemeier Hansson, and their copyright notice is below
# with it.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

SYNUI=${1:?usage: plugin_load.sh /path/to/synui /path/to/quickshell-tree}
TREE=${2:?usage: plugin_load.sh /path/to/synui /path/to/quickshell-tree}

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

export XDG_RUNTIME_DIR="$TMP" HOME="$TMP" XDG_CONFIG_HOME="$TMP/.config"
export WLR_BACKENDS=headless
CFG="$TMP/.config/synui"
mkdir -p "$CFG"

# ── The fixture ─────────────────────────────────────────────────────────────
#
# Omarchy's Spacer widget, verbatim from basecamp/omarchy (MIT):
#
#   Copyright (c) David Heinemeier Hansson
#   Permission is hereby granted, free of charge, to any person obtaining a copy
#   of this software and associated documentation files (the "Software"), to
#   deal in the Software without restriction … The above copyright notice and
#   this permission notice shall be included in all copies or substantial
#   portions of the Software.
#
# Chosen because it is the smallest one that still exercises the whole path:
# it roots at BarWidget, imports qs.Commons, reads Style and takes a `settings`
# value — which is every piece of the contract this compatibility rests on.
PLUG="$TMP/.config/omarchy/plugins/omarchy.spacer"
mkdir -p "$PLUG"
cat > "$PLUG/manifest.json" <<'MANIFEST'
{
  "schemaVersion": 1,
  "id": "omarchy.spacer",
  "name": "Spacer",
  "version": "1.0.0",
  "author": "Omarchy",
  "kinds": ["bar-widget"],
  "entryPoints": { "barWidget": "Spacer.qml" },
  "barWidget": { "displayName": "Spacer", "category": "Layout", "allowMultiple": true }
}
MANIFEST
cat > "$PLUG/Spacer.qml" <<'SPACER'
import QtQuick
import qs.Ui
import qs.Commons

BarWidget {
  id: root
  moduleName: "omarchy.spacer"

  readonly property int space: Style.space(root.setting("width", 12))

  implicitWidth: root.vertical ? root.barSize : root.space
  implicitHeight: root.vertical ? root.space : root.barSize
}
SPACER

printf 'omarchy.spacer=on\n' > "$CFG/plugins.state"

python3 -c "from PIL import Image
Image.new('RGB', (64, 64), (128, 128, 128)).save('$TMP/wp.png')" 2>/dev/null \
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

# synui-plugins has to be reachable: the registry shells out to it to scan.
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

# ⛔ THE NEEDLE IS `ERROR`, NOT `Error:`. quickshell prints its own level tag in
# capitals with ANSI colour around it, so a grep for the friendly spelling
# matches nothing and the check passes however broken the tree is — the trap the
# older quickshell load tests fell into and never caught anything with.
#
# ⚠ ONE LINE IS FILTERED, BY NAME, AND ONLY ONE. There is no pipewire daemon in
# a headless test session, so quickshell's audio service logs
# "Failed to connect pipewire context" every run. Filtering the whole SERVICE
# would hide a real audio-module fault; filtering that one message leaves every
# other ERROR — including every one a plugin can cause — still fatal here.
errs=$(grep "ERROR" "$QSLOG" | grep -v "Failed to connect pipewire context")
[ -z "$errs" ] && ok "the bar loaded with no errors" \
               || bad "the bar logged an ERROR:
$errs"

# And it got all the way to the end, which "no errors" alone does not prove: a
# tree that fails to load prints its errors and then simply stops.
grep -q "Configuration Loaded" "$QSLOG" \
    && ok "…and the configuration finished loading" \
    || bad "the bar never reported the configuration loaded"

# The specific one the Loader prints when a plugin will not instantiate.
grep -qi "plugin.*failed to load" "$QSLOG" \
    && bad "a plugin failed to load" \
    || ok "…and no plugin failed to load"

# And the registry saw it, which is what proves the scan ran inside the bar
# rather than only in the shell script's own tests.
SYNUI_PLUGIN_DIRS="$TMP/.config/omarchy/plugins" \
    "$TMP/bin/synui-plugins" scan | grep -q "^omarchy.spacer" \
    && ok "the registry lists Omarchy's own widget as hostable" \
    || bad "the registry did not list the fixture"

printf '\n  %d passed, %d failed\n' "$pass" "$fail_n"
[ "$fail_n" = 0 ] || exit 1
echo "plugin_load: PASS"
