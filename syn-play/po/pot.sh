#!/usr/bin/env bash
# pot.sh — syn-play's message template, from BOTH of its source languages.
#
# The GUI window is QML and the CLI/TUI is C, and they share one catalog: a
# word both of them use is translated once and cannot disagree. po/meson.build
# compiles each .po twice — to JSON for the window and to a .mo for the binary.
#
# ⛔ THE RECORD PROTOCOL IS NOT IN HERE. `--rec` and the `serve` stream carry
# tab-separated rows whose tags (`s`, `q`, `h`, `f`, `playlist`, `q-more`),
# field names (`state`, `path`, `title`, `pos`, `duration`, `volume`) and some
# of their VALUES (`playing`, `paused`, `idle`, `stopped`) are matched with
# `===` in the QML. None of them is marked, and tests/i18n_test.sh proves it by
# running every offline --rec command under a catalog that translates
# EVERYTHING and diffing the bytes.
#
# ⚠ AND THE THIRD READER OF THOSE RECORDS IS THIS PROGRAM. tui.c sets
# `g_out = OUT_REC`, runs sp_playlist_list() into a pipe and parses the rows
# back, so a translated record makes the TUI stop recognising its own output.

# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$1
po=$2
out=${3:-$po}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

"$root/tools/qml-xgettext.py" --root "$root/data" --files "$po/POTFILES" \
    -o "$tmp/qml.pot" --strict

# ⚠ --keyword=P_:1,2 so ngettext's two forms are both extracted.
#
# ⛔ AND **NOT** --omit-header, WHICH SILENTLY MANGLES THE MSGIDS. With no header
# there is no Content-Type to declare a charset, so xgettext writes the .pot as
# ASCII and DROPS every non-ASCII character from the strings it extracted —
# `%s.pacnew — merge it` came out as `%s.pacnew  merge it`, with no warning
# about the loss. A msgid that lost a character never matches the source string
# at runtime, so those entries would have been permanently English however well
# translated. msgcat --use-first below keeps the QML half's header anyway.
xgettext --language=C --from-code=UTF-8 --no-location \
         --keyword=_ --keyword=N_ --keyword=P_:1,2 \
         -o "$tmp/c.pot" "$root"/src/*.c

# ⛔ AND PROVE IT ROUND-TRIPPED. Every non-ASCII run in a marked string has to
# survive into the template; this is the check that would have caught the flag.
if ! LC_ALL=C grep -qP '[\x80-\xff]' "$tmp/c.pot"; then
    if LC_ALL=C grep -qP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c; then
        echo "pot.sh: the C sources have non-ASCII msgids but the template has none" >&2
        echo "        — xgettext wrote ASCII and dropped them. See the note above." >&2
        exit 1
    fi
fi

msgcat --use-first --no-location -o "$out/syn-play.pot" "$tmp/qml.pot" "$tmp/c.pot"
printf 'pot.sh: %s msgids (%s from QML, %s from the C)\n' \
    "$(grep -c '^msgid ' "$out/syn-play.pot")" \
    "$(grep -c '^msgid ' "$tmp/qml.pot")" "$(grep -c '^msgid ' "$tmp/c.pot")"
