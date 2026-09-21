#!/usr/bin/env bash
# restore_missing_files_test.sh — a package whose files vanished is put back,
# and nothing else is touched.
#
# The failure this exists for: synapse-voice took the speech models over from
# chibi 26 in one `pacman -U`, chibi 27 was upgraded in the next, and pacman
# deleted the models because chibi's old file list still named them. See
# tools/restore-missing-files.sh.
#
# pacman and sudo are stand-ins on PATH, driven by a tiny database of file
# lists under $T/db and a fake root under $T/root, so this runs anywhere and
# can never reinstall anything on the machine running it.
#
#   1. files ABSENT from disk → reinstalled from the tree's package, and back
#   2. ⛔ "Permission denied" is not missing (synguard's root-only rules.d)
#   3. an intact package is left alone
#   4. no package file of the installed version → says so, installs nothing
#   5. the tree lacks it → the local repo / pacman cache is used
#   6. synapse-llama answers as synapse-llama-cuda; that name finds the file
#   7. ⛔ an OLDER build in the tree is never used (that is a downgrade)
#   8. ⛔ syn never picks up syn-update's package file
#   9. a component that is not installed is skipped
#  10. reinstalled and still missing → reported, and the run still exits 0
#  11. build-all.sh calls it after its last install, over every component
set -u

here=$(cd "$(dirname "$0")" && pwd)
S=${1:-$here/../restore-missing-files.sh}
B=$here/../../build-all.sh
[ -f "$S" ] || { echo "  ABORT no restore-missing-files.sh at $S"; exit 1; }

pass=0; fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
export FAKEDB="$T/db" ROOT="$T/root" CALLS="$T/calls"
mkdir -p "$T/bin" "$FAKEDB/.provides" "$ROOT" "$T/tree" "$T/repo" "$T/cache"
: > "$CALLS"

cat > "$T/bin/pacman" <<'EOF'
#!/usr/bin/env bash
db=$FAKEDB
case $1 in
  -Q)
    n=$2; [ -f "$db/.provides/$n" ] && n=$(cat "$db/.provides/$n")
    [ -f "$db/$n/version" ] || exit 1
    echo "$n $(cat "$db/$n/version")" ;;
  -Qk)
    n=$2; total=0; miss=0
    while read -r p; do
        total=$((total + 1))
        if grep -qxF "$p" "$db/$n/eacces" 2>/dev/null; then
            echo "warning: $n: $p (Permission denied)" >&2; miss=$((miss + 1))
        elif [ ! -e "$ROOT$p" ]; then
            echo "warning: $n: $p (No such file or directory)" >&2; miss=$((miss + 1))
        fi
    done < "$db/$n/files"
    echo "$n: $total total files, $miss missing files"
    [ "$miss" -eq 0 ] ;;
  -U)
    shift
    while [ $# -gt 0 ]; do
        case $1 in --noconfirm) shift ;; --overwrite) shift 2 ;; *) f=$1; shift ;; esac
    done
    echo "$f" >> "$CALLS"
    n=$(head -1 "$f")
    [ -f "$db/$n/broken" ] && exit 0
    while read -r p; do mkdir -p "$(dirname "$ROOT$p")"; : > "$ROOT$p"; done < "$db/$n/files" ;;
esac
EOF
printf '#!/bin/sh\nexec "$@"\n' > "$T/bin/sudo"
chmod +x "$T/bin/pacman" "$T/bin/sudo"
export PATH="$T/bin:$PATH" RESTORE_PKG_DIRS="$T/repo:$T/cache"

# pkg <name> <version> <file>... — installed, every file present on disk.
pkg() {
    local n=$1 v=$2; shift 2
    mkdir -p "$FAKEDB/$n"; echo "$v" > "$FAKEDB/$n/version"
    printf '%s\n' "$@" > "$FAKEDB/$n/files"
    local p; for p in "$@"; do mkdir -p "$(dirname "$ROOT$p")"; : > "$ROOT$p"; done
}
# pkgfile <dir> <name> <version> <arch> — a package file naming its package.
pkgfile() { mkdir -p "$1"; echo "$2" > "$1/$2-$3-$4.pkg.tar.zst"; }

pkg synapse-voice 0.1.0-1 /usr/share/faster-whisper/small/model.bin \
    /usr/share/piper-voices/en_GB-cori-medium.onnx /usr/lib/synapse-voice/engine
rm "$ROOT/usr/share/faster-whisper/small/model.bin" "$ROOT/usr/share/piper-voices/en_GB-cori-medium.onnx"
pkgfile "$T/tree/synapse-voice" synapse-voice 0.1.0-1 x86_64

pkg synguard 0.1.0-9 /etc/synguard/rules.d/00-base.rules /usr/bin/synguard
echo /etc/synguard/rules.d/00-base.rules > "$FAKEDB/synguard/eacces"
pkgfile "$T/tree/synguard" synguard 0.1.0-9 x86_64

