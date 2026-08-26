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

# ⛔ POINTED SOMEWHERE HARMLESS FROM THE FIRST LINE, not only in the registry
# section below. `browse`, `catalogue` and `add <id>` fetch the community list
# when the cache is missing, and the cache defaults to the USER'S ~/.cache — so
# a test added above that section would reach omarchyplugins.com from a build
# machine and write into whoever is building. The registry section overrides
# both with its own fixture; these two are the floor under it.
export SYNUI_PLUGIN_REGISTRY_CACHE="$TREE/registry-default.tsv"
export SYNUI_PLUGIN_REGISTRY="file://$TREE/no-registry-here.json"

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
# ⛔ THE WHOLE HEADER, IN ORDER, AND NOT A PREFIX MATCH. Plugins.qml reads this
# TSV by POSITION — one place parses a manifest, so the columns are a contract
# rather than a guess — and a column INSERTED in the middle shifts every field
# after it into the wrong property with nothing said by either end. Appending is
# the only safe way to grow this, which is why the assertion is exact: it fails
# on an insertion and it fails on a rename, and it is meant to.
check "scan emits the header the bar reads" \
      $'id\tname\tdescription\tdir\tentry\tenabled\tunsupported\tpanel\tservice' "$hdr"

# The two columns that carry a plugin's session-scoped entry points. Mounted
# once for the desktop rather than once per bar (see PluginMount), and scanned
# straight past for as long as this file emitted seven columns — which is why a
# widget declaring a `panel` appeared on the bar and did nothing when clicked.
mkdir -p "$TREE/plugins/has.panel"
cat > "$TREE/plugins/has.panel/manifest.json" <<'MANIFEST'
{ "schemaVersion": 1, "id": "has.panel", "name": "Panelled", "version": "1.0.0",
  "kinds": ["service", "panel", "bar-widget"],
  "entryPoints": { "service": "S.qml", "panel": "P.qml", "barWidget": "B.qml" } }
MANIFEST
: > "$TREE/plugins/has.panel/B.qml"
: > "$TREE/plugins/has.panel/P.qml"
: > "$TREE/plugins/has.panel/S.qml"
check "a panel entry point is scanned"   "P.qml" "$(col has.panel 8)"
check "…and a service entry point too"   "S.qml" "$(col has.panel 9)"

# ⚠ AND A PATH OUT OF THE PLUGIN IS NOT AN ENTRY POINT. The manifest is
# third-party; the rule barWidget already lives under applies to these two as
# well, and a refusal is not available here because there is no widget to hang
# the reason on — the entry is simply not offered.
mkdir -p "$TREE/plugins/panel.escape"
cat > "$TREE/plugins/panel.escape/manifest.json" <<'MANIFEST'
{ "schemaVersion": 1, "id": "panel.escape", "name": "Escaper", "version": "1.0.0",
  "kinds": ["panel", "bar-widget"],
  "entryPoints": { "panel": "../../../etc/passwd", "barWidget": "B.qml" } }
MANIFEST
: > "$TREE/plugins/panel.escape/B.qml"
check "a panel path escaping the plugin is dropped" "" "$(col panel.escape 8)"
check "a panel entry point that is not there is dropped" "" "$(col good.plain 8)"

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
    # ⛔ 3, NOT 0. It installed and it is on — and it will not draw, because the
    # type it reaches for is not in this bar's qs.Ui. The window only shows a
    # message when the exit code is non-zero, so reporting 0 here is exactly how
    # three dead widgets came to be reported as "installed and on". 3 rather
    # than 1 so a script can still tell it from "did not install".
    check "a degraded install exits 3, not 0" "3" "$rc"
    check "…and it is still on disk and on" "on" \
          "$(plug scan | awk -F'\t' '$1=="fixture.cloned" {print $6}')"
    # ⚠ NAMED. "something is missing" is a message nobody can act on.
    case "$out" in
        *BarIconButtonThatDoesNotExist*)
            ok "…and the type this bar has not got is named" ;;
        *)  bad "the missing type was not named (got [$out])" ;;
    esac
    check "check reports the same thing for what is installed" "1" \
          "$(plug check fixture.cloned | grep -c 'BarIconButtonThatDoesNotExist')"
    plug remove fixture.cloned >/dev/null 2>&1

    # ⚠ AND THE CLEAN CASE, or the assertion above would pass just as well
    # against a check that fails everything. This one names only types the
    # module really provides, so it must install silently and exit 0.
    REPO2="$TREE/fixture-repo-ok"
    mkdir -p "$REPO2"
    cat > "$REPO2/manifest.json" <<'MANIFEST2'
{ "schemaVersion": 1, "id": "fixture.clean", "name": "Clean widget",
  "version": "1.0.0", "description": "names only what the module has",
  "kinds": ["bar-widget"], "entryPoints": { "barWidget": "W.qml" } }
