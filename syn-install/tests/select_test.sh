#!/usr/bin/env bash
# select_test.sh — the checkbox pages: the tables, the presets, and the deps
#
# Step 4 stopped being "one preset and six y/n questions" and became ~70
# checkboxes across six pages. Three things about that can go wrong silently,
# and none of them fails a build:
#
#   1. A ROW THAT NAMES A PACKAGE NOTHING CARRIES. The target has the local
#      SynapseOS repo and the Arch mirrors and nothing else at that point. A
#      name pacman cannot resolve does not fail loudly — it loses the whole
#      transaction it is in, which is what samsung-m2020 did to the Full preset.
#   2. A PRESET THAT DRIFTS FROM THE TABLE. Full, Standard and Minimal are
#      columns of the same rows now precisely so they cannot; this checks that
#      sel_reset actually reads them, because a typo in the column index would
#      make every preset identical and nothing would say so.
#   3. A DEPENDENCY RULE THAT DOES NOT MATCH THE PKGBUILD. sel_resolve_deps
#      re-ticks what a kept package hard-depends on. If that list and the real
#      `depends=` disagree, the install still works — pacman pulls the package
#      in anyway — but the summary, the package count and the service
#      enablement all describe a machine that was not installed.
#
# Run through the SYN_INSTALL_SOURCE_ONLY seam, so nothing here touches a disk.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
script="$here/../syn-install.sh"
base=$(cd "$here/../.." && pwd)

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

# shellcheck disable=SC1090
SYN_INSTALL_SOURCE_ONLY=1 source "$script"

echo "=== the tables are well formed ==="

