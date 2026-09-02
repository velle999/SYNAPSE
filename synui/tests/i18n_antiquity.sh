#!/usr/bin/env bash
# i18n_antiquity.sh — the Antiquity shell's words, reachable by a translator.
#
# tests/i18n_bar.sh asks this of the SYNAPSE bar and tests/i18n.sh asks it of
# the compositor's C. This is the third consumer: `bar_shell = antiquity`
# starts quickshell-antiquity/ instead of quickshell/, and every trap the bar
# suite documents applies here unchanged — qsTr() that compiles and translates
# nothing, a file missing from POTFILES, a non-literal argument, two language
# lists drifting apart. Read that file's header for the why of each.
#
# What is DIFFERENT here, and is the reason this is its own suite:
#
# ⛔ A. THE WEATHER WIDGET SWITCHES ON WORDS THAT ARRIVE FROM THE NETWORK.
#    `case "Snow":`, "Rain", "Drizzle", "Thunderstorm" and "Atmosphere" are
#    OpenWeatherMap's `weather[0].main` values, compared and never drawn.
#    Translating one is a switch that silently stops matching on every
#    non-English desktop — the humor never turns wet, the fog never appears,
#    and the widget looks merely wrong rather than broken.
#
# ⛔ B. AND SO ARE THE THREE TEMPERATURE UNITS. "metric", "standard" and
#    "imperial" are what getTemp() compares Config.settings.openWeatherMap.unit
#    against AND what the user types into the Unit field. The settings sentence
#    that lists them passes them through .arg() for exactly that reason: a
#    German invited to type "metrisch" is being invited into a field that will
#    refuse it.
#
# ⛔ C. Qt.formatDateTime(d, "<format>") IGNORES THE LOCALE. Measured under
#    quickshell 0.3.1: on a de_DE and a ja_JP desktop it still returns
#    "Tuesday, 1 September 2026", because the string overload formats against
#    QLocale::c() — even though Qt.locale().name reads de_DE / ja_JP correctly
#    in the same process. Every clock in this tree used it, so the shell drew
#    an English date under thirteen translated labels. The overload that does
#    use the locale is Date.toLocaleString(Qt.locale(), fmt), and this suite
#    fails on any return of the old one.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
tree="$root/quickshell-antiquity"
fails=0

check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "Antiquity shell translations"

listed=$(grep -vE '^\s*#|^\s*$' "$root/po-antiquity/POTFILES" | sort)

# ── 1. qsTr() is never used ───────────────────────────────
# ⚠ CODE ONLY, and counted rather than `| grep -q` — both traps are written up
# in tests/i18n_bar.sh. I18n.qml's own header explains why qsTr() is a trap and
# would otherwise fail the check it exists to enforce.
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
done < <(grep -rlE 'I18n\.(tr|trn)\(' "$tree" --include='*.qml' 2>/dev/null | sort)
check "every .qml using I18n.tr() is in po-antiquity/POTFILES" "" "$missing"

# ── 3. every file in POTFILES exists ──────────────────────
gone=""
while IFS= read -r rel; do
    [ -f "$tree/$rel" ] || gone="$gone $rel"
done <<<"$listed"
check "every file in po-antiquity/POTFILES exists" "" "$gone"

# ── 4. every marked argument is a literal, and the template is current ──
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
strict=$("$root/tools/qml-xgettext.py" --root "$tree" --files "$root/po-antiquity/POTFILES" \
         -o "$tmp/new.pot" --strict 2>&1 >/dev/null)
check "every I18n.tr() argument is a string literal" "" "$strict"

if [ -f "$tmp/new.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po-antiquity/synui-antiquity.pot" 2>/dev/null || echo 0)
    now=$(grep -c '^msgid "' "$tmp/new.pot" 2>/dev/null || echo 0)
    check "po-antiquity/synui-antiquity.pot is current ($have msgids)" "$now" "$have"
fi

