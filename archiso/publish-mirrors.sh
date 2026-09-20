#!/usr/bin/env bash
# Publish a built ISO to the mirrors that can take it in ONE PIECE.
#
#   Cloudflare R2   -> https://dl.soslinux.org/<iso>
#   Internet Archive -> https://archive.org/download/synapseos-<ver>/<iso>
#
# This is the counterpart to publish-release.sh, not a replacement for it.
# GitHub caps a release asset at 2 GiB, which is why that script splits the
# image into .part files; neither mirror here has that limit, so both get the
# whole ISO and the site links to R2 as the primary download. The GitHub parts
# stay as a third road.
#
# Sidecars (.sha256, .b2sum, .asc) go to EVERY host that carries the image.
# A checksum is only useful next to the download it describes, and someone who
# takes the ISO from archive.org must not have to know that GitHub exists to
# check it.
#
# Requires: rclone (R2), ia (Internet Archive). Setup is at the bottom of
# archiso/README.md. Neither is needed if you skip that mirror.
set -euo pipefail

# ⚠ NOT UNDER sudo — the same trap publish-release.sh documents at length, for
# the same reason: these credentials are PER USER. rclone reads
# $HOME/.config/rclone/rclone.conf and ia reads $HOME/.config/ia.ini, so under
# sudo both look in /root, find nothing, and report it as "not configured" on a
# machine where they are configured fine. Hand the job back rather than letting
# somebody re-run a setup that was never broken.
if [[ "$(id -u)" -eq 0 ]]; then
    if [[ -n "${SUDO_USER:-}" && "$SUDO_USER" != 'root' ]]; then
        echo "publish-mirrors: credentials are per user; re-running as $SUDO_USER" >&2
        exec sudo -u "$SUDO_USER" -H -- "$0" "$@"
    fi
    echo "publish-mirrors: run this as your own user, not root — rclone and ia keep their config in \$HOME/.config" >&2
    exit 1
fi

# ── Destinations ────────────────────────────────────────────────────────────
#
# ⛔ NO ACCOUNT IDENTIFIERS IN THIS FILE. R2's endpoint contains the account id
# and the S3 keys are secrets; both belong in ~/.config/rclone/rclone.conf,
# which is not in this repo. The script names a REMOTE, and the remote knows
# where it points. Same for ia.ini.
r2_remote="${R2_REMOTE:-r2}"          # rclone remote name
r2_bucket="${R2_BUCKET:-synapseos-dl}"
r2_public="${R2_PUBLIC_BASE:-https://dl.soslinux.org}"

skip_r2=0
skip_ia=0
force=0

usage() {
    cat <<'EOF'
usage: publish-mirrors.sh <version> [--skip-r2] [--skip-ia] [--force]

  <version>    e.g. 1.0.0 — the ISO must already be built and checksummed
               in archiso/out.

  --skip-r2    Do not upload to Cloudflare R2.
  --skip-ia    Do not upload to the Internet Archive.
  --force      Re-upload even when the remote copy is already the right size.

Environment (all optional, defaults in the script):
  R2_REMOTE        rclone remote name              (default: r2)
  R2_BUCKET        bucket to upload into           (default: synapseos-dl)
  R2_PUBLIC_BASE   public base URL to verify against
  IA_ITEM          archive.org identifier          (default: synapseos-<ver>)
EOF
}

ver=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-r2) skip_r2=1 ;;
        --skip-ia) skip_ia=1 ;;
        --force)   force=1 ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "publish-mirrors: unknown option $1" >&2; usage >&2; exit 1 ;;
        *)  [[ -z $ver ]] || { echo "publish-mirrors: version given twice" >&2; exit 1; }
            ver="$1" ;;
    esac
    shift
done
[[ -n $ver ]] || { usage >&2; exit 1; }

if (( skip_r2 && skip_ia )); then
    echo "publish-mirrors: both mirrors skipped — nothing to do" >&2
    exit 1
fi

out="$(cd "$(dirname "$0")/out" && pwd)"
iso="SynapseOS-${ver}-x86_64.iso"
ia_item="${IA_ITEM:-synapseos-${ver}}"

