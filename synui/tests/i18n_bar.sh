#!/usr/bin/env bash
# i18n_bar.sh — a translated string in the BAR must be reachable by a translator.
#
# The compositor's own catalogs have tests/i18n.sh beside them. This is the same
# question one tier up, in QML, where there is one extra way to get it wrong.
#
# ⛔ 1. qsTr() IS A TRAP AND IT IS THE FIRST THING CHECKED. Qt's translation
#    path needs someone to call QTranslator::load() and installTranslator()
#    before the QML engine starts. quickshell 0.3.1 does neither — there is no
#    installTranslator anywhere in the binary and no way to reach one from QML.
#    So qsTr("Volume") compiles, returns "Volume", and translates nothing, in
#    every language, forever. It reads in a diff exactly like a marked string.
#    Marking the bar up with it would be a day's work that ships an English bar
#    and reports success.
#
# ⚠ 2. A FILE MISSING FROM po-bar/POTFILES is silently English. Its I18n.tr()
#    calls still compile and still look up at runtime, so nothing warns; the
#    strings simply never reach a template and no translator is offered them.
#
# ⚠ 3. A NON-LITERAL ARGUMENT extracts nothing. tools/qml-xgettext.py reads the
#    source, not the running program, so I18n.tr(someVariable) is the QML shape
#    of the N_() trap src/i18n.h documents — marked-looking and English.
#
# ⚠ 4. THE TWO LANGUAGE LISTS MUST AGREE. The bar and the compositor's panels
#    are on screen together; a German control panel beside an English bar reads
#    as the bar being broken rather than as work not done.
#
# ⛔ 5. AND THE CONFIG KEYS WERE NOT SWEPT UP. A bar row carries its bar.json
#    spelling in the field beside its label, and a start-menu row carries the
#    page it opens. Translating one does not make a German bar — it makes a bar
#    that writes German into bar.json and silently loses the row, or a menu
#    whose category opens a page that does not exist. Same rule as ctlpanel.c's
#    settings keys, and the same reason it is asserted rather than trusted.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
tree="$root/quickshell"
fails=0

check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "synui bar translations"

listed=$(grep -vE '^\s*#|^\s*$' "$root/po-bar/POTFILES" | sort)

