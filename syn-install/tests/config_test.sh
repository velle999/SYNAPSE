#!/usr/bin/env bash
# config_test.sh — the --config answer-file machinery
#
# Two kinds of check, and the second is the one that will actually catch
# something one day.
#
# The first exercises config_load/answer/config_report_unused directly, through
# the SYN_INSTALL_SOURCE_ONLY seam, so the parsing and the symbolic-value maps
# are tested without a disk.
#
# The second is a DRIFT check between the installer and the documentation. The
# key vocabulary lives in two places by necessity — `answer <key>` call sites in
# syn-install.sh, and profile-example.nix, which is the only place a user finds
# out a key exists. A key added to one and not the other is not an error
# anywhere: the installer just never gets an answer it could have had, or the
# example documents something that does nothing. Both directions are checked.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
script="$here/../syn-install.sh"
example="$here/../../syn/nix-profile-example.nix"

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# shellcheck disable=SC1090
SYN_INSTALL_SOURCE_ONLY=1 . "$script"

echo "=== config_load parses ==="
cat > "$tmp/a.conf" <<'CONF'
# a comment line
disk = vda
filesystem=btrfs          # a trailing comment
fullname = "Velle Sinclair"
part_swap =
  bootloader   =   limine
CONF
config_load "$tmp/a.conf"
check "key with spaces around ="        "vda"             "${ANSWERS[disk]}"
check "no spaces at all"                "btrfs"           "${ANSWERS[filesystem]}"
check "trailing comment is stripped"    "btrfs"           "${ANSWERS[filesystem]}"
check "quoted value keeps its spaces"   "Velle Sinclair"  "${ANSWERS[fullname]}"
check "empty value is a value"          ""                "${ANSWERS[part_swap]}"
check "empty value is SET, not absent"  "set"             "$([ -n "${ANSWERS[part_swap]+set}" ] && echo set)"
check "leading indentation"             "limine"          "${ANSWERS[bootloader]}"
check "comment line is not a key"       "no"              "$([ -n "${ANSWERS[a]+set}" ] && echo yes || echo no)"

echo ""
echo "=== answer() translates and falls through ==="
out=$(answer filesystem _fs -m ext4=1,btrfs=2,xfs=3,f2fs=4 >/dev/null; echo "$_fs")
check "symbolic value maps to the menu number" "2" "$out"

# A raw number must still work — the map is a convenience, not a gate.
ANSWERS[filesystem]=4
out=$(answer filesystem _fs -m ext4=1,btrfs=2,xfs=3,f2fs=4 >/dev/null; echo "$_fs")
check "a value not in the map passes through" "4" "$out"

ANSWERS[encrypt]=true
out=$(answer encrypt _enc -m yes=y,no=n,true=y,false=n >/dev/null; echo "$_enc")
check "true maps onto the [y/N] prompt's y" "y" "$out"

ANSWERS[password]=hunter2
out=$(answer password _pw -s 2>&1)
check "a secret is not echoed" "yes" \
      "$(grep -q '\*\*\*\*' <<<"$out" && echo yes || echo no)"
check "…but it still reaches the variable" "hunter2" \
      "$(answer password _pw -s >/dev/null; echo "$_pw")"

# Absent key -> a real read. stdin stands in for the person at the machine.
unset 'ANSWERS[username]'
out=$(answer username _u <<<"typed" >/dev/null; echo "$_u")
check "an unanswered key still asks" "typed" "$out"

# --ack auto-continues, but ONLY because a config was loaded at all.
out=$(answer press_enter_start _a --ack </dev/null 2>&1)
check "an acknowledgement needs no key" "yes" \
      "$(grep -q -- '--config' <<<"$out" && echo yes || echo no)"

echo ""
echo "=== unused keys are reported ==="
ANSWERS=([bootlaoder]=limine); ANSWERS_USED=()
out=$(config_report_unused 2>&1)
check "a misspelled key is named" "yes" \
      "$(grep -q 'bootlaoder' <<<"$out" && echo yes || echo no)"
