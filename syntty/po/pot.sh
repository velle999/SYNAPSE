#!/usr/bin/env bash
# pot.sh — syntty's message template.
#
# ⛔ THE DIAGNOSTIC SUBCOMMANDS ARE NOT IN HERE. `dump`, `win --stats`, `fit`,
# `mouse` and `key` print what tests/syntty_test.sh parses; none of it is
# marked, and tests/i18n_test.sh fails on a `_()` inside those writers. See
# include/i18n.h.
#
# ⛔ AND **NOT** --omit-header, WHICH SILENTLY MANGLES THE MSGIDS. With no header
# there is no Content-Type to declare a charset, so xgettext writes the .pot as
# ASCII and DROPS every non-ASCII character from the strings it extracted, with
# no warning about the loss — a msgid that lost a character never matches the
# source string, so it is permanently English however well translated.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$1
out=${2:-$root/po}

xgettext --language=C --from-code=UTF-8 --no-location \
         --add-comments=TRANSLATORS \
         --keyword=_ --keyword=N_ --keyword=P_:1,2 \
         --package-name=syntty \
         -o "$out/syntty.pot" "$root"/src/*.c

printf 'pot.sh: %s msgids\n' "$(grep -c '^msgid ' "$out/syntty.pot")"
