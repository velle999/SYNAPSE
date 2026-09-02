#!/usr/bin/env bash
# i18n_test.sh — a translated string in synfiles must be reachable by a translator.
#
# ⛔ 1. qsTr() IS A TRAP AND IT IS THE FIRST THING CHECKED. Qt's translation
#    path needs someone to call QTranslator::load() and installTranslator()
#    before the QML engine starts. quickshell does neither — there is no
#    installTranslator anywhere in the binary and no way to reach one from QML.
#    So qsTr("Copy") compiles, returns "Copy", and translates nothing, in every
#    language, forever. It reads in a diff exactly like a marked string.
#
# ⚠ 2. A NON-LITERAL ARGUMENT extracts nothing. tools/qml-xgettext.py reads the
#    source, not the running program, so I18n.tr(someVariable) is marked-looking
#    and English.
#
# ⛔ 3. AND THE KEYS BESIDE THE LABELS WERE NOT SWEPT UP. A context-menu row
#    carries the action it runs in `act:` beside its label, and a property row
#    carries the record key the binary emitted in `key:`. Translating either
#    does not make a German file manager — it makes one whose menu items do
#    nothing and whose properties panel stops decoding paths. Same rule as
#    ctlpanel.c's settings keys, asserted rather than trusted.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. Everything below parses
# tool output, and the gettext tools are themselves translated: on a Japanese
# desktop xgettext writes `警告:` and a filter matching the English word finds
# nothing. ⚠ LANGUAGE is UNSET, not set — gettext reads it before LC_ALL, so an
# ambient LANGUAGE=ja would answer Japanese to the German runs below.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/build/synfiles}
QML="$root/data/synfiles.qml"
fails=0

check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

# The Plural-Forms header of a .po, joined across its continuation lines.
#
# ⚠ SINGLE-QUOTED PYTHON, DELIBERATELY. Written inline with double quotes the
# shell eats the backslashes the regex needs and it matches nothing — which
# reported all thirteen catalogs as disagreeing with the desktop when every one
# of them agreed. Same escaping trap the bar's gate hit.
plural_of() {
    python3 -c 'import re,sys
t = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r"Plural-Forms: (.*?)\\n", t, re.S)
print(re.sub(r"\"\s*\n\s*\"", "", m.group(1)).strip() if m else "")' "$1"
}

echo "synfiles translations"

# ── 1. qsTr() is never used ───────────────────────────────
# ⚠ COMMENTS STRIPPED FIRST. A checker that describes what it forbids finds
# itself — synui's equivalent matched its own I18n.qml header on the first run.
qstr=$(sed -e 's://.*::' "$QML" | grep -n 'qsTr[[:space:]]*(\|qsTranslate[[:space:]]*(' | tr '\n' ' ')
check "no qsTr() — quickshell has no translator to load it" "" "$qstr"

# ── 2. every marked argument is a literal, and the template is current ──
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
# ⚠ THE TEMPLATE IS BOTH LANGUAGES NOW, so pot.sh regenerates it — the QML
# through qml-xgettext.py and src/*.c through real xgettext, merged. Counting
# only the QML half would call the template current while every _() added to
# the C since was missing from it.
err=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean (and every I18n.tr() argument is literal)" "" "$err"

if [ -f "$tmp/synfiles.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/synfiles.pot" 2>/dev/null || echo 0)
    now=$(grep -c '^msgid "' "$tmp/synfiles.pot" 2>/dev/null || echo 0)
    check "po/synfiles.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted with
# no warning — a msgid that lost a character can never match the source string.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/synfiles.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

# ⛔ AND THE RECORD IS LOCALE-INDEPENDENT, WHICH ONLY RUNNING IT CAN PROVE.
# A `_()` on the wrong side of a `g_out == OUT_REC` branch is invisible to any
# amount of reading; it shows up the moment the program is asked the same
# question in two languages. ⛔ And the binary has to be able to FIND a catalog:
# an uninstalled synfiles loads none and answers English in every language,
# which is how a check like this passes with the bug in front of it.
if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/synfiles.mo" "$root/po/de.po" 2>/dev/null
    if localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        h=$tmp/home; mkdir -p "$h/d"; : > "$h/d/one.txt"
        drift=""
        for cmd in "list $h/d" "about" "places" "config list"; do
            a=$(HOME=$h SYNFILES_LOCALEDIR=$mo LC_ALL=C.UTF-8 \
                $BIN --rec $cmd 2>/dev/null | md5sum)
            b=$(HOME=$h SYNFILES_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                $BIN --rec $cmd 2>/dev/null | md5sum)
            [ "$a" = "$b" ] || drift="$drift [$cmd]"
        done
        check "every --rec command answers the same in German as in C" "" "$drift"

        # ...and the HUMAN path does, or nothing is being translated at all.
        # ⚠ A **C** MSGID the QML half does NOT also contain: the window was
        # translated long before the C side existed and says many of the same
        # words, so a shared one would run this against an untranslated CLI.
        if msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF 'msgid "the trash is empty"'; then
            h1=$(HOME=$h SYNFILES_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN trash list 2>&1 | md5sum)
            h2=$(HOME=$h SYNFILES_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN trash list 2>&1 | md5sum)
            check "...while the human path DOES change" "differs" \
                  "$([ "$h1" = "$h2" ] && echo same || echo differs)"
        else
            printf '  skip  the German catalog has no C strings yet\n'
        fi
    else
        printf '  skip  localedef could not build de_DE (nothing asserted)\n'
    fi
