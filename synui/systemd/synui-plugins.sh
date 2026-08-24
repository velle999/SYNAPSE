#!/usr/bin/env bash
# synui-plugins — bar plugins, in Omarchy's format.
#
# ── WHY OMARCHY'S FORMAT AND NOT ONE OF OUR OWN ─────────────────────────────
#
# SynapseOS's bar is quickshell, and so is Omarchy's — their whole desktop is
# one long-lived quickshell process in which the bar, the panels and the
# overlays are plugins. That makes their plugin format the only one already
# describing "a QML widget you can drop into a quickshell bar", and adopting it
# means a widget written once can be dropped into either desktop instead of
# each project growing an incompatible directory layout for the same idea.
#
# The format is a directory holding `manifest.json` and some QML:
#
#   {
#     "schemaVersion": 1,
#     "id": "example.clock",          <- namespaced; `omarchy.` is theirs
#     "name": "Clock",
#     "version": "1.0.0",
#     "author": "...",
#     "description": "...",
#     "kinds": ["bar-widget"],
#     "entryPoints": { "barWidget": "Clock.qml" },
#     "barWidget": { "displayName": "Clock", "category": "Time",
#                    "allowMultiple": false }
#   }
#
# ⚠ `entryPoints` IS KEYED IN camelCase WHILE `kinds` IS HYPHENATED. The kind is
# `bar-widget` and its entry point is `barWidget`. That is their spelling, not a
# typo here, and getting it wrong is the single likeliest reason a hand-written
# manifest loads nothing.
#
# ── WHAT SYNAPSEOS CAN AND CANNOT HOST ──────────────────────────────────────
#
# ⛔ AN ARBITRARY OMARCHY WIDGET WILL NOT RUN HERE, and pretending otherwise
# would be the worst outcome — a plugin system that silently shows nothing.
# Their shipped widgets root at `BarWidget` (that part is portable and is
# implemented here) but they also `import qs.Ui` and `import qs.Commons`, which
# are Omarchy's own singletons — Style.qml alone is 23KB of API — and several
# `import Quickshell.Hyprland`, which talks to a compositor socket that does not
# exist on synui.
#
# So what is supported is the FORMAT and the documented BarWidget contract:
# a widget that roots at BarWidget and uses `moduleName`, `settings`,
# `setting()`, `vertical` and `barSize` runs on both desktops. One that reaches
# into Omarchy's internals is REFUSED, by name, with the import that did it —
# see `scan`. A refusal you can read is worth more than a widget that is
# quietly absent.
#
# ── The single writer of plugins.state ──────────────────────────────────────
#
# The bar watches that file (quickshell Plugins.qml, FileView watchChanges), so
# a change here reaches the screen with no reload and no IPC — the same
# arrangement synui-widgets has with widgets.state, and for the same reason:
# one format, one place it can be wrong.
#
# Everything is OFF until switched on, and the file does not exist until
# something is. A plugin is third-party code in the bar's own process; it has
# to be asked for.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -u

CONF_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
STATE="$CONF_HOME/synui/plugins.state"

# The bar's QML tree, which is also what `import qs.<Module>` resolves against —
# quickshell maps that prefix to the shell root. Overridable so the tests can
# point at a checkout instead of the installed copy.
SHELL_ROOT="${SYNUI_BAR:-/usr/share/synui/quickshell}"

# ⚠ OMARCHY'S OWN DIRECTORY IS SEARCHED FIRST AND ON PURPOSE. `omarchy plugin
# add <git-url>` clones into ~/.config/omarchy/plugins, and a user who has done
# that on this machine should find the plugin listed here rather than having to
# copy it. Ours is second so a name collision resolves to the one we ship.
DIRS="${SYNUI_PLUGIN_DIRS:-$CONF_HOME/omarchy/plugins:$CONF_HOME/synui/plugins:/usr/share/synui/plugins}"

usage() {
    cat <<EOF
usage: synui-plugins [list|scan]
       synui-plugins <id> [on|off|toggle]
       synui-plugins add <git-url> [name]
       synui-plugins remove <id>
       synui-plugins browse [text]
       synui-plugins refresh
       synui-plugins tui
       synui-plugins gui

  list                  what is installed, and whether each one is on
  scan                  the same, as TSV — what the bar reads
  synui-plugins x on    turn one on
  add <id>              install one out of browse
  add <git-url>         clone a plugin into ~/.config/synui/plugins and, if it
                        is hostable, turn it on
  remove <id>           delete one you installed. Only from your own directory
  browse [text]         what you can install. Around nine hundred community bar
                        widgets, so it takes words and narrows: every one of
                        them has to match, across the id, the name, the
                        description, the category, the tags and the author.
                        --all prints the lot; --refresh fetches first
  check [<id>]          whether what is installed can actually draw, and what
                        it is missing if not
  refresh               fetch the community list now
  catalogue             browse as TSV — what the window reads
  tui                   all of it in the terminal, with arrow keys and /
  gui                   all of it in a window

  What `browse` offers comes from two places: the widgets shipped with synui,
  each loaded into a real bar before it was listed, and omarchyplugins.com's
  community registry, cached under ~/.cache/synui/plugins and refreshed when it
  is a week old. Everything from the registry is somebody else's claim about
  somebody else's desktop — whether a widget can actually run here is answered
  at install time, by name, by the same refusal check below.

  Plugins are directories holding a manifest.json, in Omarchy's shell-plugin
  format. Searched, in order:
EOF
    printf '    %s\n' $(printf '%s\n' "$DIRS" | tr ':' ' ')
    cat <<EOF

  Only the "bar-widget" kind is hosted. A widget must root at BarWidget and
  keep to that contract; one importing qs.Ui, qs.Commons or Quickshell.Hyprland
  is listed as unsupported with the reason, because those are Omarchy's own
  and have no counterpart here.
EOF
}

# One field out of a flat JSON object, without a JSON parser.
#
# ⚠ DELIBERATELY NOT `grep | cut`. A description containing the word "id" would
# match a naive grep for it, and a manifest is somebody else's file. The anchor
# is the QUOTED key followed by a colon, and only the first match counts.
jfield() {  # jfield <file> <key>
    sed -n 's/.*"'"$2"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$1" | head -1
}