ANSWERS=([bootloader]=limine); ANSWERS_USED=([bootloader]=1)
out=$(config_report_unused 2>&1)
check "a consumed key is not named" "" "$out"

echo ""
echo "=== the key vocabulary matches the documented one ==="
#
# Keys the installer consumes. Two forms: the literal `answer <key>` sites, and
# ask_opt, which derives its key from the WANT_*/core_* variable it sets.
# The [[:space:]]* after the anchor is load-bearing: nearly every call site is
# indented, and without it this matched only the handful at column 0 and then
# reported thirty correct keys as undocumented.
#
# The trailing alternation is what stops PROSE from registering as a call site:
# a real one is followed by a flag, a `||`, a `;` or the end of the line, and
# the sentence "answer is still asked, so …" in the --help text is not. That
# sentence really did show up here as a key named `is`.
consumed=$( { grep -ohE '(^|;)[[:space:]]*answer [a-z_0-9]+ [A-Za-z_][A-Za-z_0-9]*([[:space:]]+(-[a-z-]+|\|\||;)|[[:space:]]*$)' "$script" |
                  sed 's/^[;[:space:]]*//' | awk '{print $2}'
              # pick <question> <key> <var> ... — the numbered menus. Its key is
              # the field after a QUOTED question, so strip that first; the
              # `answer` pattern above cannot see these, and without this every
              # menu key (filesystem, bootloader, preset, desktop, ai_model,
              # install_mode) reads as documented-but-never-consumed.
              grep -ohE '^[[:space:]]*pick "[^"]*" [a-z_0-9]+ ' "$script" |
                  sed -E 's/^[[:space:]]*pick "[^"]*" //' | awk '{print $1}'
              grep -ohE '^[[:space:]]*ask_opt [A-Za-z_0-9]+' "$script" |
                  awk '{print tolower($2)}'
              # The checkbox pages. Their keys are the first field of a row in
              # one of the SEL_* tables rather than an `answer` call site —
              # multi_select() reads the key out of the row, so the table IS
              # the call site, and there are ~70 of them. A row whose key does
              # not start comp_/sw_ is invisible here, which is why the format
              # comment in syn-install.sh says the prefix is load-bearing.
              grep -ohE '^[[:space:]]*"(comp|sw)_[a-z0-9_]+\|' "$script" |
                  tr -d '" ' | cut -d'|' -f1
            } | sort -u)

