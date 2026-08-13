#!/usr/bin/env bash
# optgroup_test.sh — every optional install group is plumbed all the way through
#
# Step 4 offers a set of WANT_* groups (AI model, Bluetooth, printing, file
# manager, Wine, phone). Each one has to be named in SIX places: the defaults
# block, the Full preset, the Minimal preset, the Custom questionnaire, the
# read-back summary, and at least one thing that actually acts on it.
#
# Miss one and nothing breaks loudly. Miss it in Minimal and a "core daemons
# only" install silently drags in the tree that option exists to decline; miss
# it in the summary and the read-back — whose whole job is "a picker whose
# result you only discover afterwards is worse than no picker" — quietly stops
# telling the truth about one row. Neither shows up until someone installs a
# machine, which is the one place this project cannot iterate.
#
# So the list is not hardcoded here: it is discovered from the defaults block,
# and every group found is held to the same six. Adding a seventh option means
# this test starts checking it with no edit, which is the point.
#
# Static, like layout_test.sh's table checks: it reads syn-install.sh rather
# than running it, because the parts being asserted about sit inside the
# interactive loop that partitions disks.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
script="$here/../syn-install.sh"

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected %s, got %s\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

# Comment lines are stripped for the same reason layout_test.sh strips them:
# the comments in syn-install.sh name these variables while explaining them, so
# matching against the raw file would find WANT_PHONE in the prose that says
# what WANT_PHONE is for and call it plumbed.
code=$(grep -vE '^[[:space:]]*#' "$script")
has() { grep -qF -- "$1" <<<"$code" && echo yes || echo no; }

# ── The groups, discovered rather than listed ────────────────────────────────
#
# The defaults block is the one place every group must appear by construction —
# it is what makes the Standard preset the default — so it is the authoritative
# list. Anchored on the assignment so a mention in a later conditional cannot
# invent a group that is never defaulted.
groups=$(grep -oE '^[[:space:]]+WANT_[A-Z]+=[01]' "$script" |
         grep -oE 'WANT_[A-Z]+' | sort -u)

echo "=== optional install groups ==="
printf '  found: %s\n' "$(echo $groups)"

# A regression on its own: if the discovery above breaks, every check below
# passes vacuously and this file becomes decoration.
n=$(echo "$groups" | grep -c .)
check "at least five option groups were discovered" yes \
      "$([ "$n" -ge 5 ] && echo yes || echo no)"

# The two presets that set every group explicitly. Sliced out by line so a
# WANT_ set in Full cannot be mistaken for one set in Minimal — they are four
# lines apart and both are just `WANT_X=0/1` runs.
full_body=$(sed -n '/^        1)$/,/^            ;;$/p' "$script")
min_body=$(sed -n  '/^        3)$/,/^            ;;$/p' "$script")
check "the Full preset body was located"    yes "$([ -n "$full_body" ] && echo yes || echo no)"
check "the Minimal preset body was located" yes "$([ -n "$min_body" ]  && echo yes || echo no)"

# The read-back summary: from the "Installing:" header to the confirm prompt.
# Sliced and searched plainly rather than matched against the echo format —
# the first cut of this required "<label> : $(...)" and reported WANT_BLUETOOTH
# missing, when the truth was that "Bluetooth" is exactly the column width and
# so has no padding space before its colon. A test that is fussy about
# whitespace fails on correct code, which is worse than not having it.
summary_line=$(grep -n 'Install this selection?' "$script" | head -1 | cut -d: -f1)
header_line=$(grep -n "bold 'Installing:'" "$script" | head -1 | cut -d: -f1)
summary=$(sed -n "${header_line},${summary_line}p" "$script")

# Everything from the confirm prompt onward is "after the choice is made",
# which is where a group has to be consumed by something.
after=$(tail -n "+${summary_line}" "$script" | grep -vE '^[[:space:]]*#')

# WANT_MODEL is the one group that is not a yes/no in the Custom questionnaire.
# The model is DOWNLOADED during the install now (the ISO stopped carrying a
# gguf), so it is a four-way pick — which model, or none — asked on every preset
# except Minimal rather than only under Custom. It is held to that picker below
# instead of to ask_opt; every other check in this loop still applies to it.
picker_only="WANT_MODEL"

for g in $groups; do
    echo ""
    echo "  $g"

    check "    set by the Full preset"    yes \
          "$(grep -qF -- "$g" <<<"$full_body" && echo yes || echo no)"
    check "    set by the Minimal preset" yes \
          "$(grep -qF -- "$g" <<<"$min_body" && echo yes || echo no)"
    if grep -qw -- "$g" <<<"$picker_only"; then
        check "    offered as a picker rather than a y/n" yes \
              "$(has 'answer ai_model model_pick')"
    else
        check "    offered in the Custom questionnaire" yes "$(has "ask_opt $g")"
    fi

    # The summary prints one line per group, naming it inside the $(...) that
    # picks yes/no — so its presence in that slice is the test that the row
    # exists at all.
    check "    reported in the read-back summary" yes \
          "$(grep -qF -- "$g" <<<"$summary" && echo yes || echo no)"

    # Consumed by something once the selection is confirmed. Deliberately not a
    # form match: the existing groups are consulted as `[ "$X" = 1 ] &&`,
    # `if [ "$X" = 1 ]` and `if [ "$X" != 1 ]`, and pinning a spelling would
    # only make this test brittle about syntax it does not care about.
    check "    acted on after the selection is confirmed" yes \
          "$(grep -qF -- "$g" <<<"$after" && echo yes || echo no)"
done

# ── The model picker ────────────────────────────────────────────────────────
#
# Two things about it are worth pinning, and neither would fail loudly.
#
# First, skipping must stay possible: it was a supported answer when the model
# was a yes/no, and a picker with no way out would take a 4 GB download from
# someone who only wanted a desktop.
#
# Second — and this is the one that could actually ship broken — the installer
# hands its choice to `syn-model download <name>` in the target. The names are
# written down in two components, and a name syn-model does not know does not
# fail the install: it prints "Unknown model", exits 1, and the installer warns
# and carries on, so the machine boots with no model and nothing says why.
echo ""
echo "=== the model picker ==="
model_sh="$here/../../syn-model/syn-model.sh"

check "declining the model is still an answer" yes "$(has 'MODEL_CHOICE=none')"
check "and it is offered at the picker itself" yes "$(has 'none=4')"
check "the recommended default is marked as such" yes \
      "$(grep -q 'recommended' <<<"$code" && echo yes || echo no)"

# Every name the picker can produce, minus the sentinel that means "no model".
model_names=$(grep -oE 'MODEL_CHOICE=[a-z0-9-]+' <<<"$code" |
              cut -d= -f2 | sort -u | grep -vx none)
check "the picker names at least three models" yes \
      "$([ "$(echo "$model_names" | grep -c .)" -ge 3 ] && echo yes || echo no)"
check "syn-model.sh was found" yes "$([ -f "$model_sh" ] && echo yes || echo no)"

for m in $model_names; do
    # syn-model's dispatch is a case whose arms are `name|alias)`, so a name it
    # accepts is one that opens an arm.
    check "syn-model download accepts '$m'" yes \
          "$(grep -qE "^[[:space:]]*${m}[|)]" "$model_sh" && echo yes || echo no)"
done

echo ""
if [ "$fails" -gt 0 ]; then
    printf '%d check(s) failed\n' "$fails"
    exit 1
fi
echo "all checks passed"
