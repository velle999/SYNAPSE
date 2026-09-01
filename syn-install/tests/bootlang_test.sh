#!/usr/bin/env bash
# bootlang_test.sh — the language chosen before anything else.
#
# There are three ways an answer arrives and one place it has to end up:
#
#   `lang=` on the kernel command line   (the bootloader's language submenu)
#   the live image's own picker          (when nothing else answered)
#   /run/synapseos/language              (what either of them wrote, read back
#                                         by step 7 and by the graphical
#                                         installer)
#
# What is asserted here is the MAPPING, because that is the part that is wrong
# silently: an unrecognised `lang=` must produce nothing — so the picker asks —
# rather than a row that happens to be first, and a value that IS recognised
# must land on the row whose keyboard and font pack match. Getting that wrong
# hands somebody a system in the wrong language with a keyboard they cannot
# type on, which is the failure the whole two-column keyboard table in
# syn-install.sh exists to describe.
#
# Sources syn-install.sh with SYN_INSTALL_SOURCE_ONLY=1, which stops it before
# it touches anything.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
export SYN_INSTALL_SOURCE_ONLY=1

TMP=$(mktemp -d /tmp/bootlang.XXXXXX)
trap 'rm -rf "$TMP"' INT TERM EXIT

export SYN_BOOT_LANG_FILE="$TMP/language"
export SYN_CMDLINE_FILE="$TMP/cmdline"

# shellcheck source=/dev/null
. "$here/../syn-install.sh"

fails=0
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

field() { echo "$1" | cut -d'|' -f"$2"; }

echo "boot-time language selection"

# ── locale_row_for: what a `lang=` value resolves to ──────
check "a full locale"          "de_DE.UTF-8" "$(field "$(locale_row_for de_DE.UTF-8)" 2)"
check "a bare language code"   "de_DE.UTF-8" "$(field "$(locale_row_for de)" 2)"
check "case does not matter"   "de_DE.UTF-8" "$(field "$(locale_row_for DE_de.utf-8)" 2)"
check "a row number"           "de_DE.UTF-8" "$(field "$(locale_row_for 3)" 2)"

# `lang=en` has to mean something, and English (US) is first in the table for
# exactly this reason. If somebody reorders those two rows, this changes.
check "a bare code takes the first row of that language" "en_US.UTF-8" \
      "$(field "$(locale_row_for en)" 2)"
check "the specific one still wins over the bare one"    "en_GB.UTF-8" \
      "$(field "$(locale_row_for en_GB.UTF-8)" 2)"

# ⚠ THE KEYBOARD IS TWO NAMESPACES AND THEY DISAGREE — see the note above
# LOCALE_ROWS. These four rows are the ones where the console name and the XKB
# name are not the same string, so they are the ones a mapping bug shows up in.
check "en_GB: console keymap"  "uk"       "$(field "$(locale_row_for en_GB.UTF-8)" 3)"
check "en_GB: XKB layout"      "gb"       "$(field "$(locale_row_for en_GB.UTF-8)" 4)"
check "ja: console keymap"     "jp106"    "$(field "$(locale_row_for ja)" 3)"
check "ja: XKB layout"         "jp"       "$(field "$(locale_row_for ja)" 4)"
check "pt_BR: console keymap"  "br-abnt2" "$(field "$(locale_row_for pt)" 3)"
check "pt_BR: XKB layout"      "br"       "$(field "$(locale_row_for pt)" 4)"
check "ko: XKB layout"         "kr"       "$(field "$(locale_row_for ko)" 4)"

# The font pack — the column that decides whether the installed system can draw
# the alphabet it was asked for.
check "ja carries a CJK font pack"   "noto-fonts-cjk"   "$(field "$(locale_row_for ja)" 5)"
check "hi carries an extra font pack" "noto-fonts-extra" "$(field "$(locale_row_for hi)" 5)"
check "de needs no extra fonts"       ""                 "$(field "$(locale_row_for de)" 5)"

# ── Nothing, rather than a guess ──────────────────────────
check "an unknown locale resolves to nothing" "" "$(locale_row_for xx_XX.UTF-8)"
check "an unknown code resolves to nothing"   "" "$(locale_row_for klingon)"
check "an out-of-range row resolves to nothing" "" "$(locale_row_for 99)"
check "an empty value resolves to nothing"    "" "$(locale_row_for "")"

# ── cmdline_lang: only a whole word counts ────────────────
echo 'initrd=x archisobasedir=arch lang=fr_FR.UTF-8 quiet' > "$SYN_CMDLINE_FILE"
check "lang= is read off the command line" "fr_FR.UTF-8" "$(cmdline_lang)"

echo 'initrd=x quiet splash' > "$SYN_CMDLINE_FILE"
check "no lang= reads as nothing" "" "$(cmdline_lang)"

# ⚠ A SUBSTRING MATCH WOULD TAKE BOTH OF THESE. Neither is the parameter.
echo 'initrd=x nolang=de blang=es quiet' > "$SYN_CMDLINE_FILE"
check "a word merely ENDING in lang= is not the parameter" "" "$(cmdline_lang)"

echo 'lang= quiet' > "$SYN_CMDLINE_FILE"
check "an empty lang= is not an answer" "" "$(cmdline_lang)"

echo 'lang=de quiet' > "$SYN_CMDLINE_FILE"
check "lang= as the first word" "de" "$(cmdline_lang)"

# ── boot_locale_row: what step 7 and the GUI read back ────
rm -f "$SYN_BOOT_LANG_FILE"
check "no file means nobody has answered yet" "" "$(boot_locale_row)"

locale_row_for pl > "$SYN_BOOT_LANG_FILE"
check "the recorded row comes back whole" "pl_PL.UTF-8" "$(field "$(boot_locale_row)" 2)"
check "the recorded row keeps its label"  "Polski"      "$(field "$(boot_locale_row)" 1)"

# The record the graphical installer consumes is the same row, TAB-separated —
# same shape as --list-locales, so one parser reads both.
check "the GUI record has five fields" "5" \
      "$(boot_locale_row | tr '|' '\t' | awk -F'\t' '{print NF}')"

echo
echo "  the answer reaches the places that REPORT it"

# ⚠ localectl SAID "(unset)" ON A WORKING JAPANESE INSTALL. The layout was
# applied — synui had it and the keyboard worked — but nothing wrote it where
# localectl reads, so syn-settings' region pane (which parses exactly that
# `localectl status` line) called the desktop keyboard unknown on every non-US
# install. Asserted against the source, because the write-out is deep in the
# install path rather than in a function this can call.
src="$here/../syn-install.sh"
check "the desktop layout is written where localectl reads it" yes \
      "$(grep -q 'xorg.conf.d/00-keyboard.conf' "$src" && echo yes || echo no)"

# ⚠ AND IT MUST BE THE VALIDATED NAME. $XKB_LAYOUT is what the table or the
# user said; $SYNUI_XKB is that after the "does xkbcommon know this layout"
# check, which falls back to 'us'. Writing the raw one would put a layout
# xkbcommon rejects into the file that describes the machine.
check "it writes the VALIDATED layout, not the raw one" yes \
      "$(awk '/00-keyboard.conf/,/^XKBCONF$/' "$src" | grep -q 'XkbLayout" "\$SYNUI_XKB' && echo yes || echo no)"

echo
if [ "$fails" -eq 0 ]; then echo "all boot-language checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
