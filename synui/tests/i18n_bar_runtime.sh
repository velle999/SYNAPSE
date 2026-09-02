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
# They are handed to the stub FileView as CONTENT keyed by path — see
# tests/qmlstubs/Quickshell/Quickshell.qml for why reading the disk is not this
# suite's job and cannot be done from plain QML anyway.
DE_JSON='{"":{"language":"de","nplurals":2,"plural":"n != 1"},
 "Volume":"Lautstärke",
 "%1 update":["%1 Aktualisierung","%1 Aktualisierungen"],
 "Empty":""}'
RU_JSON='{"":{"language":"ru","nplurals":3,
     "plural":"n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2"},
 "%1 file":["%1 файл","%1 файла","%1 файлов"]}'
PL_JSON='{ this is not json'

# ⚠ The keys are the paths I18n.qml will ASK FOR, which is the second half of
# what this suite pins: get the path wrong and the injected content is never
# found, exactly as a wrong path on a real disk would not be.
FILES="{\"$T/i18n/de.json\": $(printf '%s' "$DE_JSON" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))'),
        \"$T/i18n/ru.json\": $(printf '%s' "$RU_JSON" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))'),
        \"$T/i18n/pl.json\": $(printf '%s' "$PL_JSON" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')}"

# probe <name> <env-json> <expression> <expected>
#
# ⛔ SUCCESS IS EXIT 7, NOT EXIT 0, AND THAT IS THE WHOLE POINT. `qml` exits
# ZERO when it cannot load the file at all — it prints "Did not load any
# objects, exiting." and returns success — so a probe that treated 0 as a pass
# reported ok for a QML file that never ran a line. Every assertion in this
# suite did exactly that: a deliberately impossible comparison passed, and so
# did `I18n.tr("Volume")` checked against the string NONSENSE. Twenty-three
# green assertions proving nothing.
#
# So the QML says 7 when it matched and 1 when it did not, and ANY other status
# — 0 from a file that did not load, a crash, a timeout — is a failure of the
# harness and is reported as one rather than as a pass.
probe() {
    local name="$1" envjson="$2" expr="$3" want="$4" rc
    cat > "$T/probe.qml" <<QML
import QtQuick
import Quickshell
import "shell"
QtObject {
    Component.onCompleted: {
        Quickshell._env = $envjson
        Quickshell._files = $FILES
        var got = String($expr)
        Qt.exit(got === "$want" ? 7 : 1)
    }
}
QML
    QT_QPA_PLATFORM=offscreen "$QML" -I "$root/tests/qmlstubs" -I "$T" \
        "$T/probe.qml" >/dev/null 2>"$T/err"
    rc=$?
    case "$rc" in
        7) ok "$name" ;;
        1) bad "$name"
           # ⚠ The value is fetched in a SECOND run rather than printed by the
           # first: console.warn() is swallowed by `qml` in this configuration,
           # so the only channel out of the probe is its exit status.
           printf '          wanted [%s]\n' "$want" >&2 ;;
        *) bad "$name — the probe did not run (qml exited $rc)"
           sed 's/^/          /' "$T/err" >&2 ;;
    esac
}

echo "bar i18n at runtime — $QML"

D="\"SYN_I18N_DIR\":\"$T/i18n\""

# ── the language comes out of the environment, glibc's way ─────────────────
probe "LANG picks the catalog"                "{$D,\"LANG\":\"de_DE.UTF-8\"}"   'I18n.language' 'de'
probe "LC_ALL outranks LANG"                  "{$D,\"LANG\":\"fr_FR.UTF-8\",\"LC_ALL\":\"de_DE.UTF-8\"}" 'I18n.language' 'de'
probe "LANGUAGE outranks both"                "{$D,\"LANG\":\"fr_FR.UTF-8\",\"LANGUAGE\":\"de:en\"}" 'I18n.language' 'de'
# ⛔ glibc's own rule, and the one a naive reading gets wrong.
probe "a C locale VETOES LANGUAGE"            "{$D,\"LANG\":\"C\",\"LANGUAGE\":\"de\"}" 'I18n.language' ''
probe "English is no catalog at all"          "{$D,\"LANG\":\"en_US.UTF-8\"}"   'I18n.language' ''
probe "the territory and codeset are dropped" "{$D,\"LANG\":\"de_AT.UTF-8@euro\"}" 'I18n.language' 'de'

# ── the catalogs are found BESIDE the file, with no environment at all ─────
#
# ⛔ THE ONE THING SELF-LOCATION CHANGES IS WHICH DIRECTORY IS COMPUTED, and
# that is what this pins. Qt.resolvedUrl() inside a singleton resolves against
# the singleton's own file, which is what lets one byte-identical copy of
# I18n.qml serve eight separate packages — none of which may depend on synui.
# Every case above sets SYN_I18N_DIR; this is the case a real install takes.
#
# ⚠ It asserts the PATH, not a lookup, and deliberately. Where the path comes
# from is the new logic; that FileView then reads it is the same code the
# twenty cases below exercise, and the stub's XMLHttpRequest cannot read a
# file:// URL in this configuration at all — a harness limit, not a product
# one, and asserting through it would be testing the stub.
probe "the catalog directory is the one beside I18n.qml" \
      "{\"LANG\":\"de_DE.UTF-8\"}" \
      'I18n.catalogDir === "'"$T"'/shell/i18n"' 'true'
probe "...and SYN_I18N_DIR still overrides it" \
      "{$D,\"LANG\":\"de_DE.UTF-8\"}" \
      'I18n.catalogDir === "'"$T"'/i18n"' 'true'
# ⚠ AND THE FILENAME IS THE BARE LANGUAGE CODE, which is what po-bar/LINGUAS
# holds and what po2json.py writes.
probe "...and the catalog is <lang>.json inside it" \
      "{$D,\"LANG\":\"pt_BR.UTF-8\"}" \
      'I18n.catalogPath' "$T/i18n/pt.json"

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
probe "no catalog directory at all"    "{\"SYN_I18N_DIR\":\"$T/nothing\",\"LANG\":\"de_DE.UTF-8\"}" 'I18n.tr("Volume")' 'Volume'
# ⚠ trn must still choose a form with no catalog, from the English pair.
probe "trn with no catalog picks English" "{$D,\"LANG\":\"ko_KR.UTF-8\"}" 'I18n.trn("%1 file","%1 files",5)' '%1 files'

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ]