MANIFEST2
    cat > "$REPO2/W.qml" <<'WIDGET2'
import QtQuick
import qs.Ui
import qs.Commons
BarIconButton {
    moduleName: "fixture.clean"
    text: "\uf0e7"
    fontSize: Style.bar.iconFont
}
WIDGET2
    git -c init.defaultBranch=main init -q "$REPO2"
    git -C "$REPO2" -c user.email=t@example.invalid -c user.name=t add -A
    git -C "$REPO2" -c user.email=t@example.invalid -c user.name=t commit -qm f

    plug add "file://$REPO2" >/dev/null 2>&1
    check "a widget naming only what the module has installs clean" "0" "$?"
    plug remove fixture.clean >/dev/null 2>&1
else
    ok "git or Qt 6's qmllint is absent — the clone path is not checked here"
fi

# ── The window itself ───────────────────────────────────────────────────────
# ── Their directory name, and one row per plugin ────────────────────────────
#
# ⛔ WHAT THIS GUARDS IS A WIDGET THAT RUNS AND SHOWS NOTHING. The Vitals widget
# spawns its own helper as a literal `$HOME/.config/omarchy/plugins/<id>/
# stats.sh`, hardcoded, with no way to configure it — installed anywhere else it
# starts nothing for ever. On the machine this was written on it failed that way
# 291 times in one session and the widget looked merely empty. So a plugin we
# install is ALSO reachable under the path it may expect.
#
# ⚠ AND THE SECOND PATH MUST NOT BECOME A SECOND WIDGET. Both directories are
# searched, and neither scan() nor Plugins.qml used to dedupe: two rows for one
# id is two copies on the bar, each with its own timers and its own state.
#
# These run with the DEFAULT search path — SYNUI_PLUGIN_DIRS is what everything
# above overrides, and the whole point here is which directories the shim picks
# on its own.
CFG=$XDG_CONFIG_HOME
MY=$CFG/synui/plugins
mkdir -p "$MY/link.me"
cat > "$MY/link.me/manifest.json" <<'MANIFEST'
{ "schemaVersion": 1, "id": "link.me", "name": "Linked", "version": "1.0.0",
  "kinds": ["bar-widget"], "entryPoints": { "barWidget": "W.qml" } }
MANIFEST
printf 'import QtQuick\nItem { }\n' > "$MY/link.me/W.qml"

defaults() { env -u SYNUI_PLUGIN_DIRS bash "$SYNPLUG" "$@"; }

# ⚠ The clone section above put a plugin in omarchy's directory to prove that
# one is searched, and an omarchy tree we did not create is one we never touch —
# which is asserted below, and would otherwise make the first check here fail
# for the right reason at the wrong time.
rm -rf "$CFG/omarchy"

defaults relink >/dev/null 2>&1
check "a plugin is reachable under omarchy's name too" "$MY/link.me" \
      "$(readlink "$CFG/omarchy/plugins/link.me")"
check "…and it is still ONE row in the scan" "1" \
      "$(defaults scan | awk -F'\t' '$1=="link.me"' | wc -l)"
# ⚠ OURS, NOT THE LINK. Every consumer of this TSV takes the first row for an
# id and then reads column four to find the files; resolving to the compat path
# would work by accident today and break the moment the link is not there.
check "…and the row points at the real directory" "$MY/link.me" \
      "$(defaults scan | awk -F'\t' '$1=="link.me" {print $4}')"

