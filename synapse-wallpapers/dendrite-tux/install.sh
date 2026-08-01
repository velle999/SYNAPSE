#!/bin/bash
# Install the rendered wallpapers where synui-wpengine looks for them.
#
# Two destinations:
#
#   (default)   the user's Steam Workshop tree. This is where a subscription
#               would land, and it takes precedence in synui-wpengine.
#   --system    /usr/share/synapse/wallpapers/431960, the read-only tree the
#               OS ships its own wallpapers in. Needs root; this is what the
#               package does.
#
# Neither destination needs Steam. The Workshop "content" directory is just a
# directory — nothing about it requires Steam to have created it — and the
# engine's `--bg` takes a PATH as happily as an id, which is how a wallpaper
# in the system tree gets rendered at all. Steam's 1.6G assets tree is read by
# SCENE wallpapers only; these are video, and render with no assets dir.
#
# Ids come from the 90000000xx block because Steam's real ids are file ids in
# the 1e9..4e9 range and never reach it, so a subscription cannot land on top.
#
# Steam does NOT garbage-collect directories it does not know about (it acts
# only on ids in appworkshop_431960.acf), but a "verify integrity" or a full
# reinstall of Wallpaper Engine will take the user tree with it. That is why
# this is a script and the packs are kept in the repo: re-run it. The system
# tree is owned by pacman and survives all of that.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build"
STEAM="${STEAM_ROOT:-$HOME/.local/share/Steam}"
WORKSHOP="$STEAM/steamapps/workshop/content/431960"
SYSROOT="${DESTDIR:-}/usr/share/synapse/wallpapers/431960"

# id:basename — the mp4/preview pair each pack expects, named by project.json's
# "file"/"preview" keys.
PACKS=(
	"9000000001:synapse-dendrite-tux"
	"9000000002:synapse-dendrite-tux-portrait"
	"9000000003:synapse-dendrite-tux-rgb"
	"9000000004:synapse-dendrite-tux-rgb-portrait"
)

die() { echo "install.sh: $*" >&2; exit 1; }

case "${1:-}" in
	--system) DEST="$SYSROOT" ;;
	"")       DEST="$WORKSHOP" ;;
	*)        die "usage: install.sh [--system]" ;;
esac

# mkdir rather than die: the directory not existing means Steam has not made
# it, which is no longer a reason we cannot install here.
mkdir -p "$DEST" || die "cannot create $DEST${DEST:+ (need root for --system?)}"

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
	if [ -e "$DEST/$id" ] && ! grep -q '"synapse-dendrite-tux' "$DEST/$id/project.json" 2>/dev/null; then
		die "$DEST/$id exists and is not ours — refusing to overwrite"
	fi

	dst="$DEST/$id"
	mkdir -p "$dst"
	install -m644 "$src/project.json" "$dst/project.json"
	install -m644 "$mp4" "$dst/$name.mp4"
	install -m644 "$jpg" "$dst/preview.jpg"
	printf '%-12s %s\n' "$id" "$dst"
done

echo
echo "Apply with:  synui-wpengine set <output> <id>"
