#!/usr/bin/env bash
#
# plugin_order.sh — the bar's own row order for plugin widgets.
#
# scan_dirs' order is a directory listing — effectively alphabetical by plugin
# folder name — and nobody chose it. `synui-plugins <id> up|down` writes
# plugins-order.state, and scan() (see its own header) reorders every row to
# match before Plugins.qml ever sees them: the bar has no ordering logic of
# its own, the same split every other plugin question in this suite makes
# between "the shell script decides" and "the bar draws what it is told".
#
# Fixture tree, scratch HOME, same shape as plugins.sh — a test that could
# reorder a plugin on the machine running it is one bad path away from
# changing the desktop it is testing.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
SYNPLUG=${1:-$HERE/../systemd/synui-plugins.sh}
[ -r "$SYNPLUG" ] || { echo "not readable: $SYNPLUG" >&2; exit 1; }

TREE=$(mktemp -d)
trap 'rm -rf "$TREE"' EXIT

export XDG_CONFIG_HOME="$TREE/cfg"
export SYNUI_PLUGIN_DIRS="$TREE/plugins"
export SYNUI_BAR="$HERE/../quickshell"
export SYNUI_PLUGIN_CATALOGUE="$HERE/../data/plugins/catalogue.tsv"
export SYNUI_PLUGIN_FILTER="$HERE/../data/plugins/registry.py"
export SYNUI_PLUGIN_REGISTRY_CACHE="$TREE/registry-default.tsv"
export SYNUI_PLUGIN_REGISTRY="file://$TREE/no-registry-here.json"

pass=0 fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (want '$2', got '$3')"; fi; }

plug() { bash "$SYNPLUG" "$@"; }

mk() {  # mk <id> — a plain hostable widget, nothing about it under test
    local id=$1
    mkdir -p "$TREE/plugins/$id"
    cat > "$TREE/plugins/$id/manifest.json" <<MANIFEST
{ "schemaVersion": 1, "id": "$id", "name": "$id", "version": "1.0.0",
  "kinds": ["bar-widget"], "entryPoints": { "barWidget": "W.qml" } }
MANIFEST
    printf 'import QtQuick\nItem { }\n' > "$TREE/plugins/$id/W.qml"
}

ids() { plug scan | tail -n +2 | cut -f1; }
order_file="$XDG_CONFIG_HOME/synui/plugins-order.state"

echo "synui-plugins order — $SYNPLUG"

# ── the starting order is scan_dirs' own, unordered ─────────────────────────
mk a.id; mk b.id; mk c.id

[ -e "$order_file" ] \
    && bad "plugins-order.state exists before anything was reordered" \
    || ok "nothing is reordered until it is asked for"

check "a fresh scan lists them alphabetically, unasked" \
      $'a.id\nb.id\nc.id' "$(ids)"

# ── moving one earlier ───────────────────────────────────────────────────────
plug b.id up >/dev/null
check "b.id up swaps it with a.id"             $'b.id\na.id\nc.id' "$(ids)"
[ -r "$order_file" ] \
    && ok "…and plugins-order.state now exists" \
    || bad "plugins-order.state was not written"
check "…holding exactly the new order"         $'b.id\na.id\nc.id' \
      "$(cat "$order_file")"

# ── moving one later, from the order the above left behind ─────────────────
plug c.id up >/dev/null
check "c.id up swaps it with a.id, not b.id — order is CURRENT, not natural" \
      $'b.id\nc.id\na.id' "$(ids)"

plug b.id down >/dev/null
check "b.id down swaps it with c.id"           $'c.id\nb.id\na.id' "$(ids)"

# ── the ends refuse to fall off ──────────────────────────────────────────────
plug c.id up >/dev/null
check "the first row has no further up — the list is unchanged" \
      $'c.id\nb.id\na.id' "$(ids)"

plug a.id down >/dev/null
check "the last row has no further down — the list is unchanged" \
      $'c.id\nb.id\na.id' "$(ids)"

