#!/usr/bin/env bash
# livelang_test.sh — the language chosen at boot has to REACH two places.
#
# Both halves of this failed on the 0.3.0 image, and they fail in opposite
# directions:
#
#   1. THE SCREEN CANNOT DRAW IT. A Linux VT font holds at most 512 glyphs and
#      the console has no shaper, so Japanese came out as a wall of boxes and
#      Arabic as the Latin glyphs that happen to sit in those slots —
#      `y`ml SynapseOS mn ▪lSwr@ ▪lHy@.` The catalogs were right; the console
#      cannot show them. The text installer keeps English prose there.
#      ⚠ AND ONLY THERE: the same script run from a terminal on the desktop
#      draws all thirteen, so the test is TERM as much as the language.
#
#   2. THE SESSION NEVER HEARD. The live desktop is started by
#      `systemctl start synui.service`, so its environment is the systemd
#      manager's — `export LANG` in this script reaches nothing, and
#      /etc/locale.conf was read at boot, minutes before anybody chose. Every
#      window in the live session came up English. `systemctl set-environment`
#      is what carries it, and this asserts the call is MADE, because nothing
#      about the desktop's language is visible from a test.
#
# Sources syn-install.sh with SYN_INSTALL_SOURCE_ONLY=1, which stops it before
# it touches anything, and runs live_language_apply against stubs on PATH.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
export SYN_INSTALL_SOURCE_ONLY=1

TMP=$(mktemp -d /tmp/livelang.XXXXXX)
trap 'rm -rf "$TMP"' INT TERM EXIT

export SYN_BOOT_LANG_FILE="$TMP/language"
export SYN_CMDLINE_FILE="$TMP/cmdline"
: > "$SYN_CMDLINE_FILE"

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

echo "the live image's language, on the screen and in the session"

# ── 1. which scripts a VT can draw ────────────────────────
#
# ⚠ THE ANSWER IS A PAIR — the language AND the terminal — and testing either
# alone passes on a build that ignores the other.
draws() {  # draws <term> <locale> -> yes|no
    TERM=$1 console_can_draw "$2" && echo yes || echo no
}

check "a VT cannot draw Japanese"          no  "$(draws linux ja_JP.UTF-8)"
check "…nor Chinese"                       no  "$(draws linux zh_CN.UTF-8)"
check "…nor Korean"                        no  "$(draws linux ko_KR.UTF-8)"
check "…nor Arabic"                        no  "$(draws linux ar_SA.UTF-8)"
check "…nor Hindi"                         no  "$(draws linux hi_IN.UTF-8)"

# Cyrillic and Greek FIT in 512 glyphs, and live_language_apply loads the font
# that has them. Refusing these would take working languages away.
check "a VT draws Russian (latarcyrheb-sun16)" yes "$(draws linux ru_RU.UTF-8)"
check "…and every Latin language"              yes "$(draws linux de_DE.UTF-8)"
check "…and English"                           yes "$(draws linux en_US.UTF-8)"

# The same script, run where there IS a shaper.
check "a real terminal draws Japanese"     yes "$(draws foot ja_JP.UTF-8)"
check "…and Arabic"                        yes "$(draws xterm-256color ar_SA.UTF-8)"

# ⚠ AN EMPTY TERM IS A PIPE, NOT A VT — the graphical installer runs this
# script for its lists and reads them into QML, which draws everything. This
# read `linux|''` for one commit, which would have handed the one window that
# CAN show Japanese the English rows.
check "a pipe is not a console"            yes "$(draws '' ja_JP.UTF-8)"

# ── 2. …and the catalog follows the screen ────────────────
#
# The catalogs themselves are fine — this is about which one is LOADED.
TERM=linux syn_lang_load ja_JP.UTF-8
check "a VT keeps English prose for Japanese" en "$SYN_LANG"
TERM=linux syn_lang_load de_DE.UTF-8
check "…and loads German, which it can draw"  de "$SYN_LANG"
TERM=foot syn_lang_load ja_JP.UTF-8
check "a terminal that can draw it gets it"   ja "$SYN_LANG"
syn_lang_load en_US.UTF-8   # leave the suite in English

# ── 3. the session that has not started yet ───────────────
#
# ⚠ STUBS ON PATH, and every one of these is called for real by
# live_language_apply: without them the test would load a keymap and a font
# onto the terminal running it, and try to generate a locale on this machine.
stub="$TMP/bin"; mkdir -p "$stub"
cat > "$stub/systemctl" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >> "$TMP/systemctl.log"
EOF
for c in loadkeys setfont locale-gen; do
    printf '#!/bin/sh\nexit 0\n' > "$stub/$c"
done
# `locale charmap` answering successfully is what live_language_apply reads as
# "this locale exists", which is the branch the export and the set-environment
# live in. A stub says yes without generating anything.
printf '#!/bin/sh\necho UTF-8\n' > "$stub/locale"
chmod +x "$stub"/*
PATH="$stub:$PATH"

# ⚠ /etc IS NOT WRITABLE HERE and that is part of the point: live_language_apply
# has to survive every one of its writes failing, because on a real image any of
# them can. The assertion below is about the call it makes, not the files.
( PATH="$stub:$PATH"; live_language_apply ja_JP.UTF-8 jp106 jp ) >/dev/null 2>&1

got=$(grep -c '^set-environment LANG=ja_JP.UTF-8$' "$TMP/systemctl.log" 2>/dev/null || true)
check "the locale is pushed into systemd's environment" "1" "$got"

# The desktop is started by a UNIT, so this is the only line that reaches it.
# A build that dropped it would still pass every other check in this file.
have=$(grep -c 'systemctl set-environment' "$here/../syn-install.sh")
check "…from live_language_apply, in the source" "1" "$have"

echo ""
if [ "$fails" -eq 0 ]; then
    echo "all live-language checks passed"
else
    echo "$fails failed"
fi
exit $(( fails > 0 ))
