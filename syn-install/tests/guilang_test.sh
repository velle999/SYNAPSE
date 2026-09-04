#!/usr/bin/env bash
# guilang_test.sh — the graphical installer's words, end to end.
#
# The window has no catalog of its own. `syn-install --strings` prints the one
# the SCRIPT loaded as `english<TAB>translation` records, syn-install-gui.sh
# dumps that to /run before quickshell starts, and syn-install-gui.qml parses it
# into the map root.t() reads. Three pieces, and every one of them fails the
# same silent way: the window opens, in English, looking exactly like a language
# with no catalog.
#
# So this drives both ends of that pipe:
#
#   1. THE DUMP — that the records are well formed, that English is EMPTY on
#      purpose, that the escaping survives a multi-line sentence, and that the
#      keys the WINDOW asks for are the keys the dump ANSWERS with.
#
#   2. THE READER — parseStrings/lookup/t/tf, lifted verbatim out of
#      syn-install-gui.qml and run under Qt's qml. Not a copy of them: the
#      functions are cut from the shipped file, and failing to find them is a
#      FAILURE here, not a skip, because a renamed function with a stale copy
#      beside it is the bug this is for.
#
# ⛔ RUN UNDER Qt's qml, NEVER quickshell — quickshell connects to Wayland at
# startup and on a developer's box that is the LIVE session. Same rule, and the
# same reason, as synui/tests/i18n_bar_runtime.sh.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
SCRIPT="$root/syn-install.sh"

pass=0 fail=0
ok()  { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  FAIL  %s\n' "$1" >&2; fail=$((fail + 1)); }
check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then ok "$1"
    else bad "$1 — expected [$2], got [$3]"; fi
}

T=$(mktemp -d /tmp/guilang.XXXXXX); trap 'rm -rf "$T"' INT TERM EXIT

# ⛔ THE CATALOGS UNDER TEST ARE THIS CHECKOUT'S. syn_lang_load looks in
# /usr/share/syn-install/lang FIRST, so on a machine with the package installed
# every one of these assertions would be about the LAST RELEASE — passing while
# the tree being committed is broken, which is the whole shape of
# "verified here, failed there".
export SYN_LANG_DIR="$T/nonexistent"

echo "the graphical installer's words"

# ── 1. the dump ───────────────────────────────────────────
dump() { bash "$SCRIPT" --strings "$1" 2>/dev/null; }

# ⚠ ENGLISH IS ZERO RECORDS, NOT AN ERROR. Every value would equal its key, and
# the window already has the English in its own source.
check "English prints no records"        0    "$(dump en_US.UTF-8 | wc -l)"
check "…and exits 0 anyway"              0    "$(dump en_US.UTF-8 >/dev/null; echo $?)"

de_n=$(dump de_DE.UTF-8 | wc -l)
[ "$de_n" -gt 300 ] && ok "German prints $de_n records" \
                    || bad "German printed $de_n records, expected the catalog"

# Every language ships the same set, so a catalog filled from a stale key list
# shows up as a short dump rather than as missing words on one page.
uneven=""
for l in de fr es pt it nl pl ru ja zh ko hi ar; do
    n=$(dump "${l}_XX.UTF-8" | wc -l)
    [ "$n" = "$de_n" ] || uneven="$uneven $l:$n"
done
check "…and so does every other language" "" "$uneven"

# ⛔ --strings IGNORES console_can_draw(). That rule is about a Linux VT with
# 512 glyphs; this is a query answered into a pipe for a window that shapes text
# and draws all thirteen. Applying it here is the 113 bug on the other side of
# the same function, and it would leave the ONE installer that can show Japanese
# showing English.
check "a VT's TERM does not silence the dump" "$de_n" \
      "$(TERM=linux dump ja_JP.UTF-8 | wc -l)"

# One record per line, exactly one tab, and no raw newline in either column —
# a third of these sentences are multi-line and the separator is a newline.
bad_records=$(dump de_DE.UTF-8 | awk -F'\t' 'NF != 2 { n++ } END { print n + 0 }')
check "every record is english<TAB>translation" 0 "$bad_records"