rows_total=0
for list in "${SEL_ALL_LISTS[@]}"; do
    declare -n rows="$list"
    for row in "${rows[@]}"; do
        rows_total=$((rows_total + 1))
        IFS='|' read -r key std full group pkgs label desc <<<"$row"
        # Seven fields, every one non-empty. A missing separator silently shifts
        # every column left — the packages field then holds a label, and the
        # install asks pacman for "Files".
        n=$(awk -F'|' '{print NF}' <<<"$row")
        check "$key has 7 fields" 7 "$n"
        check "$key: std is 0 or 1" yes "$([ "$std" = 0 ] || [ "$std" = 1 ] && echo yes || echo no)"
        check "$key: full is 0 or 1" yes "$([ "$full" = 0 ] || [ "$full" = 1 ] && echo yes || echo no)"
        check "$key: group is core, app, sw or flat" yes \
              "$(case "$group" in core|app|sw|flat) echo yes ;; *) echo no ;; esac)"
        check "$key: names at least one package" yes "$([ -n "$pkgs" ] && echo yes || echo no)"
        # The widths the two-column page is built on. printf truncates rather
        # than wrapping, so an over-long one is not a broken layout — it is a
        # description silently cut off mid-word, which is worth catching here.
        check "$key: label fits the column (<= 13)" yes \
              "$([ "${#label}" -le 13 ] && echo yes || echo no)"
        check "$key: description fits the column (<= 15)" yes \
              "$([ "${#desc}" -le 15 ] && echo yes || echo no)"
    done
done
check "the tables are not empty" yes "$([ "$rows_total" -ge 60 ] && echo yes || echo no)"

echo ""
echo "=== every key is unique ==="
#
# Two rows with the same key is the worst shape here: the second silently wins,
# the first checkbox does nothing, and the profile key means whichever the
# author was not thinking of.
all_keys=$(for list in "${SEL_ALL_LISTS[@]}"; do
               declare -n rows="$list"
               printf '%s\n' "${rows[@]}" | cut -d'|' -f1
           done)
check "no duplicate keys" "$(wc -l <<<"$all_keys")" "$(sort -u <<<"$all_keys" | wc -l)"

echo ""
echo "=== our packages exist in this tree ==="
#
# Only the comp_ rows: a sw_ row names an Arch package, which cannot be checked
# without the repos, and pacman -Si here would answer against THIS machine's
# repositories rather than the target's.
for list in SEL_COMPONENTS; do
    declare -n rows="$list"
    for row in "${rows[@]}"; do
        IFS='|' read -r key std full group pkgs _ <<<"$row"
        for p in $pkgs; do
            # By PKGNAME, not by directory: linux-wallpaperengine is built out
            # of linux-wallpaperengine-pkg/, so checking for a directory of the
            # same name reports a package that certainly exists as missing.
            # What the ISO's local repo will actually carry is the pkgname.
            check "$key: $p is a pkgname in this tree" yes \
                  "$(grep -lxF "pkgname=$p" "$base"/*/PKGBUILD >/dev/null 2>&1 \
                     && echo yes || echo no)"
        done
    done
done

echo ""
echo "=== the presets read their own columns ==="

sel_reset standard; std_core="$(sel_packages core)"; std_app="$(sel_packages app)"; std_sw="$(sel_packages sw)"
sel_reset full;     full_sw="$(sel_packages sw)";    full_app="$(sel_packages app)"
sel_reset minimal;  min_core="$(sel_packages core)"; min_app="$(sel_packages app)"; min_sw="$(sel_packages sw)"

check "Minimal installs no apps"     "" "$min_app"
check "Minimal installs no software" "" "$min_sw"
check "Minimal still installs the core" yes \
      "$([ -n "$min_core" ] && echo yes || echo no)"
# The whole point of Standard: an installed machine has a browser. It did not,
# unless the Full preset happened to drag Firefox in as nexus-chat's dependency.
check "Standard installs a web browser" yes \
      "$(grep -q firefox <<<"$std_sw" && echo yes || echo no)"
check "Full installs more software than Standard" yes \
      "$([ "$(wc -w <<<"$full_sw")" -gt "$(wc -w <<<"$std_sw")" ] && echo yes || echo no)"
check "Full installs at least as many apps as Standard" yes \
      "$([ "$(wc -w <<<"$full_app")" -ge "$(wc -w <<<"$std_app")" ] && echo yes || echo no)"
# Steam is the one thing that must NOT be on these pages: it turns on multilib
# and [cachyos], which is want_steam's job and has a whole block of its own.
check "steam is not a checkbox row" no \
      "$(grep -qxF steam <<<"$(tr ' ' '\n' <<<"$full_sw")" && echo yes || echo no)"

echo ""
echo "=== the dependency rules match the PKGBUILDs ==="
#
# Each rule says "keeping <by> forces <need>". The proof is the dependent's own
# depends= line, read from the tree rather than from memory.
declare -A pkg_of=()
for list in SEL_COMPONENTS; do
    declare -n rows="$list"
    for row in "${rows[@]}"; do
        IFS='|' read -r key _ _ _ pkgs _ <<<"$row"
        pkg_of[$key]="${pkgs%% *}"
    done
done

# The same pairs sel_resolve_deps walks, scraped out of it so a rule added
# there without a PKGBUILD behind it is caught here.
rules=$(grep -oE 'comp_[a-z]+:comp_[a-z]+' "$script" | sort -u)
check "the rules were found in the script" yes \
      "$([ -n "$rules" ] && echo yes || echo no)"
for pair in $rules; do
    need=${pair%%:*}; by=${pair#*:}
    need_pkg="${pkg_of[$need]:-}"; by_pkg="${pkg_of[$by]:-}"
    check "$by -> $need: both are rows in the table" yes \
          "$([ -n "$need_pkg" ] && [ -n "$by_pkg" ] && echo yes || echo no)"
    check "$by_pkg really depends on $need_pkg" yes \
          "$(sed -n '/^depends=/,/)/p' "$base/$by_pkg/PKGBUILD" |
             grep -q "'$need_pkg'" && echo yes || echo no)"
done

echo ""
echo "=== deselecting a dependency turns it back on ==="

sel_reset standard
PICKED[comp_syntty]=0
PICKED[comp_synapd]=0
PICKED[comp_synconfine]=0
PICKED[comp_synmodel]=0
WANT_MODEL=1; MODEL_CHOICE=mistral-7b
# ⚠ NOT `out=$(sel_resolve_deps)`. Command substitution is a subshell, so every
# PICKED it re-ticks is discarded the moment it returns and all four assertions
# below read the values they started with — a test that reports the feature
# broken while the feature works.
tmp=$(mktemp)
sel_resolve_deps >"$tmp" 2>&1
out=$(cat "$tmp"); rm -f "$tmp"
check "syntty came back (synui depends on it)"   1 "${PICKED[comp_syntty]}"
check "synapd came back (synnet depends on it)"  1 "${PICKED[comp_synapd]}"
check "syn-confine came back (vibe depends on it)" 1 "${PICKED[comp_synconfine]}"
check "syn-model came back (a model was chosen)" 1 "${PICKED[comp_synmodel]}"
check "and it said so rather than doing it quietly" yes \
      "$(grep -q 'Added back' <<<"$out" && echo yes || echo no)"

# The other direction: with nothing that needs them, they stay off. A rule that
# fires unconditionally is indistinguishable from no rule at all.
sel_reset minimal
for k in comp_synui comp_synnet comp_vibe comp_synfirstboot; do PICKED[$k]=0; done
PICKED[comp_syntty]=0; PICKED[comp_synapd]=0; PICKED[comp_synconfine]=0; PICKED[comp_synmodel]=0
WANT_MODEL=0; MODEL_CHOICE=none
sel_resolve_deps >/dev/null 2>&1
check "syntty stays off when synui is off"     0 "${PICKED[comp_syntty]}"
check "synapd stays off when nothing needs it" 0 "${PICKED[comp_synapd]}"
check "syn-model stays off when no model was chosen" 0 "${PICKED[comp_synmodel]}"

echo ""
echo "=== a profile answers a page without asking ==="
#
# The unattended contract: with every row on a page answered, multi_select
# prints the page and returns instead of blocking on read. If this regresses,
# a graphical install hangs on a page nobody can see.
sel_reset standard
for row in "${SEL_SW_MEDIA[@]}"; do
    ANSWERS[$(cut -d'|' -f1 <<<"$row")]=no
done
ANSWERS[sw_mpv]=yes
# Same subshell trap as above: multi_select writes PICKED, so it cannot run
# inside $( ). Its output goes to a file and the assertions read the array.
tmp=$(mktemp)
multi_select "Audio and video" SEL_SW_MEDIA </dev/null >"$tmp" 2>&1
out=$(cat "$tmp"); rm -f "$tmp"
check "the page did not ask" no \
      "$(grep -q 'Toggle' <<<"$out" && echo yes || echo no)"
check "but it printed the page for the transcript" yes \
      "$(grep -q 'Audio and video' <<<"$out" && echo yes || echo no)"
check "the profile's yes took"  1 "${PICKED[sw_mpv]}"
check "and its noes took too"   0 "${PICKED[sw_vlc]}"
check "and every key was marked used" yes \
      "$([ -n "${ANSWERS_USED[sw_mpv]+set}" ] && echo yes || echo no)"

echo ""
echo "=== the selection is written down for syn-update ==="
#
# The record is the only thing that lets syn-update tell "you did not want
# this" apart from "the tree has gained a component". Without it, a machine
# that took eleven of twenty-five components got the other fourteen back on
# its first update — this whole page, undone, silently.
#
# What it must get right is the mapping: syn-update speaks in PACKAGE names,
# and a row can carry more than one (comp_synguard is synguard AND
# synapse_kmod), so a per-key file would leave half the suite unaccounted for.
sel_reset standard
PICKED[comp_vibe]=0
PICKED[comp_synguard]=1
PICKED[comp_nexus]=0
man=$(sel_manifest)

check "an unticked component is recorded as declined" yes       "$(grep -qx 'vibe = declined' <<<"$man" && echo yes || echo no)"
check "a ticked one is recorded as selected" yes       "$(grep -qx 'synui = selected' <<<"$man" && echo yes || echo no)"
check "a row carrying two packages records BOTH" yes       "$(grep -qx 'synguard = selected' <<<"$man" &&
         grep -qx 'synapse_kmod = selected' <<<"$man" && echo yes || echo no)"

# Arch packages are not ours to update and syn-update never touches them, so a
# line about firefox would be a claim this file has no business making.
check "the Arch shelf is not in the file" yes       "$(grep -q '^firefox' <<<"$man" && echo no || echo yes)"

# EVERY comp_ row, ticked or not. A row that is simply absent reads to
# syn-update as "never offered here", which is the one state that still
# installs on its own — so an omission is the original bug, spelled differently.
missing=""
for row in "${SEL_COMPONENTS[@]}"; do
    IFS='|' read -r key std full group pkgs _ <<<"$row"
    for p in $pkgs; do
        grep -qx "$p = \(selected\|declined\)" <<<"$man" || missing="$missing $p"
    done
done
check "every component package has a line" "" "$missing"

# Written from PICKED after sel_resolve_deps, so what the file says and what
# pacman installs are the same set. Ticking vibe forces synapd and syn-confine
# back on; the file has to agree.
sel_reset minimal
PICKED[comp_vibe]=1
PICKED[comp_synapd]=0
PICKED[comp_synconfine]=0
sel_resolve_deps >/dev/null 2>&1
man=$(sel_manifest)
check "a dependency ticked back on is recorded as selected" yes       "$(grep -qx 'synapd = selected' <<<"$man" &&
         grep -qx 'syn-confine = selected' <<<"$man" && echo yes || echo no)"

echo ""
echo "=== no page is wider than a terminal ==="
#
# 80 columns, and the pages are built to 77. printf's precision is what keeps
# this true; without it a 16-character description wraps the right-hand column
# onto a line of its own and the grid reads as broken.
sel_reset standard
widest=0
for list in "${SEL_ALL_LISTS[@]}"; do
    while IFS= read -r line; do
        line=$(sed 's/\x1b\[[0-9;]*m//g' <<<"$line")
        [ "${#line}" -gt "$widest" ] && widest=${#line}
    done < <(multi_select "$list" "$list" </dev/null 2>&1)
done
check "the widest row fits in 80 columns" yes \
      "$([ "$widest" -le 79 ] && echo yes || echo no)"

echo ""
if [ "$fails" -gt 0 ]; then
    printf '%d check(s) failed\n' "$fails"
    exit 1
fi
echo "all checks passed"
