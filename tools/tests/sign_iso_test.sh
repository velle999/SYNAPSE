#!/usr/bin/env bash
# sign_iso_test.sh — a passphrase prompt that times out is opened again, and
# the build signs only after everything else is on disk.
#
# The failure this exists for: build.sh signs at the end of an hour-long
# build, pinentry gives up on its own after 60 s, and gpg reported "signing
# failed: Timeout". The build then exited before writing the .sha256 and
# .b2sum, because signing came first.
#
# A throwaway keyring under $T and a stand-in pinentry that answers from a
# queue, so this never touches the real keyring and needs nobody at a screen.
#
#   1. two timeouts, then the passphrase → signed, verified, prompt opened 3×
#   2. Cancel → stops at once, no .asc, prompt opened once
#   3. build.sh writes the checksums and hands out/ back BEFORE it signs
set -u

here=$(cd "$(dirname "$0")" && pwd)
S=${1:-$here/../../archiso/sign-iso.sh}
B=$here/../../archiso/build.sh
[ -f "$S" ] || { echo "  ABORT no sign-iso.sh at $S"; exit 1; }
command -v gpg >/dev/null && command -v gpgconf >/dev/null \
    || { echo "  SKIP  gpg is not installed"; exit 77; }

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }

T=$(mktemp -d)
export GNUPGHOME="$T/gnupg"
trap 'gpgconf --kill gpg-agent 2>/dev/null; rm -rf "$T"' EXIT
mkdir -p "$GNUPGHOME" "$T/out"; chmod 700 "$GNUPGHOME"

# The stand-in pinentry. GETPIN takes the next word from $T/queue: "timeout"
# and "cancel" answer with pinentry's own error codes (source 5, Pinentry;
# 62 GPG_ERR_TIMEOUT, 99 GPG_ERR_CANCELED); anything else, or an empty queue,
# gives the passphrase.
{
    printf '#!/usr/bin/env bash\nT=%q\n' "$T"
    cat <<'EOF'
echo "OK Pleased to meet you"
while IFS= read -r line; do
    case "$line" in
        GETPIN*)
            echo prompt >> "$T/prompts"
            next=$(head -n1 "$T/queue" 2>/dev/null); sed -i 1d "$T/queue" 2>/dev/null
            case "$next" in
                timeout) echo "ERR 83886142 Timeout <Pinentry>" ;;
                cancel)  echo "ERR 83886179 Operation cancelled <Pinentry>" ;;
                *)       printf 'D correct horse\nOK\n' ;;
            esac ;;
        BYE*) echo "OK closing connection"; exit 0 ;;
        *) echo OK ;;
    esac
done
EOF
} > "$T/pinentry"
chmod +x "$T/pinentry"
# No cache, so every signature asks.
printf 'pinentry-program %s\ndefault-cache-ttl 0\nmax-cache-ttl 0\n' "$T/pinentry" \
    > "$GNUPGHOME/gpg-agent.conf"

gpg -q --batch --pinentry-mode loopback --passphrase 'correct horse' \
    --quick-gen-key 'sign-iso test <test@example.invalid>' ed25519 sign never 2>/dev/null \
    || { echo "  ABORT could not make a throwaway key"; exit 1; }
KEY=$(gpg --with-colons --list-secret-keys 2>/dev/null | awk -F: '/^fpr/ { print $10; exit }')
[ -n "$KEY" ] || { echo "  ABORT throwaway key has no fingerprint"; exit 1; }

ISO="$T/out/SynapseOS-9.9.9-x86_64.iso"
head -c 65536 /dev/urandom > "$ISO"

sign() {
    : > "$T/prompts"; rm -f "$ISO.asc"
    # Bounded, so a loop that never ends fails here instead of hanging CI.
    SYNAPSE_SIGNING_KEY="$KEY" timeout 60 "$S" "$ISO" > "$T/log" 2>&1
}
prompts() { wc -l < "$T/prompts" | tr -d ' '; }

# ── 1. timeouts are asked again ──────────────────────────────────────────────
printf 'timeout\ntimeout\npass\n' > "$T/queue"
if sign; then ok "two timeouts then the passphrase: signed"
else bad "two timeouts then the passphrase: exit $? — $(tail -n1 "$T/log")"; fi
[ "$(prompts)" = 3 ] && ok "the prompt was opened 3 times" \
    || bad "the prompt was opened $(prompts) times, expected 3"
gpg --verify "$ISO.asc" "$ISO" 2>/dev/null && ok "the .asc verifies" \
    || bad "no verifying .asc after the retries"
[ "$(grep -c 'timed out' "$T/log")" = 2 ] && ok "each timeout is reported" \
    || bad "expected 2 'timed out' lines: $(grep -c 'timed out' "$T/log")"

# ── 2. Cancel stops ──────────────────────────────────────────────────────────
printf 'cancel\npass\n' > "$T/queue"
if sign; then bad "Cancel still signed"; else ok "Cancel fails the signing"; fi
[ "$(prompts)" = 1 ] && ok "Cancel is not asked again" \
    || bad "the prompt was opened $(prompts) times after Cancel, expected 1"
[ ! -e "$ISO.asc" ] && ok "Cancel leaves no .asc" || bad "Cancel left an .asc"

# ── 3. build.sh signs last ───────────────────────────────────────────────────
line_of() { grep -nF -- "$1" "$B" | head -n1 | cut -d: -f1; }
sig=$(line_of 'step "Signing ISO"')
sum=$(line_of 'sha256sum "$(basename "${ISO_FILE}")"')
own=$(line_of 'ok "Output dir + files owned by')
if [ -n "$sig" ] && [ -n "$sum" ] && [ -n "$own" ]; then
    [ "$sum" -lt "$sig" ] && ok "checksums are written before signing" \
        || bad "build.sh signs (line $sig) before the checksums (line $sum)"
    [ "$own" -lt "$sig" ] && ok "out/ is handed back before signing" \
        || bad "build.sh signs (line $sig) before the handback (line $own)"
else
    bad "build.sh no longer has the signing, checksum or handback step this reads"
fi

echo "  $pass passed, $fail failed"
[ "$fail" = 0 ]
