#!/bin/bash
# Regenerate SynapseOS's copy of the bluez5 hardware-quirk database.
#
# Reads PipeWire's packaged database, injects /usr/share/synui/bluez5-quirks.conf
# at the top of its device list, and writes the result under SPA_DATA_DIR_TREE.
# Run by the 72-synui-bluez-quirks pacman hook whenever pipewire-audio or synui
# is installed or upgraded.
#
# ── Why a copy at all ────────────────────────────────────────────────────────
# /usr/share/spa-0.2/bluez5/bluez-hardware.conf is owned by pipewire-audio and
# has NO drop-in directory and NO /etc search path: editing it in place works
# until the next PipeWire upgrade reverts it. SPA_DATA_DIR is the only supported
# way to redirect the lookup, and it redirects the whole tree — there is no way
# to shadow one file. So the tree gets shadowed, and this script exists to keep
# the shadow honest: it is regenerated from the packaged file on every upgrade,
# so it never carries a stale copy of upstream's quirks.
#
# Do NOT try to do this with a wireplumber monitor.bluez rule and
# bluez5.enable-hw-volume instead. The key does land on the card — it shows up
# in `pactl list cards` — but the bluez5 monitor reads that setting once at
# creation and never looks again, so as a per-device rule it is a silent no-op.
#
# ── Failing safe ─────────────────────────────────────────────────────────────
# The systemd drop-in does not set SPA_DATA_DIR directly; it sources $ENV_FILE
# with a leading `-`, so a missing file is not an error. This script is the only
# thing that writes that file, and it writes it last. If anything here fails,
# the file is removed and WirePlumber goes back to reading the packaged database
# — a desktop with upstream's quirks, which is where it started. Pointing
# SPA_DATA_DIR at a tree with no database in it would instead take away every
# quirk upstream ships, which is worse than the bug this fixes.
set -euo pipefail

SRC=/usr/share/spa-0.2/bluez5/bluez-hardware.conf
QUIRKS=/usr/share/synui/bluez5-quirks.conf
TREE=/usr/share/synui/spa-0.2
DST="$TREE/bluez5/bluez-hardware.conf"
ENV_FILE="$TREE/env"

# The line the quirks go under. First match wins in this list, so injecting at
# the top is what makes our entries beat upstream's — and is why nothing here
# has to care whether upstream already lists the same device.
ANCHOR='bluez5.features.device = ['

die() {
    # Take the redirect down BEFORE reporting, so the failure cannot leave
    # WirePlumber pointed at a tree this script did not finish writing.
    rm -f "$ENV_FILE"
    echo "synui-bluez-quirks: $1" >&2
    echo "synui-bluez-quirks: SPA_DATA_DIR redirect disabled; using the packaged database" >&2
    exit 1
}

[ -r "$SRC" ]    || die "$SRC is missing or unreadable (is pipewire-audio installed?)"
[ -r "$QUIRKS" ] || die "$QUIRKS is missing (synui is only half-installed?)"

grep -qF "$ANCHOR" "$SRC" || die "no '$ANCHOR' in $SRC — upstream changed the format"

mkdir -p "$TREE/bluez5"

# Write via a temp file and rename, so a reader never sees a half-written
# database: WirePlumber may well be running while pacman upgrades PipeWire.
tmp=$(mktemp "$DST.XXXXXX")
trap 'rm -f "$tmp"' EXIT

{
    cat <<EOF
# GENERATED FILE — DO NOT EDIT. Rewritten by synui-bluez-quirks on every
# pipewire-audio or synui upgrade; any change here is lost at that point.
#
# This is PipeWire's own $SRC
# with the SynapseOS entries from $QUIRKS
# injected at the top of the device list. Edit that file instead.
#
# Reached through SPA_DATA_DIR, set by
# /usr/lib/systemd/user/wireplumber.service.d/10-synapse-spa-data-dir.conf

EOF
    # awk, not sed: the quirks file is arbitrary text with braces, brackets and
    # ampersands in it, all of which sed's replacement syntax would eat.
    awk -v quirks="$QUIRKS" -v anchor="$ANCHOR" '
        { print }
        !done && index($0, anchor) {
            while ((getline line < quirks) > 0) print line
            close(quirks)
            done = 1
        }
    ' "$SRC"
} > "$tmp"

# Prove the injection landed rather than trusting that it did. An anchor that
# matched inside a comment, or an empty quirks file, both produce a perfectly
# valid database with no quirk in it — and a silent no-op here looks exactly
# like a working fix until the next time something connects.
grep -qF 'SynapseOS quirks' "$tmp" || die "quirks were not injected into $DST"

chmod 644 "$tmp"
mv -f "$tmp" "$DST"
trap - EXIT

# Last, and only now: arm the redirect.
printf 'SPA_DATA_DIR=%s\n' "$TREE" > "$ENV_FILE"
chmod 644 "$ENV_FILE"
