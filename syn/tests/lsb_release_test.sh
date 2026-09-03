#!/usr/bin/env bash
# lsb_release_test.sh — the SynapseOS identity reaches lsb_release
#
# The thing under test is a seam, not a function: syn-lsb-release writes a file
# that a THIRD-PARTY script (/usr/bin/lsb_release, from Arch's lsb-release
# package) sources, and what matters is the sentence that script finally prints.
# Checking only that we wrote four DISTRIB_ lines would pass on a file
# lsb_release ignores — which is exactly the failure this feature exists to fix,
# since /etc/os-release is full of correct values lsb_release never reads.
#
# So the checks below run the real lsb_release against a fake /etc. Its config
# directory is a hardcoded INFO_ROOT="/etc" with no environment override, so the
# rig copies the script and repoints that one line. Nothing here touches the
# live /etc, and nothing needs root.
#
# The fake /etc reproduces the live one exactly, including the two details that
# are the whole reason the bug exists: os-release is a SYMLINK (so lsb_release's
# `find -type f` skips it) and arch-release is an empty regular file (so that is
# what it finds instead).
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
syn="$here/.."
gen="$syn/syn-lsb-release.sh"
hook="$syn/syn-lsb-release.hook"
pkgb="$syn/PKGBUILD"

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

echo "=== files ==="
check "syn-lsb-release.sh exists" yes "$([ -f "$gen" ] && echo yes || echo no)"
check "the hook exists" yes "$([ -f "$hook" ] && echo yes || echo no)"
check "the script parses" yes "$(bash -n "$gen" 2>/dev/null && echo yes || echo no)"

# The PKGBUILD is the only thing that puts either on a disk, and the hook's Exec
# is an absolute path — so the path it names and the path package() installs to
# have to be the same string. They are in two different files, which is how they
# drift.
check "PKGBUILD ships the script" yes \
      "$(grep -q 'usr/lib/syn/syn-lsb-release"' "$pkgb" && echo yes || echo no)"
check "PKGBUILD ships the hook" yes \
      "$(grep -q '75-syn-lsb-release.hook' "$pkgb" && echo yes || echo no)"
exec_path=$(sed -n 's/^Exec = //p' "$hook")
check "the hook execs the path the PKGBUILD installs" "/usr/lib/syn/syn-lsb-release" "$exec_path"

# PostTransaction is not a style preference. The file being rewritten is the one
# pacman writes during the transaction, so PreTransaction would be overwritten
# seconds later and the hook would appear to do nothing.
check "the hook runs PostTransaction" "PostTransaction" "$(sed -n 's/^When = //p' "$hook")"
check "the hook triggers on lsb-release" yes \
      "$(grep -q '^Target = lsb-release$' "$hook" && echo yes || echo no)"
check "the hook triggers on syn itself" yes \
      "$(grep -q '^Target = syn$' "$hook" && echo yes || echo no)"

# ── the rig ─────────────────────────────────────────────────────────────
fake="$tmp/etc"
mkdir -p "$fake" "$tmp/usr/lib"
: > "$fake/arch-release"                       # empty, exactly as Arch ships it
cat > "$tmp/usr/lib/os-release" <<'OSREL'
NAME="SynapseOS"
PRETTY_NAME="SynapseOS 9.9.9"
ID=synapseos
ID_LIKE=arch
VERSION="9.9.9"
VERSION_ID="9.9.9"
OSREL
ln -s ../usr/lib/os-release "$fake/os-release"  # ⚠ a symlink, as on a real box
cat > "$fake/lsb-release" <<'LSB'
DISTRIB_ID="Arch"
DISTRIB_RELEASE="rolling"
DISTRIB_DESCRIPTION="Arch Linux"
LSB

echo ""
echo "=== the script rewrites the file ==="
if ! SYN_OS_RELEASE="$tmp/usr/lib/os-release" SYN_LSB_RELEASE="$fake/lsb-release" \
        bash "$gen" 2>"$tmp/err"; then
    echo "  ABORT  syn-lsb-release failed:"; sed 's/^/    /' "$tmp/err"; exit 1
fi
check "DISTRIB_ID comes from os-release NAME" 'DISTRIB_ID="SynapseOS"' \
      "$(grep '^DISTRIB_ID=' "$fake/lsb-release")"
check "DISTRIB_RELEASE comes from VERSION_ID" 'DISTRIB_RELEASE="9.9.9"' \
      "$(grep '^DISTRIB_RELEASE=' "$fake/lsb-release")"
check "DISTRIB_DESCRIPTION comes from PRETTY_NAME" 'DISTRIB_DESCRIPTION="SynapseOS 9.9.9"' \
      "$(grep '^DISTRIB_DESCRIPTION=' "$fake/lsb-release")"