cd "$out"
[[ -f $iso ]] || { echo "publish-mirrors: missing $out/$iso" >&2; exit 1; }

# ── What goes up ────────────────────────────────────────────────────────────
#
# The ISO first, then its sidecars. Order matters for a reader watching the
# output: the 4.6 GB transfer is the one that can fail halfway.
uploads=("$iso")
for side in "$iso.sha256" "$iso.b2sum" "$iso.asc"; do
    [[ -f $side ]] && uploads+=("$side")
done

iso_bytes="$(stat -c '%s' "$iso")"

echo "verifying the local $iso before anything is uploaded ..."
sha256sum -c "$iso.sha256"

# ⛔ VERIFIED BEFORE IT IS PUBLISHED, exactly as publish-release.sh does it.
# Publishing a signature nobody checked is how a release ships one that cannot
# verify, and the person who finds out is a stranger doing the right thing.
if [[ -f "$iso.asc" ]]; then
    echo "verifying the signature ..."
    gpg --verify "$iso.asc" "$iso" \
        || { echo "publish-mirrors: $iso.asc does NOT verify against $iso — refusing" >&2; exit 1; }
    echo "  signature ok"
else
    echo >&2
    echo "publish-mirrors: WARNING — no $iso.asc, these mirrors will carry an UNSIGNED image." >&2
    echo "  Sign it now, no rebuild:  ./archiso/sign-iso.sh $ver" >&2
    echo "  Then re-run this. (Ctrl-C now if that is what you want.)" >&2
    echo >&2
fi

# ── Read-back verification ──────────────────────────────────────────────────
#
# ⛔ AN UPLOAD TOOL'S EXIT STATUS SAYS THE TRANSFER RETURNED, NOT THAT THE FILE
# IS SERVABLE. A multipart upload that lands out of order, a bucket whose public
# access was never switched on, a custom domain pointing at the wrong bucket —
# every one of those exits 0 and serves either nothing or a corrupt image. So
# the check is made from OUTSIDE, over the public URL a user would use.
#
# Two probes, because each catches what the other misses:
#   - Content-Length against the local size, which catches truncation and a
#     transfer that silently restarted.
#   - The LAST mebibyte, by range request, hashed and compared. A mis-ordered
#     or short multipart assembly has the right total length and the wrong
#     tail, and the tail is the cheapest part of a 4.6 GB file to prove.
verify_public() {
    local url="$1" want_bytes="$2" file="$3"
    local head got_bytes want_tail got_tail start

    local rc
    head="$(curl -fsSL --retry 3 --retry-delay 2 -I "$url" 2>/dev/null)"; rc=$?
    if (( rc != 0 )); then
        # ⚠ SAY WHICH END IS BROKEN. curl exit 6 is "could not resolve host",
        # which is a fact about THIS machine — a stale negative DNS entry, an
        # unplugged resolver — and has nothing to do with the bucket. Reporting
        # it as "no response" sent a real release to the Cloudflare dashboard
        # to re-check a custom domain that was correct all along, while the
        # actual cause was a cached NXDOMAIN from before the record existed.
        if (( rc == 6 )); then
            local host="${url#*://}"; host="${host%%/*}"
            echo "  ✗ $url — THIS MACHINE cannot resolve $host (curl 6)." >&2
            echo "      The upload is unaffected; rclone uses the S3 endpoint, not this name." >&2
            echo "      A record added minutes ago can sit behind a cached NXDOMAIN for the" >&2
            echo "      zone's negative TTL. Check with a resolver that is not yours:" >&2
            echo "        curl -s -H 'accept: application/dns-json' \\" >&2
            echo "          'https://1.1.1.1/dns-query?name=${host}&type=A'" >&2
            echo "      If that answers, wait it out and re-run — uploads are skipped." >&2
        else
            echo "  ✗ $url — no response (curl $rc)" >&2
        fi
        return 1
    fi
    got_bytes="$(printf '%s' "$head" | awk 'BEGIN{IGNORECASE=1} /^content-length:/ {gsub(/\r/,""); print $2; exit}')"
    if [[ "$got_bytes" != "$want_bytes" ]]; then
        echo "  ✗ $url — serves ${got_bytes:-no} bytes, local file is $want_bytes" >&2
        return 1
    fi

    start=$(( want_bytes - 1048576 ))
    (( start < 0 )) && start=0
    want_tail="$(tail -c 1048576 "$file" | sha256sum | cut -d' ' -f1)"

    # ⚠ CONFIRM THE RANGE WAS HONOURED BEFORE BELIEVING THE HASH. A host that
    # ignores Range answers 200 with the WHOLE file, which hashes to something
    # that is not the tail — indistinguishable from corruption unless the
    # status code is checked, and it would report a good 4.6 GB upload as
    # assembled wrong after quietly re-downloading all of it.
    local tmp code
    tmp="$(mktemp)"
    code="$(curl -sSL --retry 3 --retry-delay 2 -r "${start}-" \
                 -o "$tmp" -w '%{http_code}' "$url" 2>/dev/null)" || true
    if [[ "$code" != "206" ]]; then
        rm -f "$tmp"
        echo "  ⚠ $url — HTTP $code to a range request (expected 206); size checked, tail not" >&2
        return 0
    fi
    got_tail="$(sha256sum < "$tmp" | cut -d' ' -f1)"
    rm -f "$tmp"
    if [[ "$want_tail" != "$got_tail" ]]; then
        echo "  ✗ $url — last 1 MiB does not match; the remote copy is assembled wrong" >&2
        return 1
    fi

    echo "  ✓ $url — $want_bytes bytes, tail matches"
    return 0
}

