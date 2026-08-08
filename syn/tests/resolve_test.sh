#!/usr/bin/env bash
# resolve_test.sh — the DaVinci Resolve launch override is the whole feature
#
# `syn resolve` mostly reports, and reporting wrong is annoying. One part of it
# is not: the generated desktop entry in /usr/local/share/applications is what
# every menu launch of Resolve goes through once it exists. Two ways for it to
# be silently wrong, and both look identical to a working one from the outside:
#
#   - it still runs /opt/resolve/bin/resolve, so the environment is never
#     applied and nothing says so; or
#   - it loses Name/Icon/MimeType off the packaged entry it was generated from,
#     so Resolve appears in the menu as a nameless, iconless row.
#
# There is no Resolve on a build machine and none on the ISO, so the packaged
# entry is faked here. That is honest rather than a shortcut: the thing under
# test is a transformation of that file, and the file is a fixed format.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
sr="$here/../syn-resolve.sh"

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

[ -f "$sr" ] || { echo "  FAIL  syn-resolve.sh is missing"; exit 1; }

echo "=== syntax ==="
if bash -n "$sr" 2>"$tmp/syntax"; then
    echo "  ok    parses"
else
    echo "  FAIL  does not parse:"; sed 's/^/        /' "$tmp/syntax"; exit 1
fi

# The packaged entry, as the AUR package writes it: RESOLVE_INSTALL_LOCATION
# already substituted, %u on the Exec, and the StartupWMClass the PKGBUILD
# appends. Every field but Exec has to survive.
mkdir -p "$tmp/usr/share/applications" "$tmp/usr/local/share/applications"
cat > "$tmp/usr/share/applications/DaVinciResolve.desktop" <<'ENTRY'
[Desktop Entry]
Type=Application
Name=DaVinci Resolve
GenericName=Video Editor
Exec=/opt/resolve/bin/resolve %u
TryExec=/opt/resolve/bin/resolve
Icon=davinci-resolve
Categories=AudioVideo;AudioVideoEditing;
MimeType=application/x-resolveproj;
StartupWMClass=resolve
ENTRY

run_setup() {
    SYN_RESOLVE_PKG_DESKTOP="$tmp/usr/share/applications/DaVinciResolve.desktop" \
    SYN_RESOLVE_OUR_DESKTOP="$tmp/usr/local/share/applications/DaVinciResolve.desktop" \
    SYN_RESOLVE_ICD_DIR="$tmp/icd" \
    SYN_RESOLVE_BIN="$tmp/opt/resolve/bin/resolve" \
        bash "$sr" setup "$@" >"$tmp/out" 2>&1
}

out="$tmp/usr/local/share/applications/DaVinciResolve.desktop"

echo ""
echo "=== the override is generated from the packaged entry ==="
run_setup --quiet
check "override written" "yes" "$([ -f "$out" ] && echo yes || echo no)"

# THE assertion. A generated file that still launches the raw binary applies no
# environment at all and looks completely normal.
check "Exec goes through the launcher" "1" \
      "$(grep -c '^Exec=/usr/lib/syn/syn-resolve launch' "$out")"
check "no Exec still points at the raw binary" "0" \
      "$(grep -c '^Exec=/opt/resolve/bin/resolve' "$out")"

# The arguments have to survive: %u is how a .resolveproj double-click reaches
# it, and dropping it makes the file association silently do nothing.
check "%u kept on the Exec line" "1" "$(grep -c '^Exec=.*launch %u$' "$out")"

# TryExec must NOT be rewritten — it is the "is this installed" probe, and
# pointing it at our launcher answers yes on a machine with no Resolve.
check "TryExec still probes the real binary" "1" \
      "$(grep -c '^TryExec=/opt/resolve/bin/resolve$' "$out")"

echo ""
echo "=== everything else off the packaged entry survives ==="
for f in "Name=DaVinci Resolve" "GenericName=Video Editor" "Icon=davinci-resolve" \
         "Categories=AudioVideo;AudioVideoEditing;" \
         "MimeType=application/x-resolveproj;" "StartupWMClass=resolve"; do
    check "kept: ${f%%=*}" "1" "$(grep -cxF "$f" "$out")"
done

echo ""
echo "=== it is idempotent ==="
# The pacman hook runs this on every Resolve upgrade AND every syn upgrade, so a
# second run appending or double-rewriting would corrupt it over time.
cp "$out" "$tmp/first"
run_setup --quiet
check "second run is byte-identical" "same" \
      "$(cmp -s "$tmp/first" "$out" && echo same || echo differs)"

echo ""
echo "=== with no Resolve installed it does nothing, quietly ==="
rm -f "$tmp/usr/share/applications/DaVinciResolve.desktop" "$out"
run_setup --quiet
rc=$?    # captured NOW: the next check overwrites $?
check "no override invented" "no" "$([ -f "$out" ] && echo yes || echo no)"
# The hook runs this on every syn upgrade, including on the overwhelming
# majority of machines that will never install Resolve. Exiting non-zero there
# makes pacman report a failed hook after a transaction that was fine.
check "exits clean so the hook stays silent" "0" "$rc"

echo ""
echo "=== the hook contract ==="
hook="$here/../syn-resolve.hook"
check "hook exists" "yes" "$([ -f "$hook" ] && echo yes || echo no)"
# A hook that shells out to pacman deadlocks against the transaction that
# invoked it. The guard is that setup's hook path never installs, so the hook
# must use it.
check "hook calls the non-installing path" "1" \
      "$(grep -c 'Exec = /usr/lib/syn/syn-resolve setup --quiet' "$hook")"
check "hook runs after the transaction" "1" \
      "$(grep -c '^When = PostTransaction' "$hook")"
# Triggering on syn alone would miss a box where Resolve is installed later;
# triggering on Resolve alone would miss one where syn is.
check "triggers on Resolve and on syn" "5" \
      "$(grep -c '^Target = ' "$hook")"

echo ""
echo "=== the PKGBUILD ships all of it ==="
pkgbuild="$here/../PKGBUILD"
for f in syn-resolve.sh syn-resolve.hook; do
    check "in source=(): $f" "1" "$(grep -c "\"$f\"" "$pkgbuild")"
done
check "source and sha256sums are the same length" "same" \
      "$(a=$(sed -n '/^source=(/,/)/p' "$pkgbuild" | grep -c '"');
         b=$(sed -n '/^sha256sums=(/,/)/p' "$pkgbuild" | grep -c "SKIP");
         [ "$a" = "$b" ] && echo same || echo "$a vs $b")"
check "hook installed under its ordered name" "1" \
      "$(grep -c 'libalpm/hooks/74-syn-resolve.hook' "$pkgbuild")"
check "launcher installed where the hook execs it" "1" \
      "$(grep -c 'usr/lib/syn/syn-resolve"' "$pkgbuild")"

# The dispatch in syn.sh has to reach it, and by the same absolute path.
check "syn.sh dispatches resolve" "1" \
      "$(grep -c 'resolve).*exec /usr/lib/syn/syn-resolve' "$here/../syn.sh")"

echo ""
if [ "$fails" -eq 0 ]; then
    echo "all syn resolve checks passed"
else
    echo "$fails check(s) failed"
fi
exit $([ "$fails" -eq 0 ] && echo 0 || echo 1)