# Whether a JSON array field contains a given string.
jarray_has() {  # jarray_has <file> <key> <want>
    tr -d '\n' < "$1" \
        | sed -n 's/.*"'"$2"'"[[:space:]]*:[[:space:]]*\[\([^]]*\)\].*/\1/p' \
        | grep -q "\"$3\""
}

state_on() {  # state_on <id>
    [ -r "$STATE" ] || return 1
    grep -q "^$1=on\$" "$STATE"
}

# ── Why this plugin cannot be hosted, or nothing ────────────────────────────
#
# Ordered most-specific first, so the answer names the actual blocker rather
# than the first thing noticed.
refusal() {  # refusal <dir> <manifest> <entry>
    local dir=$1 man=$2 entry=$3

    [ -n "$entry" ] || { printf 'no barWidget entry point'; return; }
    [ -f "$dir/$entry" ] || { printf 'entry point %s is missing' "$entry"; return; }

    # ⚠ A safe RELATIVE path, which is Omarchy's own validation rule. A manifest
    # is third-party and `"barWidget": "../../../etc/passwd"` must not be read,
    # let alone loaded into the bar's process.
    case "$entry" in
        /*|*..*) printf 'entry point escapes the plugin directory'; return ;;
    esac

    # ── The imports with no counterpart here ────────────────────────────────
    #
    # Checked in the ENTRY POINT only. A plugin's other files are its business
    # until the entry point pulls one in, and a false refusal is worse than a
    # late failure: the Loader in Bar.qml catches what gets past this and logs
    # which plugin it was.
    #
    # Hyprland first, because it is the one that can never be satisfied. synui
    # is a wlroots compositor of its own and Quickshell.Hyprland talks to
    # Hyprland's IPC socket; there is nothing to shim.
    if grep -qE '^[[:space:]]*import[[:space:]]+Quickshell\.Hyprland' "$dir/$entry"; then
        printf 'needs Hyprland (synui is not Hyprland)'
        return
    fi

    # ⚠ ASKED OF THE FILESYSTEM, NOT OF A HARDCODED LIST. quickshell resolves
    # `import qs.Foo` to <shell root>/Foo, so whether a plugin's qs import can
    # be satisfied is a question about which module directories this bar ships —
    # and that changes. A list here would refuse `qs.Ui` today (which IS
    # provided: it is where BarWidget lives) or keep allowing a module after it
    # was removed.
    #
    # Only the FIRST missing one is reported: a widget written against Omarchy's
    # Commons will import several, and naming them all turns a reason into a
    # paragraph.
    local mod
    for mod in $(sed -n 's/^[[:space:]]*import[[:space:]]\+qs\.\([A-Za-z0-9_]\+\).*/\1/p' \
                     "$dir/$entry"); do
        [ -d "$SHELL_ROOT/$mod" ] && continue
        printf 'needs qs.%s, which synui-bar does not provide' "$mod"
        return
    done
}

# ── One field out of a tab-separated row ────────────────────────────────────
#
# ⛔ AND NOT `IFS=$'\t' read -r a b c`, WHICH IS WRONG ON THIS DATA. Tab is IFS
# WHITESPACE to the shell, so a run of them collapses into one delimiter and
# every EMPTY field silently disappears — the fields after it shift left and
# land in the wrong variables. A registry row is `id name desc repo` and then
# three empty columns, so the read handed the category to `ref`, `add` believed
# it had a sub-path, and a clone that should have been a clone tried to be a
# sparse checkout of a directory that does not exist. It failed with "could not
# fetch", which names the symptom and not one word of the cause.
#
# `cut` splits on a literal tab and keeps empty fields. Once per install, not
# per row.
fld() { printf '%s\n' "$1" | cut -f"$2"; }

