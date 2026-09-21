# signing_lib.sh — a throwaway update-signing key for the syn-update suites.
#
# syn-update builds only commits signed by a key in SYN_UPDATE_KEYS_DIR, so a
# stand-in upstream has to be signed like the real one. Source this after
# setting T (the suite's temp dir) and GIT_CONFIG_GLOBAL, then:
#
#   signing_setup        make a key, point SYN_UPDATE_KEYS_DIR at its public
#                        half, and sign every commit made under GIT_CONFIG_GLOBAL
#   signing_teardown     stop the gpg-agent the temp keyring started
#
# After signing_setup, SIGN_FPR is the key's fingerprint.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

# new_signing_key <gnupghome> <name>: prints the fingerprint.
new_signing_key() {
    mkdir -p -m 700 "$1"
    GNUPGHOME="$1" gpg --batch --quiet --pinentry-mode loopback --passphrase '' \
        --quick-gen-key "$2 <test@invalid>" ed25519 sign never 2>/dev/null || return 1
    GNUPGHOME="$1" gpg --batch --with-colons --list-secret-keys 2>/dev/null |
        awk -F: '$1 == "fpr" { print $10; exit }'
}

signing_setup() {
    command -v gpg >/dev/null 2>&1 || { echo "SKIP: gpg not installed."; exit 77; }
    export GNUPGHOME="$T/gnupg"
    SIGN_FPR=$(new_signing_key "$GNUPGHOME" "syn-update test") && [ -n "$SIGN_FPR" ] ||
        { echo "  ABORT could not make a test signing key"; exit 1; }
    mkdir -p "$T/keys"
    gpg --batch --armor --export "$SIGN_FPR" > "$T/keys/test.asc"
    git config --file "$GIT_CONFIG_GLOBAL" commit.gpgsign true
    git config --file "$GIT_CONFIG_GLOBAL" user.signingkey "$SIGN_FPR"
    export SYN_UPDATE_KEYS_DIR="$T/keys"
}

signing_teardown() {
    local h
    for h in "$T"/gnupg*; do
        [ -d "$h" ] && gpgconf --homedir "$h" --kill gpg-agent 2>/dev/null
    done
    return 0
}