# ── a plugin installed after ordering existed joins at the END ─────────────
mk d.id
check "d.id, never ordered, is appended after the three that are" \
      $'c.id\nb.id\na.id\nd.id' "$(ids)"

# ── a plugin that WAS ordered and then removed leaves no gap ───────────────
rm -rf "$TREE/plugins/a.id"
check "removing a.id drops it from the row with nothing left behind" \
      $'c.id\nb.id\nd.id' "$(ids)"
# The stale line survives in the file itself — order_effective drops it by
# not being asked about it again, not by rewriting the file — and a plugin
# reinstalled under the same id lands back where it was.
check "…but the file still remembers where it went" 1 \
      "$(grep -c '^a\.id$' "$order_file")"
mk a.id
check "…so reinstalling it returns it to its old place" \
      $'c.id\nb.id\na.id\nd.id' "$(ids)"

# ── the whole order at once, which is what the bar's menu writes ───────────
#
# `up`/`down` are a MOVE and have to scan to find out what they are moving
# within; `order` is the finished list, stated by a caller that already has it
# — the bar's right-click menu is drawing the very row it just reordered. No
# scan, so no walk of every plugin directory on every click of an arrow.
plug order d.id a.id b.id c.id >/dev/null
check "order sets the whole row at once"       $'d.id\na.id\nb.id\nc.id' "$(ids)"
check "…and the file holds exactly what it was given" $'d.id\na.id\nb.id\nc.id' \
      "$(cat "$order_file")"

# An id nothing answers to is written and then ignored, exactly as a removed
# plugin's own line already is — same rule, so a caller stating an order it
# read a moment ago cannot lose a plugin to a race with an uninstall.
plug order gone.id d.id a.id b.id c.id >/dev/null
check "an id with no plugin behind it changes nothing about the row" \
      $'d.id\na.id\nb.id\nc.id' "$(ids)"
check "…and is kept in the file, like any other line for something absent" 1 \
      "$(grep -c '^gone\.id$' "$order_file")"

before=$(cat "$order_file")
plug order >/dev/null 2>&1
[ $? -ne 0 ] && ok "order with no ids fails rather than wiping the row" \
             || bad "order with no ids returned 0"
check "…and leaves the file exactly as it was" "$before" "$(cat "$order_file")"

# Back to where the checks above left off, so what follows reads the order it
# expects rather than this section's.
plug order c.id b.id a.id d.id >/dev/null

# ── a refused plugin cannot be reordered either ─────────────────────────────
#
# Same gate `on` goes through: writing an order entry for a plugin the bar
# will not host is the same class of lie a state file claiming it is "on"
# would be — a row nothing will draw.
mkdir -p "$TREE/plugins/no.hypr"
cat > "$TREE/plugins/no.hypr/manifest.json" <<'MANIFEST'
{ "schemaVersion": 1, "id": "no.hypr", "name": "no.hypr", "version": "1.0.0",
  "kinds": ["bar-widget"], "entryPoints": { "barWidget": "H.qml" } }
MANIFEST
printf 'import QtQuick\nimport Quickshell.Hyprland\nItem { }\n' \
    > "$TREE/plugins/no.hypr/H.qml"
plug no.hypr up >/dev/null 2>&1
[ $? -ne 0 ] && ok "reordering an unhostable plugin fails" \
             || bad "reordering an unhostable plugin returned 0"
check "…and the order file is untouched by it" 0 \
      "$(grep -c '^no\.hypr$' "$order_file")"

plug nothing.here up >/dev/null 2>&1
[ $? -ne 0 ] && ok "reordering an unknown id fails rather than inventing a row" \
             || bad "reordering an unknown id returned 0"

if [ "$fail" -eq 0 ]; then
    printf 'plugin_order: all %d checks passed\n' "$pass"
    exit 0
fi
printf 'plugin_order: %d of %d checks failed\n' "$fail" "$((pass + fail))"
exit 1
