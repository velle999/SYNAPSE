#!/usr/bin/env bash
# pot.sh — synstudio's message template, from BOTH of its source languages.
#
# The window is QML and the panels are C tables; a translator needs the union,
# and the window needs ONE catalog to look either up in. See po/meson.build.
#
# ⛔ THE C HALF IS NOT OPTIONAL. `I18n.tr(row.label)` in the window is a dynamic
# lookup — safe only because every label it can be handed is extracted here.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$1
po=$2                 # where POTFILES is read from
# ⚠ A THIRD ARGUMENT so a test can regenerate WITHOUT overwriting the checked-in
# template — the currency check compares the two, and a script that always wrote
# in place would make that comparison compare a file with itself.
out=${3:-$po}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

"$root/tools/qml-xgettext.py" --root "$root/data" --files "$po/POTFILES" \
    -o "$tmp/qml.pot" --strict

# ⚠ --omit-header, or msgcat has two headers to reconcile and picks one at
# random. The QML half's header is the one that ships.
xgettext --language=C --keyword=N_ --from-code=UTF-8 --no-location --omit-header \
         -o "$tmp/c.pot" \
         "$root/src/develop.c" "$root/src/timeline.c" "$root/src/thumb.c"

# ⚠ --use-first: a msgid in both halves keeps the QML half's entry, so a string
# the window also spells itself does not end up duplicated with two comments.
msgcat --use-first --no-location -o "$out/synstudio.pot" "$tmp/qml.pot" "$tmp/c.pot"
printf 'pot.sh: %s msgids (%s from QML, %s from the C tables)\n' \
    "$(grep -c '^msgid ' "$out/synstudio.pot")" \
    "$(grep -c '^msgid ' "$tmp/qml.pot")" "$(grep -c '^msgid ' "$tmp/c.pot")"