# Written empty it would read like a codename we failed to fill in, and
# lsb_release prints "n/a" for absent and empty alike.
check "no empty codename line where os-release has none" 0 \
      "$(grep -c '^DISTRIB_CODENAME=' "$fake/lsb-release")"
check "it left no temp file behind" 0 "$(find "$fake" -name 'lsb-release.syn.*' | wc -l)"

echo ""
echo "=== lsb_release actually says it ==="
# Guarded, not assumed: lsb-release is an optional Arch package (it arrives as a
# steam dependency), so a checkout on a box without it says so rather than
# passing vacuously.
if ! command -v lsb_release >/dev/null 2>&1; then
    echo "  SKIP  lsb_release is not installed — the file checks above still ran"
else
    sed -e "s|^INFO_ROOT=\"/etc\"|INFO_ROOT=\"$fake\"|" \
        -e "s|^ALTERNATE_DISTRIB_FILE=\"/etc/|ALTERNATE_DISTRIB_FILE=\"$fake/|" \
        -e "s|^CHECKFIRST=\"/etc/|CHECKFIRST=\"$fake/|" \
        "$(command -v lsb_release)" > "$tmp/lsb_release"
    chmod +x "$tmp/lsb_release"
    check "Distributor ID"  "SynapseOS"         "$("$tmp/lsb_release" -i -s)"
    # ⚠ THE QUOTES ARE lsb_release's OWN, not ours and not a bug to fix. In
    # short mode it prints the description as \"$DISTRIB_DESCRIPTION\" — a
    # literal pair of quote characters around the value. Arch's own file comes
    # back as "Arch Linux" the same way, so expecting a bare string here fails
    # against correct output and invites somebody to strip quotes we never wrote.
    want_desc='"SynapseOS 9.9.9"'   # the inner quotes are part of the value
    check "Description"     "$want_desc"        "$("$tmp/lsb_release" -d -s)"
    check "Release"         "9.9.9"             "$("$tmp/lsb_release" -r -s)"

    # ⚠ The rig has to be able to FAIL, or every check above is decoration.
    # Restore the Arch file and confirm the same harness reports Arch — that is
    # what proves these values came from our file and not from somewhere else.
    printf 'DISTRIB_ID="Arch"\nDISTRIB_RELEASE="rolling"\nDISTRIB_DESCRIPTION="Arch Linux"\n' \
        > "$fake/lsb-release"
    check "the rig still reports Arch without our file" "Arch" "$("$tmp/lsb_release" -i -s)"
fi

echo ""
echo "=== the guards ==="
# Creating the file where the package is absent would leave an unowned file at a
# path lsb-release owns, and the next `pacman -S steam` would abort the whole
# transaction with "exists in filesystem".
SYN_OS_RELEASE="$tmp/usr/lib/os-release" SYN_LSB_RELEASE="$tmp/absent" bash "$gen"
check "absent lsb-release is left absent" no "$([ -e "$tmp/absent" ] && echo yes || echo no)"

# A damaged or missing os-release must leave whatever is there alone rather than
# blank the file out — a machine that cannot say its own name is not improved by
# also losing the answer it had.
printf 'DISTRIB_ID="Arch"\n' > "$tmp/keep"
SYN_OS_RELEASE="$tmp/nonexistent" SYN_LSB_RELEASE="$tmp/keep" bash "$gen"
check "missing os-release leaves the file untouched" 'DISTRIB_ID="Arch"' "$(cat "$tmp/keep")"
printf 'ID=broken\nVERSION_ID=1\n' > "$tmp/os-noname"
SYN_OS_RELEASE="$tmp/os-noname" SYN_LSB_RELEASE="$tmp/keep" bash "$gen"
check "os-release with no NAME leaves it untouched" 'DISTRIB_ID="Arch"' "$(cat "$tmp/keep")"

# The hook re-runs on every upgrade of lsb-release, so a second run has to land
# on the same bytes as the first.
cp "$tmp/usr/lib/os-release" "$tmp/os-cn"
echo 'VERSION_CODENAME=prism' >> "$tmp/os-cn"
printf 'DISTRIB_ID="Arch"\n' > "$tmp/idem"
SYN_OS_RELEASE="$tmp/os-cn" SYN_LSB_RELEASE="$tmp/idem" bash "$gen"
first=$(md5sum < "$tmp/idem")
check "VERSION_CODENAME is carried through when set" 'DISTRIB_CODENAME="prism"' \
      "$(grep '^DISTRIB_CODENAME=' "$tmp/idem")"
SYN_OS_RELEASE="$tmp/os-cn" SYN_LSB_RELEASE="$tmp/idem" bash "$gen"
check "a second run writes the same bytes" "$first" "$(md5sum < "$tmp/idem")"

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
