#!/usr/bin/env bash
#
# fetch_validate_test.sh — what `syn-model fetch` refuses.
#
# The request file is written by an unprivileged desktop process into a
# group-writable directory and read by a unit running as root, so these
# refusals are the privilege boundary rather than input tidiness. Each case
# below is a way of asking root to write a file somewhere it should not, or to
# fetch from somewhere it should not.
#
# Nothing here touches the network: every case must be refused before curl is
# reached, and a case that reaches curl fails the test by hanging or by
# writing a file, both of which are checked.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

set -uo pipefail

SELF_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SYN_MODEL="$SELF_DIR/../syn-model.sh"

[ "$(id -u)" = 0 ] && { echo "SKIP: must not run as root (the overrides are refused)"; exit 77; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

export SYN_MODEL_RUN_DIR="$TMP/run"
export SYN_MODEL_DIR="$TMP/models"
mkdir -p "$SYN_MODEL_RUN_DIR/req" "$SYN_MODEL_DIR"

fail=0
ok()   { echo "  ok   — $1"; }
bad()  { echo "  FAIL — $1"; fail=1; }

# Queue a request and run fetch on it. Echoes nothing; sets $rc and $out.
run_case() {
    local token="$1" url="$2" file="$3"
    printf 'url=%s\nfile=%s\n' "$url" "$file" > "$SYN_MODEL_RUN_DIR/req/$token.request"
    out=$(timeout 20 bash "$SYN_MODEL" fetch "$token" 2>&1); rc=$?
}

# A refusal must exit non-zero AND leave nothing behind in the models dir.
expect_refused() {
    local what="$1"
    if [ "$rc" = 0 ]; then bad "$what — exited 0"; return; fi
    if [ "$rc" = 124 ]; then bad "$what — reached the network (timed out)"; return; fi
    local leftover; leftover=$(find "$SYN_MODEL_DIR" -type f | wc -l)
    if [ "$leftover" != "$expect_files" ]; then bad "$what — wrote a file"; return; fi
    ok "$what"
}

echo "syn-model fetch: refusals"
expect_files=0

# ── The destination ───────────────────────────────────────────
# A path, not a bare filename: the request must not be able to choose the
# directory root writes into.
run_case t1 "https://huggingface.co/x/y/resolve/main/m.gguf" "../../../etc/cron.d/pwn.gguf"
expect_refused "destination with .. path segments"

run_case t2 "https://huggingface.co/x/y/resolve/main/m.gguf" "/etc/ld.so.preload"
expect_refused "absolute destination path"

run_case t3 "https://huggingface.co/x/y/resolve/main/m.gguf" "sub/dir/m.gguf"
expect_refused "destination with a slash"

run_case t4 "https://huggingface.co/x/y/resolve/main/m.gguf" "model.so"
expect_refused "destination that is not a .gguf"

run_case t5 "https://huggingface.co/x/y/resolve/main/m.gguf" ".hidden.gguf"
expect_refused "destination starting with a dot"

run_case t6 "https://huggingface.co/x/y/resolve/main/m.gguf" 'm.gguf; rm -rf /'
expect_refused "destination carrying shell metacharacters"

# ── The URL ───────────────────────────────────────────────────
run_case u1 "http://huggingface.co/x/y/resolve/main/m.gguf" "m.gguf"
expect_refused "plain http URL"

run_case u2 "https://evil.example.com/m.gguf" "m.gguf"
expect_refused "host that is not huggingface.co"

run_case u3 "https://huggingface.co.evil.example.com/m.gguf" "m.gguf"
expect_refused "host that only starts with huggingface.co"

run_case u4 "file:///etc/shadow" "m.gguf"
expect_refused "file:// URL"

run_case u5 'https://huggingface.co/x/y/m.gguf" -o /etc/pwn "' "m.gguf"
expect_refused "URL carrying quotes and a second curl flag"

# ── The token ─────────────────────────────────────────────────
# The token is interpolated into two paths before anything is read.
printf 'url=https://huggingface.co/x/m.gguf\nfile=m.gguf\n' > "$TMP/escaped.request"
out=$(timeout 20 bash "$SYN_MODEL" fetch "../escaped" 2>&1); rc=$?
expect_refused "token escaping the request directory"

out=$(timeout 20 bash "$SYN_MODEL" fetch "" 2>&1); rc=$?
expect_refused "empty token"

# ── Overwriting ───────────────────────────────────────────────
# An existing model is never replaced — including the one synapd is running.
: > "$SYN_MODEL_DIR/synapse.gguf"
expect_files=1
run_case o1 "https://huggingface.co/x/y/resolve/main/m.gguf" "synapse.gguf"
expect_refused "overwriting an installed model"

# The refusal must also be visible to the panel, not only in the exit code.
if grep -q "state=failed" "$SYN_MODEL_RUN_DIR/o1.progress" 2>/dev/null; then
    ok "refusal is reported in the progress file"
else
    bad "refusal is reported in the progress file"
fi

# ── The request is consumed ───────────────────────────────────
# Read once and unlinked, so the group-writable copy cannot be swapped after
# it has been validated.
if [ -e "$SYN_MODEL_RUN_DIR/req/u1.request" ]; then
    bad "request file is unlinked once read"
else
    ok "request file is unlinked once read"
fi

echo
[ "$fail" = 0 ] && echo "PASS" || echo "FAIL"
exit $fail
