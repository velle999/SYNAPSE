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

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
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

if [ -f "$tmp/new.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/syn-cal.pot" 2>/dev/null || echo 0)
    now=$(grep -c '^msgid "' "$tmp/new.pot" 2>/dev/null || echo 0)
    check "po/syn-cal.pot is current ($have msgids)" "$now" "$have"
fi

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

echo
if [ "$fails" -eq 0 ]; then echo "all syn-cal translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
