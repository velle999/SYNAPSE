#!/usr/bin/env bash
# i18n_test.sh — a translated string in syn-cal must be reachable by a translator.
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
# ⛔ 3. THE VIEW BUTTON'S LABEL WAS ITS KEY. `root.view = modelData.toLowerCase()`
#    turned the drawn word straight into the value nine `root.view === "month"`
#    branches match on, so a German "Monat" would have selected a view that does
#    not exist and drawn an empty window. The id and the label are separate
#    fields now, and this suite fails if the lowercase trick comes back.
#
# ⛔ 4. HALF THE DATES IN THIS FILE MUST NOT GO THROUGH THE LOCALE.
#    Qt.formatDate(d, "<format>") formats against QLocale::c() — measured under
#    quickshell 0.3.1, "MMMM yyyy" returns "September 2026" on a de_DE and a
#    ja_JP session alike. For a HEADING that is a bug, and every heading here
#    uses toLocaleDateString(Qt.locale(), …) with the format string as a msgid,
#    because the locale fixes the month NAMES and only a translator fixes the
#    ORDER. For the ISO dates it is exactly right and must stay: "yyyy-MM-dd"
#    is a map key, a `--from=` argument, and the contents of a field the binary
#    parses — and toLocaleDateString under ar_EG renders it ٢٠٢٦-٠٩-٠١.
#
# ⛔ 5. "not set" IS THE BINARY'S WORD for an account with no token, compared
#    four times. The sentence drawn beside it is a different string.
#
# ⛔ 6. AND THE PROVIDER IDS ARE PROTOCOL. `id: "google" | "microsoft" |
#    "caldav"` is what root.authKind is matched against and what reaches the
#    binary; only the label is drawn — and the three labels are brand names,
#    left English on purpose.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. Everything below parses
# tool output, and the gettext tools are themselves translated: on a Japanese
# desktop xgettext writes `警告:` and a `grep -v 'warning:'` filter matches
# nothing. ⚠ LANGUAGE is UNSET, not set — gettext reads it before LC_ALL, so an
# ambient LANGUAGE=ja would answer Japanese to the German runs below and the
# check that the human path IS translated would compare Japanese with Japanese
# and pass. The deliberate foreign-locale runs set LC_ALL per command.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/build/syn-cal}
QML="$root/data/syn-cal.qml"
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

echo "syn-cal translations"

# ── 1. qsTr() is never used ───────────────────────────────
# ⚠ COMMENTS STRIPPED FIRST. A checker that describes what it forbids finds
# itself — synui's equivalent matched its own I18n.qml header on the first run.
qstr=$(sed -e 's://.*::' "$QML" | grep -n 'qsTr[[:space:]]*(\|qsTranslate[[:space:]]*(' | tr '\n' ' ')
check "no qsTr() — quickshell has no translator to load it" "" "$qstr"

# ── 2. every marked argument is a literal, and the template is current ──
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
strict=$("$root/tools/qml-xgettext.py" --root "$root/data" --files "$root/po/POTFILES" \
         -o "$tmp/new.pot" --strict 2>&1 >/dev/null)
check "every I18n.tr() argument is a string literal" "" "$strict"

# ⚠ THE TEMPLATE IS BOTH LANGUAGES NOW, so it is pot.sh that regenerates it —
# qml-xgettext.py over the QML and real xgettext over src/*.c, merged. Counting
# only the QML half would call the template current while every C string added
# since was missing from it.
err=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
if [ -f "$tmp/syn-cal.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/syn-cal.pot" 2>/dev/null || echo 0)
    now=$(grep -c '^msgid "' "$tmp/syn-cal.pot" 2>/dev/null || echo 0)
    check "po/syn-cal.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted with
# no warning — a msgid that lost a character can never match the source string,
# so it is permanently English however well translated. pot.sh refuses that
# flag; this asserts the result rather than the flag.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/syn-cal.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

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

