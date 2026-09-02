#!/usr/bin/env bash
# pot.sh — syn-settings' message template, from BOTH of its source languages.
#
# The window is QML and the readers are C, and they share one catalog: a word
# both use is translated once and cannot disagree.
#
# ⛔ THE C HALF IS N_() ONLY — THERE IS NO _() IN THIS PROGRAM. syn-settings
# prints nothing but its TSV record, and the record is never translated: it is
# what data/syn-settings.qml parses and what `syn-settings --rec region |
# column -t` is meant to stay readable in. N_() puts the label and the sentence
# under it into the catalog and returns them unchanged, so the row still
# carries the English word and the WINDOW translates it at the draw site. See
# include/i18n.h.
#
# ⛔ AND **NOT** --omit-header, WHICH SILENTLY MANGLES THE MSGIDS. With no
# header there is no Content-Type to declare a charset, so xgettext writes the
# .pot as ASCII and DROPS every non-ASCII character from the strings it
# extracted, with no warning about the loss. A msgid that lost a character never
# matches the source string at runtime, so those entries are permanently English
# however well translated. msgcat --use-first below keeps the QML half's header.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$1
po=$2
out=${3:-$po}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

"$root/tools/qml-xgettext.py" --root "$root/data" --files "$po/POTFILES" \
    -o "$tmp/qml.pot" --strict

xgettext --language=C --from-code=UTF-8 --no-location \
         --add-comments=TRANSLATORS \
         --keyword=N_ \
         -o "$tmp/c.pot" "$root"/src/*.c

# ⛔ AND PROVE IT ROUND-TRIPPED. Every non-ASCII run in a marked string has to
# survive into the template; this is the check that would have caught the flag.
if ! LC_ALL=C grep -qP '[\x80-\xff]' "$tmp/c.pot"; then
    if LC_ALL=C grep -qP 'N_\("[^"]*[\x80-\xff]' "$root"/src/*.c; then
        echo "pot.sh: the C sources have non-ASCII msgids but the template has none" >&2
        echo "        — xgettext wrote ASCII and dropped them. See the note above." >&2
        exit 1
    fi
fi

msgcat --use-first --no-location -o "$out/syn-settings.pot" "$tmp/qml.pot" "$tmp/c.pot"
printf 'pot.sh: %s msgids (%s from QML, %s from the C)\n' \
    "$(grep -c '^msgid ' "$out/syn-settings.pot")" \
    "$(grep -c '^msgid ' "$tmp/qml.pot")" "$(grep -c '^msgid ' "$tmp/c.pot")"
