#!/usr/bin/env bash
# i18n_test.sh — syn-arcade's words, reachable by a translator, and its records
# untouched.
#
# syn-arcade has THREE front-ends over one set of code paths — a CLI you use
# over SSH, a desktop window, and big screen mode on a television — and they
# need opposite things from a catalog:
#
# ⛔ 1. THE RECORD PROTOCOL MUST NEVER BE TRANSLATED. Every `--rec` command
#    emits percent-encoded tab-separated rows, and the FIRST ROW NAMES THE
#    COLUMNS. Both windows key off those names, and they match on VALUES too:
#    a hud position is `top-left`, a fit scaler is `integer`, a filter is `fsr`,
#    a pad axis is `ABS_X`, an `action` cell is `detail` or `toggle:hud`. Those
#    are identifiers, not words. A translated one is a window that stops
#    recognising its own records — and `fit run <id>` taking an id the window
#    can no longer produce.
#
# ⚠ 2. THE HUMAN PATH MUST BE. Half of what this tool does is answering
#    questions about a machine somebody is not sitting at, and that half is
#    read on a terminal.
#
# ⚠ 3. AND THE LABELS IN BETWEEN travel in a record for a WINDOW to translate
#    at the draw site. They are marked N_(), which puts them in the catalog and
#    returns them unchanged, so the record still carries the English word.
#
# ⛔ 4. ONE CATALOG SERVES ALL THREE. po/*.po is compiled to JSON for the two
#    windows and to a .mo for the binary, so a word they share is translated
#    once. ⚠ The .mo is named after the DOMAIN — syn-arcade.mo — which is why
#    the build uses meson's i18n module rather than a custom_target: a loop can
#    only name its output de.mo, which installs to the right directory under a
#    name libintl never looks for, and every string silently falls back to
#    English.
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
BIN=${2:-$root/build/syn-arcade}
fails=0
check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "syn-arcade translations"

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# ⛔ THE SAME SANDBOX THE MAIN SUITE USES, AND FOR THE SAME REASON. syn-arcade
# writes synuirc, MangoHud.conf and its own config; a run of it with the real
# XDG_CONFIG_HOME would touch the live desktop. Nothing below writes, but the
# record commands READ those paths, and reading the developer's own files makes
# the C-versus-German diff depend on what is on this machine.
export XDG_CONFIG_HOME="$tmp/config"; mkdir -p "$XDG_CONFIG_HOME"
export XDG_CACHE_HOME="$tmp/cache";   mkdir -p "$XDG_CACHE_HOME"
export XDG_RUNTIME_DIR="$tmp/run";    mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
export MANGOHUD_CONFIGFILE="$tmp/config/MangoHud/MangoHud.conf"
export SYN_ARCADE_NO_NET=1
unset WAYLAND_DISPLAY DISPLAY 2>/dev/null || true

# ── 1. the template is current, and nothing was mangled making it ──────────
err=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
if [ -f "$tmp/syn-arcade.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/syn-arcade.pot" 2>/dev/null)
    now=$(grep -c '^msgid "' "$tmp/syn-arcade.pot" 2>/dev/null)
    check "po/syn-arcade.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted,
# with no warning about the loss — and this program's messages are full of em
# dashes. A msgid that lost a character never matches the source string, so it
# is permanently English however well translated. pot.sh refuses that flag now;
# this asserts the RESULT rather than the flag.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/syn-arcade.pot" | awk '{print ($1>0)}')
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

# ⚠ EVERY --rec COMMAND THAT RUNS WITH NO HARDWARE AND NO SESSION. The runtime
# check is the only thing that can tell a marked record from a marked sentence,
# so it has to be the broad one.
# ⛔ `hud --rec`, NOT `hud show --rec`. `show` is the VERB that unhides the
# overlay — it writes the config and prints a sentence — and `--rec` after it is
# ignored. The first version of this list had it, and the hostile-catalog check
# below reported the drift on its first run, correctly: that command's output is
# prose and is supposed to change.
REC_CMDS="about --rec
hud --rec
hud choices hud-position
pads --rec
binds --rec
map --rec
fit apps --rec
fit screens --rec
fit choices filter
fit choices scaler
big apps --rec
big games --rec
big recent --rec
big windows --rec
big news --rec
big media --rec
big music status --rec
big music source --rec
big music yt --rec
big transport status --rec"