# id \t name \t description \t dir \t entry \t on \t refusal
scan() {
    local d p id name desc kinds entry why on
    printf 'id\tname\tdescription\tdir\tentry\tenabled\tunsupported\n'
    printf '%s\n' "$DIRS" | tr ':' '\n' | while IFS= read -r d; do
        [ -n "$d" ] && [ -d "$d" ] || continue
        for p in "$d"/*/; do
            [ -f "$p/manifest.json" ] || continue
            id=$(jfield "$p/manifest.json" id)
            [ -n "$id" ] || continue
            jarray_has "$p/manifest.json" kinds "bar-widget" || continue

            name=$(jfield "$p/manifest.json" name)
            desc=$(jfield "$p/manifest.json" description)
            entry=$(jfield "$p/manifest.json" barWidget)
            why=$(refusal "${p%/}" "$p/manifest.json" "$entry")
            on=off
            state_on "$id" && on=on
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                   "$id" "${name:-$id}" "$desc" "${p%/}" "$entry" "$on" "$why"
        done
    done
}

set_state() {  # set_state <id> <on|off>
    mkdir -p "$(dirname "$STATE")"
    local tmp="$STATE.tmp.$$"
    { [ -r "$STATE" ] && grep -v "^$1=" "$STATE"; } > "$tmp" 2>/dev/null
    printf '%s=%s\n' "$1" "$2" >> "$tmp"
    # ⚠ Through a temp file and mv, like every other state file synui writes:
    # truncating the real one and dying mid-write loses every plugin choice.
    mv "$tmp" "$STATE"
}

# Where `add` puts things. Never Omarchy's directory: that one is theirs, and
# `omarchy plugin add` is what owns it. Two programs writing one tree is how a
# plugin ends up half-removed by whichever ran last.
MINE="$CONF_HOME/synui/plugins"

# What `browse` offers. Beside the shipped example, so a checkout and an install
# both find it.
CATALOGUE="${SYNUI_PLUGIN_CATALOGUE:-/usr/share/synui/plugins/catalogue.tsv}"
[ -r "$CATALOGUE" ] || CATALOGUE="$(dirname "$0")/../data/plugins/catalogue.tsv"

# ── One scan per run ────────────────────────────────────────────────────────
#
# ⚠ `scan` WALKS EVERY SEARCH DIRECTORY AND READS EVERY MANIFEST IN IT, and the
# browsers below ask it a question per row. With five rows that was invisible;
# with the registry it is hundreds of walks for one listing, which is the
# difference between a window that opens and one that seems hung. Once per run,
# into a temp file, and the browsers join against it with awk instead of asking
# again.
SCAN_CACHE=""
scan_once() {
    [ -n "$SCAN_CACHE" ] && [ -r "$SCAN_CACHE" ] && { cat "$SCAN_CACHE"; return; }
    SCAN_CACHE=$(mktemp) || { scan; return; }
    scan > "$SCAN_CACHE"
    cat "$SCAN_CACHE"
}
# After anything that changes what is on disk — the cache is a snapshot and an
# install that is not forgotten is an install the next listing cannot see.
scan_drop() { [ -n "$SCAN_CACHE" ] && rm -f "$SCAN_CACHE"; SCAN_CACHE=""; }
trap 'scan_drop' EXIT

# Is this id already on disk, whatever directory it came from?
installed_id() { scan_once | awk -F'\t' -v i="$1" '$1==i {print $1; exit}'; }

# ── The community registry ──────────────────────────────────────────────────
#
# catalogue.tsv above is the hand-tested floor: widgets out of Omarchy's own
# repository, each loaded into a real bar before its row was written. It is
# also, on its own, a browser that shows five things — while the format has
# something like nine hundred community bar widgets written for it, listed at
# omarchyplugins.com, and that is where the interesting ones are. Their
# catalogue has a `games` tag with two dozen widgets behind it: Tetris, Snake,
# Minesweeper, 2048, Wordle, solitaire. None of that was reachable from here.
#
# So a browser reads BOTH: the shipped rows first, then everything the registry
# lists that this desktop could host. data/plugins/registry.py turns their JSON
# into rows and documents which listings it drops and why — in short, anything
# that is not a bar widget, because an overlay, a service or a whole shell suite
# has no host here and offering one is offering an install that cannot end in a
# widget on screen.
#
# ⚠ THE CACHE IS A CACHE, AND NONE OF IT HAS BEEN LOADED INTO A BAR HERE. Every
# fact in it came from somebody else's server; the `trust` column says which
# rows are theirs and which are ours, and `browse` prints it. A widget that
# reaches for Quickshell.Hyprland or a qs.Ui type synui does not ship is caught
# at INSTALL time, by name, by the refusal check — a listing cannot know that
# and this does not pretend to.
REGISTRY_URL="${SYNUI_PLUGIN_REGISTRY:-https://omarchyplugins.com/catalog.json}"
REGISTRY="${SYNUI_PLUGIN_REGISTRY_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/synui/plugins/registry.tsv}"
REGISTRY_FILTER="${SYNUI_PLUGIN_FILTER:-/usr/share/synui/plugins/registry.py}"
[ -r "$REGISTRY_FILTER" ] || REGISTRY_FILTER="$(dirname "$0")/../data/plugins/registry.py"
# How old the cache may get before a browser refreshes it unasked. A widget list
# is not weather: a week is soon enough to see new things and rare enough that
# `browse` is a local command nearly every time it is run.
REGISTRY_MAX_AGE=${SYNUI_PLUGIN_REGISTRY_MAX_AGE:-604800}

registry_refresh() {
    command -v curl >/dev/null 2>&1 || {
        printf 'synui-plugins: curl is not installed\n' >&2; return 1; }
    command -v python3 >/dev/null 2>&1 || {
        printf 'synui-plugins: python3 is not installed\n' >&2; return 1; }
    [ -r "$REGISTRY_FILTER" ] || {
        printf 'synui-plugins: %s is missing\n' "$REGISTRY_FILTER" >&2; return 1; }

    local tmp
    tmp=$(mktemp -d) || return 1
    if ! curl -fsSL --max-time 60 -o "$tmp/catalog.json" -- "$REGISTRY_URL"; then
        printf 'synui-plugins: could not reach %s\n' "$REGISTRY_URL" >&2
        rm -rf "$tmp"; return 1
    fi
    if ! python3 "$REGISTRY_FILTER" < "$tmp/catalog.json" > "$tmp/registry.tsv"; then
        rm -rf "$tmp"; return 1
    fi
    # ⛔ A REGISTRY THAT PARSED TO NOTHING IS A PARSE THAT WENT WRONG, not an
    # empty world. Overwriting a good cache with it would turn one renamed field
    # upstream into a browser that silently shows five widgets again — which is
    # the exact complaint this whole change answers. The old file stays.
    if [ "$(grep -cv '^[[:space:]]*\(#\|$\)' "$tmp/registry.tsv")" -lt 1 ]; then
        printf 'synui-plugins: the registry parsed to nothing — keeping the last one\n' >&2
        rm -rf "$tmp"; return 1
    fi
    mkdir -p "$(dirname "$REGISTRY")"
    mv "$tmp/registry.tsv" "$REGISTRY"
    rm -rf "$tmp"
}

# Fetch it if there is none, or if the one there has aged out.
#
# ⚠ NEVER FATAL. `browse` is useful with the shipped rows alone, so a machine
# with no network gets the short list and a line saying why it is short — not an
# error and not an empty window.
registry_ready() {  # registry_ready [force]
    local now mtime
    if [ "${1:-}" = force ] || [ ! -r "$REGISTRY" ]; then
        registry_refresh || return 1
        return 0
    fi
    now=$(date +%s)
    mtime=$(stat -c %Y "$REGISTRY" 2>/dev/null || printf '%s' "$now")
    [ "$((now - mtime))" -lt "$REGISTRY_MAX_AGE" ] && return 0
    registry_refresh || return 1
}

# Every row a browser has: the shipped ones first, then the registry's, minus
# any id already shipped — one widget is one row whichever list it came from.
# Twelve tab-separated fields; catalogue.tsv's header names them.
catalogue_rows() {
    [ -r "$CATALOGUE" ] && grep -v '^[[:space:]]*\(#\|$\)' "$CATALOGUE"
    [ -r "$REGISTRY" ] || return 0
    awk -F'\t' -v cat="$CATALOGUE" '
        BEGIN { while ((getline line < cat) > 0) {
                    if (line ~ /^[[:space:]]*(#|$)/) continue
                    split(line, f, "\t")
                    if (f[1] != "") shipped[f[1]] = 1 } }
        /^[[:space:]]*(#|$)/ { next }
        $1 != "" && !($1 in shipped)
    ' "$REGISTRY"
}

# ── Fetching ONE widget out of a repository that holds many ─────────────────
#
# Omarchy's bar widgets live together in shell/plugins/bar/widgets, so a plain
# clone would drop their whole desktop into a plugin directory. A partial +
# sparse checkout takes the one path; --filter=blob:none means the blobs for
# everything else are never downloaded at all, so this is a few files over the
# wire rather than a repository.
#
# ⚠ AND THE MANIFEST IS RENAMED ON THE WAY IN. Theirs is <Base>.manifest.json
# because the widgets share a directory; a plugin directory holds one plugin and
# the scanner looks for manifest.json. That rename is the entire difference
# between the two shapes.
fetch_subpath() {  # fetch_subpath <repo> <ref> <path> <base> <dest>
    local repo=$1 ref=$2 path=$3 base=$4 dest=$5 tmp
    tmp=$(mktemp -d) || return 1
    if ! git clone --depth 1 --branch "$ref" --filter=blob:none --sparse \
             -- "$repo" "$tmp/r" >/dev/null 2>&1; then
        rm -rf "$tmp"; return 1
    fi
    # ⚠ THE LICENCE IS IN THE CHECKOUT SET, NOT ONLY IN THE COPY LOOP BELOW. A
    # sparse checkout of the widget path alone leaves the repository ROOT empty,
    # so the LICENSE the loop goes looking for is not on disk and the copy
    # silently does nothing — which would put somebody else's MIT code on a
    # user's machine with no notice attached. Naming the files here is what
    # makes them exist to copy.
    if ! git -C "$tmp/r" sparse-checkout set --no-cone \
             "$path" "/LICENSE" "/LICENSE.md" "/COPYING" >/dev/null 2>&1; then
        rm -rf "$tmp"; return 1
    fi
    local src="$tmp/r/$path"
    [ -f "$src/$base.qml" ] || { rm -rf "$tmp"; return 1; }

    mkdir -p "$dest"
    cp "$src/$base.qml" "$dest/" || { rm -rf "$tmp" "$dest"; return 1; }
    if [ -f "$src/$base.manifest.json" ]; then
        cp "$src/$base.manifest.json" "$dest/manifest.json"
    elif [ -f "$src/manifest.json" ]; then
        cp "$src/manifest.json" "$dest/manifest.json"
    else
        rm -rf "$tmp" "$dest"; return 1
    fi
    # Whatever else the widget names beside itself — their KeyboardLayout has a
    # KeyboardLayoutModel.js, and a widget without its model is a widget that
    # loads and then throws on first use.
    for extra in "$src/$base"*.js; do
        [ -f "$extra" ] && cp "$extra" "$dest/"
    done
    # The licence travels with the code. MIT requires the notice in "all copies
    # or substantial portions", and a file copied onto somebody's machine is a
    # copy however it got there.
    for lic in LICENSE LICENSE.md COPYING; do
        [ -f "$tmp/r/$lic" ] && { cp "$tmp/r/$lic" "$dest/LICENSE"; break; }
    done
    rm -rf "$tmp"
    return 0
}

# ── The types a widget uses that this bar has not got ───────────────────────
#
# `refusal` above answers "can this be hosted at all" from the IMPORTS. That is
# the right question for a hard no — a module that is not there — but it passes
# a widget that imports qs.Ui, which synui does provide, and then reaches for one
# of the thirty-odd types in Omarchy's version of it that synui does not. The
# registry is nine hundred widgets written against their Ui, so that case went
# from theoretical to the common one overnight.
#
# ⚠ A WARNING AND NEVER A REFUSAL. A type can be named on a path that never
# runs — a popup nobody opens — so the widget may be perfectly usable with a
# corner missing, and refusing it would be worse than saying so. It also cannot
# see everything: this asks about the ENTRY POINT, like refusal does.
#
# ⛔ /usr/lib/qt6/bin/qmllint, NEVER /usr/bin/qmllint. The one on PATH belongs to
# another Qt, takes the file, prints nothing and exits 0 — a check that always
# passes, which is indistinguishable from a widget that is fine.
#
# ⚠ AND IT NEEDS A `qs/` DIRECTORY TO POINT AT. quickshell resolves `import
# qs.Foo` to <shell root>/Foo at runtime, which no import path reproduces; the
# symlink farm is what makes qmllint resolve the same names the bar will. Asked
# without it, our OWN shipped example fails — proof the setup, not the widget, is
# what would have been under test.
unresolved_types() {  # unresolved_types <dir> <entry>
    local lint=/usr/lib/qt6/bin/qmllint
    [ -x "$lint" ] || return 0
    [ -n "${2:-}" ] && [ -f "$1/$2" ] || return 0
    [ -d "$SHELL_ROOT" ] || return 0

    local t m
    t=$(mktemp -d) || return 0
    mkdir -p "$t/qs"
    for m in "$SHELL_ROOT"/*/; do
        [ -f "$m/qmldir" ] && ln -s "$m" "$t/qs/$(basename "${m%/}")"
    done
    "$lint" -I "$t" "$1/$2" 2>&1 |
        sed -n 's/.*: \([A-Za-z0-9_]*\) was not found\..*/\1/p' |
        sort -u | tr '\n' ' '
    rm -rf "$t"
}

