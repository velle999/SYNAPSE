#!/usr/bin/env bash
# sign-iso.sh — GPG-sign a built SynapseOS ISO, and verify the signature made.
#
# ⚠ THIS FILE IS THE ONLY PLACE THAT SIGNS. build.sh calls it rather than
# repeating the gpg invocation, so "which key, as which user, verified how"
# has exactly one answer and one place to change it.
#
# It exists as its own script for a second reason: a signature can be added
# AFTER the fact. Forgetting to sign used to cost a full rebuild, which on this
# machine is most of an hour — so the forgetting was expensive enough that the
# real fix was never to forget, which is not a fix. Signing is detached: the
# ISO's bytes do not change, so the .sha256 and .b2sum written at build time
# stay correct and nothing downstream needs regenerating.
#
# Usage:
#   ./sign-iso.sh                    # newest ISO in out/
#   ./sign-iso.sh 0.3.0              # by version
#   ./sign-iso.sh /path/to/some.iso
#   ./sign-iso.sh --check-key        # prove the key is usable; sign nothing
#   ./sign-iso.sh --force 0.3.0      # re-sign one that already has a good .asc
#
# Which key:
#   1. $SYNAPSE_SIGNING_KEY
#   2. archiso/release-key.fingerprint
#
# ⚠ THE KEY IS NAMED, NEVER DEFAULTED. `gpg --detach-sign` with no -u picks the
# first usable secret key, which is whatever the developer happens to have — and
# a release signed with somebody's personal identity instead of the release key
# is not a thing you can quietly undo afterwards. The fingerprint file is a
# fingerprint, which is public, not a key: it names the key on this project's
# behalf so nobody has to remember to say it, and it is still a name rather than
# a guess.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${SCRIPT_DIR}/out"
FPR_FILE="${SCRIPT_DIR}/release-key.fingerprint"

C_OK='\033[38;5;82m'; C_WARN='\033[38;5;214m'; C_ERR='\033[38;5;196m'; C_RESET='\033[0m'
ok()   { echo -e "${C_OK}[  ok  ]${C_RESET} $*"; }
warn() { echo -e "${C_WARN}[ warn ]${C_RESET} $*" >&2; }
err()  { echo -e "${C_ERR}[ err  ]${C_RESET} $*" >&2; exit 1; }

# ── As the invoking user, never as root ──────────────────────────────────────
#
# ⛔ The release key lives in a PERSON's keyring. mkarchiso needs root, so
# build.sh runs under sudo and would hand us root — where a bare gpg reads
# /root/.gnupg, which on any sane machine holds no release key at all. That
# fails with "no default secret key" on the one box this was written for, and on
# a box where root DOES have a key it would quietly sign with the wrong one.
#
# So hand the job back, exactly like publish-release.sh does with gh's token.
# Refuse outright only for a real root login, where there is nobody to hand
# it to.
if [[ "$(id -u)" -eq 0 ]]; then
    if [[ -n "${SUDO_USER:-}" && "$SUDO_USER" != 'root' ]]; then
        # -H so HOME (and therefore GNUPGHOME) follows, or gpg reads root's
        # keyring anyway and this accomplishes nothing. -- so an argument
        # starting with a dash cannot be read as an option to sudo.
        exec sudo -u "$SUDO_USER" -H -- "$0" "$@"
    fi
    err "the release key lives in a user's keyring — run this as yourself, not as root"
fi

# ── Arguments ────────────────────────────────────────────────────────────────
CHECK_ONLY=false
FORCE=false
WANT=""
for arg in "$@"; do
    case "$arg" in
        --check-key) CHECK_ONLY=true ;;
        --force)     FORCE=true ;;
        --help|-h)   sed -n '3,32p' "$0" | sed 's/^# \?//'; exit 0 ;;
        -*)          err "unknown option: $arg" ;;
        *)           WANT="$arg" ;;
    esac
done

