#!/usr/bin/env bash
# i18n_test.sh — syn-play's words, reachable by a translator, and its records
# untouched.
#
# ⛔ 1. THE RECORD PROTOCOL MUST NEVER BE TRANSLATED, AND IT HAS THREE READERS.
#    data/syn-play.qml parses the `serve` stream; tests/cli_test.sh parses
#    `--rec`; and syn-play itself parses `--rec` — tui.c sets `g_out = OUT_REC`,
#    runs sp_playlist_list() into a pipe and reads the rows back, because a
#    second directory walk would be a second idea of what a playlist file is.
#    So a translated record does not merely confuse a window: the TUI stops
#    recognising its own output.
#
#    ⚠ AND SOME VALUES ARE MATCHED, not just the field names. The QML compares
#    `state` against `playing`, `paused`, `idle` and `stopped` with `===`.
#
# ⚠ 2. THE HUMAN PATH MUST BE TRANSLATED, and for this program that is three
#    faces — the CLI, the TUI's chrome, and the window.
#
# ⛔ 3. ONE CATALOG SERVES BOTH. po/*.po is compiled to JSON for the window and
#    to a .mo for the binary. ⚠ The .mo is named after the DOMAIN —
#    syn-play.mo — which is why the build uses meson's i18n module rather than a
#    custom_target: a loop can only name its output de.mo, which installs to the
#    right directory under a name libintl never looks for, and every string
#    silently falls back to English.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. Everything below parses
# tool output, and the gettext tools are themselves translated: on a Japanese
# desktop xgettext writes `警告:`, a `grep -v 'warning:'` filter matches
# nothing, and "po/pot.sh runs clean" fails on a warning already judged fine.
# ⚠ LANGUAGE is UNSET, not set — gettext reads it before LC_ALL, so an ambient
# LANGUAGE=ja would answer Japanese to the German runs below and the assertion
# that the human path IS translated would compare Japanese with Japanese and
# pass. The deliberate foreign-locale runs set LC_ALL per command and still win.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/build/syn-play}
fails=0
check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "syn-play translations"

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# ⛔ A SCRATCH SYNPLAY_HOME, AND NO PLAYER. Two reasons, both structural. The
# record commands below have to produce the SAME ROWS in both locales for a
# diff to mean anything, and this machine's real history changes while the test
# runs. And SYNPLAY_HOME moves the SOCKET as well as the paths, so nothing here
# can reach — or stop — the mpv somebody is listening to.
#
# ⚠ SEEDED, not empty. An empty history and an empty playlist list take the
# "nothing yet" branch, which is prose: the record rows this exists to compare
# would never be printed and the diff would compare two empty strings.
#
# ⛔ THE HISTORY FILE IS `when pos dur PATH TITLE`, IN THAT ORDER, AND BOTH OF
# THE LAST TWO ARE PERCENT-ENCODED. Written the other way round the row still
# loads — nothing validates it — and `find` then matches a "title" that is a
# path, so `--rec find` answers a hit built out of a corrupt fixture. It looked
# right in the output; the only tell was the path and the title swapped.
H=$tmp/home
mkdir -p "$H/share/syn-play/playlists" "$H/config/syn-play" "$H/Music"
: > "$H/Music/song-one.mp3"
: > "$H/Music/other-track.mp3"
printf '#EXTM3U\n#EXTINF:-1,song one\n%s/Music/song-one.mp3\n' "$H" \
    > "$H/share/syn-play/playlists/evening.m3u8"
printf '1700000000\t61.500\t180.000\t%s/Music/song-one.mp3\tsong one\n' "$H" \
    > "$H/share/syn-play/history.tsv"
printf '%s\n' "$H/Music" > "$H/config/syn-play/roots"

export SYNPLAY_HOME="$H"
export HOME="$H"

# ── 1. the template is current, and nothing was mangled making it ──────────
err=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
if [ -f "$tmp/syn-play.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/syn-play.pot" 2>/dev/null)
    now=$(grep -c '^msgid "' "$tmp/syn-play.pot" 2>/dev/null)
    check "po/syn-play.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted,
# with no warning about the loss — and this program's messages are full of em
# dashes and one pair of arrows in the TUI's key legend. A msgid that lost a
# character never matches the source string, so it is permanently English
# however well translated. pot.sh refuses that flag now; this asserts the
# RESULT rather than the flag.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/syn-play.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

# ── 2. ⛔ EVERY RECORD IS BYTE-IDENTICAL IN EVERY LANGUAGE ─────────────────
#
# ⚠ RUN, not grepped. A `_()` where an `N_()` belongs is invisible to any
# amount of reading; it shows up the moment the program is asked the same
# question in two languages.
#
# ⛔ AND THE LOCALE HAS TO EXIST. LANGUAGE is vetoed under C, so a box with no
# generated locales silently tests nothing. localedef into a scratch LOCPATH —
# never locale-gen, which is root and system-wide.

