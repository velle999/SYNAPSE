#!/usr/bin/env bash
# i18n_bar_runtime.sh — the bar's translation bridge, actually translating.
#
# tests/i18n_bar.sh checks that every string is REACHABLE by a translator. This
# checks the other half: that a catalog on disk changes what the bar would draw.
# Between them sits quickshell/I18n.qml, and until this existed nothing executed
# a line of it.
#
# ⛔ RUN UNDER Qt's qml, NOT UNDER quickshell, AND THAT IS NOT A SHORTCUT.
# quickshell connects to Wayland at startup, and on a developer's desktop that
# means the LIVE session — `env -u WAYLAND_DISPLAY` does not prevent it, it
# falls back to the running compositor's socket. A test that has to be run
# carefully is a test that stops being run. tests/qmlstubs/ provides the two
# types I18n.qml touches, and everything else it does — the language
# resolution, the JSON parse, the plural rule, the fallbacks — is the real file.
#
# ⚠ ONE PROCESS PER LANGUAGE. A QML singleton is built once per engine and its
# language binding evaluates then, so each case is its own run with the
# environment baked in. That is also what happens for real: the language is
# fixed at login.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
QML=/usr/lib/qt6/bin/qml
[ -x "$QML" ] || { echo "SKIP: Qt 6's qml runtime is not installed."; exit 77; }

pass=0 fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/i18n" "$T/shell"
cp "$root/quickshell/I18n.qml" "$T/shell/"
printf 'singleton I18n 1.0 I18n.qml\n' > "$T/shell/qmldir"

# Two hand-written catalogs, not generated ones: this test is about the READER.
cat > "$T/i18n/de.json" <<'JSON'
{"":{"language":"de","nplurals":2,"plural":"n != 1"},
 "Volume":"Lautstärke",
 "%1 update":["%1 Aktualisierung","%1 Aktualisierungen"],
 "Empty":""}
JSON
cat > "$T/i18n/ru.json" <<'JSON'
{"":{"language":"ru","nplurals":3,
     "plural":"n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2"},
 "%1 file":["%1 файл","%1 файла","%1 файлов"]}
JSON
printf '{ this is not json' > "$T/i18n/pl.json"

# probe <name> <env-json> <expression> <expected>
probe() {
    local name="$1" envjson="$2" expr="$3" want="$4"
    cat > "$T/probe.qml" <<QML
import QtQuick
import Quickshell
import "shell"
QtObject {
    Component.onCompleted: {
        Quickshell._env = $envjson
        var got = String($expr)
        if (got !== "$want") {
            console.warn("got [" + got + "] want [$want]")
            Qt.exit(1)
        }
        Qt.exit(0)
    }
}
QML
    if QT_QPA_PLATFORM=offscreen "$QML" -I "$root/tests/qmlstubs" -I "$T" \
         "$T/probe.qml" 2>"$T/err"; then
        ok "$name"
    else
        bad "$name"; sed 's/^/          /' "$T/err" >&2
    fi
}

echo "bar i18n at runtime — $QML"

D="\"SYNUI_I18N_DIR\":\"$T/i18n\""

# ── the language comes out of the environment, glibc's way ─────────────────
probe "LANG picks the catalog"                "{$D,\"LANG\":\"de_DE.UTF-8\"}"   'I18n.language' 'de'
probe "LC_ALL outranks LANG"                  "{$D,\"LANG\":\"fr_FR.UTF-8\",\"LC_ALL\":\"de_DE.UTF-8\"}" 'I18n.language' 'de'
probe "LANGUAGE outranks both"                "{$D,\"LANG\":\"fr_FR.UTF-8\",\"LANGUAGE\":\"de:en\"}" 'I18n.language' 'de'
# ⛔ glibc's own rule, and the one a naive reading gets wrong.
probe "a C locale VETOES LANGUAGE"            "{$D,\"LANG\":\"C\",\"LANGUAGE\":\"de\"}" 'I18n.language' ''
probe "English is no catalog at all"          "{$D,\"LANG\":\"en_US.UTF-8\"}"   'I18n.language' ''
probe "the territory and codeset are dropped" "{$D,\"LANG\":\"de_AT.UTF-8@euro\"}" 'I18n.language' 'de'

# ── it translates ──────────────────────────────────────────────────────────
probe "a hit is translated"          "{$D,\"LANG\":\"de_DE.UTF-8\"}" 'I18n.tr("Volume")'      'Lautstärke'
probe "a miss falls back to English" "{$D,\"LANG\":\"de_DE.UTF-8\"}" 'I18n.tr("Not in here")' 'Not in here'
probe "an EMPTY msgstr falls back"   "{$D,\"LANG\":\"de_DE.UTF-8\"}" 'I18n.tr("Empty")'       'Empty'
probe "English draws the msgid"      "{$D,\"LANG\":\"en_US.UTF-8\"}" 'I18n.tr("Volume")'      'Volume'

# ── plurals, including a language with three forms ─────────────────────────
probe "plural: German singular" "{$D,\"LANG\":\"de_DE.UTF-8\"}" 'I18n.trn("%1 update","%1 updates",1)' '%1 Aktualisierung'
probe "plural: German plural"   "{$D,\"LANG\":\"de_DE.UTF-8\"}" 'I18n.trn("%1 update","%1 updates",5)' '%1 Aktualisierungen'
probe "plural: Russian form 0 (1)"   "{$D,\"LANG\":\"ru_RU.UTF-8\"}" 'I18n.trn("%1 file","%1 files",1)'  '%1 файл'
probe "plural: Russian form 1 (3)"   "{$D,\"LANG\":\"ru_RU.UTF-8\"}" 'I18n.trn("%1 file","%1 files",3)'  '%1 файла'
probe "plural: Russian form 2 (11)"  "{$D,\"LANG\":\"ru_RU.UTF-8\"}" 'I18n.trn("%1 file","%1 files",11)' '%1 файлов'
probe "plural: Russian form 2 (100)" "{$D,\"LANG\":\"ru_RU.UTF-8\"}" 'I18n.trn("%1 file","%1 files",100)' '%1 файлов'

# ── every failure is English, never an exception ───────────────────────────
probe "malformed JSON does not throw"  "{$D,\"LANG\":\"pl_PL.UTF-8\"}" 'I18n.tr("Volume")' 'Volume'
probe "a language with no catalog"     "{$D,\"LANG\":\"ko_KR.UTF-8\"}" 'I18n.tr("Volume")' 'Volume'
probe "no catalog directory at all"    "{\"SYNUI_I18N_DIR\":\"$T/nothing\",\"LANG\":\"de_DE.UTF-8\"}" 'I18n.tr("Volume")' 'Volume'
# ⚠ trn must still choose a form with no catalog, from the English pair.
probe "trn with no catalog picks English" "{$D,\"LANG\":\"ko_KR.UTF-8\"}" 'I18n.trn("%1 file","%1 files",5)' '%1 files'

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ]
