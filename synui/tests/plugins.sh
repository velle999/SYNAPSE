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

# ⛔ THE CATALOGUE AND THE FILTER OUT OF THIS CHECKOUT, NOT /usr/share. Both
# default to the installed copies, and an installed copy is the LAST RELEASE —
# so without these the suite proves things about the package already on the
# machine and passes green while the tree it was run on is broken. It bit here:
# the twelve-column catalogue was in the tree, the seven-column one was
# installed, and the test read the installed one.
export SYNUI_PLUGIN_CATALOGUE="$HERE/../data/plugins/catalogue.tsv"
export SYNUI_PLUGIN_FILTER="$HERE/../data/plugins/registry.py"

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

# ── the community registry ──────────────────────────────────────────────────
#
# `browse` used to be five rows out of the file this package ships. It is now
# those five plus everything omarchyplugins.com lists that this desktop could
# host, which is where the games and most of the widgets anybody wants actually
# are — and a filter deciding what "could host" means is a thing that can go
# quietly wrong in both directions.
#
# ⛔ NOT ONE BYTE OVER THE NETWORK. The URL is an override and curl reads
# file://, so the whole fetch path — curl, registry.py, the cache write, the
# merge, the search — runs against a fixture. A test that phones a website is a
# test that fails on a train.
#
# ⚠ AND THE CACHE IS REDIRECTED INTO THE SCRATCH TREE. Its default is
# ~/.cache/synui, which is the user's; a suite that wrote there would rewrite
# the list on the machine running it.
export SYNUI_PLUGIN_REGISTRY_CACHE="$TREE/registry.tsv"
export SYNUI_PLUGIN_REGISTRY="file://$TREE/catalog.json"

cat > "$TREE/catalog.json" <<'CATALOG'
{ "generatedAt": "2026-08-24", "plugins": [
  { "id": "fixture.keeper", "name": "Keeper", "description": "A bar widget that survives the filter",
    "author": "Someone", "category": "Widgets", "tags": ["bar", "games"],
    "kind": "Bar widget", "status": "Available", "repo": "https://example.invalid/keeper",
    "installCommand": "omarchy plugin add https://example.invalid/keeper.git --enable",
    "sourceType": "community", "verificationStatus": "verified",
    "repositoryLayout": "root-plugin", "installAvailable": true, "stars": 12 },
  { "id": "fixture.overlay", "name": "Overlay", "description": "Not a bar widget",
    "kind": "Overlay", "status": "Available", "repo": "https://example.invalid/overlay",
    "sourceType": "community", "repositoryLayout": "root-plugin", "installAvailable": true },
  { "id": "fixture.broken", "name": "Broken", "description": "Their own harness says it does not load",
    "kind": "Bar widget", "status": "Compatibility failed", "repo": "https://example.invalid/broken",
    "sourceType": "community", "repositoryLayout": "root-plugin", "installAvailable": true },
  { "id": "fixture.manual", "name": "Manual", "description": "Has its own installer",
    "kind": "Bar widget", "status": "Manual setup", "repo": "https://example.invalid/manual",
    "sourceType": "community", "repositoryLayout": "root-plugin", "installAvailable": false },
  { "id": "fixture.mono", "name": "Mono", "description": "One widget inside a monorepo",
    "kind": "Bar widget", "status": "Available", "repo": "https://example.invalid/mono",
    "sourceType": "community", "repositoryLayout": "monorepo", "installAvailable": true },
  { "id": "omarchy.spacer", "name": "Spacer", "description": "Their own, and ours is hand-tested",
    "kind": "Bar widget", "status": "Available", "repo": "https://example.invalid/spacer",
    "sourceType": "builtin", "repositoryLayout": "root-plugin", "installAvailable": true },
  { "id": "fixture.badurl", "name": "Bad URL", "description": "The address is not one git could be handed",
    "kind": "Bar widget", "status": "Available", "repo": "http://example.invalid/x; rm -rf /",
    "sourceType": "community", "repositoryLayout": "root-plugin", "installAvailable": true }
] }
CATALOG

plug refresh >/dev/null 2>&1
check "refresh writes the cache" "yes" \
      "$([ -s "$TREE/registry.tsv" ] && echo yes || echo no)"

# ⚠ COUNTED, never `| grep -q`: a producer killed by SIGPIPE on the first match
# reports 141, and a construct that fails ON A MATCH is the worst kind of test.
n=$(grep -cv '^[[:space:]]*\(#\|$\)' "$TREE/registry.tsv")
check "one of seven listings survives the filter" "1" "$n"

check "…and it is the bar widget"  "fixture.keeper" \
      "$(awk -F'\t' '!/^#/{print $1; exit}' "$TREE/registry.tsv")"
check "…with twelve columns"       "12" \
      "$(awk -F'\t' '!/^#/{print NF; exit}' "$TREE/registry.tsv")"
# The install URL comes off their own install command, `.git` and all.
check "…and the URL git will be handed" "https://example.invalid/keeper.git" \
      "$(awk -F'\t' '!/^#/{print $4; exit}' "$TREE/registry.tsv")"