# ⚠ EVERY --rec COMMAND THAT RUNS WITH NO PLAYER. The ones that would START one
# are deliberately absent: `resume`, `open` and a bare path all call
# sp_connect_or_start(), and a test that launched mpv twice to compare two
# records would be a test that plays music at whoever ran it.
REC_CMDS="--rec status
--rec queue
--rec history
--rec playlist list
--rec find song
--rec find nothingmatchesthis"

if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    # ⛔ AND THE BINARY HAS TO BE ABLE TO FIND A CATALOG. Its compiled-in
    # localedir is under the install prefix, so an uninstalled syn-play loads
    # nothing and answers English in every language — which is how the first
    # version of a check like this passed with a _() sitting in a record.
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/syn-play.mo" "$root/po/de.po" 2>/dev/null

    if ! localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        printf '  skip  localedef could not build de_DE (nothing asserted)\n'
    else
        # ── 2a. against the REAL German catalog ───────────────────────────
        printf '%s\n' "$REC_CMDS" | while IFS= read -r cmd; do
            [ -n "$cmd" ] || continue
            a=$(SYN_PLAY_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN $cmd 2>/dev/null | md5sum)
            b=$(SYN_PLAY_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                $BIN $cmd 2>/dev/null | md5sum)
            [ "$a" = "$b" ] || printf '[%s]' "$cmd"
        done > "$tmp/drift.a"
        check "every --rec command answers the same in German as in C" "" \
              "$(cat "$tmp/drift.a")"

        # ...and the HUMAN path does not, or nothing is being translated at all.
        # ⚠ Only once a catalog has something in it: before that this is a skip,
        # not a failure, because "not translated yet" is a legitimate state.
        # ⚠ A **C** MSGID, not just any: the QML half of this catalog can be
        # full while the C half is empty, so a bare count is over the threshold
        # either way and this check would claim the CLI was translated when not
        # one of its strings was.
        if msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF 'msgid "Nothing is playing.'; then
            h1=$(SYN_PLAY_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN status 2>&1 | md5sum)
            h2=$(SYN_PLAY_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN status 2>&1 | md5sum)
            check "...while the human path DOES change" "differs" \
                  "$([ "$h1" = "$h2" ] && echo same || echo differs)"
        else
            printf '  skip  the German catalog is not filled yet\n'
        fi

        # ── 2b. ⛔ AND AGAINST A CATALOG THAT TRANSLATES EVERYTHING ───────
        #
        # The strongest version of 2a, and it does not depend on anybody having
        # translated anything. A catalog is built from the TEMPLATE with every
        # msgstr marked, and every --rec command is run under it. A record that
        # changes has a string in it reaching gettext, whether or not de.po
        # happens to carry that entry today.
        #
        # ⚠ THIS IS WHAT CATCHES A MARKED FIELD NAME. `state`, `path`, `title`,
        # `pos`, `duration`, `volume` and the `q`/`h`/`f`/`playlist` tags are
        # what the window and the TUI key off; marking one is invisible to 2a
        # until somebody translates it, and then it is a window — and a TUI —
        # that has stopped recognising its own rows.
        #
        # ⚠ FOUR THINGS HAD TO BE RIGHT HERE AND EACH FAILED SILENTLY when this
        # was first written for a sibling component:
        #
        #   · msgfilter READS A FILE. `-i -` writes an empty catalog and says
        #     nothing, and an empty one turns this into a skip reporting
        #     "unavailable".
        #   · THE MARKER IS A SUFFIX ONLY. A prefix moves the leading "\n" of
        #     every msgid that starts with one and msgfmt drops the entry —
        #     leaving untranslated exactly the strings this exists to catch.
        #   · AND NEVER ON AN EMPTY LINE, for the same reason.
        #   · THE HEADER ENTRY IS REPAIRED AFTERWARDS. msgfilter marks it too,
        #     so `charset=UTF-8` becomes `charset=UTF-8⟧` and nothing loads.
        #   · AND IT IS ADDRESSED AS de_DE, not LANGUAGE=hostile — LANGUAGE is
        #     ignored under the C locale, so the catalog was never opened and
        #     the whole check quietly passed.
        hos=$tmp/hos; mkdir -p "$hos/de/LC_MESSAGES"
        if msgen "$root/po/syn-play.pot" -o "$tmp/ident.po" 2>/dev/null &&
           msgfilter -i "$tmp/ident.po" -o "$tmp/hostile.raw" \
                     sed -e '/./s/$/⟧/' 2>/dev/null &&
           sed 's/^\("[A-Za-z-]*: .*\)⟧\(\\n"\)$/\1\2/' \
               "$tmp/hostile.raw" > "$tmp/hostile.po" &&
           msgfmt -o "$hos/de/LC_MESSAGES/syn-play.mo" "$tmp/hostile.po" 2>/dev/null
        then
            printf '%s\n' "$REC_CMDS" | while IFS= read -r cmd; do
                [ -n "$cmd" ] || continue
                a=$(SYN_PLAY_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN $cmd 2>/dev/null | md5sum)
                b=$(SYN_PLAY_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                    $BIN $cmd 2>/dev/null | md5sum)
                [ "$a" = "$b" ] || printf '[%s]' "$cmd"
            done > "$tmp/drift.b"
            check "every --rec command survives a catalog that translates EVERYTHING" \
                  "" "$(cat "$tmp/drift.b")"

            # ...and the catalog WAS reached, or the check above proved nothing.
            g1=$(SYN_PLAY_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN status 2>&1 | md5sum)
            g2=$(SYN_PLAY_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN status 2>&1 | md5sum)
            check "...and that catalog WAS reached (the human path changed)" \
                  "differs" "$([ "$g1" = "$g2" ] && echo same || echo differs)"

            # ⛔ AND THE HOSTILE RUN STILL PRODUCED ROWS. Every command above
            # exits non-zero on one path or another — `queue` with no player
            # exits 3, `find` with no match exits 4 — so a change that made them
            # print NOTHING would leave two empty strings comparing equal and
            # the whole of 2b passing on air.
            rows=$(SYN_PLAY_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                   $BIN --rec playlist list 2>/dev/null | grep -c .)
            check "...and the record commands actually emitted rows" "1" "$rows"
        else
            printf '  skip  msgen/msgfilter unavailable (nothing asserted)\n'
        fi
    fi
