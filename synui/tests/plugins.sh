#!/usr/bin/env bash
#
# plugins.sh — bar plugins, in Omarchy's shell-plugin format.
#
# SynapseOS's bar is quickshell and so is Omarchy's, which makes their plugin
# format the only one already describing "a QML widget you can drop into a
# quickshell bar". synui adopts it so a widget written once loads on either
# desktop rather than each project growing its own directory layout for the same
# idea.
#
# ⛔ WHAT THIS FILE IS REALLY GUARDING IS THE REFUSALS. Enabling a plugin the bar
# cannot host produces the worst possible outcome — a state file claiming
# something is on, and an empty space where it should be. Their widgets root at
# BarWidget (portable, and reimplemented in Ui/BarWidget.qml) but also import
# qs.Commons and Quickshell.Hyprland, which are their own singletons and a
# compositor socket synui does not have. Every one of those has to be caught
# BEFORE it reaches the bar, by name, with the reason.
#
# Everything runs against a fixture tree under a scratch HOME: the real plugin
# directories are the user's, and a test that could enable a plugin on the
# machine running it is one bad path away from changing the desktop it is
# testing.
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
# The real bar tree, so the "does this qs.<Module> exist" check is asked of the
# modules synui actually ships rather than of a fixture that could drift from
# them. This is what makes the qs.Ui / qs.Commons split a real answer.
export SYNUI_BAR="$HERE/../quickshell"

pass=0 fail=0
ok()   { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (want '$2', got '$3')"; fi; }

plug() { bash "$SYNPLUG" "$@"; }

# ── the fixtures ────────────────────────────────────────────────────────────
mk() {  # mk <id> <entry-file> <qml body...>
    local id=$1 entry=$2; shift 2
    mkdir -p "$TREE/plugins/$id"
    cat > "$TREE/plugins/$id/manifest.json" <<MANIFEST
{
  "schemaVersion": 1,
  "id": "$id",
  "name": "$id widget",
  "version": "1.0.0",
  "description": "fixture for $id",
  "kinds": ["bar-widget"],
  "entryPoints": { "barWidget": "$entry" },
  "barWidget": { "displayName": "$id", "category": "Test", "allowMultiple": false }
}
MANIFEST
    [ $# -gt 0 ] && printf '%s\n' "$@" > "$TREE/plugins/$id/$entry"
    return 0
}

# Hostable: roots at BarWidget out of qs.Ui, which synui DOES provide.
mk good.plain Plain.qml 'import QtQuick' 'import qs.Ui' 'BarWidget { }'
# Hostable and importing nothing of ours at all.
mk good.bare Bare.qml 'import QtQuick' 'Item { }'
# Not hostable: Hyprland's IPC has no counterpart here.
mk no.hypr H.qml 'import QtQuick' 'import Quickshell.Hyprland' 'BarWidget { }'
# Hostable now: qs.Commons is provided — a shim over synui's own theme, so an
# Omarchy widget picks up THIS desktop's font, spacing and ink rather than
# dragging their palette across. It was refused until synui 469.
mk yes.commons C.qml 'import QtQuick' 'import qs.Commons' 'BarWidget { }'
# Not hostable: a qs module nothing provides. Named rather than invented from
# the same list as the others, so this keeps testing the RULE — "ask the
# filesystem" — after Commons stopped being an example of it.
mk no.qsmod Q.qml 'import QtQuick' 'import qs.NoSuchModule' 'BarWidget { }'
# Not hostable: the manifest names a file that is not there.
mk no.entry Missing.qml
# Not hostable: the entry point climbs out of the plugin directory.
mk no.escape ../../../etc/passwd
# Not a bar widget at all — a kind synui does not host, so not listed.
mkdir -p "$TREE/plugins/other.kind"
cat > "$TREE/plugins/other.kind/manifest.json" <<'MANIFEST'
{ "schemaVersion": 1, "id": "other.kind", "name": "Overlay", "version": "1.0.0",
  "kinds": ["overlay"], "entryPoints": { "overlay": "O.qml" } }
MANIFEST

why() { plug scan | awk -F'\t' -v i="$1" '$1==i {print $7}'; }
col() { plug scan | awk -F'\t' -v i="$1" -v c="$2" '$1==i {print $c}'; }

echo "synui-plugins — $SYNPLUG"

# ── the format is read at all ───────────────────────────────────────────────
hdr=$(plug scan | head -1)
check "scan emits the header the bar reads" \
      $'id\tname\tdescription\tdir\tentry\tenabled\tunsupported' "$hdr"

check "a bar-widget manifest is listed"        "good.plain" "$(col good.plain 1)"
check "…with its name off the manifest"        "good.plain widget" "$(col good.plain 2)"
check "…and its entry point"                   "Plain.qml" "$(col good.plain 5)"

# ⚠ The entry point is keyed `barWidget` while the kind is `bar-widget`. That
# asymmetry is Omarchy's spelling, and reading the wrong one is the likeliest
# way for this to list a plugin and then load nothing.
grep -q '"barWidget"' "$TREE/plugins/good.plain/manifest.json"
check "the entry point is keyed barWidget, not bar-widget" 0 $?

# A kind synui does not host is not a refusal — it is not this list's business.
# Showing it as "unsupported" would report a fault about a plugin that is
# working perfectly well as somebody else's overlay.
check "a non-bar-widget kind is not listed at all" "" "$(col other.kind 1)"

# ── the refusals, which are the point ───────────────────────────────────────
check "a plain BarWidget plugin is hostable"   "" "$(why good.plain)"
check "…and so is one importing nothing of ours" "" "$(why good.bare)"

case "$(why no.hypr)" in
    *Hyprland*) ok "a Hyprland import is refused, by name" ;;
    *) bad "a Hyprland import is refused, by name (got '$(why no.hypr)')" ;;