# The escaping, on a sentence that really is multi-line in the script.
python3 - "$SCRIPT" "$T" <<'PY' > "$T/esc" 2>&1
import subprocess, sys, os
script, tmp = sys.argv[1], sys.argv[2]
env = dict(os.environ, SYN_LANG_DIR=os.path.join(tmp, "nonexistent"))
out = subprocess.run(["bash", script, "--strings", "de_DE.UTF-8"],
                     capture_output=True, text=True, env=env).stdout
multi = [ln for ln in out.split("\n") if "\\n" in ln]
print("multiline" if multi else "none")
# …and it survives being turned back into the sentence it came from.
roundtrip = all(
    "\n" in ln.split("\t")[0].replace("\\n", "\n") or
    "\n" in ln.split("\t")[1].replace("\\n", "\n")
    for ln in multi)
print("roundtrip" if roundtrip else "broken")
PY
check "a multi-line sentence is escaped"  "multiline" "$(sed -n 1p "$T/esc")"
check "…and unescapes to a real newline"  "roundtrip" "$(sed -n 2p "$T/esc")"

# ── 2. the window asks for what the dump answers ──────────
#
# ⚠ THIS IS THE ONE THAT ROTS. Edit an English sentence in the .qml and nothing
# breaks, nothing warns: root.t() asks for a key no catalog has and prints the
# new English, in all thirteen languages, indefinitely. tools/i18n-extract.py
# already reads the window's strings, so the two lists CAN be compared — and
# the failure is named as a sentence rather than counted.
"$root/tools/gui-strings.py" > "$T/gui_keys" 2>"$T/gui_err" \
    || bad "tools/gui-strings.py failed: $(cat "$T/gui_err")"
dump de_DE.UTF-8 | cut -f1 > "$T/dumped"

python3 - "$T" <<'PY' > "$T/cover"
import sys
t = sys.argv[1]
dumped = set(open(t + "/dumped", encoding="utf-8").read().split("\n"))
keys = [k for k in open(t + "/gui_keys", encoding="utf-8").read().split("\n") if k]
missing = [k for k in keys if k.replace("\n", "\\n") not in dumped]
print(len(keys))
print(len(missing))
# Every sentence with no entry has to be one nobody translates: a product name.
# Anything else is a key that drifted, and it is printed so it can be read.
for m in missing:
    if len(m.split()) > 3 or m.endswith(('.', '?', ':')):
        print("DRIFTED:", m)
PY
n_keys=$(sed -n 1p "$T/cover"); n_missing=$(sed -n 2p "$T/cover")
[ "$n_keys" -gt 150 ] && ok "the window marks $n_keys sentences" \
                      || bad "gui-strings.py found only $n_keys sentences"
translated=$((n_keys - n_missing))
[ "$translated" -gt 150 ] && ok "…$translated of them are in the catalogs" \
                          || bad "only $translated marked sentences are translated"
drift=$(sed -n '3,$p' "$T/cover")
check "…and every untranslated one is a product name" "" "$drift"

# The launcher has to dump from the SAME binary the window will run, or a
# checkout would be translated by the installed package's catalogs.
have=$(grep -c 'SYN_INSTALL_BIN' "$root/syn-install-gui.sh")
[ "$have" -ge 1 ] && ok "the launcher dumps from the binary the window calls" \
                  || bad "syn-install-gui.sh does not honour SYN_INSTALL_BIN"

# ── 3. the reader, cut out of the shipped file ────────────
QMLBIN=/usr/lib/qt6/bin/qml
python3 - "$root/syn-install-gui.qml" > "$T/funcs.js" <<'PY'
import re, sys
src = open(sys.argv[1], encoding="utf-8").read()
out = []
for name in ("lookup", "t", "tf", "parseStrings"):
    m = re.search(r"^\s*function %s\(" % re.escape(name), src, re.M)
    if not m:
        sys.exit("missing function %s" % name)
    i = src.index("{", m.end() - 1)
    depth, j = 0, i
    while j < len(src):
        if src[j] == "{": depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0: break
        j += 1
    out.append(src[m.start():j + 1])