# ── 5. all three language lists agree ─────────────────────
aq=$(grep -vE '^\s*#|^\s*$' "$root/po-antiquity/LINGUAS" | sort | tr '\n' ' ')
cc=$(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS" | sort | tr '\n' ' ')
check "po-antiquity/LINGUAS matches po/LINGUAS" "$cc" "$aq"

# ── 6. every language named has a .po ─────────────────────
absent=""
while IFS= read -r l; do
    [ -f "$root/po-antiquity/$l.po" ] || absent="$absent $l"
done < <(grep -vE '^\s*#|^\s*$' "$root/po-antiquity/LINGUAS")
check "every language in po-antiquity/LINGUAS has a .po" "" "$absent"

# ── 7. ⛔ THE PROTOCOL WORDS ARE UNTOUCHED — trap A above ──
owm=$(grep -n 'case *I18n\.' "$tree/widgets/WeatherWidget.qml" 2>/dev/null | tr '\n' ' ')
check "no OpenWeatherMap condition is translated" "" "$owm"

# ...and each of the five is still there to be matched, spelled the way the
# service spells it. A rename is as silent as a translation, and this is the
# half that says so: absent from the switch, the humor never turns wet.
# ⚠ NAMED, not counted in one grep — "four of five present" has to report
# WHICH, or the next reader re-derives it.
gonecase=""
for w in Snow Rain Drizzle Thunderstorm Atmosphere; do
    grep -qF "case \"$w\":" "$tree/widgets/WeatherWidget.qml" 2>/dev/null \
        || gonecase="$gonecase $w"
done
check "all five weather conditions are still matched literally" "" "$gonecase"

# ── 7b. ⛔ AND THE THREE UNIT VALUES — trap B above ────────
# ⚠ SCOPED TO getTemp()'S OWN switch, not to `case I18n.` anywhere in the
# file — the weather conditions above are also cases, and a pattern that
# matched both reported a translated condition as a translated unit. Two
# distinct hazards deserve two checks that can disagree.
unit=$(sed -n '/function getTemp/,/^    }/p' "$tree/widgets/WeatherWidget.qml" |
       grep -n 'case *I18n\.' | tr '\n' ' ')
check "no temperature unit value is translated" "" "$unit"
for u in metric standard imperial; do
    sed -n '/function getTemp/,/^    }/p' "$tree/widgets/WeatherWidget.qml" |
        grep -qF "case \"$u\":" || unit="$unit missing:$u"
done
check "...and all three are still matched literally" "" "$unit"
# ⚠ NO `|| echo 0`. grep -c prints 0 AND exits 1 when it matches nothing, so
# the fallback fires on the failing case and the count comes back "0\n0" —
# which reads as a broken test rather than a caught bug.
check "...and the sentence that lists them passes them through .arg()" "1" \
      "$(grep -c 'available options: %1").arg("metric, standard, imperial")' \
         "$tree/popups/SettingsWindow.qml" 2>/dev/null)"

# ── 7c. ⛔ NO WIDGET TYPE NAME IS TRANSLATED ───────────────
#
# Config.widgetTypes are the keys widgets.json is written with and the
# filenames the loader resolves ("Weather" -> WeatherWidget.qml). A translated
# one is a saved widget that never loads again.
wt=$(sed -n '/widgetTypes/,/\]/p' "$tree/Config.qml" | grep -c 'I18n\.' 2>/dev/null)
check "no widget type name is translated" "0" "$wt"

# ── 8. ⛔ NO CLOCK FORMATS AGAINST THE C LOCALE — trap C above ──
fdt=$(grep -rn 'Qt\.formatDateTime *(' "$tree" --include='*.qml' 2>/dev/null | tr '\n' ' ')
check "no Qt.formatDateTime() — it ignores the locale" "" "$fdt"

# And every format string that carries a WORD is in the catalog, because the
# locale fixes the names and only a translator can fix the ORDER: ja wants
# yyyy年M月d日dddd, not "dddd, d MMMM yyyy" with Japanese words in it.
rawfmt=$(grep -rnE 'toLocaleString\(Qt\.locale\(\), *"[^"]*(dddd|MMMM|MMM|AP)' \
         "$tree" --include='*.qml' 2>/dev/null | tr '\n' ' ')
check "every format string with a word in it goes through the catalog" "" "$rawfmt"

