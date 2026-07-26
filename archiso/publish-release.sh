#!/usr/bin/env bash
# Publish a SynapseOS ISO as a GitHub Release.
# GitHub caps release assets at 2 GiB (and LFS at 2 GB), so the ISO ships
# as 1900 MiB split parts that users reassemble with `cat`.
# Requires: gh (authenticated), an ISO + .sha256/.b2sum in archiso/out.
set -euo pipefail

ver="${1:?usage: publish-release.sh <version>   e.g. publish-release.sh 0.1.0}"
out="$(cd "$(dirname "$0")/out" && pwd)"
# Resolved BEFORE the cd below, like $out is. It was relative once, which after
# `cd "$out"` pointed at archiso/out/archiso/release-notes and silently found
# nothing — the release went out with boilerplate and the script said so in a
# line that scrolled past.
notesdir="$(cd "$(dirname "$0")" && pwd)/release-notes"
iso="SynapseOS-${ver}-x86_64.iso"

cd "$out"
[[ -f $iso ]] || { echo "missing $out/$iso" >&2; exit 1; }

echo "verifying $iso ..."
sha256sum -c "$iso.sha256"

if [[ ! -f $iso.part00 ]]; then
    echo "splitting into 1900 MiB parts ..."
    split -b 1900M -d "$iso" "$iso.part"
fi
sha256sum "$iso".part[0-9]* > "$iso.parts.sha256"

# Per-release notes, if they have been written: archiso/release-notes/<ver>.md.
# They go ABOVE the download boilerplate, because "what changed" is what someone
# opening a release page came for — the reassembly instructions are reference.
# Without one you get the boilerplate alone, which is what every release before
# 0.2.0 shipped with.
notes="$(mktemp)"
trap 'rm -f "$notes"' EXIT
custom="${notesdir}/${ver}.md"
if [[ -f $custom ]]; then
    cat "$custom" >> "$notes"
    printf '\n---\n\n' >> "$notes"
    echo "using release notes: $custom"
else
    echo "no release-notes/${ver}.md — publishing with the download boilerplate only"
fi
cat >> "$notes" <<EOF
## Download

The ISO is split into parts to fit GitHub's 2 GiB release-asset limit.
Download **all** \`.part*\` files, then reassemble and verify:

\`\`\`sh
cat ${iso}.part* > ${iso}
sha256sum -c ${iso}.sha256
\`\`\`

(Optionally verify each part before reassembly: \`sha256sum -c ${iso}.parts.sha256\`)

## Write to USB

\`\`\`sh
sudo dd if=${iso} of=/dev/sdX bs=4M status=progress oflag=sync
\`\`\`
EOF

gh release create "v$ver" \
    --title "SynapseOS $ver" \
    --notes-file "$notes" \
    "$iso".part[0-9]* "$iso.parts.sha256" "$iso.sha256" "$iso.b2sum"
