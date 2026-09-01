#!/usr/bin/env bash
# i18n_test.sh — the installer's translations cannot drift into being wrong.
#
# The catalogs are keyed by the ENGLISH SENTENCE (see the note above
# syn_lang_load in syn-install.sh). That buys a great deal — a missing
# translation is automatically the English, and no screen can ever show an id —
# and it costs exactly one thing: editing an English string orphans its
# thirteen translations, silently, in both directions. The new sentence has no
# entry so it prints English, and the old entry sits in every catalog matching
# nothing at all. Nothing says a word about either at runtime.
#
# So the two checks that matter are:
#
#   ORPHANS — a key matching no string in the script. That is a translation
#             that will never be seen again, and the only evidence that an
#             English string moved.
#   FORMATS — a `tf` translation whose printf conversions do not match the
#             English. printf is handed the TRANSLATION as its format, so a
#             missing %s eats an argument and an extra one reads past the end.
#             This is the check with teeth: it is the only way a translation
#             can corrupt output rather than merely be absent.
#
# Coverage is REPORTED, not required. An unfinished language is a working
# language, and failing the build over it would mean a new English string could
# not be added without thirteen translations in the same commit.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
fails=0

check() {
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

echo "syn-install translations"
echo

# ── The extractor still finds strings ─────────────────────
n=$("$root/tools/i18n-extract.py" --list | wc -l)
check "the extractor finds the translatable strings" yes \
      "$([ "$n" -gt 250 ] && echo yes || echo no)"

# ── Every catalog: no orphans, and report coverage ────────
shopt -s nullglob
cats=("$root"/lang/*.sh)
check "at least one catalog is shipped" yes \
      "$([ "${#cats[@]}" -gt 0 ] && echo yes || echo no)"

for cat in "${cats[@]}"; do
    out=$("$root/tools/i18n-extract.py" --check "$cat")
    if grep -q ORPHAN <<<"$out"; then
        printf '  FAIL  %s\n' "$out"
        fails=$((fails + 1))
    else
        printf '  ok    %s\n' "$out"
    fi
done

# ── printf conversions must survive translation ───────────
#
# Counted per conversion type rather than in total: %s and %d are not
# interchangeable, and a translation that turned one into the other would be
# counted equal by a naive total.
python3 - "$root" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
spec = root / "tools" / "i18n-extract.py"
ns = {"__name__": "extract", "__file__": str(spec)}
exec(compile(spec.read_text(encoding="utf-8"), str(spec), "exec"), ns)
keys = ns["strings"]((root / "syn-install.sh").read_text(encoding="utf-8"))

conv = re.compile(r"%(?:\d+\$)?[-+ #0]*[\d.]*([diouxXeEfgGcs%])")
def sig(s):
    return sorted(c for c in conv.findall(s) if c != "%")

bad = 0
for cat in sorted((root / "lang").glob("*.sh")):
    text = cat.read_text(encoding="utf-8")
    # One entry per ["key"]="value", value possibly spanning lines.
    for m in re.finditer(r'^\s*\["((?:[^"\\]|\\.)*)"\]="((?:[^"\\]|\\.)*)"$',
                         text, re.M | re.S):
        unesc = lambda s: (s.replace('\\"', '"').replace("\\$", "$")
                            .replace("\\`", "`").replace("\\\\", "\\"))
        k, v = unesc(m.group(1)), unesc(m.group(2))
        if sig(k) != sig(v):
            print("  FAIL  %s: conversions differ\n        EN %s -> %s\n        %s %s -> %s"
                  % (cat.name, sig(k), k[:60], cat.stem, sig(v), v[:60]))
            bad += 1
# ⚠ A TRANSLATION THAT IS ANOTHER ENGLISH STRING, WORD FOR WORD.
#
# This is what an off-by-one looks like from the outside, and it happened: a
# translator's list slipped by one line at "Target:", so sixteen consecutive
# short labels each carried the neighbouring label's text — `["not"]` came out
# as "Bootloader", `["Disk:"]` as "Installazione di:". Every count still
# matched, no conversion changed, and the screens would simply have been wrong.
#
# The rule that catches it: a translation should not BE a different English
# string. A handful legitimately are — proper nouns and technical words that do
# not change ("Bootloader", "Desktop:", "Boot:") — so it is a warning with a
# threshold rather than a hard failure on the first one.
for cat in sorted((root / "lang").glob("*.sh")):
    text = cat.read_text(encoding="utf-8")
    keyset = set(keys)
    echoes = []
    for m in re.finditer(r'^\s*\["((?:[^"\\]|\\.)*)"\]="((?:[^"\\]|\\.)*)"$',
                         text, re.M | re.S):
        unesc = lambda s: (s.replace('\\"', '"').replace("\\$", "$")
                            .replace("\\`", "`").replace("\\\\", "\\"))
        k, v = unesc(m.group(1)), unesc(m.group(2))
        if v != k and v in keyset:
            echoes.append((k, v))
    if len(echoes) > 6:
        print("  FAIL  %s: %d translations are word-for-word ANOTHER English\n"
              "        string — the shape of a list that slipped a line:"
              % (cat.name, len(echoes)))
        for k, v in echoes[:6]:
            print("          [%s] = %r" % (k[:40], v[:40]))
        bad += 1

print("  ok    printf conversions match the English in every catalog"
      if not bad else "  %d problem(s)" % bad)
sys.exit(1 if bad else 0)
PY
[ $? -eq 0 ] || fails=$((fails + 1))

# ── The mechanism itself ──────────────────────────────────
out=$(
  SYN_INSTALL_SOURCE_ONLY=1 SYN_LANG_DIR="$root/lang" bash -c '
    . '"$root"'/syn-install.sh
    syn_lang_load de;  t "Network connected"; echo
    syn_lang_load de;  t "a string no catalog will ever hold"; echo
    syn_lang_load xx;  t "Network connected"; echo
    syn_lang_load C;   t "Network connected"; echo
    syn_lang_load de;  tf "  Installing GRUB (%s)...\n" uefi
  ' 2>/dev/null
)
check "a translated string is translated"          "Netzwerk verbunden" "$(sed -n 1p <<<"$out")"
check "an untranslated string is the English"      "a string no catalog will ever hold" "$(sed -n 2p <<<"$out")"
check "an unknown language leaves it English"      "Network connected"  "$(sed -n 3p <<<"$out")"
check "C is not a language"                        "Network connected"  "$(sed -n 4p <<<"$out")"
# ⚠ The trailing newline is the point: tf reads the array directly because
# $(t …) strips it, and a format that lost its newline runs the next line on.
check "tf keeps the trailing newline"              "  GRUB wird installiert (uefi)..." "$(sed -n 5p <<<"$out")"

echo
if [ "$fails" -eq 0 ]; then echo "all translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