esac

# ⚠ THE DIFFERENCE IS ASKED OF THE FILESYSTEM, AND THIS IS THE CHECK THAT PROVED
# IT WORTH IT. quickshell resolves `import qs.Foo` to <shell root>/Foo, so a
# hardcoded list would have refused qs.Ui — where BarWidget lives — and would
# have gone on refusing qs.Commons after the shim for it was added. Neither
# needed a line changed: the module directory appeared and the answer changed
# with it.
case "$(why no.qsmod)" in
    *qs.NoSuchModule*) ok "an unprovided qs module is refused, and named" ;;
    *) bad "an unprovided qs module is refused, and named (got '$(why no.qsmod)')" ;;
esac
check "…and qs.Commons is hostable now that it is provided" "" "$(why yes.commons)"
[ -d "$SYNUI_BAR/Ui" ] && ok "…and qs.Ui is provided, which is why it passes" \
                       || bad "quickshell/Ui is missing — BarWidget has no home"
[ -d "$SYNUI_BAR/Commons" ] && ok "…as is qs.Commons" \
                            || bad "quickshell/Commons is missing"

case "$(why no.entry)" in
    *missing*) ok "a manifest naming a file that is not there is refused" ;;
    *) bad "a missing entry point is refused (got '$(why no.entry)')" ;;
esac

# ⛔ A manifest is THIRD-PARTY. `"barWidget": "../../../etc/passwd"` must not be
# read, let alone loaded into the bar's own process.
case "$(why no.escape)" in
    *escapes*|*missing*) ok "an entry point escaping the plugin directory is refused" ;;
    *) bad "a path escape is refused (got '$(why no.escape)')" ;;
esac

# ── turning one on ──────────────────────────────────────────────────────────
[ -e "$XDG_CONFIG_HOME/synui/plugins.state" ] \
    && bad "the state file exists before anything was enabled" \
    || ok "nothing is enabled until it is asked for"

plug good.plain on >/dev/null
check "a hostable plugin turns on"             "on"  "$(col good.plain 6)"
check "…and nothing else came on with it"      "off" "$(col good.bare 6)"

plug good.plain toggle >/dev/null
check "toggle turns it back off"               "off" "$(col good.plain 6)"

# ⛔ THE ONE THAT MATTERS. Writing `on` for a plugin the bar will not host
# leaves a state file claiming something that never appears — which is the
# failure the whole refusal path exists to prevent.
plug no.hypr on >/dev/null 2>&1
rc=$?
[ "$rc" -ne 0 ] && ok "enabling an unhostable plugin fails" \
                || bad "enabling an unhostable plugin returned 0"
check "…and it did not reach the state file"   "off" "$(col no.hypr 6)"

plug nothing.here on >/dev/null 2>&1
[ $? -ne 0 ] && ok "an unknown id fails rather than inventing a row" \
             || bad "an unknown id returned 0"

# ── Omarchy's own directory is searched ─────────────────────────────────────
#
# `omarchy plugin add <git-url>` clones into ~/.config/omarchy/plugins. Somebody
# who has done that on this machine should find the plugin listed rather than
# having to copy it, which is most of what "reads Omarchy's format" is worth.
unset SYNUI_PLUGIN_DIRS
mkdir -p "$XDG_CONFIG_HOME/omarchy/plugins/cloned.one"
cat > "$XDG_CONFIG_HOME/omarchy/plugins/cloned.one/manifest.json" <<'MANIFEST'
{ "schemaVersion": 1, "id": "cloned.one", "name": "Cloned", "version": "1.0.0",
  "kinds": ["bar-widget"], "entryPoints": { "barWidget": "C.qml" } }
MANIFEST
printf 'import QtQuick\nItem { }\n' > "$XDG_CONFIG_HOME/omarchy/plugins/cloned.one/C.qml"
check "a plugin in omarchy's own directory is found" \
      "cloned.one" "$(plug scan | awk -F'\t' '$1=="cloned.one" {print $1}')"

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ]