# ── Fetching a repository that IS one plugin ────────────────────────────────
#
# The registry's rows are all `root-plugin`: manifest.json at the repository
# root, which is the shape `omarchy plugin add <git-url>` installs and the shape
# `synui-plugins add <git-url>` already handled. This is that same clone, given
# a name instead of asked to guess one from the URL.
#
# 0 cloned, 2 cloned but no manifest (a listing pointing at something that is
# not a plugin), 1 the clone itself failed.
clone_plugin() {  # clone_plugin <repo> <ref> <dest>
    local repo=$1 ref=$2 dest=$3
    mkdir -p "$(dirname "$dest")"
    # --depth 1: a plugin is a few QML files and nobody wants its history.
    if [ -n "$ref" ]; then
        git clone --depth 1 --branch "$ref" -- "$repo" "$dest" >/dev/null 2>&1 \
            || { rm -rf "$dest"; return 1; }
    else
        # No ref: the repository's default branch. A listing pins the commit it
        # validated, but a shallow clone of a whole plugin wants the branch its
        # author is shipping from.
        git clone --depth 1 -- "$repo" "$dest" >/dev/null 2>&1 \
            || { rm -rf "$dest"; return 1; }
    fi
    [ -f "$dest/manifest.json" ] || { rm -rf "$dest"; return 2; }
    return 0
}