# ── Which key ────────────────────────────────────────────────────────────────
KEY="${SYNAPSE_SIGNING_KEY:-}"
KEY_FROM="\$SYNAPSE_SIGNING_KEY"
if [[ -z "$KEY" && -f "$FPR_FILE" ]]; then
    # First line that is neither blank nor a comment, with whitespace and the
    # spaces gpg prints inside a fingerprint removed — a fingerprint copied out
    # of `gpg --list-keys` arrives in groups of four.
    KEY="$(grep -vE '^\s*(#|$)' "$FPR_FILE" | head -1 | tr -d '[:space:]')"
    KEY_FROM="${FPR_FILE#"${SCRIPT_DIR}/"}"
fi

[[ -n "$KEY" ]] || err "no signing key named.
     Put the release key's fingerprint in ${FPR_FILE}
     (see \"Signing a release\" in archiso/README.md), or pass it as
     SYNAPSE_SIGNING_KEY=<fingerprint>. Signing with whatever key gpg picks
     first is how a release ends up carrying somebody's personal identity."

# The secret half is what signing needs, and its absence is the failure worth
# catching EARLY: --check-key is what build.sh runs in its preflight, so a build
# host without the key says so in the first ten seconds rather than after an
# hour of mkarchiso.
gpg --list-secret-keys "$KEY" &>/dev/null \
    || err "no SECRET key for ${KEY} in $(id -un)'s keyring (named by ${KEY_FROM}).
     \`gpg --list-secret-keys\` shows what is there. On a machine that is not the
     release box, build with --no-sign and sign where the key lives."

if [[ "$CHECK_ONLY" == "true" ]]; then
    ok "signing key present: ${KEY}  (from ${KEY_FROM})"
    exit 0
fi

# ── Which ISO ────────────────────────────────────────────────────────────────
if [[ -z "$WANT" ]]; then
    ISO="$(ls -1t "${OUT_DIR}"/SynapseOS-*-x86_64.iso 2>/dev/null | head -1)"
    [[ -n "$ISO" ]] || err "no ISO in ${OUT_DIR}"
elif [[ -f "$WANT" ]]; then
    ISO="$WANT"
else
    ISO="${OUT_DIR}/SynapseOS-${WANT}-x86_64.iso"
    [[ -f "$ISO" ]] || err "no such image: ${ISO}"
fi

# ⛔ The .asc is written NEXT TO the ISO, so the directory has to be writable —
# and out/ is created by mkarchiso as root. build.sh hands it back before it
# gets here; a hand-run after a build that did not says so with a subject
# instead of dying inside gpg with "Permission denied" and no filename.
[[ -w "$(dirname "$ISO")" ]] || err "$(dirname "$ISO") is not writable by you — a build left it
     owned by $(stat -c '%U' "$(dirname "$ISO")"). Fix with:
       sudo chown $(id -un) $(dirname "$ISO")"

if [[ -f "${ISO}.asc" && "$FORCE" != "true" ]]; then
    if gpg --verify "${ISO}.asc" "$ISO" &>/dev/null; then
        ok "already signed, and it verifies: ${ISO}.asc  (--force to re-sign)"
        exit 0
    fi
    # A signature that does not verify is not a reason to stop: it is the exact
    # thing that has to be replaced, and leaving it in place is how a bad .asc
    # reaches a release page.
    warn "${ISO}.asc does not verify — replacing it"
fi

echo "signing $(basename "$ISO") with ${KEY} ..."
gpg --detach-sign --armor --yes --local-user "$KEY" "$ISO" \
    || err "signing failed — is ${KEY} in $(id -un)'s keyring, and did the passphrase prompt reach you?"

# ⛔ VERIFIED IMMEDIATELY, against the file that will actually ship. gpg exits 0
# on plenty of things that are not a good signature over this ISO, and an
# unverifiable .asc published beside a download is worse than none: it invites
# people to run a check that cannot pass.
gpg --verify "${ISO}.asc" "$ISO" \
    || err "the signature just written does not verify against ${ISO}"

ok "Signed and verified: ${ISO}.asc  (key ${KEY})"
