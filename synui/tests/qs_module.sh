#!/usr/bin/env bash
#
# qs_module.sh — the qs.Ui / qs.Commons module a bar plugin imports.
#
# ⛔ WHAT THIS GUARDS IS AN ENTIRE CLASS OF SILENT FAILURE. A plugin naming a
# type this bar does not provide fails LOUDLY — quickshell refuses to load it and
# says which name. A plugin reading a PROPERTY that is not there gets
# `undefined`, which lays out as zero, and draws a widget that is present,
# running, enabled and invisible. Nothing logs it. Three community widgets went
# on and never appeared for exactly that reason.
#
# So there are two checks here and neither is about behaviour:
#
#   1. Every file in the module resolves every name it uses. A typo in
#      Style.qml is not a compile error in QML — it is a binding that silently
#      evaluates to undefined for every widget on the desktop.
#   2. qmldir and the directory agree in BOTH directions. A type not listed in
#      qmldir does not exist to an importer however complete the file is, and a
#      qmldir line pointing at a file that was renamed breaks the whole module
#      for every plugin at once.
#
# ⚠ AND IT NEEDS A `qs/` SYMLINK FARM TO ASK THE QUESTION AT ALL. quickshell
# resolves `import qs.Foo` to <shell root>/Foo at runtime, which no import path
# reproduces. Without the farm, qmllint cannot resolve `qs.Ui` and reports every
# type as missing — including in the file that defines it. The tell is that our
# OWN shipped example fails; if that happens, the harness is wrong, not the code.
#
# ⛔ /usr/lib/qt6/bin/qmllint, NEVER /usr/bin/qmllint — the one on PATH belongs
# to another Qt, prints nothing and exits 0. A check that always passes is worse
# than no check.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
TREE=${1:-$HERE/../quickshell}
[ -d "$TREE" ] || { echo "not a directory: $TREE" >&2; exit 1; }

LINT=/usr/lib/qt6/bin/qmllint
[ -x "$LINT" ] || { echo "SKIP: Qt 6's qmllint is not installed."; exit 77; }

pass=0 fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }

FARM=$(mktemp -d)
trap 'rm -rf "$FARM"' EXIT
mkdir -p "$FARM/qs"
for m in "$TREE"/*/; do
    [ -f "$m/qmldir" ] && ln -s "$m" "$FARM/qs/$(basename "${m%/}")"
done

echo "qs module tests — $TREE"

# ── 1. every module file resolves every name it uses ────────────────────────
for mod in Ui Commons; do
    [ -d "$TREE/$mod" ] || continue
    # ⚠ COUNTED, never `| grep -q`: grep -q exits on the first match, the
    # producer takes SIGPIPE, and under pipefail the pipeline reports 141 — a
    # FAILURE on a match. Same trap the plugin and synpkg suites document.
    missing=$("$LINT" -I "$FARM" "$TREE/$mod"/*.qml 2>&1 |
              sed -n 's/.*: \([A-Za-z0-9_]*\) was not found\..*/\1/p' | sort -u | tr '\n' ' ')
    if [ -z "$missing" ]; then
        ok "qs.$mod resolves every type it names"
    else
        bad "qs.$mod names types nothing provides: $missing"
    fi
done

# ── 2. qmldir and the directory agree, both ways ────────────────────────────
for mod in Ui Commons; do
    [ -f "$TREE/$mod/qmldir" ] || continue

    # Listed but absent: the module is broken for every importer at once.
    gone=""
    while read -r file; do
        [ -n "$file" ] || continue
        [ -f "$TREE/$mod/$file" ] || gone="$gone $file"
    done <<LISTED
$(awk '$1 !~ /^#/ && NF >= 2 { print $NF }' "$TREE/$mod/qmldir" | grep '\.qml$')
LISTED
    if [ -z "$gone" ]; then
        ok "every type qs.$mod lists is on disk"
    else
        bad "qs.$mod lists files that are not there:$gone"
    fi

    # ⚠ AND THE OTHER DIRECTION, WHICH IS THE ONE THAT ACTUALLY HAPPENS. A type
    # written, committed and never added to qmldir does not exist to a plugin —
    # the file is perfect and the import fails, which reads as the plugin being
    # wrong.
    unlisted=""
    for f in "$TREE/$mod"/*.qml; do
        base=$(basename "$f")
        grep -q "[[:space:]]$base\$" "$TREE/$mod/qmldir" || unlisted="$unlisted $base"
    done
    if [ -z "$unlisted" ]; then
        ok "every .qml in qs.$mod is listed in its qmldir"
    else
        bad "qs.$mod has files no qmldir line names:$unlisted"
    fi
done

# ── 3. the shipped example still resolves ───────────────────────────────────
#
# The canary for the harness itself. It is a real plugin written against this
# contract, so if IT stops resolving, either the module regressed or the farm
# above is not doing its job — and the second is the one that would otherwise
# make every check here pass for the wrong reason.
EX="$HERE/../data/plugins/synapse.uptime/Uptime.qml"
if [ -f "$EX" ]; then
    m=$("$LINT" -I "$FARM" "$EX" 2>&1 |
        sed -n 's/.*: \([A-Za-z0-9_]*\) was not found\..*/\1/p' | sort -u | tr '\n' ' ')
    [ -z "$m" ] && ok "the shipped example plugin resolves against the module" \
                || bad "the shipped example plugin does not resolve: $m"
fi

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ]
