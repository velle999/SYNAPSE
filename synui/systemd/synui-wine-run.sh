#!/usr/bin/env bash
# synui-wine-run — Dolphin right-click "Run with Wine" for .exe/.msi/.bat.
#
# wine.desktop already registers these mimetypes (Exec=wine start /unix %f), so
# a double-click runs an exe once wine is installed. This adds the *explicit*
# top-level context-menu entry — the "ready for exes" part — plus a terminal
# variant that keeps wine's console output on screen, which is how we actually
# debug the old Windows games (see the gaming notes).
#
# Run direct (wine "$base") rather than `wine start /unix`, from the exe's own
# directory: many old games look for their data files relative to the exe, and
# `start` detaches before we can hold a terminal open on a crash.
#
# Usage: synui-wine-run run|term FILE
set -euo pipefail

mode=${1:-run}
file=${2:-}
[ -n "$file" ] && [ -e "$file" ] || {
    echo "usage: synui-wine-run run|term FILE" >&2
    exit 2
}

dir=$(cd -- "$(dirname -- "$file")" && pwd)
base=$(basename -- "$file")
cd -- "$dir"

if [[ "${base,,}" == *.msi ]]; then
    cmd=(wine msiexec /i "$base")
else
    cmd=(wine "$base")
fi

case "$mode" in
    run)
        exec "${cmd[@]}"
        ;;
    term)
        # foot inherits synui's session; bash -c holds the window after exit so
        # a crash line is readable. $1 is the dir, the rest is the wine command.
        exec foot -- bash -c \
            'cd "$1"; shift; "$@"; s=$?; echo; echo "[wine exited $s — press Enter]"; read -r' \
            _ "$dir" "${cmd[@]}"
        ;;
    *)
        echo "unknown mode: $mode" >&2
        exit 2
        ;;
esac
