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

# ⛔ **NOT** --omit-header, WHICH SILENTLY MANGLES THE MSGIDS. With no header
# there is no Content-Type to declare a charset, so xgettext writes the .pot as
# ASCII and DROPS every non-ASCII character from the strings it extracted, with
# no warning about the loss. A msgid that lost a character never matches the
# source string at runtime, so those entries are permanently English however
# well translated. Found in synpkg 47, where `%s.pacnew — merge it` came out as
# `%s.pacnew  merge it`; every label in THESE tables happens to be pure ASCII,
# so nothing was harmed here — but the next one with a · or an em dash in it
# would have been. msgcat --use-first below keeps the QML half's header anyway.
xgettext --language=C --keyword=N_ --from-code=UTF-8 --no-location \
         -o "$tmp/c.pot" \
         "$root/src/develop.c" "$root/src/timeline.c" "$root/src/thumb.c"

# ⛔ AND PROVE IT ROUND-TRIPPED, so the flag cannot come back unnoticed.
if LC_ALL=C grep -qP 'N_\("[^"]*[\x80-\xff]' \
        "$root/src/develop.c" "$root/src/timeline.c" "$root/src/thumb.c" &&
   ! LC_ALL=C grep -qP '[\x80-\xff]' "$tmp/c.pot"; then
    echo "pot.sh: the C tables have non-ASCII labels but the template has none" >&2
    echo "        — xgettext wrote ASCII and dropped them. See the note above." >&2
    exit 1
fi

# ⚠ --use-first: a msgid in both halves keeps the QML half's entry, so a string
# the window also spells itself does not end up duplicated with two comments.
msgcat --use-first --no-location -o "$out/synstudio.pot" "$tmp/qml.pot" "$tmp/c.pot"
printf 'pot.sh: %s msgids (%s from QML, %s from the C tables)\n' \
    "$(grep -c '^msgid ' "$out/synstudio.pot")" \
    "$(grep -c '^msgid ' "$tmp/qml.pot")" "$(grep -c '^msgid ' "$tmp/c.pot")"
