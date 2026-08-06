#!/usr/bin/env bash
#
# arsenal_test — covers the parts of SYNAPSE Arsenal that can be checked without
# the BlackArch repository being enabled on the build machine. The fixture is
# what makes that possible: it is the same TSV arsenal-query emits, so both
# front-ends can be driven end-to-end offline.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$here/.."
fail=0

check() {  # check <label> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fail=$((fail + 1))
    fi
}

# ── Fixture ─────────────────────────────────────────────────────────────────
F=$(mktemp -d); trap 'rm -rf "$F"' EXIT
printf 'blackarch-scanner\t3\nblackarch-webapp\t2\n' > "$F/categories.tsv"
{
    printf 'nmap\t1\t7.9-1\tNetwork scanner.\n'
    printf 'masscan\t0\t1.3-2\tFast port scanner.\n'
    printf 'zmap\t0\t3.0-1\tInternet-wide scanner.\n'
} > "$F/packages.blackarch-scanner.tsv"

Q="$root/arsenal-query.sh"
export SYN_ARSENAL_FIXTURE="$F"

echo
echo "  arsenal-query"
check "status reports the fixture as enabled" "enabled" \
    "$("$Q" status | cut -f1)"
check "categories are listed" "2" \
    "$("$Q" categories | wc -l)"
check "packages are listed" "3" \
    "$("$Q" packages blackarch-scanner | wc -l)"
check "every package row has 4 fields" "0" \
    "$("$Q" packages blackarch-scanner | awk -F'\t' 'NF!=4' | wc -l)"
check "the installed flag survives the round trip" "1" \
    "$("$Q" packages blackarch-scanner | awk -F'\t' '$1=="nmap"{print $2}')"

# A non-blackarch group must be refused rather than listed: the same code path
# would otherwise happily render 'gnome' as a security category.
"$Q" packages gnome >/dev/null 2>&1
check "a non-blackarch group is rejected" "1" "$?"
check "a missing group argument is rejected" "1" \
    "$("$Q" packages >/dev/null 2>&1; echo $?)"

# ── TUI ─────────────────────────────────────────────────────────────────────
echo
echo "  arsenal-tui"
export SYN_ARSENAL_QUERY="$Q" NO_COLOR=1 SYN_ARSENAL_PAGE=10
tui_out=$(printf '1\nq\nq\n' | "$root/arsenal-tui.sh" 2>&1 | sed 's/\x1b\[[0-9;]*[A-Za-z]//g')
check "the category list renders" "yes" \
    "$(printf '%s' "$tui_out" | grep -q 'scanner' && echo yes || echo no)"
check "drilling into a category lists its packages" "yes" \
    "$(printf '%s' "$tui_out" | grep -q 'masscan' && echo yes || echo no)"
check "an installed package is marked" "yes" \
    "$(printf '%s' "$tui_out" | grep -q '●.*nmap' && echo yes || echo no)"
check "the blackarch- prefix is stripped from categories" "0" \
    "$(printf '%s' "$tui_out" | grep -c 'blackarch-scanner')"

# ── Repo bootstrap: the check that actually matters ──────────────────────────
#
# arsenal-enable-repo refuses to run a strap.sh that no longer pins BlackArch's
# master key. That predicate is the security boundary of the whole feature, so
# it is exercised directly here against a tampered script.
echo
echo "  arsenal-enable-repo"
FPR=$(grep -oE '[0-9A-F]{40}' "$root/arsenal-enable-repo.sh" | head -1)
check "a master key fingerprint is pinned" "40" "${#FPR}"

printf 'echo hi\n--recv-keys %s\n' "$FPR" > "$F/good-strap.sh"
printf 'echo hi\n--recv-keys DEADBEEF00000000000000000000000000000000\n' > "$F/evil-strap.sh"
check "a strap.sh pinning our key passes the check" "0" \
    "$(grep -qF "$FPR" "$F/good-strap.sh"; echo $?)"
check "a strap.sh pinning a DIFFERENT key is refused" "1" \
    "$(grep -qF "$FPR" "$F/evil-strap.sh"; echo $?)"
check "enable-repo requires root" "1" \
    "$("$root/arsenal-enable-repo.sh" >/dev/null 2>&1; echo $?)"

# strap.sh untars the keyring straight into /usr/share/pacman/keyrings, leaving
# three files owned by no package — so a plain `pacman -S blackarch-keyring`
# afterwards dies with "exists in filesystem". Every site that installs the
# keyring must scope an --overwrite over those paths, or enabling the repo and
# then owning its keyring can never both succeed on the same machine.
ka=$(grep -c -- "--overwrite '/usr/share/pacman/keyrings/blackarch\*'" "$root/arsenal-enable-repo.sh")
check "every keyring install in enable-repo scopes --overwrite" "2" "$ka"
# Counted against the INVOCATIONS, not against mentions of the package name:
# comments and the `pacman -Q` presence check name it too, and counting those
# made this assert 6 == 2 and fail for no real reason.
ki=$(grep -c 'pacman -S --noconfirm --needed' "$root/arsenal-enable-repo.sh")
check "no keyring install site was missed" "$ki" "$ka"

# ── Launcher ────────────────────────────────────────────────────────────────
echo
echo "  syn-arsenal"
check "--help works" "0" \
    "$("$root/syn-arsenal.sh" --help >/dev/null 2>&1; echo $?)"
# No display must degrade to the terminal browser, not die with a Wayland error.
out=$(env -u WAYLAND_DISPLAY -u DISPLAY sh -c "printf 'q\n' | '$root/syn-arsenal.sh'" 2>&1 \
      | sed 's/\x1b\[[0-9;]*[A-Za-z]//g')
check "no display falls back to the terminal browser" "yes" \
    "$(printf '%s' "$out" | grep -q 'falling back to the terminal' && echo yes || echo no)"

echo
if [ "$fail" -eq 0 ]; then echo "  all checks passed"; else echo "  $fail check(s) FAILED"; fi
echo
exit $(( fail > 0 ))
