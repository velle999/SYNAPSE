#!/usr/bin/env bash
#
# arsenal-query — the one data source behind both SYNAPSE Arsenal front-ends
# (the quickshell GUI and `syn arsenal`). Everything either of them knows about
# BlackArch comes through here, so the two can never drift apart.
#
# Output is TSV, not JSON, on purpose. Nothing in a base SynapseOS install can
# parse JSON (no jq, no python guarantee for a shell caller), and hand-rolling
# JSON escaping in shell is how you ship a UI that breaks on the first package
# description containing a quote. QML splits TSV in two lines of JavaScript.
#
# Tabs are stripped from every field before emission for the same reason: a
# stray tab in a description would silently shift every later column.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/syn-arsenal"

# A test fixture stands in for the sync database so the front-ends can be
# exercised on a machine where BlackArch is not enabled. It is a directory of
# the same TSV this script emits: categories.tsv, packages.<group>.tsv.
FIXTURE="${SYN_ARSENAL_FIXTURE:-}"

die() { echo "arsenal-query: $*" >&2; exit 1; }

# ── Is the repo actually there? ─────────────────────────────────────────────
# Distinguishes "no BlackArch" from "BlackArch with nothing in it": the second
# means a configured repo that has never been synced, and the fix for that
# (pacman -Sy) is not the fix for the first.
repo_enabled() {
    [ -n "$FIXTURE" ] && return 0
    pacman-conf --repo-list 2>/dev/null | grep -qx blackarch
}

cmd_status() {
    if [ -n "$FIXTURE" ]; then
        printf 'enabled\t%s\tfixture\n' "$(wc -l < "$FIXTURE/categories.tsv" 2>/dev/null || echo 0)"
        return 0
    fi
    if ! repo_enabled; then
        printf 'disabled\t0\t\n'
        return 0
    fi
    local n
    n=$(pacman -Sl blackarch 2>/dev/null | wc -l)
    if [ "$n" -eq 0 ]; then
        printf 'unsynced\t0\t\n'
        return 0
    fi
    # The keyring PACKAGE being absent is not cosmetic: strap.sh imports the
    # signing keys into pacman's keyring directly, so installs keep working and
    # nothing looks wrong — but key ROTATIONS only ever arrive as an upgrade of
    # blackarch-keyring. Without it the repo silently ages out of trust.
    local keyring=ok
    pacman -Q blackarch-keyring >/dev/null 2>&1 || keyring=missing
    printf 'enabled\t%s\t%s\n' "$n" "$keyring"
}

# ── Categories ──────────────────────────────────────────────────────────────
# `pacman -Sg` with no argument lists every group name; passing many groups at
# once returns "<group> <pkg>" for all of them in ONE call, which is what makes
# a 53-category count cheap enough to do eagerly (one pacman invocation, not 53).
#
# The bare "blackarch" group is filtered out deliberately — it is the union of
# every other group (~5000 packages) and listing it alongside the real
# categories invites a click that renders the entire repo into one pane.
cmd_categories() {
    if [ -n "$FIXTURE" ]; then cat "$FIXTURE/categories.tsv"; return 0; fi
    repo_enabled || return 0

    local cache="$CACHE/categories.tsv" groups
    if [ -f "$cache" ] && [ -z "${SYN_ARSENAL_NOCACHE:-}" ] \
       && [ "$cache" -nt /var/lib/pacman/sync/blackarch.db ]; then
        cat "$cache"; return 0
    fi

    mapfile -t groups < <(pacman -Sg 2>/dev/null | grep '^blackarch-')
    [ "${#groups[@]}" -eq 0 ] && return 0

    mkdir -p "$CACHE" 2>/dev/null
    pacman -Sg "${groups[@]}" 2>/dev/null \
        | awk '{ c[$1]++ } END { for (g in c) printf "%s\t%d\n", g, c[g] }' \
        | sort > "$cache.tmp" && mv -f "$cache.tmp" "$cache"
    cat "$cache"
}

# ── Packages in one category ────────────────────────────────────────────────
# Descriptions come from a single batched `pacman -Si` over just this group's
# members. Per-package -Si would be one process per row; batched it is one call
# for the whole pane, off the local sync db.
cmd_packages() {
    local group="${1:-}"
    [ -n "$group" ] || die "packages: need a group"
    case "$group" in blackarch-*) ;; *) die "packages: '$group' is not a blackarch group" ;; esac

    if [ -n "$FIXTURE" ]; then cat "$FIXTURE/packages.$group.tsv" 2>/dev/null; return 0; fi
    repo_enabled || return 0

    local members
    mapfile -t members < <(pacman -Sg "$group" 2>/dev/null | awk '{print $2}' | sort -u)
    [ "${#members[@]}" -eq 0 ] && return 0

    # One pass for what is installed; a set lookup beats `pacman -Q` per row.
    local installed
    installed=$(pacman -Qq 2>/dev/null)

    pacman -Si "${members[@]}" 2>/dev/null | awk -v inst="$installed" '
        BEGIN {
            FS = " *: "
            n = split(inst, a, "\n"); for (i = 1; i <= n; i++) have[a[i]] = 1
        }
        /^Name +:/        { name = $2 }
        /^Version +:/     { ver  = $2 }
        /^Description +:/ {
            desc = $2
            gsub(/\t/, " ", desc); gsub(/\t/, " ", name); gsub(/\t/, " ", ver)
            printf "%s\t%d\t%s\t%s\n", name, (name in have) ? 1 : 0, ver, desc
        }
    ' | sort
}

# ── Mutations ───────────────────────────────────────────────────────────────
# pkexec rather than sudo: the GUI has no terminal to prompt in, and polkit is
# already how the rest of the desktop escalates (synui-iso-mount, syn-model).
# --needed so re-installing an installed package is a no-op rather than a
# pointless reinstall.
cmd_install() {
    local pkg="${1:-}"; [ -n "$pkg" ] || die "install: need a package"
    pkexec pacman -S --noconfirm --needed -- "$pkg"
}

cmd_remove() {
    local pkg="${1:-}"; [ -n "$pkg" ] || die "remove: need a package"
    pkexec pacman -Rns --noconfirm -- "$pkg"
}

case "${1:-}" in
    status)     cmd_status ;;
    categories) cmd_categories ;;
    packages)   shift; cmd_packages "$@" ;;
    install)    shift; cmd_install "$@" ;;
    remove)     shift; cmd_remove "$@" ;;
    *)
        cat >&2 <<EOF
Usage: arsenal-query <command>

  status              enabled|unsynced|disabled <count> <keyring-state>
  categories          <group>\t<count>
  packages <group>    <pkg>\t<installed 0|1>\t<version>\t<description>
  install <pkg>       via pkexec
  remove <pkg>        via pkexec
EOF
        exit 2 ;;
esac
