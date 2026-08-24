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

  list                  what is installed, and whether each one is on
  scan                  the same, as TSV — what the bar reads
  synui-plugins x on    turn one on
  add <git-url>         clone a plugin into ~/.config/synui/plugins and, if it
                        is hostable, turn it on
  remove <id>           delete one you installed. Only from your own directory

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

# Is this id already on disk, whatever directory it came from?
installed_id() { scan | awk -F'\t' -v i="$1" '$1==i {print $1; exit}'; }

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

case "${1:-list}" in
    -h|--help|help) usage ;;
    scan) scan ;;
    browse)
        [ -r "$CATALOGUE" ] || { printf 'synui-plugins: no catalogue at %s\n' \
                                        "$CATALOGUE" >&2; exit 1; }
        printf '  %-24s %-28s %s\n' "ID" "NAME" "STATE"
        while IFS=$'\t' read -r id name desc repo ref path base; do
            case "$id" in ''|'#'*) continue ;; esac
            if [ -n "$(installed_id "$id")" ]; then st=installed; else st="—"; fi
            printf '  %-24s %-28s %s\n' "$id" "$name" "$st"
            printf '  %-24s %s\n' "" "$desc"
        done < "$CATALOGUE"
        printf '\n  synui-plugins add <id>        install one of these\n'
        printf '  synui-plugins add <git-url>  install a plugin repository\n'
        ;;
    add)
        url=${2:-}
        [ -n "$url" ] || { printf 'synui-plugins add: need a git URL or a catalogue id\n' >&2; exit 2; }
        command -v git >/dev/null 2>&1 || {
            printf 'synui-plugins: git is not installed\n' >&2; exit 2; }

        # A catalogue id rather than a URL: one widget out of a repository that
        # holds many. Tried FIRST, because an id is unambiguous — it has no
        # scheme and no slash, so nothing that is a URL can be mistaken for one.
        if [ "${url#*/}" = "$url" ] && [ -r "$CATALOGUE" ]; then
            row=$(awk -F'\t' -v i="$url" '$1==i {print; exit}' "$CATALOGUE")
            if [ -n "$row" ]; then
                IFS=$'\t' read -r cid cname cdesc crepo cref cpath cbase <<EOFROW
$row
EOFROW
                [ -n "$(installed_id "$cid")" ] && {
                    printf 'synui-plugins: %s is already installed\n' "$cid" >&2; exit 1; }
                dest="$MINE/${cid##*.}"
                [ -e "$dest" ] && { printf 'synui-plugins: %s already exists\n' "$dest" >&2; exit 1; }
                printf 'fetching %s from %s…\n' "$cbase" "$crepo"
                fetch_subpath "$crepo" "$cref" "$cpath" "$cbase" "$dest" || {
                    printf 'synui-plugins: could not fetch %s\n' "$cid" >&2; exit 1; }
                why=$(scan | awk -F'\t' -v i="$cid" '$1==i {print $7}' | head -1)
                if [ -n "$why" ]; then
                    printf 'installed %s, but it cannot be hosted — %s\n' "$cid" "$why" >&2
                    exit 2
                fi
                set_state "$cid" on
                printf '%s: installed and on\n' "$cid"
                exit 0
            fi
            printf 'synui-plugins: no catalogue entry %s, and it is not a URL\n' "$url" >&2
            printf '  `synui-plugins browse` lists what there is\n' >&2
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

        why=$(scan | awk -F'\t' -v i="$id" '$1==i {print $7}' | head -1)
        if [ -n "$why" ]; then
            # ⚠ KEPT, NOT DELETED. It is on disk and named, so `list` can say
            # why it cannot run — and a plugin refused today may be hostable
            # after a synui that provides what it wants. Throwing it away would
            # make the reason unreadable.
            printf 'installed %s, but it cannot be hosted — %s\n' "$id" "$why" >&2
            exit 2
        fi
        set_state "$id" on
        printf '%s: installed and on\n' "$id"
        ;;
    remove)
        id=${2:-}
        [ -n "$id" ] || { printf 'synui-plugins remove: need a plugin id\n' >&2; exit 2; }
        dir=$(scan | awk -F'\t' -v i="$id" '$1==i {print $4}' | head -1)
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
        scan | tail -n +2 | while IFS=$'\t' read -r id name desc dir entry on why; do
            if [ -n "$why" ]; then
                printf '  %-28s unsupported — %s\n' "$id" "$why"
            else
                printf '  %-28s %-4s %s\n' "$id" "$on" "$desc"
            fi
        done
        [ -r "$STATE" ] || printf '\n  (nothing enabled yet — synui-plugins <id> on)\n'
        ;;
    *)
        id=$1
        want=${2:-toggle}
        # ⛔ REFUSED RATHER THAN ENABLED. Writing `on` for a plugin the bar will
        # not host leaves a state file claiming something that never appears,
        # which is the failure this whole command exists to make impossible.
        why=$(scan | awk -F'\t' -v i="$id" '$1==i {print $7}' | head -1)
        found=$(scan | awk -F'\t' -v i="$id" '$1==i {print $1}' | head -1)
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