case "${1:-list}" in
    -h|--help|help) usage ;;
    scan) scan ;;
    catalogue)
        # ⚠ The window's half of `browse`. Separate rather than a --tsv flag on
        # it for the reason `scan` is separate from `list`: one is laid out for
        # a person and the other is parsed, and a formatter that has to do both
        # ends up doing neither well.
        # ⚠ AND IT FETCHES. The window calls this the moment it opens, so a
        # first run with no cache would draw the shipped rows alone and look
        # exactly like the bug this replaced — a browser showing five widgets.
        # Not fatal: with no network the shipped rows still print, and the
        # window's own header says how many rows it got.
        registry_ready "${2:-}" || true
        printf 'id\tname\tdescription\tinstalled\tenabled\tcategory\ttags\tauthor\tstars\ttrust\n'
        # ⚠ ONE scan, JOINED — not a scan per row. See scan_once.
        catalogue_rows | awk -F'\t' -v OFS='\t' '
            NR == FNR { if (FNR > 1 && $1 != "") { inst[$1] = 1; en[$1] = $6 }; next }
            { i = ($1 in inst) ? 1 : 0
              print $1, $2, $3, i, (i ? en[$1] : "off"), $8, $9, $10, $11, $12 }
        ' <(scan_once) -
        ;;
    check)
        # What `add` asks, asked again — for the widgets that went on before
        # anything asked, and for anything installed by hand or by Omarchy's own
        # command. Read-only.
        cid=${2:-}
        scan_once | tail -n +2 | while IFS= read -r line; do
            rid=$(fld "$line" 1); rdir=$(fld "$line" 4); rentry=$(fld "$line" 5)
            [ -n "$cid" ] && [ "$cid" != "$rid" ] && continue
            rwhy=$(fld "$line" 7)
            if [ -n "$rwhy" ]; then
                printf '  %-30s unsupported — %s\n' "$rid" "$rwhy"
                continue
            fi
            rmiss=$(unresolved_types "$rdir" "$rentry")
            if [ -n "$rmiss" ]; then
                printf '  %-30s will not draw — needs %s\n' "$rid" "$rmiss"
            else
                printf '  %-30s ok\n' "$rid"
            fi
        done
        ;;
    refresh)
        # The registry, fetched now rather than whenever it next ages out. What
        # the window's Refresh button runs, and the answer to "it is not in the
        # list yet" for a widget published this morning.
        printf 'fetching %s…\n' "$REGISTRY_URL"
        registry_refresh || exit 1
        printf '%s widget(s) listed · %s\n' \
               "$(grep -cv '^[[:space:]]*\(#\|$\)' "$REGISTRY")" "$REGISTRY"
        ;;
    tui)
        # ⚠ NEEDS A TERMINAL, AND SAYS SO. Piped or run from a launcher there is
        # no tty to read a keypress from, and a UI that silently does nothing is
        # worse than one that refuses — `list` is the answer for a pipe.
        [ -t 0 ] && [ -t 1 ] || {
            printf 'synui-plugins: tui needs a terminal — try `list`\n' >&2; exit 2; }

        registry_ready || true

        tui_rows=""
        tui_filter=""
        # ⚠ FOUR FIELDS, AND THE FOURTH IS NEVER DRAWN. The list is hundreds of
        # rows long, so it needs a search — and a search over what is on the
        # screen would not find the two dozen games, whose category is "Widgets"
        # and whose only mention of the word is a tag. The haystack carries the
        # description, the category, the tags and the author; the row shows the
        # three columns that fit.
        tui_load() {
            scan_drop
            tui_rows=$(
                scan_once | tail -n +2 | awk -F'\t' '
                    { name = ($2 != "") ? $2 : $1
                      st   = ($7 != "") ? "unsupported" : $6
                      print $1 "\t" name "\t" st "\t" $1 " " name " " $3 }' 
                catalogue_rows | awk -F'\t' '
                    NR == FNR { if (FNR > 1 && $1 != "") inst[$1] = 1; next }
                    !($1 in inst) {
                        print $1 "\t" $2 "\tavailable\t" $1 " " $2 " " $3 " " $8 " " $9 " " $10
                    }
                ' <(scan_once) -
            )
            # ⚠ A SUBSTRING, NEVER A PATTERN. The filter is typed by a person
            # and `grep` would read a stray `[` in it as a broken bracket
            # expression and print an error over the alternate screen.
            [ -n "$tui_filter" ] && tui_rows=$(printf '%s\n' "$tui_rows" |
                awk -v t="$tui_filter" 'BEGIN { t = tolower(t) }
                                        index(tolower($0), t)')
            [ -n "$tui_rows" ] || tui_rows=$(printf '\t(nothing matches %s)\t\t' "$tui_filter")
        }

        sel=1
        tui_load
        # ⛔ THE CURSOR AND THE ALTERNATE SCREEN COME BACK ON EVERY EXIT PATH,
        # including Ctrl+C. A TUI that dies with the cursor hidden leaves the
        # terminal it was run from unusable, and the person who has to fix it
        # cannot see what they are typing.
        cleanup_tui() { printf '\033[?25h\033[?1049l'; stty echo 2>/dev/null; }
        trap 'cleanup_tui; exit 0' INT TERM
        printf '\033[?1049h\033[?25l'

        while :; do
            n=$(printf '%s\n' "$tui_rows" | grep -c . )
            [ "$sel" -lt 1 ] && sel=1
            [ "$sel" -gt "$n" ] && sel=$n
            # ⚠ A PAGE, NOT THE WHOLE LIST. Hundreds of rows printed into a
            # 24-line terminal is 24 rows of the end of the list and a cursor
            # nobody can find. The window follows the selection.
            page=$(( $(tput lines 2>/dev/null || printf 24) - 8 ))
            [ "$page" -lt 5 ] && page=5
            first=$(( ((sel - 1) / page) * page + 1 ))
            printf '\033[H\033[2J'
            printf '  BAR PLUGINS\n'
            if [ -n "$tui_filter" ]; then
                printf '  %s — %s of %s matching \047%s\047\n\n' \
                       "widgets for the bar, in Omarchy's format" "$sel" "$n" "$tui_filter"
            else
                printf '  %s — %s of %s\n\n' \
                       "widgets for the bar, in Omarchy's format" "$sel" "$n"
            fi
            i=$((first - 1))
            printf '%s\n' "$tui_rows" | sed -n "${first},$((first + page - 1))p" |
            while IFS=$'\t' read -r id name st hay; do
                i=$((i + 1))
                if [ "$i" = "$sel" ]; then mark='>'; else mark=' '; fi
                printf '  %s %-30s %-24s %s\n' "$mark" "$id" "$name" "$st"
            done
            printf '\n  \047 \047 toggle/install   / search   r remove   g refresh   q quit\n'

            # One keypress. An escape sequence arrives as three bytes and the
            # two after ESC are read without a timeout only because a bare ESC
            # is not a key this offers.
            IFS= read -rsn1 k 2>/dev/null || { cleanup_tui; exit 0; }
            case "$k" in
                $'\033') IFS= read -rsn2 -t 0.1 k2 2>/dev/null
                        case "$k2" in
                            '[A') sel=$((sel - 1)) ;;
                            '[B') sel=$((sel + 1)) ;;
                        esac ;;
                k) sel=$((sel - 1)) ;;
                j) sel=$((sel + 1)) ;;
                q) cleanup_tui; exit 0 ;;
                g) cleanup_tui
                   printf 'fetching %s…\n' "$REGISTRY_URL"
                   registry_ready force || true
                   printf '\033[?1049h\033[?25l'
                   sel=1; tui_load ;;
                /) cleanup_tui
                   printf 'search (blank shows everything): '
                   IFS= read -r tui_filter
                   printf '\033[?1049h\033[?25l'
                   sel=1; tui_load ;;
                ' '|'')
                    row=$(printf '%s\n' "$tui_rows" | sed -n "${sel}p")
                    rid=$(printf '%s' "$row" | cut -f1)
                    rst=$(printf '%s' "$row" | cut -f3)
                    [ -n "$rid" ] || continue
                    cleanup_tui
                    case "$rst" in
                        available)   "$0" add "$rid" ;;
                        unsupported) printf '%s cannot be hosted\n' "$rid" ;;
                        *)           "$0" "$rid" toggle ;;
                    esac
                    printf '\npress a key…'; IFS= read -rsn1 _ 2>/dev/null
                    printf '\033[?1049h\033[?25l'
                    tui_load ;;
                r)
                    row=$(printf '%s\n' "$tui_rows" | sed -n "${sel}p")
                    rid=$(printf '%s' "$row" | cut -f1)
                    [ -n "$rid" ] || continue
                    cleanup_tui
                    "$0" remove "$rid"
                    printf '\npress a key…'; IFS= read -rsn1 _ 2>/dev/null
                    printf '\033[?1049h\033[?25l'
                    tui_load ;;
            esac
        done
        ;;
    gui)
        command -v quickshell >/dev/null 2>&1 || {
            printf 'synui-plugins: quickshell is not installed\n' >&2; exit 2; }
        qml=/usr/share/synui/plugins-gui.qml
        [ -r "$qml" ] || qml="$(dirname "$0")/../data/plugins-gui.qml"
        [ -r "$qml" ] || { printf 'synui-plugins: %s is missing\n' "$qml" >&2; exit 1; }
        # ⚠ QS_APP_ID, OR THE WINDOW WEARS SOMEBODY ELSE'S IDENTITY. Every one of
        # these apps is a quickshell app that hands its whole environment to what
        # it spawns, so a window opened from another one inherits that one's id
        # and gets no dock entry of its own. Same call synpkg's gui makes.
        exec env QS_APP_ID=synui-plugins quickshell -p "$qml"
        ;;
    browse)
        # ⚠ A LIST THIS LONG NEEDS A SEARCH AND A PAGE. There are hundreds of
        # community bar widgets; dumping every one is not a browser, it is a
        # wall. Words narrow it — ALL of them have to match, so `browse game
        # snake` is an intersection and not a pile — and --all is there for a
        # pipe, which is the one caller that wants the wall.
        shift
        browse_force=""; browse_all=""; browse_terms=""
        while [ $# -gt 0 ]; do
            case "$1" in
                --refresh) browse_force=force ;;
                --all)     browse_all=1 ;;
                --)        shift; browse_terms="$browse_terms $*"; break ;;
                -*)        printf 'synui-plugins browse: unknown option %s\n' "$1" >&2
                           exit 2 ;;
                *)         browse_terms="$browse_terms $1" ;;
            esac
            shift
        done
        browse_terms=${browse_terms# }

        browse_live=1
        registry_ready "$browse_force" || browse_live=0

        [ -r "$CATALOGUE" ] || [ -r "$REGISTRY" ] || {
            printf 'synui-plugins: no catalogue at %s and no registry at %s\n' \
                   "$CATALOGUE" "$REGISTRY" >&2; exit 1; }

        browse_limit=${SYNUI_PLUGIN_BROWSE_LIMIT:-40}
        [ -n "$browse_all" ] && browse_limit=0

        printf '  %-36s %-24s %-6s %s\n' "ID" "NAME" "STARS" "STATE"
        catalogue_rows | awk -F'\t' -v term="$browse_terms" -v limit="$browse_limit" '
            function cut(v, n) { return length(v) > n ? substr(v, 1, n - 1) "…" : v }
            NR == FNR {
                if (FNR > 1 && $1 != "") { inst[$1] = 1; en[$1] = $6; why[$1] = $7 }
                next
            }
            {
                # Searched across id, name, description, category, tags and
                # author — the tags are why `browse games` finds two dozen
                # widgets whose category is "Widgets".
                hay = tolower($1 " " $2 " " $3 " " $8 " " $9 " " $10)
                if (term != "") {
                    n = split(tolower(term), t, /[ ,]+/)
                    for (k = 1; k <= n; k++)
                        if (t[k] != "" && index(hay, t[k]) == 0) next
                }
                total++
                if (limit > 0 && total > limit) next

                state = "—"
                if ($1 in inst) state = (why[$1] != "") ? "unsupported" : en[$1]
                stars = ($11 != "" && $11 + 0 > 0) ? "\u2605 " $11 : ""

                # ⛔ THE ID IS NEVER TRUNCATED. It is the argument to `add`, so
                # an id with an ellipsis in it is a row nobody can install —
                # and the long ones are exactly the reverse-DNS names people
                # cannot retype from memory. A long id pushes the columns right
                # on its own line instead; nothing after it is load-bearing.
                tail = ""
                if ($8  != "") tail = tail " · " $8
                if ($10 != "") tail = tail " · " $10
                if ($12 != "" && $12 != "shipped") tail = tail " · " $12
                # The description gives way to the tail, not the other way
                # round: the category, the author and whether anybody has
                # vouched for it are what a chooser is choosing on.
                room = 74 - length(tail)
                if (room < 24) room = 24
                printf "  %-36s %-24s %-6s %s\n", $1, cut($2, 24), stars, state
                printf "  %-36s %s\n", "", cut($3, room) tail
            }
            END {
                shown = (limit > 0 && total > limit) ? limit : total
                printf "\n  %d shown", shown
                if (shown != total) printf " of %d — --all for the rest", total
                printf "\n"
            }
        ' <(scan_once) -

        if [ "$browse_live" = 0 ]; then
            printf '  (the community registry could not be reached — these are the shipped rows)\n'
        fi
        printf '\n  synui-plugins browse games   narrow it: every word has to match\n'
        printf '  synui-plugins add <id>       install one of them\n'
        printf '  synui-plugins add <git-url>  install a plugin repository\n'
        printf '  synui-plugins refresh        fetch the list again now\n'
        ;;
    add)
        url=${2:-}
        [ -n "$url" ] || { printf 'synui-plugins add: need a git URL or a catalogue id\n' >&2; exit 2; }
        command -v git >/dev/null 2>&1 || {
            printf 'synui-plugins: git is not installed\n' >&2; exit 2; }

        # A catalogue id rather than a URL: one widget out of a repository that
        # holds many. Tried FIRST, because an id is unambiguous — it has no
        # scheme and no slash, so nothing that is a URL can be mistaken for one.
        if [ "${url#*/}" = "$url" ]; then
            registry_ready || true
            row=$(catalogue_rows | awk -F'\t' -v i="$url" '$1==i {print; exit}')
            if [ -n "$row" ]; then
                cid=$(fld "$row" 1); cname=$(fld "$row" 2)
                crepo=$(fld "$row" 4); cref=$(fld "$row" 5)
                cpath=$(fld "$row" 6); cbase=$(fld "$row" 7)
                [ -n "$(installed_id "$cid")" ] && {
                    printf 'synui-plugins: %s is already installed\n' "$cid" >&2; exit 1; }
                # ⛔ THE WHOLE ID NAMES THE DIRECTORY, NOT ITS LAST DOTTED PART.
                # Two people have published a widget called Snake; `${cid##*.}`
                # would name both directories `snake` and the second install
                # would collide with the first for no reason its owner could
                # see. Non-path characters become underscores; the id itself is
                # already checked against a pattern before it is ever listed.
                dest="$MINE/$(printf '%s' "$cid" | tr -c 'A-Za-z0-9._-' '_')"
                [ -e "$dest" ] && { printf 'synui-plugins: %s already exists\n' "$dest" >&2; exit 1; }

                if [ -n "$cpath" ]; then
                    # A shipped row: one widget out of a repository of many.
                    printf 'fetching %s from %s…\n' "$cbase" "$crepo"
                    fetch_subpath "$crepo" "$cref" "$cpath" "$cbase" "$dest" || {
                        printf 'synui-plugins: could not fetch %s\n' "$cid" >&2; exit 1; }
                else
                    # A registry row: the repository IS the plugin, so this is
                    # the clone `add <git-url>` does — the id only chose the URL.
                    printf 'cloning %s…\n' "$crepo"
                    clone_plugin "$crepo" "$cref" "$dest"
                    case $? in
                        0) ;;
                        2) printf 'synui-plugins: %s has no manifest.json — the listing is wrong about %s\n' \
                                  "$crepo" "$cid" >&2; exit 1 ;;
                        *) printf 'synui-plugins: could not clone %s\n' "$crepo" >&2; exit 1 ;;
                    esac
                fi

                # ⚠ THE ID THE MANIFEST CLAIMS, NOT THE ONE THE LISTING USED.
                # A registry id is somebody's listing key and the two do drift;
                # the bar keys plugins.state on what is inside the directory, so
                # writing the listing's id would leave `on`, `off` and `remove`
                # all pointing at a plugin that is not there.
                mid=$(jfield "$dest/manifest.json" id)
                [ -n "$mid" ] || mid=$cid
                [ "$mid" = "$cid" ] || printf '  (its manifest calls itself %s)\n' "$mid"

                scan_drop
                why=$(scan_once | awk -F'\t' -v i="$mid" '$1==i {print $7}' | head -1)
                if [ -n "$why" ]; then
                    printf 'installed %s, but it cannot be hosted — %s\n' "$mid" "$why" >&2
                    exit 2
                fi
                miss=$(unresolved_types "$dest" "$(jfield "$dest/manifest.json" barWidget)")
                set_state "$mid" on
                if [ -n "$miss" ]; then
                    # ⛔ EXIT 3, NOT 0. It IS installed and it IS on — but a
                    # window that only shows a message when the exit code is
                    # non-zero showed nothing at all here, so three widgets went
                    # on and never appeared and the browser said "installed and
                    # on" three times. A degraded install is not a clean one and
                    # must not report as one. 3 rather than 1 so a script can
                    # still tell it from "did not install".
                    printf '%s needs types this bar has not got: %s\n' "$mid" "$miss" >&2
                    printf '  installed and on, but it will not draw. See the README.\n' >&2
                    exit 3
                fi
                printf '%s: installed and on\n' "$mid"
                exit 0
            fi
            printf 'synui-plugins: no catalogue entry %s, and it is not a URL\n' "$url" >&2
            printf '  `synui-plugins browse %s` searches for it\n' "$url" >&2
            exit 1
        fi

        # The directory name. Given, or the URL's last path element with a
        # trailing .git dropped — which is what `git clone` would have picked.
        name=${3:-}
        if [ -z "$name" ]; then
            name=${url##*/}; name=${name%.git}
        fi
        # ⛔ ONE PATH ELEMENT, ALWAYS. The name reaches a path, and a URL ending
        # in `../../.local/bin` would otherwise clone over something else. A
        # name with a slash in it is refused rather than sanitised: quietly
        # rewriting somebody's argument is how you install the wrong thing.
        case "$name" in
            */*|.|..|"") printf 'synui-plugins: bad plugin name %s\n' "$name" >&2; exit 2 ;;
        esac

        dest="$MINE/$name"
        [ -e "$dest" ] && { printf 'synui-plugins: %s already exists\n' "$dest" >&2
                            printf '  remove it first: synui-plugins remove <id>\n' >&2
                            exit 1; }
        mkdir -p "$MINE"
        # --depth 1: a plugin is a few QML files and nobody wants its history.
        git clone --depth 1 -- "$url" "$dest" || {
            rm -rf "$dest"; printf 'synui-plugins: clone failed\n' >&2; exit 1; }

        [ -f "$dest/manifest.json" ] || {
            rm -rf "$dest"
            printf 'synui-plugins: %s has no manifest.json — not a plugin\n' "$url" >&2
            printf '  the format is documented in %s\n' \
                   "/usr/share/synui/plugins/README.md" >&2
            exit 1; }

        id=$(jfield "$dest/manifest.json" id)
        [ -n "$id" ] || { rm -rf "$dest"
                          printf 'synui-plugins: manifest has no id\n' >&2; exit 1; }

        scan_drop
        why=$(scan_once | awk -F'\t' -v i="$id" '$1==i {print $7}' | head -1)
        if [ -n "$why" ]; then
            # ⚠ KEPT, NOT DELETED. It is on disk and named, so `list` can say
            # why it cannot run — and a plugin refused today may be hostable
            # after a synui that provides what it wants. Throwing it away would
            # make the reason unreadable.
            printf 'installed %s, but it cannot be hosted — %s\n' "$id" "$why" >&2
            exit 2
        fi
        miss=$(unresolved_types "$dest" "$(jfield "$dest/manifest.json" barWidget)")
        set_state "$id" on
        if [ -n "$miss" ]; then
            # See the note on the same check above: exit 3, so the window has
            # something to show.
            printf '%s needs types this bar has not got: %s\n' "$id" "$miss" >&2
            printf '  installed and on, but it will not draw. See the README.\n' >&2
            exit 3
        fi
        printf '%s: installed and on\n' "$id"
        ;;
    remove)
        id=${2:-}
        [ -n "$id" ] || { printf 'synui-plugins remove: need a plugin id\n' >&2; exit 2; }
        dir=$(scan_once | awk -F'\t' -v i="$id" '$1==i {print $4}' | head -1)
        [ -n "$dir" ] || { printf 'synui-plugins: no plugin with id %s\n' "$id" >&2; exit 1; }
        # ⛔ ONLY OUT OF OUR OWN DIRECTORY. The other two are Omarchy's (theirs
        # to manage, with their own command) and the package's (pacman's). A
        # remove that reached either would delete a file somebody else believes
        # they own, and on the packaged one the next upgrade would put it back.
        case "$dir" in
            "$MINE"/*) ;;
            *) printf 'synui-plugins: %s is not one you installed (%s)\n' "$id" "$dir" >&2
               printf '  turn it off instead: synui-plugins %s off\n' "$id" >&2
               exit 2 ;;
        esac
        rm -rf "$dir"
        set_state "$id" off
        printf '%s: removed\n' "$id"
        ;;
    list)
        # ⚠ awk, NOT `read` — see fld(). A manifest with no description is
        # legal, and a `read` over these fields would shift every column after
        # it left and print the plugin's directory where its description goes.
        scan_once | tail -n +2 | awk -F'\t' '
            $7 != "" { printf "  %-28s unsupported — %s\n", $1, $7; next }
                      { printf "  %-28s %-4s %s\n", $1, $6, $3 }' 
        [ -r "$STATE" ] || printf '\n  (nothing enabled yet — synui-plugins <id> on)\n'
        ;;
    *)
        id=$1
        want=${2:-toggle}
        # ⛔ REFUSED RATHER THAN ENABLED. Writing `on` for a plugin the bar will
        # not host leaves a state file claiming something that never appears,
        # which is the failure this whole command exists to make impossible.
        why=$(scan_once | awk -F'\t' -v i="$id" '$1==i {print $7}' | head -1)
        found=$(scan_once | awk -F'\t' -v i="$id" '$1==i {print $1}' | head -1)
        if [ -z "$found" ]; then
            printf 'synui-plugins: no plugin with id %s\n' "$id" >&2
            printf 'synui-plugins: `synui-plugins list` shows what is installed\n' >&2
            exit 1
        fi
        if [ -n "$why" ]; then
            printf 'synui-plugins: %s cannot be hosted — %s\n' "$id" "$why" >&2
            exit 2
        fi
        case "$want" in
            on|off) set_state "$id" "$want" ;;
            toggle) if state_on "$id"; then set_state "$id" off; want=off
                    else set_state "$id" on; want=on; fi ;;
            *) printf 'synui-plugins: on, off or toggle — not %s\n' "$want" >&2; exit 2 ;;
        esac
        printf '%s: %s\n' "$id" "$want"
        ;;
esac
