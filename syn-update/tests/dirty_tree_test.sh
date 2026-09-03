#!/usr/bin/env bash
# dirty_tree_test.sh — the guard that refuses to fetch into a modified tree,
# and the escape hatch that was supposed to get past it.
#
# ⛔ THIS IS A REGRESSION SUITE FOR ONE OUTAGE. chibi 22 renamed its whisper
# downloads from `whisper-model.bin` to `whisper-<rev>-model.bin` and rewrote
# .gitignore to match the new shape ONLY. Every machine that had ever built
# chibi kept four files on disk that the new patterns did not cover; they
# became untracked; fetch_src refuses to fetch into a tree with local
# modifications — and BOTH of velle's machines died on every check, with the
# window reporting "Could not determine update status".
#
# Two things have to hold for that to be impossible rather than merely fixed:
# the ignore patterns must cover the name a rename LEFT BEHIND, and --force
# must actually leave a clean tree.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

here=$(cd "$(dirname "$0")" && pwd)
script="$here/../syn-update.sh"
repo=$(cd "$here/../.." && pwd)

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1"; fail=$((fail + 1)); }

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

echo "syn-update: the dirty-tree guard"

# ── 1. the ignore patterns, against BOTH names chibi has used ───────────────
#
# The repository's real .gitignore, not a copy of what it ought to say.
g=$tmp/ignore
mkdir -p "$g/chibi" && git -C "$g" init -q .
cp "$repo/.gitignore" "$g/.gitignore"
git -C "$g" add .gitignore
git -C "$g" -c user.email=t@t -c user.name=t commit -qm base

# The name chibi 22 moved TO…
touch "$g/chibi/whisper-536b0662742c-model.bin" \
      "$g/chibi/whisper-536b0662742c-config.json" \
      "$g/chibi/whisper-536b0662742c-tokenizer.json" \
      "$g/chibi/whisper-536b0662742c-vocabulary.txt"
# …and the one it moved FROM, which is still on every machine that built it.
touch "$g/chibi/whisper-model.bin" "$g/chibi/whisper-config.json" \
      "$g/chibi/whisper-tokenizer.json" "$g/chibi/whisper-vocabulary.txt"

left=$(git -C "$g" status --porcelain)
if [ -z "$left" ]; then
    ok "both of chibi's whisper naming schemes are ignored"
else
    bad "these would block every syn-update check: $(echo "$left" | tr '\n' ' ')"
fi

# ⚠ THE OLD NAMES SPECIFICALLY, asserted on their own — a pattern that covered
# only the new ones passes the shape of the check above the moment somebody
# deletes the legacy files from their machine, which is what made this
# invisible on the box it was written on.
rm -f "$g"/chibi/whisper-536b*
left=$(git -C "$g" status --porcelain)
[ -z "$left" ] \
    && ok "…including the pre-rename names on their own" \
    || bad "the legacy download is untracked: $(echo "$left" | tr '\n' ' ')"

# ── 2. --force has to leave a CLEAN tree ────────────────────────────────────
#
# ⛔ `reset --hard` RESTORES TRACKED FILES AND LEAVES UNTRACKED ONES ALONE. So
# a tree made dirty by a stray file passed the guard under --force, got reset,
# and was still dirty on the next run: the one documented escape hatch did not
# escape, and the refusal repeated for ever.
run_checkout() {   # run_checkout <SRC> <FORCE>
    {
        echo "SRC=$1"; echo "FORCE=$2"; echo 'REPO_REF=main'
        echo 'die() { echo "fail  $*"; exit 1; }'
        sed -n '/^checkout_remote() {/,/^}/p' "$script"
        echo 'checkout_remote'
    } | bash 2>&1
}

s=$tmp/src
mkdir -p "$s" && git -C "$s" init -q -b main .
printf 'keep-*.bin\n' > "$s/.gitignore"
git -C "$s" add .gitignore
git -C "$s" -c user.email=t@t -c user.name=t commit -qm base
git -C "$s" branch -f origin/main main 2>/dev/null
git -C "$s" remote add origin "$s" 2>/dev/null
git -C "$s" update-ref refs/remotes/origin/main main

touch "$s/stray-leaving.txt"     # untracked, NOT ignored — the outage
touch "$s/keep-461mb.bin"        # untracked but IGNORED — the model

run_checkout "$s" 1 >/dev/null 2>&1
after=$(git -C "$s" status --porcelain)
[ -z "$after" ] \
    && ok "--force leaves the tree clean, so the next check is not refused" \
    || bad "--force left it dirty: $(echo "$after" | tr '\n' ' ')"

# ⚠ -fd AND NOT -fdx. Without -x, git clean leaves ignored files alone — which
# is the 461MB whisper model and every other cached download this tree keeps.
# -x would re-download half a gigabyte to remove a stray text file.
[ -f "$s/keep-461mb.bin" ] \
    && ok "…without deleting the cached downloads it is ignoring" \
    || bad "--force deleted an ignored file — that is a re-download of the model"

# ── 3. and it must NOT clean when it was not asked to ───────────────────────
s2=$tmp/src2
mkdir -p "$s2" && git -C "$s2" init -q -b main .
git -C "$s2" commit -q --allow-empty -m base -c user.email=t@t -c user.name=t 2>/dev/null ||
    git -C "$s2" -c user.email=t@t -c user.name=t commit -q --allow-empty -m base
git -C "$s2" update-ref refs/remotes/origin/main main
touch "$s2/someone-elses-work.txt"
run_checkout "$s2" 0 >/dev/null 2>&1
[ -f "$s2/someone-elses-work.txt" ] \
    && ok "an unforced checkout does not delete anything" \
    || bad "checkout_remote cleaned without --force — that is somebody's work gone"

echo
[ "$fail" = 0 ] && echo "$pass passed, 0 failed" \
                || { echo "$pass passed, $fail failed"; exit 1; }
