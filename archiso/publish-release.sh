#!/usr/bin/env bash
# Publish a SynapseOS ISO as a GitHub Release.
# GitHub caps release assets at 2 GiB (and LFS at 2 GB), so the ISO ships
# as 1900 MiB split parts that users reassemble with `cat` (or `copy /b` on
# Windows -- see the boilerplate below; most people downloading a Linux ISO are
# still on the OS they are leaving, so Windows is a first-class path here, not
# an appendix).
# Requires: gh (authenticated), an ISO + .sha256/.b2sum in archiso/out.
set -euo pipefail

# ⚠ NOT UNDER sudo — and this is the whole reason for the guard rather than a
# line in the header nobody re-reads.
#
# gh's credentials are PER USER: the token lives in $HOME/.config/gh/hosts.yml.
# Run this with sudo and gh reads /root/.config/gh/hosts.yml, which on this box
# does not exist, so it stops with "you need to authenticate" — on a machine
# where `gh auth status` says, correctly, that you are logged in. Nothing about
# the message names root or HOME, so the obvious reading is that the login has
# broken, and re-running `gh auth login` fixes it for the user who was never the
# problem. 0.2.9.2 was retried four times that way.
#
# Nothing here needs root either. It reads the ISO, writes split parts beside it
# and uploads them; the build is what needs root, and build.sh gives the output
# back to the invoking user when it is done.
#
# So: hand the job back to the user who typed sudo. Refuse outright only for a
# real root login, where there is nobody to hand it to.
if [[ "$(id -u)" -eq 0 ]]; then
    if [[ -n "${SUDO_USER:-}" && "$SUDO_USER" != 'root' ]]; then
        echo "publish-release: gh is authenticated per user; re-running as $SUDO_USER" >&2
        # -H so HOME follows, or gh looks in /root's config anyway and this
        # accomplishes nothing. -- so a version that starts with a dash cannot
        # be read as an option to sudo.
        exec sudo -u "$SUDO_USER" -H -- "$0" "$@"
    fi
    echo "publish-release: run this as your own user, not root — gh's token lives in \$HOME/.config/gh" >&2
    exit 1
fi

# ⚠ Leftovers from a previous root run are root-owned, and the split below is
# SKIPPED when part00 exists — so the parts stay readable but .parts.sha256 is
# rewritten every time, and that write is what fails. Say which files and why,
# rather than letting the redirection die with "Permission denied" and no
# subject.
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
for f in "$iso".part[0-9]* "$iso.parts.sha256"; do
    [[ -e $f ]] || continue
    [[ -w $f ]] && continue
    echo "publish-release: $out/$f is not writable by you — a previous run left it" >&2
    echo "  owned by $(stat -c '%U' "$f"). Fix with:" >&2
    echo "    sudo chown $(id -un) $out/$iso.part* $out/$iso.parts.sha256" >&2
    exit 1
done

sha256sum "$iso".part[0-9]* > "$iso.parts.sha256"

# The Windows join line, built from the parts that actually exist rather than
# hardcoded to two: `copy` has no equivalent of `cat file.part*` that is safe to
# publish. A wildcard source concatenates in DIRECTORY order, which is not
# guaranteed to be sorted, and a silently mis-ordered ISO is a corrupt ISO that
# still writes to the stick and still boots into a garbage squashfs. Naming
# every part in order is the only form with no ordering to get wrong.
win_parts=""
for p in "$iso".part[0-9]*; do
    [[ -n $win_parts ]] && win_parts+=" + "
    win_parts+="$p"
done

# Per-release notes, if they have been written: archiso/release-notes/<ver>.md.
# They go ABOVE the download boilerplate, because "what changed" is what someone
# opening a release page came for — the reassembly instructions are reference.
# Without one you get the boilerplate alone, which is what every release before
# 0.2.0 shipped with.
notes="$(mktemp)"
trap 'rm -f "$notes"' EXIT
custom="${notesdir}/${ver}.md"
# Set when the notes file documents the download ITSELF, in which case the
# boilerplate below is skipped. A per-release file can spell the instructions out
# with the real part names and the real checksum instead of the generic form, and
# 0.2.9.md does — appending the boilerplate on top of that gives the release page
# two Download sections that disagree about how specific they are.
have_own_download=0
if [[ -f $custom ]]; then
    # A notes file that spells the download out itself carries the real checksum,
    # and the way that is written is by pasting it in after the build. v0.2.9.2
    # went out with the literal PASTE-THE-SHA256-HERE-AFTER-BUILDING on the
    # release page, where the one instruction that exists to prove the download
    # is intact had nothing to compare against. Nothing complained, because a
    # placeholder is only wrong to a reader. So it is a gate, not a habit: the
    # marker is the word PASTE in capitals, which no prose here uses.
    if grep -nE 'PASTE[-_ ]?[A-Z]' "$custom" >&2; then
        echo "publish-release: release-notes/${ver}.md still holds the placeholder above — fill it in" >&2
        exit 1
    fi
    cat "$custom" >> "$notes"
    echo "using release notes: $custom"
    if grep -qE '^## (Getting the image|Download)' "$custom"; then
        have_own_download=1
        echo "  ...which documents the download itself — skipping the boilerplate"
    else
        # The rule divides the notes from the boilerplate, so it is printed only
        # when there is boilerplate to divide them from — otherwise the page ends
        # on a horizontal line with nothing under it.
        printf '\n---\n\n' >> "$notes"
    fi