# Remote size as rclone sees it, so an already-complete upload can be skipped
# without downloading anything. Prints nothing when the object is absent.
r2_remote_size() {
    rclone size --json "${r2_remote}:${r2_bucket}/$1" 2>/dev/null \
        | sed -n 's/.*"bytes":\([0-9]*\).*/\1/p'
}

published_r2=0
published_ia=0

# ── Cloudflare R2 ───────────────────────────────────────────────────────────
if (( ! skip_r2 )); then
    echo
    echo "── Cloudflare R2 ──────────────────────────────────────────────"

    if ! command -v rclone >/dev/null 2>&1; then
        echo "publish-mirrors: rclone is not installed — R2 upload cannot run." >&2
        echo "  sudo pacman -S rclone, then see 'Mirrors' in archiso/README.md" >&2
        echo "  (or re-run with --skip-r2 to publish to the Archive only)" >&2
        exit 1
    fi
    if ! rclone listremotes 2>/dev/null | grep -qx "${r2_remote}:"; then
        echo "publish-mirrors: no rclone remote named '${r2_remote}' — see 'Mirrors' in archiso/README.md" >&2
        exit 1
    fi

    for f in "${uploads[@]}"; do
        want="$(stat -c '%s' "$f")"
        have="$(r2_remote_size "$f")"
        if (( ! force )) && [[ "$have" == "$want" ]]; then
            echo "already on R2 at the right size, skipping: $f"
            continue
        fi
        echo "uploading to R2: $f ($want bytes)"
        # --s3-chunk-size keeps a 4.6 GB object well inside R2's 10,000-part
        # ceiling while staying small enough that a failed part is cheap to
        # retry. Defaults would work; this makes the number a decision.
        rclone copyto --progress --s3-chunk-size 64M \
            "$f" "${r2_remote}:${r2_bucket}/$f"
    done

    echo "checking what R2 actually serves ..."
    r2_ok=1
    for f in "${uploads[@]}"; do
        verify_public "${r2_public}/$f" "$(stat -c '%s' "$f")" "$f" || r2_ok=0
    done
    if (( r2_ok )); then
        published_r2=1
    else
        echo "publish-mirrors: R2 uploaded but did not verify — NOT publishing that link." >&2
        echo "  The objects are in the bucket either way; only the public read-back failed." >&2
        echo "  If the lines above say THIS MACHINE cannot resolve, it is local DNS and the" >&2
        echo "  bucket is fine — wait out the negative TTL and re-run." >&2
        echo "  Otherwise check the custom domain is ${r2_public#https://}, that it is" >&2
        echo "  Active, and that no Worker route (e.g. '*.<zone>/*') claims that hostname" >&2
        echo "  first — a Worker route wins over an R2 custom domain and serves the site." >&2
    fi