else
    printf '  skip  no binary or no localedef (nothing asserted)\n'
fi

# ── 3. ⛔ NOTHING MARKED ON A LINE THAT WRITES A RECORD ────────────────────
#
# The static half of check 2, and it is spellable here because every record in
# this program is a literal `printf("<tag>\t…")` rather than a helper: a `_()`
# on such a line is wrong outright.
badrec=$(grep -nE 'printf\("(s|q|h|f|l|e|q-|h-|l-|f-|state|path|title|pos|duration|volume|index|count|item|hist|hit|more|none|load|saved|loaded|removed|playlist)[^"]*\\t' \
         "$root"/src/*.c | grep '_(' | tr '\n' ' ')
check "no _() on a line that writes a record" "" "$badrec"

# ── 4. ⛔ EVERY .qml THAT TRANSLATES IS IN po/POTFILES ─────────────────────
#
# A file missing from that list still compiles and still looks up at runtime,
# so nothing warns — the strings simply never reach a template and are English
# forever.
missing=""
while IFS= read -r q; do
    rel=${q#"$root/data/"}
    grep -qxF "$rel" "$root/po/POTFILES" || missing="$missing $rel"
done < <(grep -rl 'I18n\.tr(' "$root/data" --include='*.qml')
check "every .qml that calls I18n.tr() is listed in po/POTFILES" "" "$missing"

# ⛔ AND THE SINGLETON IS THE SHARED ONE, BYTE FOR BYTE. data/qml/I18n.qml is
# the same file in every quickshell app in this project; a local edit to one
# copy is a divergence nothing else would notice.
shared=$(ls "$root"/../syn-edit/data/qml/I18n.qml 2>/dev/null)
if [ -n "$shared" ]; then
    a=$(md5sum < "$root/data/qml/I18n.qml"); b=$(md5sum < "$shared")
    check "data/qml/I18n.qml is the shared copy, unedited" "$b" "$a"
else
    printf '  skip  no sibling checkout to compare I18n.qml against\n'
fi

# ── 5. the catalogs still compile, and to both shapes ─────────────────────
#
# ⚠ msgfmt -c, which is what catches a translation that reordered %s without
# saying %1$s — five catalogs failed exactly that way in another component, and
# the failure at runtime is a crash, not a wrong word.
bad=""
while IFS= read -r l; do
    po="$root/po/$l.po"
    [ -f "$po" ] || { bad="$bad $l(missing)"; continue; }
    msgfmt -c -o /dev/null "$po" 2>/dev/null || bad="$bad $l(msgfmt)"
    "$root/tools/po2json.py" "$po" -o "$tmp/$l.json" >/dev/null 2>&1 \
        || bad="$bad $l(po2json)"
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every catalog compiles to both a .mo and a JSON" "" "$bad"

# ── ⛔ AND EVERY OTHER SUITE PINS THE LOCALE IT ASSERTS IN ────────────────
#
# The three suites beside this one assert English against a binary that answers
# the desktop's language once syn-play is installed. The fix is one exported
# LC_ALL and one unset LANGUAGE; nothing else stops them being dropped, and
# dropping them breaks the build on every translated desktop and no English one.
pin=""
for suite in cli_test.sh drop_test.sh click_test.sh; do
    s="$root/tests/$suite"
    [ -f "$s" ] || continue
    grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$s" || pin="$pin $suite(LC_ALL)"
    grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$s" || pin="$pin $suite(LANGUAGE)"
done
check "every other suite pins the locale it asserts in" "" "$pin"

echo
if [ "$fails" -eq 0 ]; then echo "all syn-play translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
