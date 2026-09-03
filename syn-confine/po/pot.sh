#!/usr/bin/env bash
# pot.sh — syn-confine's message template.
#
# ⛔ usage() IS NOT IN HERE, as in every sibling — one fputs of a manual page,
# every line a flag spelling with a column of text aligned to it.
#
# ⚠ AND NEITHER IS ANYTHING THE CONFINED COMMAND SAYS. syn-confine passes its
# environment through and the child speaks for itself; tests/syn_confine_test.sh
# greps the CHILD's strerror for "denied", which is why that suite pins LC_ALL
# before anything runs. This program's own words are the only ones here.
#
# ⚠ THE --print REPORT IS TRANSLATED. It is a policy summary somebody reads to
# decide whether a sandbox is tight enough, not a record any program parses —
# the suite reads it, and the suite pins the locale it asserts in.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$1
out=${2:-$root/po}

xgettext --language=C --from-code=UTF-8 --no-location \
         --add-comments=TRANSLATORS \
         --keyword=_ --keyword=N_ --keyword=P_:1,2 \
         --package-name=syn-confine \
         -o "$out/syn-confine.pot" "$root"/src/*.c

printf 'pot.sh: %s msgids\n' "$(grep -c '^msgid ' "$out/syn-confine.pot")"
