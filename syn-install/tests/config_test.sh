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
              grep -ohE '^[[:space:]]*ask_opt [A-Za-z_0-9]+' "$script" |
                  awk '{print tolower($2)}'
            } | sort -u)

# Keys the example documents. Commented-out ones count -- being shown as an
# option IS the documentation. Nested blocks (want, core) flatten with an
# underscore exactly as render.nix does.
# Block headers may themselves be commented out (`core` is), so the optional #
# is on every pattern here, not just the leaves.
documented=$(awk '
    /^ *#? *want *= *\{/ { blk = "want"; next }
    /^ *#? *core *= *\{/ { blk = "core"; next }
    /^ *#? *\}; *$/      { blk = "";     next }
    match($0, /^ *#? *[a-z_0-9]+ *=/) {
        k = $0; sub(/^ *#? */, "", k); sub(/ *=.*/, "", k)
        if (k == "") next
        if (blk != "") print blk "_" k; else print k
    }
' "$example" | sort -u)

# The two acknowledgements are answered by the PRESENCE of a profile, never by
# a key, so they are documented in prose rather than as settable keys.
exempt="press_enter_start press_enter_reboot"
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
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
