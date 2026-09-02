#!/usr/bin/env bash
# i18n_test.sh — a translated string in syn-edit must be reachable by a translator.
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
# ⛔ 3. AND THE ENGINE'S OWN LANGUAGE WAS NOT SWEPT UP. This window owns no
#    text and no buffer: a long-lived `syn-edit serve` holds both, and the QML
#    sends it ex commands and vim keystrokes as strings — "set tree!", ":e ",
#    "gui insert", "o- [ ] ", "ggVG". A context-menu row carries the one it
#    runs in `keys:` or `act:` right beside the label a person reads.
#    Translating one does not make a German editor; it makes an editor whose
#    menu items do nothing. Same rule as ctlpanel.c's settings keys.
#
# ⛔ 4. THE MODE STRING IS THE ENGINE'S AND IS COMPARED IN NINE PLACES.
#    `st.mode === "INSERT"`, `.indexOf("V") === 0` — the caret shape, the chip
#    colour and the insert test all read it. Only the CHIP is drawn, so the
#    mapping from mode to a translated name happens in one place and the
#    comparisons keep the engine's spelling.
#
# ⛔ 5. AND `label: "-"` IS A SEPARATOR SENTINEL. The context-menu delegate
#    reads `modelData.label === "-"` to draw a 5-pixel rule instead of a row.
#    A translated one is three empty 26-pixel entries in the menu.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
QML="$root/data/syn-edit.qml"
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

echo "syn-edit translations"

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
    have=$(grep -c '^msgid "' "$root/po/syn-edit.pot" 2>/dev/null || echo 0)
    now=$(grep -c '^msgid "' "$tmp/new.pot" 2>/dev/null || echo 0)
    check "po/syn-edit.pot is current ($have msgids)" "$now" "$have"
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

# ── 4. ⛔ THE ENGINE'S OWN LANGUAGE IS UNTOUCHED ───────────
badkey=$(grep -n 'act: *I18n\.\|keys: *I18n\.\|hint: *I18n\.\|icon: *I18n\.' "$QML" | tr '\n' ' ')
check "no menu action, keystroke or icon name is translated" "" "$badkey"

# ⛔ AND NOTHING IS SENT TO THE ENGINE THROUGH THE CATALOG. send(), sendEx(),
# sendKeys() and actKeys() all take the editor's own language; a translated
# argument is a command the C core does not have.
badsend=$(grep -nE '(send|sendEx|sendKeys|actKeys)\( *I18n\.' "$QML" | tr '\n' ' ')
check "nothing sent to the engine goes through the catalog" "" "$badsend"

# ⛔ THE MODE COMPARISONS KEEP THE ENGINE'S SPELLING. Only the chip is drawn.
badmode=$(grep -nE 'st\.mode *={2,3} *I18n\.|indexOf\( *I18n\.' "$QML" | tr '\n' ' ')
check "no mode comparison is translated" "" "$badmode"
# ...and the chip DOES translate, or the whole point of the mapper is lost.
check "...but the mode chip does" "1" \
      "$(grep -c 'if (m === "REPLACE") return I18n.tr("OVERWRITE")' "$QML")"

# ⛔ THE SEPARATOR SENTINEL IS STILL A BARE "-".
sep=$(grep -n 'label: *I18n\.tr("-")' "$QML" | tr '\n' ' ')
check "the menu separator sentinel is not translated" "" "$sep"
# ⚠ "at least one", not a COUNT. The delegate matches it twice — once for the
# row height and once for the rule's visibility — and pinning the number makes
# the test fail on a refactor that changed nothing about the sentinel.
check "...and the delegate still matches it" "yes" \
      "$(grep -q 'modelData.label === "-"' "$QML" && echo yes || echo no)"

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
if [ "$fails" -eq 0 ]; then echo "all syn-edit translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