pkg chibi 0.1.0-27 /usr/lib/chibi/main.py
pkgfile "$T/tree/chibi" chibi 0.1.0-27 x86_64

pkg syn-model 0.1.0-4 /usr/bin/syn-model
rm "$ROOT/usr/bin/syn-model"

pkg synui 0.1.0-619 /usr/bin/synui
rm "$ROOT/usr/bin/synui"
pkgfile "$T/tree/synui" synui 0.1.0-618 x86_64      # older build left in the tree
pkgfile "$T/cache" synui 0.1.0-619 x86_64

pkg synapse-llama-cuda 0.1.0-2 /usr/lib/libllama.so
echo synapse-llama-cuda > "$FAKEDB/.provides/synapse-llama"
rm "$ROOT/usr/lib/libllama.so"
pkgfile "$T/tree/synapse-llama" synapse-llama-cuda 0.1.0-2 x86_64

pkg syn 0.1.0-35 /usr/bin/syn
rm "$ROOT/usr/bin/syn"
pkgfile "$T/repo" syn-update 0.1.0-35 any            # same version string, other package
pkgfile "$T/tree/syn" syn 0.1.0-350 any              # a version that merely starts the same

pkg vibe 0.1.0-34 /usr/bin/vibe
rm "$ROOT/usr/bin/vibe"
pkgfile "$T/tree/vibe" vibe 0.1.0-34 any
touch "$FAKEDB/vibe/broken"                          # reinstalling does not bring it back

out=$(bash "$S" "$T/tree" synapse-voice synguard chibi syn-model synui \
      synapse-llama syn vibe tepris 2>&1); rc=$?
called() { grep -qxF "$1" "$CALLS"; }

# 1
if called "$T/tree/synapse-voice/synapse-voice-0.1.0-1-x86_64.pkg.tar.zst" &&
   [ -e "$ROOT/usr/share/faster-whisper/small/model.bin" ] &&
   [ -e "$ROOT/usr/share/piper-voices/en_GB-cori-medium.onnx" ] &&
   grep -q "synapse-voice restored" <<< "$out"; then
    ok "missing models put back from the tree's synapse-voice package"
else bad "synapse-voice not restored"; fi

# 2
grep -q "/synguard" "$CALLS" && bad "synguard reinstalled over Permission denied" ||
    ok "Permission denied is not missing — synguard left alone"

# 3
grep -q "/chibi" "$CALLS" && bad "intact chibi was reinstalled" || ok "intact package left alone"

# 4
if grep -q "syn-model: no syn-model-0.1.0-4 package file" <<< "$out" &&
   grep -q "sudo pacman -S syn-model" <<< "$out" && ! grep -q "syn-model" "$CALLS"; then
    ok "no package file: says so, names the fix, installs nothing"
else bad "missing package file not reported properly"; fi

# 5 + 7
if called "$T/cache/synui-0.1.0-619-x86_64.pkg.tar.zst" && ! called "$T/tree/synui/synui-0.1.0-618-x86_64.pkg.tar.zst"; then
    ok "exact version from pacman's cache; the tree's older build ignored"
else bad "synui reinstalled from the wrong file"; fi

# 6
if called "$T/tree/synapse-llama/synapse-llama-cuda-0.1.0-2-x86_64.pkg.tar.zst" && [ -e "$ROOT/usr/lib/libllama.so" ]; then
    ok "synapse-llama resolved to synapse-llama-cuda and restored"
else bad "synapse-llama-cuda not restored"; fi

# 8
if ! grep -q "syn-update-0.1.0-35" "$CALLS" && ! grep -q "syn-0.1.0-350" "$CALLS" &&
   grep -q "syn: no syn-0.1.0-35 package file" <<< "$out"; then
    ok "syn never matched syn-update's file or syn 0.1.0-350"
else bad "syn picked up another package's file"; fi

# 9
grep -q tepris <<< "$out$(cat "$CALLS")" && bad "uninstalled tepris was acted on" ||
    ok "a component that is not installed is skipped"

# 10
if grep -q "vibe: 1 file(s) still missing after reinstalling" <<< "$out" && [ "$rc" -eq 0 ] &&
   grep -q "3 package(s) still have missing files" <<< "$out"; then
    ok "still missing after reinstall: reported; exit status stays 0"
else bad "unrecovered package not reported, or rc=$rc"; fi

# 11
if [ -f "$B" ]; then
    call=$(grep -n 'tools/restore-missing-files.sh" "$BASE" "${KNOWN\[@\]}"' "$B" | cut -d: -f1 | head -1)
    last=$(grep -n -E '^(build_component|build_script_pkg|build_vendored_pkg) ' "$B" | tail -1 | cut -d: -f1)
    if [ -n "$call" ] && [ -n "$last" ] && [ "$call" -gt "$last" ]; then
        ok "build-all.sh runs it after its last install (line $call > $last), over KNOWN"
    else bad "build-all.sh does not call it after the last install (call=${call:-none}, last=$last)"; fi
else bad "no build-all.sh at $B"; fi

echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