else
    echo "no release-notes/${ver}.md — publishing with the download boilerplate only"
fi
if (( have_own_download == 0 )); then
cat >> "$notes" <<EOF
## Download

The ISO is split into parts to fit GitHub's 2 GiB release-asset limit.
Download **all** \`.part*\` files into one folder, then join them back into a
single ISO and check it. Joining is a plain byte-for-byte concatenation, so any
tool that does not "help" works; the checksum is what tells you it worked.

### Linux / macOS

\`\`\`sh
cat ${iso}.part* > ${iso}
sha256sum -c ${iso}.sha256
\`\`\`

(Optionally verify each part before reassembly: \`sha256sum -c ${iso}.parts.sha256\`)

On macOS the checksum tool is spelled differently — \`shasum -a 256 -c ${iso}.sha256\`.

### Windows

Open **Command Prompt** in the folder you downloaded the parts to (Shift+right-click
the folder ▸ *Open in Terminal*, or \`cd\` to it) and run:

\`\`\`bat
copy /b ${win_parts} ${iso}
certutil -hashfile ${iso} SHA256
\`\`\`

\`/b\` is **not optional**: without it \`copy\` works in text mode and stops at
the first \`0x1A\` byte, which in an ISO is a few hundred KB in. You get a short
file, no error, and a stick that will not boot.

\`certutil\` prints the hash; compare it against the one inside
\`${iso}.sha256\`. To have Windows do the comparison instead, use **PowerShell**:

\`\`\`powershell
(Get-FileHash -Algorithm SHA256 .\\${iso}).Hash -eq (((Get-Content .\\${iso}.sha256) -split '\\s+')[0]).ToUpper()
\`\`\`

That prints \`True\` when the join is good. (\`Get-FileHash\` returns uppercase
hex and \`sha256sum\` writes lowercase, which is why the file's copy is upper-cased
before the compare — the bytes are the same either way.)

If \`copy /b\` gives you trouble, PowerShell can do the join on its own, and it
never has a text mode to fall into:

\`\`\`powershell
\$out = [System.IO.File]::Create((Join-Path \$PWD '${iso}'))
Get-ChildItem '${iso}.part*' | Sort-Object Name | ForEach-Object {
  \$in = [System.IO.File]::OpenRead(\$_.FullName); \$in.CopyTo(\$out); \$in.Close()
}
\$out.Close()
\`\`\`

## Write to USB

**This erases the USB stick.** It is written as a raw disk image, not copied as a
file, so whatever was on it is gone.

### Linux / macOS

\`\`\`sh
sudo dd if=${iso} of=/dev/sdX bs=4M status=progress oflag=sync
\`\`\`

Check \`/dev/sdX\` with \`lsblk\` first, and write to the **disk**
(\`/dev/sdb\`), not a partition on it (\`/dev/sdb1\`).

### Windows

Use one of:

- **[Rufus](https://rufus.ie/)** — pick the ISO, leave the defaults, click START.
  It will ask whether to write in *ISO Image mode* or *DD Image mode*: choose
  **DD Image mode**. This is a hybrid image and DD mode writes it unaltered;
  ISO mode rebuilds the boot files and has no reason to get this one right.
- **[balenaEtcher](https://etcher.balena.io/)** — select image, select drive,
  Flash. Nothing to configure; it only ever writes raw.
- **[Ventoy](https://www.ventoy.net/)** — if you already run a Ventoy stick,
  just copy \`${iso}\` onto it and pick it from the boot menu. Nothing is erased
  and other ISOs on the stick keep working.

Then reboot and choose the USB stick from your firmware's boot menu (usually
F12, F11, Esc or Del at power-on). Secure Boot must be off, or SynapseOS enrolled
— see the wiki's **Secure Boot** page.
EOF
fi

gh release create "v$ver" \
    --title "SynapseOS $ver" \
    --notes-file "$notes" \
    "$iso".part[0-9]* "$iso.parts.sha256" "$iso.sha256" "$iso.b2sum"
