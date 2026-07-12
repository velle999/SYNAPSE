#!/bin/sh
# synui-screenshot — the Print-key screenshot tool. A thin wrapper over grim
# (capture) and slurp (region selector); synui binds it, see seed_default_binds
# in config.c.
#
#   synui-screenshot region        select an area with slurp   (Shift+Print)
#   synui-screenshot output <NAME> one whole monitor           (Print)
#   synui-screenshot full          the entire layout           (Ctrl+Print)
#
# "output" takes the monitor name rather than working it out here on purpose:
# grim has no notion of which screen you are looking at, and on a multi-monitor
# layout a bare `grim` captures the whole bounding box — which for SYNAPSE's
# three monitors is a 3640x3000 slab with dead space in the gaps. Only the
# compositor knows the focused output, so the `screenshot` action passes it in.
#
# Every shot is written to disk *and* put on the clipboard, so it can be pasted
# straight into a chat or an editor without going near the file manager.
set -eu

mode=${1:-region}

dir="${XDG_PICTURES_DIR:-$HOME/Pictures}/Screenshots"
mkdir -p "$dir"
file="$dir/synapse-$(date +%Y%m%d-%H%M%S).png"

case "$mode" in
region)
    # slurp exits non-zero when the selection is cancelled with Escape or a
    # right-click. That is a normal way to back out, not a failure — leave
    # without writing a file or complaining.
    geom=$(slurp -d) || exit 0
    [ -n "$geom" ] || exit 0
    set -- -g "$geom"
    ;;
output)
    name=${2:-}
    [ -n "$name" ] || { echo "synui-screenshot: output needs a name" >&2; exit 2; }
    set -- -o "$name"
    ;;
full)
    set --
    ;;
*)
    echo "synui-screenshot: unknown mode '$mode'" >&2
    exit 2
    ;;
esac

grim "$@" "$file"

# Hold it on the clipboard too. wl-copy detaches a small process to serve the
# selection until something else claims it; that is how the Wayland clipboard
# works and it is meant to outlive us.
wl-copy --type image/png <"$file"

# SYNAPSE ships no notification daemon, so notify-send is a nicety, not a
# dependency — never let its absence fail the screenshot that already landed.
if command -v notify-send >/dev/null 2>&1; then
    notify-send -a synui -i "$file" "Screenshot saved" "$file" || true
fi

echo "$file"