# ── 9. the catalogs compile, to the shape I18n.qml reads ───
badjson=""
while IFS= read -r l; do
    po="$root/po-antiquity/$l.po"
    [ -f "$po" ] || continue
    msgfmt -c -o /dev/null "$po" 2>/dev/null || badjson="$badjson $l(msgfmt)"
    "$root/tools/po2json.py" "$po" -o "$tmp/$l.json" >/dev/null 2>&1 \
        || badjson="$badjson $l(po2json)"
    python3 -c "
import json
d = json.load(open('$tmp/$l.json'))
m = d.get('')
assert isinstance(m, dict), 'no header entry'
assert m.get('nplurals', 0) >= 1, 'no nplurals'
assert m.get('plural'), 'no plural rule'
" 2>/dev/null || badjson="$badjson $l(shape)"
done < <(grep -vE '^\s*#|^\s*$' "$root/po-antiquity/LINGUAS")
check "every catalog compiles to a JSON I18n.qml can read" "" "$badjson"

# ── 9b. ⛔ EACH PLURAL RULE MATCHES ITS COMPOSITOR CATALOG ──
#
# This tree has no plural msgid today, which is exactly when a wrong rule is
# invisible: the first trn() added would pick a form from a header nobody had
# ever exercised. The rules are copied from po/ and compared to it. ⚠ Joined
# across continuation lines first — Polish, Russian and Arabic wrap, and a
# single-line regex captured half of Polish's expression here, which msgfmt
# then called an "invalid plural expression".
plbad=""
while IFS= read -r l; do
    [ -f "$root/po-antiquity/$l.po" ] && [ -f "$root/po/$l.po" ] || continue
    a=$(python3 -c "
import re,sys
t = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r'\"Plural-Forms: (.*?)\\\\n\"', t, re.S)
print(re.sub(r'\"\s*\n\s*\"', '', m.group(1)).strip() if m else '')
" "$root/po-antiquity/$l.po")
    b=$(python3 -c "
import re,sys
t = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r'\"Plural-Forms: (.*?)\\\\n\"', t, re.S)
print(re.sub(r'\"\s*\n\s*\"', '', m.group(1)).strip() if m else '')
" "$root/po/$l.po")
    [ -n "$a" ] && [ "$a" = "$b" ] || plbad="$plbad $l"
done < <(grep -vE '^\s*#|^\s*$' "$root/po-antiquity/LINGUAS")
check "every plural rule matches the compositor's catalog for that language" "" "$plbad"

# ── 10. ⛔ I18n.qml IS THE BAR'S, BYTE FOR BYTE ────────────
#
# There are four copies of this file in the tree now and more in other
# packages. They are meant to be one file, and the only thing keeping them one
# is a check: a fix made in the bar's copy and not carried here is a bug that
# reproduces in one shell and not the other, which is the worst shape a bug can
# take. cmp, not a feature test — the point is that they do not drift at all.
if cmp -s "$root/quickshell/I18n.qml" "$tree/I18n.qml"; then
    check "I18n.qml is byte-identical to the bar's" "same" "same"
else
    check "I18n.qml is byte-identical to the bar's" "same" "differs"
fi

# ── 11. THE TREE STILL PARSES ─────────────────────────────
#
# ⚠ WHAT THIS CATCHES IS SYNTAX, and saying so matters. qmllint reports a
# genuine parse error as `Warning: … [syntax]` and exits 255, which is the
# signal used here; a MISSING PROPERTY it reports at exit 0, and quickshell
# then refuses the file at runtime — so a green run here is not "the shell
# starts". It is "no edit in this tree left a file that cannot be read", which
# is the failure a marking pass actually produces.
#
# ⛔ /usr/lib/qt6/bin/qmllint, never /usr/bin/qmllint — the one on PATH belongs
# to another Qt. Skips (not fails) where it is absent.
LINT=/usr/lib/qt6/bin/qmllint
if [ ! -x "$LINT" ]; then
    printf '  skip  qmllint (Qt 6 is not installed)\n'
else
    broken=""
    while IFS= read -r f; do
        "$LINT" "$f" >/dev/null 2>&1 || broken="$broken ${f#"$tree"/}"
    done < <(find "$tree" -name '*.qml' | sort)
    check "every .qml in the tree parses" "" "$broken"
fi

echo
if [ "$fails" -eq 0 ]; then echo "all Antiquity translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