# Keys the example documents. Commented-out ones count -- being shown as an
# option IS the documentation. Nested blocks (want, core) flatten with an
# underscore exactly as render.nix does.
# Block headers may themselves be commented out (`core` is), so the optional #
# is on every pattern here, not just the leaves.
#
# Any `name = {` opens a block rather than a hardcoded list of two. When this
# knew only about `want` and `core`, adding the comp/sw pages would have made
# seventy correctly-documented keys read as undocumented — and the obvious
# "fix" is to add them to the list here, which is one more place the vocabulary
# lives.
documented=$(awk '
    /^ *#? *[a-z_0-9]+ *= *\{/ {
        blk = $0; sub(/^ *#? */, "", blk); sub(/ *=.*/, "", blk); next
    }
    /^ *#? *\}; *$/      { blk = "";     next }
    match($0, /^ *#? *[a-z_0-9]+ *=/) {
        k = $0; sub(/^ *#? */, "", k); sub(/ *=.*/, "", k)
        if (k == "") next
        if (blk != "") print blk "_" k; else print k
    }
' "$example" | sort -u)

# The two acknowledgements are answered by the PRESENCE of a profile, never by
# a key, so they are documented in prose rather than as settable keys.
# live_start is the live image's three-way menu (--live, drawn only when
# syn-firstboot hands over). A --config run is by definition not that menu —
# it was started by someone who already chose to install — so the key exists to
# get pick()'s validation and typeahead handling, not to be preseeded.
exempt="press_enter_start press_enter_reboot live_start"
for k in $consumed; do
    grep -qw "$k" <<<"$exempt" && continue
    check "$k is documented in profile-example.nix" yes \
          "$(grep -qxF "$k" <<<"$documented" && echo yes || echo no)"
done
for k in $documented; do
    check "documented key $k is one the installer reads" yes \
          "$(grep -qxF "$k" <<<"$consumed" && echo yes || echo no)"
done

echo ""
echo "=== the graphical installer writes keys this script reads ==="
#
# syn-install-gui's whole job is to write one of these profiles, so its key set
# is the third place the vocabulary lives. A key it emits that nothing consumes
# is silent: the install completes, the answer is ignored, and the only trace is
# a line in the unused-key report at the end that nobody is reading, because the
# window is in front of the terminal.
#
# Matched on the literal in each L.push("<key>=…"), which is the one form
# buildConfig() uses.
gui="$here/../syn-install-gui.qml"
if [ -f "$gui" ]; then
    # Only the L.push("<key>=…") lines in buildConfig(). Matching every quoted
    # `word=` in the file swept up the shell sentinels the install chain uses to
    # report its own status (__syn_install_exit=) and demanded the installer
    # consume them as settings.
    # Quoted `key=` literals, but ONLY on the L.push lines of buildConfig().
    #
    # Anchoring on `L.push("` alone was too tight and silently dropped the two
    # confirmations, which are pushed through a ternary
    # (`L.push(aMode === "erase" ? "confirm_erase=yes" : …)`). Matching every
    # quoted `word=` in the whole file was too loose and swept up the shell
    # sentinels the install chain reports its status with
    # (__syn_install_exit=), demanding the installer consume them as settings.
    #
    # The checkbox keys are NOT literals in an L.push — buildConfig() walks the
    # window's own packGroups table and pushes `row.key + "="`, so the table is
    # where they are declared and the table is what this reads. That makes this
    # a genuine drift check between two tables that must agree row for row,
    # rather than a check that a loop exists.
    gui_keys=$( { grep -E 'L\.push\(' "$gui" | grep -oE '"[a-z_0-9]+=' | tr -d '"='
                  grep -oE 'key: "(comp|sw)_[a-z0-9_]+"' "$gui" |
                      sed 's/key: //; s/"//g'
                } | sort -u)
    for k in $gui_keys; do
        check "GUI key $k is one the installer reads" yes \
              "$(grep -qxF "$k" <<<"$consumed" && echo yes || echo no)"
    done

    # And the other direction, for the questions with no prompt of their own.
    #
    # Every ask_opt in this script is a y/n question inside the Custom preset,
    # and a graphical Custom install has to answer ALL of them: an unanswered
    # one is `read -r` on the terminal BEHIND the window, so the install stops
    # dead with nothing on screen saying why. This is the failure the GUI's
    # header note is about, and the "was it under the custom branch" part is not
    # checkable from here — but a want_* question the window does not write at
    # all is, and that is the way a new one gets added.
    for k in $(grep -ohE '^[[:space:]]*ask_opt [A-Za-z_0-9]+' "$script" |
                   awk '{print tolower($2)}' | sort -u); do
        check "Custom question $k is answered by the GUI" yes \
              "$(grep -qxF "$k" <<<"$gui_keys" && echo yes || echo no)"
    done

    # The checkbox pages have exactly the same hazard, and ~70 times over.
    # multi_select() draws its page and blocks on `read -r` as soon as ONE row
    # on it is unanswered — so a component the window forgets to write does not
    # install a package with a default, it hangs the graphical install on a
    # page nobody can see. Every row, in both directions.
    for k in $(grep -ohE '^[[:space:]]*"(comp|sw)_[a-z0-9_]+\|' "$script" |
                   tr -d '" ' | cut -d'|' -f1 | sort -u); do
        check "checkbox $k is answered by the GUI" yes \
              "$(grep -qxF "$k" <<<"$gui_keys" && echo yes || echo no)"
    done
else
    echo "  (no syn-install-gui.qml — skipped)"
fi

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
