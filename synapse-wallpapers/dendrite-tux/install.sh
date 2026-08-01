#!/bin/bash
# Install the rendered wallpapers where synui-wpengine looks for them.
#
# linux-wallpaperengine only ever reads Steam's Workshop content directory —
# `--bg` takes an id, not a path (see synui-wpengine's WORKSHOP/$id checks) —
# so a locally made wallpaper has to live there too, under an id of its own.
# The 90000000xx block is used because Steam's real ids are file ids in the
# 1e9..4e9 range and will not reach it, so `steam` cannot land a subscription
# on top of these.
#
# Steam does NOT garbage-collect directories it does not know about (it acts
# only on ids in appworkshop_431960.acf), but a "verify integrity" or a full
# reinstall of Wallpaper Engine will take the whole tree with it. That is why
# this is a script and the packs are kept in the repo: re-run it.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build"
STEAM="${STEAM_ROOT:-$HOME/.local/share/Steam}"
WORKSHOP="$STEAM/steamapps/workshop/content/431960"

# id:basename — the mp4/preview pair each pack expects, named by project.json's
# "file"/"preview" keys.
PACKS=(
	"9000000001:synapse-dendrite-tux"
	"9000000002:synapse-dendrite-tux-portrait"
)

die() { echo "install.sh: $*" >&2; exit 1; }

[ -d "$WORKSHOP" ] || die "no Workshop content at $WORKSHOP (is Wallpaper Engine installed?)"

for entry in "${PACKS[@]}"; do
	id="${entry%%:*}"
	name="${entry#*:}"
	src="$HERE/pack/$id"
	mp4="$BUILD/$name.mp4"
	jpg="$BUILD/$name-preview.jpg"

	[ -f "$src/project.json" ] || die "missing $src/project.json"
	[ -f "$mp4" ] || die "missing $mp4 — run ./make.py first"
	[ -f "$jpg" ] || die "missing $jpg — run ./make.py first"

	# Refuse to overwrite an id that is not ours. Cheap, and the cost of being
	# wrong is clobbering a subscribed wallpaper.
	if [ -e "$WORKSHOP/$id" ] && ! grep -q '"synapse-dendrite-tux' "$WORKSHOP/$id/project.json" 2>/dev/null; then
		die "$WORKSHOP/$id exists and is not ours — refusing to overwrite"
	fi

	dst="$WORKSHOP/$id"
	mkdir -p "$dst"
	install -m644 "$src/project.json" "$dst/project.json"
	install -m644 "$mp4" "$dst/$name.mp4"
	install -m644 "$jpg" "$dst/preview.jpg"
	printf '%-12s %s\n' "$id" "$dst"
done

echo
echo "Apply with:  synui-wpengine set <output> <id>"