fi

# ── Internet Archive ────────────────────────────────────────────────────────
if (( ! skip_ia )); then
    echo
    echo "── Internet Archive ───────────────────────────────────────────"

    if ! command -v ia >/dev/null 2>&1; then
        echo "publish-mirrors: the 'ia' client is not installed — Archive upload cannot run." >&2
        echo "  pipx install internetarchive && ia configure" >&2
        echo "  (or re-run with --skip-ia to publish to R2 only)" >&2
        exit 1
    fi
    # ⛔ INSTALLED IS NOT CONFIGURED — two facts, and only the first one is
    # visible to `command -v`. An unconfigured `ia` raises AuthenticationError
    # from inside requests, which under `set -e` aborts this script on a Python
    # traceback, several screens long, whose last line is the only useful one.
    # Worse, it would do that AFTER the R2 upload had already run, so a 4.6 GB
    # transfer would be followed by a crash that reads like a bug in this script.
    if ! ia configure --check >/dev/null 2>&1; then
        echo "publish-mirrors: 'ia' is installed but not logged in — Archive upload cannot run." >&2
        echo "  Run:  ia configure        (archive.org account; writes ~/.config/internetarchive/ia.ini)" >&2
        echo "  (or re-run with --skip-ia to publish to R2 only)" >&2
        exit 1
    fi

    # ⚠ AN ARCHIVE ITEM IS EFFECTIVELY PERMANENT. Uploading into an identifier
    # is easy; taking it back is a support request. So the identifier is
    # per-release (synapseos-<ver>) and never reused — a bad 1.0.0 upload does
    # not contaminate 1.0.1.
    echo "item: $ia_item"
    ia upload "$ia_item" "${uploads[@]}" \
        --metadata="title:SynapseOS ${ver}" \
        --metadata="mediatype:software" \
        --metadata="collection:opensource_media" \
        --metadata="subject:linux;operating system;arch linux;synapseos" \
        --metadata="licenseurl:https://github.com/velle999/SYNAPSE/blob/main/LICENSE" \
        --metadata="originalurl:https://soslinux.org/" \
        --retries 5

    # ⚠ THE TORRENT IS NOT THERE YET, and that is normal. The Archive's derive
    # queue builds <item>_archive.torrent after the upload settles; it can be
    # minutes or hours. Publishing the torrent link before it exists is how the
    # download page ends up with a 404 on it, so it is announced as pending
    # rather than printed as a finished URL.
    echo "checking what the Archive actually serves ..."
    ia_ok=1
    for f in "${uploads[@]}"; do
        verify_public "https://archive.org/download/${ia_item}/$f" \
            "$(stat -c '%s' "$f")" "$f" || ia_ok=0
    done
    if (( ia_ok )); then
        published_ia=1
    else
        echo "publish-mirrors: the Archive has not settled yet — this is usually TIME, not failure." >&2
        echo "  A large item can take a while to become servable. Re-run to re-check;" >&2
        echo "  uploads that already landed are skipped." >&2
    fi
fi

# ── What landed ─────────────────────────────────────────────────────────────
#
# ⛔ THE SUMMARY REPORTS WHAT WAS VERIFIED, NOT WHAT WAS ATTEMPTED. A mirror
# that uploaded but did not verify is listed as not published, because the only
# thing a link on the download page can promise is that the file is there.
echo
echo "── Published ──────────────────────────────────────────────────"
if (( published_r2 )); then
    echo "  R2       ${r2_public}/${iso}"
else
    echo "  R2       not published$( (( skip_r2 )) && echo ' (skipped)' )"
fi
if (( published_ia )); then
    echo "  Archive  https://archive.org/download/${ia_item}/${iso}"
    echo "  Torrent  https://archive.org/download/${ia_item}/${ia_item}_archive.torrent"
    echo "           ^ appears once the Archive's derive queue runs; check before linking it."
else
    echo "  Archive  not published$( (( skip_ia )) && echo ' (skipped)' )"
fi
echo
echo "ISO is $iso_bytes bytes. The download page links R2 first and the Archive as the mirror."

if (( published_r2 == 0 && published_ia == 0 )); then
    exit 1
fi
