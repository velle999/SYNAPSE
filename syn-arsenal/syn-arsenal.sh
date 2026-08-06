#!/usr/bin/env bash
#
# syn-arsenal — SYNAPSE Arsenal: browse and install BlackArch security tooling.
#
# Default is the GUI. `--tui` is the same data in a terminal (what `syn arsenal`
# runs), for an SSH session or a box with no desktop up.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

LIBDIR=/usr/lib/syn-arsenal
QML=/usr/share/syn-arsenal/arsenal.qml

# Run-from-source: everything resolves next to this script when it has not been
# installed, so the app is testable without packaging it first.
if [ ! -f "$QML" ]; then
    here="$(cd "$(dirname "$0")" && pwd)"
    [ -f "$here/arsenal.qml" ] && QML="$here/arsenal.qml"
    [ -x "$here/arsenal-query.sh" ] && LIBDIR="$here"
fi

export SYN_ARSENAL_QUERY="${SYN_ARSENAL_QUERY:-$LIBDIR/arsenal-query}"
[ -x "$SYN_ARSENAL_QUERY" ] || SYN_ARSENAL_QUERY="$LIBDIR/arsenal-query.sh"
export SYN_ARSENAL_QUERY

tui() {
    local t="$LIBDIR/arsenal-tui"
    [ -x "$t" ] || t="$LIBDIR/arsenal-tui.sh"
    exec "$t" "$@"
}

case "${1:-}" in
    --tui)         shift; tui "$@" ;;
    --enable-repo) shift
                   e="$LIBDIR/arsenal-enable-repo"
                   [ -x "$e" ] || e="$LIBDIR/arsenal-enable-repo.sh"
                   exec "$e" "$@" ;;
    -h|--help)
        cat <<EOF
Usage: syn-arsenal [--tui] [--enable-repo]

Browse BlackArch security tooling by category and install it.

  (no args)       open the graphical browser
  --tui           browse in the terminal instead (same as 'syn arsenal')
  --enable-repo   add the BlackArch repository (needs root)
EOF
        exit 0 ;;
esac

# No desktop to draw on — fall through to the terminal UI rather than failing
# with a Wayland error the user cannot act on.
if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${DISPLAY:-}" ]; then
    echo "syn-arsenal: no display — falling back to the terminal browser." >&2
    tui
fi

command -v quickshell >/dev/null 2>&1 \
    || { echo "syn-arsenal: quickshell is not installed; using the terminal browser." >&2; tui; }

exec quickshell -p "$QML"