if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    # ⛔ AND THE BINARY HAS TO BE ABLE TO FIND A CATALOG. Its compiled-in
    # localedir is under the install prefix, so an uninstalled syn-arcade loads
    # nothing and answers English in every language — which is how the first
    # version of a check like this passed with a _() sitting in a record.
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/syn-arcade.mo" "$root/po/de.po" 2>/dev/null

    if ! localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        printf '  skip  localedef could not build de_DE (nothing asserted)\n'
    else
        # ── 2a. against the REAL German catalog ───────────────────────────
        drift=""
        printf '%s\n' "$REC_CMDS" | while IFS= read -r cmd; do
            [ -n "$cmd" ] || continue
            a=$(SYN_ARCADE_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN $cmd 2>/dev/null | md5sum)
            b=$(SYN_ARCADE_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
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
           grep -qF 'msgid "syn-arcade: unknown command'; then
            h1=$(SYN_ARCADE_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN about 2>/dev/null | md5sum)
            h2=$(SYN_ARCADE_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN about 2>/dev/null | md5sum)
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
        # ⚠ THIS IS WHAT CATCHES A MARKED COLUMN NAME. `rec_row(3, "field",
        # "value", "action")` names the columns both windows key off; marking
        # one is invisible to 2a until somebody translates it, and then it is a
        # window that has stopped recognising its own records.
        #
        # ⚠ AND IT CANNOT BE A STATIC CHECK. The header row is not
        # distinguishable by reading: `rec_row(3, N_("licence"),
        # "GPL-2.0-or-later", "detail")` is a DATA row whose fields are all
        # literals, and `pads info` prints a LABEL spelled `name` while
        # `pads --rec` has a COLUMN spelled `name`. A grep for known header
        # spellings reported three false positives on its first run.
        #
        # ⚠ FOUR THINGS HAD TO BE RIGHT HERE AND EACH FAILED SILENTLY:
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
        if msgen "$root/po/syn-arcade.pot" -o "$tmp/ident.po" 2>/dev/null &&
           msgfilter -i "$tmp/ident.po" -o "$tmp/hostile.raw" \
                     sed -e '/./s/$/⟧/' 2>/dev/null &&
           sed 's/^\("[A-Za-z-]*: .*\)⟧\(\\n"\)$/\1\2/' \
               "$tmp/hostile.raw" > "$tmp/hostile.po" &&
           msgfmt -o "$hos/de/LC_MESSAGES/syn-arcade.mo" "$tmp/hostile.po" 2>/dev/null
        then
            printf '%s\n' "$REC_CMDS" | while IFS= read -r cmd; do
                [ -n "$cmd" ] || continue
                a=$(SYN_ARCADE_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN $cmd 2>/dev/null | md5sum)
                b=$(SYN_ARCADE_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                    $BIN $cmd 2>/dev/null | md5sum)
                [ "$a" = "$b" ] || printf '[%s]' "$cmd"
            done > "$tmp/drift.b"
            check "every --rec command survives a catalog that translates EVERYTHING" \
                  "" "$(cat "$tmp/drift.b")"

            # ...and the catalog WAS reached, or the check above proved nothing.
            g1=$(SYN_ARCADE_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN about 2>/dev/null | md5sum)
            g2=$(SYN_ARCADE_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN about 2>/dev/null | md5sum)
            check "...and that catalog WAS reached (the human path changed)" \
                  "differs" "$([ "$g1" = "$g2" ] && echo same || echo differs)"
        else
            printf '  skip  msgen/msgfilter unavailable (nothing asserted)\n'
        fi
    fi
else
    printf '  skip  no binary or no localedef (nothing asserted)\n'
fi


# ── 3. ⛔ NOTHING MARKED IN A COLUMN A PROGRAM READS ───────────────────────
#
# The static half of check 2. `rec_row()`'s FIRST call in any command prints the
# column names, and both windows key off them; the `action` column is an
# instruction, not a word. A `_()` anywhere in a record is wrong outright — a
# record must carry the English string so the window can look it up — so the
# only marker allowed inside rec_row() is N_().
badrec=$(grep -n 'rec_row([^)]*[^A-Za-z_0-9]_(' "$root"/src/*.c | tr '\n' ' ')
check "no _() inside a rec_row() call (N_() is the record's marker)" "" "$badrec"

# ── 4. ⛔ EVERY .qml THAT TRANSLATES IS IN po/POTFILES ─────────────────────
#
# A file missing from that list still compiles and still looks up at runtime,
# so nothing warns — the strings simply never reach a template and are English
# forever. syn-arcade has TWO windows and it is the second one that gets
# forgotten.
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

# ── ⛔ AND THE MAIN SUITE PINS THE LOCALE IT ASSERTS IN ───────────────────
#
# tests/syn_arcade_test.sh asserts English against a binary that answers the
# desktop's language once syn-arcade is installed. The fix is one exported
# LC_ALL and one unset LANGUAGE; nothing else stops them being dropped, and
# dropping them breaks the build on every translated desktop and no English one.
suite="$root/tests/syn_arcade_test.sh"
pin=""
grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$suite" || pin="$pin LC_ALL"
grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$suite" || pin="$pin LANGUAGE"
check "the main suite pins the locale it asserts in" "" "$pin"

echo
if [ "$fails" -eq 0 ]; then echo "all syn-arcade translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