# ── 4. ⛔ THE VIEW BUTTON'S ID IS NOT ITS LABEL ────────────
lower=$(grep -n 'root.view = .*toLowerCase()\|root.view === .*toLowerCase()' "$QML" | tr '\n' ' ')
check "the view is not set from a drawn word" "" "$lower"
check "...it comes from an id field instead" "yes" \
      "$(grep -q 'root.view = modelData.id' "$QML" && echo yes || echo no)"

# ── 4b. ⛔ THE MACHINE-READABLE DATES KEEP THE C LOCALE ────
#
# ⚠ ASSERTED IN BOTH DIRECTIONS, because either mistake is silent. An ISO date
# put through the locale draws Arabic-Indic digits into a --from= argument; a
# heading left on Qt.formatDate draws an English month on every desktop.
# ⚠ NOT `[^)]*` BETWEEN THE CALL AND THE FORMAT — `Qt.locale()` has a `)` in
# it, so that pattern stops before ever reaching the format string and the
# check passed on the exact line it exists to catch.
isoloc=$(grep -nE 'toLocale(Date|Time)?String\(.*"yyyy-MM' "$QML" | tr '\n' ' ')
check "no ISO date goes through the locale" "" "$isoloc"
dispc=$(grep -nE 'Qt\.formatDate(Time)?\([^,]*, *"[^"]*(MMMM|dddd)' "$QML" | tr '\n' ' ')
check "no displayed date is formatted against the C locale" "" "$dispc"
# ...and every format string carrying a word is a msgid, so the ORDER can move.
rawfmt=$(grep -nE 'toLocale(Date|Time)?String\(Qt\.locale\(\), *"' "$QML" | tr '\n' ' ')
check "every displayed format string goes through the catalog" "" "$rawfmt"

# ── 4c. ⛔ THE BINARY'S OWN WORDS ARE UNTOUCHED ────────────
badsecret=$(grep -n 'secret === *I18n\.\|secret !== *I18n\.' "$QML" | tr '\n' ' ')
check "the account's \"not set\" state is not translated" "" "$badsecret"
badid=$(grep -n 'id: *I18n\.' "$QML" | tr '\n' ' ')
check "no provider id is translated" "" "$badid"
badkind=$(grep -n 'authKind === *I18n\.' "$QML" | tr '\n' ' ')
check "no provider comparison is translated" "" "$badkind"

# ── 4d. ⛔ THE WEEKDAY HEADINGS COME FROM THE LOCALE ───────
#
# Qt already knows what a Tuesday is called in thirteen languages, and which
# abbreviation each one actually uses — "Di.", "火", and none at all in Arabic.
# A msgid per weekday would ask a translator to re-supply that and get it
# subtly wrong.
check "the weekday headings come from Qt.locale()" "yes" \
      "$(grep -q 'Qt.locale().dayName(' "$QML" && echo yes || echo no)"
dayarr=$(grep -n '"Sun", *"Mon"\|"Mon", *"Tue"' "$QML" | tr '\n' ' ')
check "...and not from a hand-written array" "" "$dayarr"

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
    ref="/home/velle/SYNAPSE/synui/po/$l.po"
    if [ -f "$ref" ]; then
        a=$(plural_of "$po"); b=$(plural_of "$ref")
        [ -n "$a" ] && [ "$a" = "$b" ] || bad="$bad $l(plural)"
    fi
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every catalog compiles and uses the desktop's plural rule" "" "$bad"

