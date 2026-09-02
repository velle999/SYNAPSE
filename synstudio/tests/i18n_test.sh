#!/usr/bin/env bash
# i18n_test.sh — a translated string in synstudio must be reachable by a translator.
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
# ⛔ 3. THE PANEL HEADINGS ARRIVE FROM THE ENGINE AND ARE MATCHED ON. `groups`,
#    `clipGroups` and `thumbGroups` are field 5 of the `keys`, `timeline keys`
#    and `thumb keys` records — spelled by C tables in src/develop.c,
#    src/timeline.c and src/thumb.c — and this window compares them
#    (`modelData === "Basic"`, `=== "Title"`, `rowsIn(group)`) as well as
#    drawing them. Translating what arrives is a panel whose sections never
#    open and whose rows land nowhere. groupLabel() is the one place a group
#    becomes a word, and this suite fails when its set and the C tables' set
#    disagree in either direction.
#
# ⛔ 4. NOTHING SENT TO THE ENGINE COMES FROM THE CATALOG. Every edit is an
#    argument list — `tlRun(["split", proj, …])`, `["transition", …]` — and the
#    words in them are the CLI's, not sentences.
#
# ⛔ 5. `root.mode` IS "photo" / "video" and is compared in dozens of places;
#    only the two tab labels are drawn. Same for the resolution presets, whose
#    `name:` is what reaches the engine and whose `label:` is what is read.
#
# ⛔ 6. AND THE EXPORT FILENAME'S TIMESTAMP KEEPS THE C LOCALE.
#    Qt.formatDateTime(new Date(), "yyyyMMdd-hhmmss") builds a FILE NAME; under
#    a locale it would carry Arabic-Indic digits into a path. That call is
#    correct exactly where it is and wrong anywhere a person reads the result —
#    see reference: the string overload formats against QLocale::c().
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
QML="$root/data/synstudio.qml"
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

echo "synstudio translations"

# ── 1. qsTr() is never used ───────────────────────────────
# ⚠ COMMENTS STRIPPED FIRST. A checker that describes what it forbids finds
# itself — synui's equivalent matched its own I18n.qml header on the first run.
qstr=$(sed -e 's://.*::' "$QML" | grep -n 'qsTr[[:space:]]*(\|qsTranslate[[:space:]]*(' | tr '\n' ' ')
check "no qsTr() — quickshell has no translator to load it" "" "$qstr"

# ── 2. every marked argument is a literal, and the template is current ──
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
# ⚠ REGENERATED WITH po/pot.sh, NOT THE QML EXTRACTOR ALONE. Half this
# template comes from the C tables; comparing against the QML half only reports
# a current template as 148 msgids short.
strict=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null)
[ -f "$tmp/synstudio.pot" ] && mv "$tmp/synstudio.pot" "$tmp/new.pot"
check "every I18n.tr() argument is a string literal" "" "$strict"

if [ -f "$tmp/new.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/synstudio.pot" 2>/dev/null || echo 0)
    now=$(grep -c '^msgid "' "$tmp/new.pot" 2>/dev/null || echo 0)
    check "po/synstudio.pot is current ($have msgids)" "$now" "$have"
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

# ── 4. ⛔ THE ENGINE'S OWN LABELS REACHED THE TEMPLATE ─────
#
# The develop, clip and thumbnail panels are built from TABLES IN C, and their
# group names and row labels are drawn exactly as the record delivers them. The
# window therefore looks them up dynamically — `I18n.tr(row.label)` — which is
# safe only because those rows are marked N_() and po/pot.sh runs real xgettext
# over them into the same .pot. Drop that half and the panels go quietly
# English with a catalog that reads as complete.
#
# ⇒ Assert the union, not a hand-written list: every label the three C tables
# can emit must be a msgid. This replaced a groupLabel() switch and its
# set-equality check — the mapper could drift from the C, this cannot.
src="$root/src"
missing=""
while IFS= read -r lbl; do
    [ -n "$lbl" ] || continue
    grep -qxF "msgid \"$lbl\"" "$root/po/synstudio.pot" || missing="$missing [$lbl]"
done < <(grep -ohE 'N_\("(\\.|[^"\\])*"\)' "$src/develop.c" "$src/timeline.c" "$src/thumb.c" |
         sed -E 's/^N_\("//; s/"\)$//' | sort -u)
check "every N_() label in the C tables is in the template" "" "$missing"

# ...and the window actually routes a group through the lookup.
check "group headings go through groupLabel()" "yes" \
      "$(grep -q 'function groupLabel(g) { return g ? I18n.tr(g) : g }' "$QML" && echo yes || echo no)"
raw=$(grep -nE '\+ (grp|cgrp|ggrp|tgrp)\.modelData$' "$QML" | tr '\n' ' ')
check "...and no heading is drawn raw" "" "$raw"

# ⛔ EVERY DYNAMIC LOOKUP SAYS WHERE ITS MSGIDS COME FROM. tools/qml-xgettext.py
# refuses a non-literal argument unless the line carries `i18n-dynamic:`; this
# counts them, so one appearing without anyone noticing is a failing test rather
# than a panel that silently stops being translatable.
dyn=$(grep -c 'i18n-dynamic:' "$QML")
check "exactly four dynamic lookups, each explained" "4" "$dyn"

# ── 4b. ⛔ NOTHING SENT TO THE ENGINE GOES THROUGH THE CATALOG ──
badsend=$(grep -nE '(tlRun|devRun|thumbRun|run)\(\[ *I18n\.|, *I18n\.tr\("(split|title|transition|track|clip|set|get)' \
          "$QML" | tr '\n' ' ')
check "nothing sent to the engine goes through the catalog" "" "$badsend"

# ── 4c. ⛔ THE TWO PAGE NAMES AND THE PRESET KEYS ARE UNTOUCHED ──
badmode=$(grep -nE 'mode *={2,3} *I18n\.|mode = *I18n\.' "$QML" | tr '\n' ' ')
check "no page name is translated" "" "$badmode"
badpreset=$(grep -n 'name: *I18n\.' "$QML" | tr '\n' ' ')
check "no preset or effect name is translated" "" "$badpreset"

# ── 4d. ⛔ THE EXPORT FILENAME KEEPS THE C LOCALE ──────────
#
# ⚠ ASSERTED IN BOTH DIRECTIONS. Qt.formatDateTime with a format string ignores
# the locale, which is a bug wherever a person reads the result and exactly
# right here: this builds a FILE NAME, and under ar_EG a locale-formatted one
# would carry Arabic-Indic digits into a path.
check "the export timestamp is still built on the C locale" "yes" \
      "$(grep -q 'Qt.formatDateTime(new Date(), "yyyyMMdd-hhmmss")' "$QML" && echo yes || echo no)"
disp=$(grep -nE 'text:.*Qt\.formatDate(Time)?\(' "$QML" | tr '\n' ' ')
check "...and no drawn text is formatted against it" "" "$disp"

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
if [ "$fails" -eq 0 ]; then echo "all synstudio translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
