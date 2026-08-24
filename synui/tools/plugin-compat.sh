#!/usr/bin/env bash
#
# plugin-compat.sh — how much of the community bar-widget corpus this desktop
# can actually host, as a number.
#
# ⛔ NOT A TEST, AND DELIBERATELY NOT IN `meson test`. It clones widgets off the
# network, which is exactly what a build must never do. It is the measurement
# that decides what to BUILD next: run it, read which type blocks the most
# widgets, implement that one, run it again.
#
# That loop is how quickshell/Ui got from 2 types to what it has now:
#
#     9 of 40 entry points resolving   ->  the module was BarWidget + WidgetButton
#    18 of 40                          ->  + BarIconButton      (21 widgets named it)
#    22 of 40                          ->  + the Panel layer    (KeyboardPanel, 22)
#    32 of 40                          ->  + Button and the toggles
#    40 of 40                          ->  + Dropdown and the fields
#
# ⚠ "RESOLVES" IS NOT "WORKS". This asks whether every type a widget names
# exists, which is a question about the module and not about behaviour — a widget
# can resolve completely and still want a host service synui has no counterpart
# for (a bar `shell.serviceFor()`, a Hyprland socket). It is the cheapest useful
# question, not the last one. plugin_load.sh is the one that starts a real bar.
#
# Usage:
#   tools/plugin-compat.sh [count]      # default 40, most-installed first
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
TREE="$HERE/../quickshell"
COUNT=${1:-40}

LINT=/usr/lib/qt6/bin/qmllint
[ -x "$LINT" ] || { echo "Qt 6's qmllint is not installed (/usr/lib/qt6/bin)." >&2; exit 1; }
command -v git >/dev/null 2>&1 || { echo "git is not installed." >&2; exit 1; }

# The same list `synui-plugins browse` reads, refreshed so the sample is what is
# actually being installed rather than whatever was cached months ago.
REG="${XDG_CACHE_HOME:-$HOME/.cache}/synui/plugins/registry.tsv"
"$HERE/../systemd/synui-plugins.sh" refresh >/dev/null 2>&1 || true
[ -r "$REG" ] || { echo "no registry at $REG — run: synui-plugins refresh" >&2; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/qs"
for m in "$TREE"/*/; do
    [ -f "$m/qmldir" ] && ln -s "$m" "$WORK/qs/$(basename "${m%/}")"
done

echo "cloning $COUNT widgets…"
n=0
while IFS=$'\t' read -r id name desc repo rest; do
    case "$id" in ''|'#'*) continue ;; esac
    n=$((n + 1))
    [ "$n" -gt "$COUNT" ] && break
    # Backgrounded: forty shallow clones in series is minutes of waiting for a
    # number. --filter=blob:none keeps each one to the files it actually needs.
    ( git clone -q --depth 1 --filter=blob:none "$repo" "$WORK/$id" >/dev/null 2>&1 & )
done < "$REG"
wait 2>/dev/null
sleep 5

total=0; clean=0
: > "$WORK/missing"
for d in "$WORK"/*/; do
    [ -f "$d/manifest.json" ] || continue
    total=$((total + 1))
    miss=$(for f in "$d"*.qml; do
               [ -f "$f" ] && "$LINT" -I "$WORK" "$f" 2>&1
           done | sed -n 's/.*: \([A-Za-z0-9_]*\) was not found\..*/\1/p' | sort -u)
    if [ -z "$miss" ]; then
        clean=$((clean + 1))
    else
        printf '%s\n' "$miss" >> "$WORK/missing"
        printf '  %-40s %s\n' "$(basename "$d")" "$(printf '%s' "$miss" | tr '\n' ' ')"
    fi
done

printf '\n  %d of %d widgets resolve completely\n\n' "$clean" "$total"
echo "  types still blocking, by how many widgets name them:"
sort "$WORK/missing" | grep -v '^$' | uniq -c | sort -rn | head -20