# ── ⛔ THE RECORD IS LOCALE-INDEPENDENT, AND ONLY RUNNING IT PROVES THAT ──
#
# ⚠ RUN, not grepped. A `_()` on the wrong side of a `g_out == OUT_REC` branch
# is invisible to any amount of reading; it shows up the moment the program is
# asked the same question in two languages. It caught three rows in syn-edit the
# day that suite was written.
#
# ⛔ AND THE BINARY HAS TO BE ABLE TO FIND A CATALOG. Its compiled-in localedir
# is under the install prefix, so an UNINSTALLED syn-cal loads nothing and
# answers English in every language — which is how a check like this passes with
# a real bug sitting in front of it. SYN_CAL_LOCALEDIR points it at one built
# here. ⛔ AND THE LOCALE HAS TO EXIST: localedef into a scratch LOCPATH, never
# locale-gen, which is root and system-wide.
if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/syn-cal.mo" "$root/po/de.po" 2>/dev/null
    if localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        # ⚠ A HOME OF ITS OWN. These commands read the account store, and the
        # answer has to be the same both times for a diff to mean anything.
        h=$tmp/home; mkdir -p "$h"
        drift=""
        for cmd in "accounts" "calendars" "month"; do
            a=$(HOME=$h SYN_CAL_LOCALEDIR=$mo LC_ALL=C.UTF-8 \
                $BIN --rec $cmd 2>/dev/null | md5sum)
            b=$(HOME=$h SYN_CAL_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                $BIN --rec $cmd 2>/dev/null | md5sum)
            [ "$a" = "$b" ] || drift="$drift [$cmd]"
        done
        check "every --rec command answers the same in German as in C" "" "$drift"

        # ...and the HUMAN path does not, or nothing is being translated at all.
        # ⚠ A **C** MSGID, and one the QML half does NOT also contain. The
        # window was fully translated long before the C side existed, and it
        # says several of the same words: "No accounts yet." is in both, so
        # using it as the sentinel ran this check against an untranslated CLI
        # and reported the failure as real. "signed in:" is the accounts
        # listing's own word and appears nowhere in the QML.
        if msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF 'msgid "signed in:"'; then
            h1=$(HOME=$h SYN_CAL_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN accounts 2>/dev/null | md5sum)
            h2=$(HOME=$h SYN_CAL_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN accounts 2>/dev/null | md5sum)
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

# ── ⛔ AND THE PROTOCOL IS NOT MARKED IN THE FIRST PLACE ──────────────────
#
# The convention: a function whose name ends _name() is read by a PROGRAM.
# acc_kind_name() is parsed back by `account add` and branched on by the window;
# week_start_name() is the word the setting is WRITTEN as. A `_()` inside one is
# a calendar that cannot read its own settings file on a German desktop.
badname=$(awk '
    /^[a-zA-Z_].*_name\(/ { inside = 1 }
    inside && /_\("/      { print FILENAME ":" FNR; }
    inside && /^}/         { inside = 0 }
' "$root"/src/*.c | tr '\n' ' ')
check "no _() inside a _name() function" "" "$badname"

badrec=$(grep -n 'rec_row(.*_("' "$root"/src/*.c | tr '\n' ' ')
check "no _() inside a rec_row() call" "" "$badrec"

# ── ⛔ AND THE MAIN SUITES PIN THE LOCALE THEY ASSERT IN ──────────────────
#
# Once syn-cal is installed, a freshly built binary loads the INSTALLED catalog
# and answers in the desktop's language, while these suites assert English —
# and `meson test` failing is a BUILD failure, so `syn-update` then refuses to
# install syn-cal at all. That is exactly what synpkg 47 did on a Japanese
# desktop. ⚠ Neither running them nor grepping them can see it from here: with
# no catalog installed gettext falls back to the msgid and everything passes in
# English, and the messages that fail are assembled at runtime and appear in no
# test file. So this asserts the FIX.
pin=""
for suite in cli_test.sh tui_test.sh contract_test.sh; do
    f="$root/tests/$suite"
    [ -f "$f" ] || continue
    grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$f" || pin="$pin $suite(LC_ALL)"
    grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$f" || pin="$pin $suite(LANGUAGE)"
done
check "the suites that assert English pin the locale" "" "$pin"

echo
if [ "$fails" -eq 0 ]; then echo "all syn-cal translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