else
    printf '  skip  no binary or no localedef (nothing asserted)\n'
fi

# ⛔ AND THE MAIN SUITE PINS THE LOCALE IT ASSERTS IN. Once synfiles is
# installed, a freshly built binary answers the desktop's language while
# tests/synfiles_test.sh asserts English — and a failing `meson test` is a build
# failure, so `syn-update` then refuses to install it. Neither running nor
# grepping that suite can see this from here, so the FIX is what is asserted.
pin=""
grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$root/tests/synfiles_test.sh" || pin="$pin LC_ALL"
grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$root/tests/synfiles_test.sh" || pin="$pin LANGUAGE"
check "the main suite pins the locale it asserts in" "" "$pin"

# ── 3. the language set matches the desktop's ─────────────
# A file manager in English on a German desktop reads as the application being
# broken, so the two lists are the same list.
mine=$(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS" | sort | tr '\n' ' ')
desk=$(grep -vE '^\s*#|^\s*$' /home/velle/SYNAPSE/synui/po/LINGUAS 2>/dev/null | sort | tr '\n' ' ')
if [ -n "$desk" ]; then
    check "po/LINGUAS matches the desktop's" "$desk" "$mine"
else
    printf '  skip  the desktop tree is not beside this one\n'
fi

absent=""
while IFS= read -r l; do
    [ -f "$root/po/$l.po" ] || absent="$absent $l"
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every language in po/LINGUAS has a .po" "" "$absent"

# ── 4. ⛔ THE KEYS BESIDE THE LABELS ARE UNTOUCHED ─────────
badkey=$(grep -n 'act: *I18n\.\|key: *I18n\.\|icon: *I18n\.' "$QML" | tr '\n' ' ')
check "no menu action, record key or icon name is translated" "" "$badkey"

# ⛔ AND THE PROPERTY LABELS GO THROUGH THE MAPPER. propLabel() translates for
# DRAWING while `key` stays what the binary emitted — propValue() and the size
# branch both match on it, so a translated key silently stops them working.
check "the property panel draws propLabel(), not the raw key" "yes" \
      "$(grep -q 'function propLabel' "$QML" &&
         grep -q 'text: root.propLabel(propRow.modelData.key)' "$QML" && echo yes || echo no)"

# ⛔ AND fmtMany() IS GONE. It glued an "s" onto an English noun — the "%d
# window%s" trap src/i18n.h documents. Its absence is asserted, not assumed:
# it would be an easy function to reintroduce for a third counter.
check "no English pluraliser was reintroduced" "0" \
      "$(grep -c 'function fmtMany' "$QML")"

# ── 5. the catalogs compile, and their plural rules are the desktop's ──
bad=""
while IFS= read -r l; do
    po="$root/po/$l.po"
    [ -f "$po" ] || continue
    msgfmt -c -o /dev/null "$po" 2>/dev/null || bad="$bad $l(msgfmt)"
    "$root/tools/po2json.py" "$po" -o "$tmp/$l.json" >/dev/null 2>&1 || bad="$bad $l(po2json)"
    # ⛔ `msginit -l ar` and `-l zh` cannot resolve those bare codes and fall
    # back to the template's English default — Arabic gets 2 plural forms
    # instead of 6, Chinese 2 instead of 1. Both are VALID rules that pass every
    # property check, so the only way to catch it is to compare against the
    # reviewed set next door.
    ref="$root/../synui/po/$l.po"
    if [ -f "$ref" ]; then
        a=$(plural_of "$po"); b=$(plural_of "$ref")
        [ -n "$a" ] && [ "$a" = "$b" ] || bad="$bad $l(plural)"
    fi
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every catalog compiles and uses the desktop's plural rule" "" "$bad"

echo
if [ "$fails" -eq 0 ]; then echo "all synfiles translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
