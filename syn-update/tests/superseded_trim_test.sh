#!/usr/bin/env bash
# superseded_trim_test.sh — trim_superseded keeps the last few of each package
#
# Every apply retires the version it replaced into /var/cache/synapseos/
# superseded, and nothing ever deleted from there: the dev box reached 959
# files and 15 GB. trim_superseded is pulled out of the script and run against
# a scratch directory, with sudo_safe stubbed to run the command as the user.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later

here=$(cd "$(dirname "$0")" && pwd)
script="$here/../syn-update.sh"

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1"; fail=$((fail + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else
            printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fail=$((fail + 1)); fi }

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
repo="$tmp/repo"; old="$repo/superseded"

run_trim() {
    {
        echo 'info() { echo "info  $*"; }'
        echo 'sudo_safe() { "$@"; }'
        echo "LOCAL_REPO=$repo"
        grep '^SUPERSEDED_KEEP=' "$script"
        sed -n '/^trim_superseded() {/,/^}/p' "$script"
        echo 'trim_superseded'
    } > "$tmp/run.sh"
    bash "$tmp/run.sh" 2>&1
}

# put <file> <minutes ago>: a retired package, published that long ago.
put() { : > "$old/$1"; touch -d "@$(( $(date +%s) - $2 * 60 ))" "$old/$1"; }
left() { (cd "$old" && ls -1 | sort | tr '\n' ' '); }

echo "=== the newest two of each package stay ==="
mkdir -p "$old"
put synui-0.1.0-640-x86_64.pkg.tar.zst 40
put synui-0.1.0-641-x86_64.pkg.tar.zst 30
put synui-0.1.0-642-x86_64.pkg.tar.zst 20
put synui-0.1.0-643-x86_64.pkg.tar.zst 10
put synui-debug-0.1.0-642-x86_64.pkg.tar.zst 20
put synui-debug-0.1.0-643-x86_64.pkg.tar.zst 10
put syn-install-0.1.0-50-any.pkg.tar.zst 30
put syn-install-0.1.0-51-any.pkg.tar.zst 20
put syn-install-0.1.0-52-any.pkg.tar.zst 10
put chibi-0.1.0-26-x86_64.pkg.tar.zst 5
put scenefx0.5-0.5-2-x86_64.pkg.tar.zst 50
put scenefx0.5-0.5-3-x86_64.pkg.tar.zst 40
put scenefx0.5-0.5-4-x86_64.pkg.tar.zst 30
out=$(run_trim)
check "older synui and syn-install files go, the rest stay" \
    "chibi-0.1.0-26-x86_64.pkg.tar.zst scenefx0.5-0.5-3-x86_64.pkg.tar.zst scenefx0.5-0.5-4-x86_64.pkg.tar.zst syn-install-0.1.0-51-any.pkg.tar.zst syn-install-0.1.0-52-any.pkg.tar.zst synui-0.1.0-642-x86_64.pkg.tar.zst synui-0.1.0-643-x86_64.pkg.tar.zst synui-debug-0.1.0-642-x86_64.pkg.tar.zst synui-debug-0.1.0-643-x86_64.pkg.tar.zst " \
    "$(left)"
case "$out" in
    *"deleted 4 old package file(s)"*) ok "says how many it deleted" ;;
    *) bad "no count of deleted files: [$out]" ;;
esac

echo "=== a second run deletes nothing and says nothing ==="
out=$(run_trim)
check "nothing more is deleted" 9 "$(ls -1 "$old" | wc -l)"
check "and nothing is reported" "" "$out"

echo "=== the order is when it was published, not the version ==="
rm -rf "$old"; mkdir -p "$old"
# A hold or a downgrade published 0.1.0-9 after 0.1.0-12.
put syn-0.1.0-10-any.pkg.tar.zst 30
put syn-0.1.0-12-any.pkg.tar.zst 20
put syn-0.1.0-9-any.pkg.tar.zst 10
run_trim >/dev/null
check "the two published last stay" \
    "syn-0.1.0-12-any.pkg.tar.zst syn-0.1.0-9-any.pkg.tar.zst " "$(left)"

echo "=== a deleted package takes its signature with it ==="
rm -rf "$old"; mkdir -p "$old"
put limine-snapper-sync-1.30.0-1-x86_64.pkg.tar.zst 30
put limine-snapper-sync-1.30.0-1-x86_64.pkg.tar.zst.sig 30
put limine-snapper-sync-1.31.0-1-x86_64.pkg.tar.zst 20
put limine-snapper-sync-1.31.0-1-x86_64.pkg.tar.zst.sig 20
put limine-snapper-sync-1.32.1-1-x86_64.pkg.tar.zst 10
put limine-snapper-sync-1.32.1-1-x86_64.pkg.tar.zst.sig 10
run_trim >/dev/null
check "the oldest package and its .sig are gone" \
    "limine-snapper-sync-1.31.0-1-x86_64.pkg.tar.zst limine-snapper-sync-1.31.0-1-x86_64.pkg.tar.zst.sig limine-snapper-sync-1.32.1-1-x86_64.pkg.tar.zst limine-snapper-sync-1.32.1-1-x86_64.pkg.tar.zst.sig " \
    "$(left)"

echo "=== a file that is not a package is left alone ==="
rm -rf "$old"; mkdir -p "$old"
put notes.txt 60
put odd.pkg.tar.zst 50
put odd.pkg.tar.zst.part 40
run_trim >/dev/null
check "nothing deleted" "notes.txt odd.pkg.tar.zst odd.pkg.tar.zst.part " "$(left)"

echo "=== no superseded directory, or an empty one ==="
rm -rf "$old"
out=$(run_trim); rc=$?
check "no directory: returns 0" 0 "$rc"
check "no directory: says nothing" "" "$out"
mkdir -p "$old"
out=$(run_trim); rc=$?
check "empty directory: returns 0" 0 "$rc"
check "empty directory: says nothing" "" "$out"

echo "=== refresh_local_repo trims, after retiring and before the index ==="
order=$(sed -n '/^refresh_local_repo() {/,/^}/p' "$script" \
        | grep -oE '^ +(prune_superseded|retire_shadowing|trim_superseded|rebuild_db)$' | tr -d ' ' | tr '\n' ' ')
check "call order" "prune_superseded retire_shadowing trim_superseded rebuild_db " "$order"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
