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

# ══ syn-model delete-request ══════════════════════════════════
#
# The same privilege boundary pointed the other way, and a sharper one: fetch
# can be talked into WRITING somewhere, this can be talked into REMOVING
# something. Every case is a way of asking root to unlink a file the desktop
# was not entitled to name.
echo
echo "syn-model delete-request: refusals"

del_case() {
    local token="$1" file="$2"
    printf 'file=%s\n' "$file" > "$SYN_MODEL_RUN_DIR/req/$token.delete"
    out=$(timeout 20 bash "$SYN_MODEL" delete-request "$token" 2>&1); rc=$?
}

# A refusal must exit non-zero and leave the victim file untouched.
expect_kept() {
    local what="$1" victim="$2"
    if [ "$rc" = 0 ]; then bad "$what — exited 0"; return; fi
    if [ ! -e "$victim" ]; then bad "$what — DELETED the file"; return; fi
    ok "$what"
}

# A real installed model to aim the bad requests at, plus something outside the
# models directory that must survive every one of them.
: > "$SYN_MODEL_DIR/keeper.gguf"
mkdir -p "$TMP/outside"
: > "$TMP/outside/precious.gguf"

del_case d1 "../outside/precious.gguf"
expect_kept "destination with .. path segments" "$TMP/outside/precious.gguf"

del_case d2 "/etc/passwd"
expect_kept "an absolute path" "/etc/passwd"

del_case d3 "sub/dir/model.gguf"
expect_kept "a name carrying a slash" "$SYN_MODEL_DIR/keeper.gguf"

del_case d4 "keeper.txt"
expect_kept "a file that is not a .gguf" "$SYN_MODEL_DIR/keeper.gguf"

del_case d5 ".hidden.gguf"
expect_kept "a dot-leading name" "$SYN_MODEL_DIR/keeper.gguf"

del_case d6 "not-installed.gguf"
if [ "$rc" = 0 ]; then bad "a model that is not there — exited 0"
else ok "a model that is not there"; fi

# A symlink in the models directory must not become a way to unlink its
# target: `[ -f ]` follows links, so this is checked with -L first.
ln -sf "$TMP/outside/precious.gguf" "$SYN_MODEL_DIR/link.gguf"
del_case d7 "link.gguf"
expect_kept "a symlink pointing out of the directory" "$TMP/outside/precious.gguf"

# The token itself is half a path.
del_case ../escape "keeper.gguf"
expect_kept "token escaping the request directory" "$SYN_MODEL_DIR/keeper.gguf"

# ── And the one that must WORK ────────────────────────────────
# A boundary that refuses everything is not a boundary, it is a bug.
del_case d8 "keeper.gguf"
if [ "$rc" = 0 ] && [ ! -e "$SYN_MODEL_DIR/keeper.gguf" ]; then
    ok "a legitimate delete removes the model"
else
    bad "a legitimate delete removes the model (rc=$rc, out=$out)"
fi

# Consumed like the fetch request, and for the same reason.
if [ -e "$SYN_MODEL_RUN_DIR/req/d8.delete" ]; then
    bad "delete request is unlinked once read"
else
    ok "delete request is unlinked once read"
fi

echo
[ "$fail" = 0 ] && echo "PASS" || echo "FAIL"
exit $fail