# ⛔ NEVER INTO A TREE THAT IS ALREADY THEIRS. `omarchy plugin add` owns that
# directory on a machine that has Omarchy, and two programs writing one tree is
# how a plugin ends up half-removed by whichever ran last. The marker is how we
# know which of the two wrote it.
rm -rf "$CFG/omarchy"
mkdir -p "$CFG/omarchy/plugins/theirs.own"
defaults relink >/dev/null 2>&1
check "an omarchy tree we did not create is left alone" "no" \
      "$([ -e "$CFG/omarchy/plugins/link.me" ] && echo yes || echo no)"
check "…and nothing of theirs is disturbed" "yes" \
      "$([ -d "$CFG/omarchy/plugins/theirs.own" ] && echo yes || echo no)"

# A link left behind by a plugin removed by hand is a manifest read that fails
# on every scan from then on.
rm -rf "$CFG/omarchy"
defaults relink >/dev/null 2>&1
rm -rf "$MY/link.me"
defaults relink >/dev/null 2>&1
check "a dangling link is cleared away" "no" \
      "$([ -L "$CFG/omarchy/plugins/link.me" ] && echo yes || echo no)"
rm -rf "$CFG/omarchy"

#
# ⚠ THE GUI HAD NO CHECK OF ANY KIND until it grew a category pane, and a QML
# file that will not load is a window that draws NOTHING — `synui-plugins gui`
# exits 0 having shown you an empty rectangle, because quickshell's refusal goes
# to its own log and not to the terminal you typed in.
#
# ⛔ LINT AND NOT A LOAD. Standing the real window up needs a compositor, and a
# load test that times out on a slower machine fails the BUILD for a reason that
# has nothing to do with the change — this file is parsed, its types resolved
# and its bindings checked, which is the half that catches a restructure.
if [ -x /usr/lib/qt6/bin/qmllint ]; then
    GUI="$(dirname "$0")/../data/plugins-gui.qml"
    /usr/lib/qt6/bin/qmllint "$GUI" >"$TREE/qmllint.out" 2>&1
    check "the plugins window parses and balances" "0" "$?"
    # ⚠ WHAT THIS CATCHES IS STRUCTURE, AND SAYING SO MATTERS. qmllint cannot
    # see Quickshell's types from here, so an unknown type is a warning and the
    # exit code stays 0 — `NotAType { }` pasted into this file passes. A syntax
    # error and an unbalanced brace both exit 255, and those are exactly what a
    # layout restructure gets wrong. Do not read a pass here as "the window
    # works"; it means the file is still a well-formed document.
else
    ok "Qt 6's qmllint is absent — the window is not linted here"
fi

# ── the window follows the DESKTOP font ────────────────────────────────────
#
# It did not. uiFont was the literal "monospace" and all nineteen pixelSizes
# were literals, so this window kept whatever face and size Qt resolved at
# startup while every other window in the suite followed
# ~/.config/synui/font.state — one window in the middle of the settings, off on
# its own. velle, 2026-08-25: "font size isn't system wide. it's supposed to be."
#
# ⚠ BOTH HALVES OR NEITHER. Qt resolves an application's default font ONCE at
# startup and QML cannot change it afterwards, so the family has to be named on
# every Text and the size has to go through root.ui() — applying either one
# alone gives a window that follows the font until somebody changes it.
if [ -f "$(dirname "$0")/../data/plugins-gui.qml" ]; then
    GUI="$(dirname "$0")/../data/plugins-gui.qml"
    grep -q 'font\.state' "$GUI"
    check "the plugins window reads font.state" "0" "$?"

    # A bare `pixelSize: 12` is the failure, and it is silent: the window draws
    # perfectly, at the wrong size, next to windows at the right one.
    bare=$(grep -c 'pixelSize:[[:space:]]*[0-9]' "$GUI" || true)
    check "no pixelSize escapes root.ui()" "0" "$bare"

    # Every font block names a family. The one allowed literal is "monospace" —
    # a command to type is not prose — and it still takes root.ui().
    unnamed=0
    for ln in $(grep -n 'font {' "$GUI" | cut -d: -f1); do
        sed -n "${ln},$((ln+2))p" "$GUI" | tr '\n' ' ' |
            grep -qE 'root\.uiFont|family: "monospace"' || unnamed=$((unnamed + 1))
    done
    check "every font block names a family" "0" "$unnamed"
fi

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ]