print("\n".join(out))
PY
if [ ! -s "$T/funcs.js" ]; then
    bad "could not lift the string functions out of syn-install-gui.qml"
elif [ ! -x "$QMLBIN" ]; then
    printf '  SKIP  the reader — Qt 6'\''s qml runtime is not installed\n'
else
    # ⛔ SUCCESS IS EXIT 7. `qml` exits ZERO on a file it could not load — it
    # prints "Did not load any objects, exiting." and returns success — so a
    # probe that read 0 as a pass reports ok for QML that never ran a line.
    # synui lost two pkgrels to exactly this, with 23 green assertions.
    probe() {  # probe <description> <expression> <expected>
        local name="$1" expr="$2" want="$3" rc
        {
            printf 'import QtQuick\nQtObject {\n    id: root\n'
            printf '    property var strings: ({})\n'
            sed 's/^/    /' "$T/funcs.js"
            printf '    Component.onCompleted: {\n'
            printf '        let got\n'
            printf '        try { got = String(%s) } catch (e) { got = "THREW: " + e }\n' "$expr"
            printf '        if (got !== %s) { console.log("      got [" + got + "]"); Qt.exit(1) }\n' "$want"
            printf '        Qt.exit(7)\n    }\n}\n'
        } > "$T/probe.qml"
        "$QMLBIN" "$T/probe.qml" >/dev/null 2>&1; rc=$?
        case "$rc" in
            7) ok "$name" ;;
            1) bad "$name" ;;
            0) bad "$name — qml loaded nothing (exit 0 is NOT a pass)" ;;
            *) bad "$name — the probe exited $rc" ;;
        esac
    }

    probe "an unknown sentence stays English" \
          'root.lookup("Nothing has an entry for this")' \
          '"Nothing has an entry for this"'
    probe "…and t() is the same lookup" \
          'root.t("Also untranslated")' '"Also untranslated"'
    probe "a record translates" \
          'root.strings = root.parseStrings("Disk\tFestplatte"), root.t("Disk")' \
          '"Festplatte"'
    probe "an escaped newline comes back as one" \
          'root.strings = root.parseStrings("a\\\\nb\tc\\\\nd"), root.t("a\nb")' \
          '"c\nd"'
    probe "an escaped tab comes back as one" \
          'root.strings = root.parseStrings("k\tx\\\\ty"), root.t("k")' \
          '"x\ty"'
    probe "a line with no tab is skipped" \
          'root.strings = root.parseStrings("junk\nDisk\tFestplatte"), root.t("Disk")' \
          '"Festplatte"'
    probe "an empty translation falls back to the English" \
          'root.strings = root.parseStrings("Disk\t"), root.t("Disk")' '"Disk"'
    probe "tf() substitutes" \
          'root.strings = root.parseStrings("%1 on LUKS2\t%1 auf LUKS2"), root.tf("%1 on LUKS2", "btrfs")' \
          '"btrfs auf LUKS2"'
    # ⚠ A TRANSLATION MAY REORDER THE PLACEHOLDERS — that is why the sentence is
    # the key and the substitution happens after the lookup, not before.
    probe "…and a translation may reorder them" \
          'root.strings = root.parseStrings("%1 keys %2 / %3\t%3 / %2 keys %1"), root.tf("%1 keys %2 / %3", "a", "b", "c")' \
          '"c / b keys a"'
    probe "tf() on an untranslated sentence still substitutes" \
          'root.tf("%1 package(s)", "12")' '"12 package(s)"'
    probe "an empty dump leaves everything English" \
          'root.strings = root.parseStrings(""), root.t("Disk")' '"Disk"'
fi

echo ""
if [ "$fail" -eq 0 ]; then
    echo "all $pass graphical-installer language checks passed"
else
    echo "$fail of $((pass + fail)) failed"
fi
exit $(( fail > 0 ))