# ── 1. qsTr() is never used ───────────────────────────────
# ⚠ COUNTED, never `| grep -q`: grep -q exits on the first match, the producer
# takes SIGPIPE, and under pipefail the pipeline reports 141 — a FAILURE on a
# match. Same trap the plugin and synpkg suites document.
# ⚠ CODE ONLY. The first version of this matched I18n.qml's own header, which
# explains at length why qsTr() is a trap — so the check failed on the file that
# exists to prevent the thing it checks for. Comments are stripped first.
qstr=$(for f in $(find "$tree" -name '*.qml'); do
           sed -e 's://.*::' "$f" |
           awk 'BEGIN{c=0}
                {line=$0
                 while (match(line, /\/\*/)) { c=1; line=substr(line,1,RSTART-1) }
                 if (c && match($0, /\*\//)) { c=0; line=substr($0,RSTART+2) }
                 if (!c) print FILENAME ":" FNR ":" line}' FILENAME="$f" |
           grep -n 'qsTr[[:space:]]*(\|qsTranslate[[:space:]]*(' | sed "s|^|$f: |"
       done | tr '\n' ' ')
check "no qsTr() — quickshell has no translator to load it" "" "$qstr"

# ── 2. every .qml that marks a string is offered to the extractor ──
missing=""
while IFS= read -r f; do
    rel=${f#"$tree"/}
    grep -qxF "$rel" <<<"$listed" || missing="$missing $rel"
# ⚠ .js TOO, AND THAT IS NOT A DETAIL. The welcome guide's every word lives in
# welcome/pages.js — a `.pragma library` file, for the reason its own header
# gives — and a check that looked only at *.qml could not see it. The guide
# shipped entirely English through eight components' worth of this work with
# nothing able to say so.
# ⚠ CODE ONLY, the same as the qsTr check above and for the same reason: the
# first run of this flagged GuideState.qml, whose only `I18n.tr(` is inside a
# COMMENT explaining why pages.js takes the singleton. A file listed because of
# its prose is a file nobody can remove from the list.
done < <(for f in $(find "$tree" \( -name '*.qml' -o -name '*.js' \) | sort); do
             sed -e 's://.*::' "$f" |
             awk 'BEGIN{c=0}
                  {line=$0
                   while (match(line, /\/\*/)) { c=1; line=substr(line,1,RSTART-1) }
                   if (c && match($0, /\*\//)) { c=0; line=substr($0,RSTART+2) }
                   if (!c) print line}' |
             grep -qE 'I18n\.(tr|trn)\(' && echo "$f"
         done)
check "every .qml or .js using I18n.tr() is in po-bar/POTFILES" "" "$missing"

# ── 3. every file in POTFILES exists ──────────────────────
gone=""
while IFS= read -r rel; do
    [ -f "$tree/$rel" ] || gone="$gone $rel"
done <<<"$listed"
check "every file in po-bar/POTFILES exists" "" "$gone"

# ── 4. every marked argument is a literal, and the template is current ──
#
# Both from one run of the extractor. --strict makes a non-literal argument an
# error rather than a skip, and the regenerated template is compared on msgids
# alone because a .pot carries no creation date here but the refs move with
# every edit above them.
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
strict=$("$root/tools/qml-xgettext.py" --root "$tree" --files "$root/po-bar/POTFILES" \
         -o "$tmp/new.pot" --strict 2>&1 >/dev/null)
check "every I18n.tr() argument is a string literal" "" "$strict"

# ⛔ AND THE GUIDE'S OWN WORDS ACTUALLY REACHED IT. A file can be listed, read
# by the extractor, and contribute NOTHING while the run reports success —
# which is exactly what happened here: pages.js took its translator as a
# parameter named `tr`, so its call sites read `tr("…")` and the extractor,
# which keys on `I18n.tr(`, matched none of them. The parameter is named I18n
# now. This asserts the result rather than the spelling.
guide=$(grep -c '^msgid "Welcome to SynapseOS"' "$root/po-bar/synui-bar.pot" 2>/dev/null)
check "the welcome guide's pages reached the template" "1" "$guide"

if [ -f "$tmp/new.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po-bar/synui-bar.pot" 2>/dev/null || echo 0)
    now=$(grep -c '^msgid "' "$tmp/new.pot" 2>/dev/null || echo 0)
    check "po-bar/synui-bar.pot is current ($have msgids)" "$now" "$have"
fi

# ── 5. the two language lists agree ───────────────────────
bar=$(grep -vE '^\s*#|^\s*$' "$root/po-bar/LINGUAS" | sort | tr '\n' ' ')
cc=$(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS" | sort | tr '\n' ' ')
check "po-bar/LINGUAS matches po/LINGUAS" "$cc" "$bar"

# ── 6. every language named has a .po ─────────────────────
absent=""
while IFS= read -r l; do
    [ -f "$root/po-bar/$l.po" ] || absent="$absent $l"
done < <(grep -vE '^\s*#|^\s*$' "$root/po-bar/LINGUAS")
check "every language in po-bar/LINGUAS has a .po" "" "$absent"

# ── 7. ⛔ THE KEYS BESIDE THE LABELS ARE UNTOUCHED ─────────
#
# These are the two that would be catastrophic rather than merely absent, and
# both look exactly like a label in a diff.
# ⚠ `action`, `page` and `arg` ONLY. `key:` was in this list and had to come
# out: it is the bar.json spelling in BarConfig's rows and a drawn meter label
# in SysMonitor's, and the name alone cannot tell them apart. BarConfig is
# asserted directly below instead, which is the file where translating a key
# would actually lose a setting.
# ⚠ --include='*.js' HERE TOO: every row in the welcome guide carries the
# action `synctl dispatch` is given right beside the label a person reads, and
# a translated one dispatches something the compositor does not have.
badkey=$(grep -rn 'action: *I18n\.\|page: *I18n\.\|arg: *I18n\.\|kind: *I18n\.\|live: *I18n\.' \
         "$tree" --include='*.qml' --include='*.js' 2>/dev/null | tr '\n' ' ')
check "no dispatch action, page id, panel arg or row kind is translated" "" "$badkey"

# ⛔ AND THE GUIDE'S PAGE IDS AND FALLBACK CHORDS. GuideState matches on `id`;
# `key` is a picture of keycaps and only a fallback at that — the live chord
# comes from `synctl binds`.
guidekey=$(grep -n 'id: *I18n\.\|key: *I18n\.' "$tree/welcome/pages.js" 2>/dev/null |
           tr '\n' ' ')
check "no welcome page id or fallback chord is translated" "" "$guidekey"

# ⛔ THE ONE THAT WOULD LOSE A SETTING. BarConfig's rows carry the bar.json
# spelling beside the label, and the file is written with it.
# ⚠ NO `|| echo 0`. grep -c prints 0 AND exits 1 when it matches nothing, so
# the fallback fired on the passing case and the count came out "0\n0".
barkey=$(sed -n '/readonly property var rows/,/\]/p' "$tree/BarConfig.qml" |
         grep -c 'key: *I18n\.')
check "no bar.json key in BarConfig is translated" "0" "$barkey"

# And the start menu's category buckets, which are the keys `apps` is indexed
# by and the sort tests against — translated only at the draw step, in
# catLabel(). A translated catTable would file every application under Other.
cattable=$(sed -n '/readonly property var catTable/,/\]/p' "$tree/StartMenu.qml" |
           grep -c 'I18n\.' 2>/dev/null)
check "the start menu's category table is not translated" "0" "$cattable"
check "...and its display names go through catLabel()" "yes" \
      "$(grep -q 'function catLabel' "$tree/StartMenu.qml" &&
         grep -q 'label: rowModel.catLabel(c)' "$tree/StartMenu.qml" && echo yes || echo no)"

# ── 8. the catalogs compile, and to the shape I18n.qml reads ──
badjson=""
while IFS= read -r l; do
    po="$root/po-bar/$l.po"
    [ -f "$po" ] || continue
    msgfmt -c -o /dev/null "$po" 2>/dev/null || badjson="$badjson $l(msgfmt)"
    "$root/tools/po2json.py" "$po" -o "$tmp/$l.json" >/dev/null 2>&1 \
        || badjson="$badjson $l(po2json)"
    python3 -c "
import json,sys
d = json.load(open('$tmp/$l.json'))
m = d.get('')
assert isinstance(m, dict), 'no header entry'
assert m.get('nplurals', 0) >= 1, 'no nplurals'
assert m.get('plural'), 'no plural rule'
" 2>/dev/null || badjson="$badjson $l(shape)"
done < <(grep -vE '^\s*#|^\s*$' "$root/po-bar/LINGUAS")
check "every catalog compiles to a JSON I18n.qml can read" "" "$badjson"

# ── 8b. ⛔ EACH LANGUAGE'S PLURAL RULE MATCHES ITS COMPOSITOR CATALOG ───────
#
# Running in range is not the same as being RIGHT. `nplurals=2; plural=(n != 1)`
# is a perfectly valid rule that passes every check below and is wrong for
# Arabic, which has six forms, and for Chinese, which has one. That is not
# hypothetical: `msginit -l ar` and `-l zh` could not resolve those bare codes
# to a locale and silently fell back to the template's English default, so both
# catalogs were created wrong and nothing said so.
#
# The two sets cover the same thirteen languages, so a disagreement is a bug by
# construction — and po/ is the older, reviewed one. Comparing them is a far
# stronger check than any property of the rule on its own.
plbad=""
while IFS= read -r l; do
    [ -f "$root/po-bar/$l.po" ] && [ -f "$root/po/$l.po" ] || continue
    # ⚠ JOINED ACROSS .po CONTINUATION LINES BEFORE COMPARING. gettext wraps a
    # long Plural-Forms over two "…" strings and where it breaks depends on the
    # header lines above it, so Russian and Polish are wrapped and the rest are
    # not. A sed line-range got this wrong in both directions — it matched the
    # wrapped pair and missed every single-line one, which reported eleven
    # false disagreements. The join is done properly here.
    a=$(python3 -c "
import re,sys
t = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r'\"Plural-Forms: (.*?)\\\\n\"', t, re.S)
print(re.sub(r'\"\s*\n\s*\"', '', m.group(1)).strip() if m else '')
" "$root/po-bar/$l.po")
    b=$(python3 -c "
import re,sys
t = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r'\"Plural-Forms: (.*?)\\\\n\"', t, re.S)
print(re.sub(r'\"\s*\n\s*\"', '', m.group(1)).strip() if m else '')
" "$root/po/$l.po")
    [ -n "$a" ] && [ "$a" = "$b" ] || plbad="$plbad $l"
done < <(grep -vE '^\s*#|^\s*$' "$root/po-bar/LINGUAS")
check "every plural rule matches the compositor's catalog for that language" "" "$plbad"

# ── 9. every plural rule runs, IN THE ENGINE THAT WILL RUN IT ──────────────
#
# ⛔ NOT RE-IMPLEMENTED IN PYTHON, AND THE FIRST VERSION OF THIS WAS. gettext
# writes the rule as a C expression — `n%10==1 && n%100!=11 ? 0 : …` — which
# JavaScript reads unchanged, which is exactly why I18n.qml can compile it with
# `new Function`. Python cannot parse it at all, so a Python eval() failed
# Polish and Russian, the two languages whose rules are ternary chains, on
# catalogs that were entirely correct. A hand-written C-to-Python translator
# was the next wrong answer: it is a second implementation of the thing under
# test, and its own first draft mis-handled the rule's outer parentheses.
#
# So the rule is handed to Qt's QML engine, which IS the engine quickshell
# runs. That checks the two things that matter together: that `new Function`
# is available there at all, and that every rule answers in range.
#
# Skips where Qt's qml runtime is absent — the same rule qs_module.sh applies
# to qmllint. ⛔ /usr/lib/qt6/bin/qml, never a `qml` on PATH that belongs to
# another Qt.
QML=/usr/lib/qt6/bin/qml
if [ ! -x "$QML" ]; then
    printf '  skip  plural rules (Qt 6 qml runtime is not installed)\n'
else
    {
        printf 'import QtQuick\nQtObject {\n  Component.onCompleted: {\n'
        printf '    var bad = 0\n'
        while IFS= read -r l; do
            [ -f "$tmp/$l.json" ] || continue
            n=$(python3 -c "import json;print(json.load(open('$tmp/$l.json'))['']['nplurals'])")
            r=$(python3 -c "import json;print(json.load(open('$tmp/$l.json'))['']['plural'])")
            printf '    try {\n'
            printf '      var f_%s = new Function("n", "return (%s)")\n' "$l" "$r"
            printf '      for (var i = 0; i < 200; i++) {\n'
            printf '        var v = Number(f_%s(i))\n' "$l"
            printf '        if (!(v >= 0 && v < %s)) { console.warn("%s out of range at " + i); bad++; break }\n' "$n" "$l"
            printf '      }\n'
            printf '    } catch (e) { console.warn("%s does not compile: " + e); bad++ }\n' "$l"
        done < <(grep -vE '^\s*#|^\s*$' "$root/po-bar/LINGUAS")
        printf '    Qt.exit(bad)\n  }\n}\n'
    } > "$tmp/plural.qml"

    QT_QPA_PLATFORM=offscreen "$QML" "$tmp/plural.qml" >/dev/null 2>"$tmp/plural.err"
    rc=$?
    check "every plural rule compiles and answers in range (Qt's own engine)" "0" "$rc"
    [ "$rc" = 0 ] || sed 's/^/        /' "$tmp/plural.err" >&2
fi

echo
if [ "$fails" -eq 0 ]; then echo "all bar translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