# Empty path and base are what "the repository IS the plugin" looks like, and
# what tells `add` to clone rather than sparse-checkout.
check "…and no sub-path, so add clones the repository" "" \
      "$(awk -F'\t' '!/^#/{print $6 $7; exit}' "$TREE/registry.tsv")"

# ⛔ THE SHIPPED ROW WINS. Both lists carry omarchy.spacer; two rows for one
# widget would offer two different installs for it, and only ours has been
# loaded into a real bar.
n=$(plug catalogue 2>/dev/null | awk -F'\t' '$1=="omarchy.spacer"' | wc -l)
check "a widget in both lists is one row" "1" "$n"
check "…and it is the shipped one" "shipped" \
      "$(plug catalogue 2>/dev/null | awk -F'\t' '$1=="omarchy.spacer" {print $10}')"

check "catalogue has ten columns"  "10" \
      "$(plug catalogue 2>/dev/null | awk -F'\t' 'NR==2 {print NF}')"

# Search: every word has to match, and it reaches the tags. `games` appears
# nowhere in the fixture's name or description — only in its tags — which is
# exactly the case that matters, because that is how the registry files games.
check "browse finds a widget by a tag" "1" \
      "$(plug browse games 2>/dev/null | grep -c '^  fixture.keeper ')"
check "browse narrows on every word" "0" \
      "$(plug browse games nothinglikethis 2>/dev/null | grep -c '^  fixture.keeper ')"

# ⛔ A REGISTRY THAT PARSED TO NOTHING MUST NOT REPLACE A GOOD CACHE. One
# renamed field upstream would otherwise turn this back into a list of five.
printf '{ "plugins": [] }\n' > "$TREE/catalog.json"
plug refresh >/dev/null 2>&1
check "an empty parse leaves the last cache alone" "fixture.keeper" \
      "$(awk -F'\t' '!/^#/{print $1; exit}' "$TREE/registry.tsv")"

# And with no registry at all, browse is still the shipped list rather than an
# error — a machine with no network gets a short answer, not a broken one.
export SYNUI_PLUGIN_REGISTRY_CACHE="$TREE/gone.tsv"
export SYNUI_PLUGIN_REGISTRY="file://$TREE/not-there.json"
plug browse >/dev/null 2>&1
check "browse survives an unreachable registry" "0" "$?"
check "…and still offers the shipped widgets" "1" \
      "$(plug browse 2>/dev/null | grep -c '^  omarchy.spacer ')"

# ── installing from a repository, and the types it reaches for ──────────────
#
# ⛔ A LOCAL REPOSITORY OVER file://, so the clone path is exercised without the
# network. `add <git-url>` is how every registry row installs — the id only
# chooses the URL — so it is the path nine hundred widgets go down.
#
# What is being proved past the clone is the SOFT warning. `refusal` answers
# from the imports and passes anything importing qs.Ui, which synui provides;
# the widgets in the registry are written against Omarchy's qs.Ui, which has
# thirty-odd types this one does not. A widget naming one of those loads with a
# corner missing, so it is installed and warned about rather than refused — and
# the warning has to NAME the type or it is not worth printing.
if command -v git >/dev/null 2>&1 && [ -x /usr/lib/qt6/bin/qmllint ]; then
    REPO="$TREE/fixture-repo"
    mkdir -p "$REPO"
    cat > "$REPO/manifest.json" <<'MANIFEST'
{ "schemaVersion": 1, "id": "fixture.cloned", "name": "Cloned widget",
  "version": "1.0.0", "description": "installed over file://",
  "kinds": ["bar-widget"], "entryPoints": { "barWidget": "W.qml" } }
MANIFEST
    # Roots at BarWidget, which this bar DOES provide, and then reaches for a
    # type it does not — which is the shape of a real Omarchy widget here.
    cat > "$REPO/W.qml" <<'WIDGET'
import QtQuick
import qs.Ui
BarWidget {
    implicitWidth: 20
    BarIconButtonThatDoesNotExist { }
}
WIDGET
    git -c init.defaultBranch=main init -q "$REPO"
    git -C "$REPO" -c user.email=t@example.invalid -c user.name=t add -A
    git -C "$REPO" -c user.email=t@example.invalid -c user.name=t commit -qm f

    out=$(plug add "file://$REPO" 2>&1)
    rc=$?
    check "a plugin clones from a git URL and turns on" "0" "$rc"
    check "…and it is on disk under our own directory" "on" \
          "$(plug scan | awk -F'\t' '$1=="fixture.cloned" {print $6}')"
    # ⚠ NAMED. "something is missing" is a message nobody can act on.
    case "$out" in
        *BarIconButtonThatDoesNotExist*)
            ok "…and the type this bar has not got is named" ;;
        *)  bad "the missing type was not named (got [$out])" ;;
    esac
    plug remove fixture.cloned >/dev/null 2>&1
else
    ok "git or Qt 6's qmllint is absent — the clone path is not checked here"
fi

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ]
