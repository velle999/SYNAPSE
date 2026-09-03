#!/usr/bin/env bash
# lang_test.sh — synsh understands, and answers in, more than English.
#
# Two halves, and they fail in opposite directions:
#
#   UNDERSTANDING is asserted with `--intent-check`, which answers whether the
#   intent tables claim a line and runs nothing. It has to say yes to the same
#   request in fourteen languages, and — the half that matters more — NO to a
#   real command that happens to look like one. An intent that claims
#   `play music.wav` costs somebody their command; an intent that misses
#   "wie spät ist es" costs them a model round trip.
#
#   ANSWERING is asserted with `--lang`, which must change what the shell says
#   without changing what it does.
#
# The folding cases are the ones with the least obvious value and the most
# history: matching used to be tolower(3) byte by byte, which is ASCII-only, so
# a capitalised or accented line matched nothing at all and the whole table was
# English-only by accident rather than by decision.
#
# Run: tests/lang_test.sh [path-to-synsh]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
SYNSH=${1:-$here/../build/synsh}
# ⛔ MADE ABSOLUTE, BECAUSE THE CASES BELOW cd. A relative `./build/synsh` is
# resolved against whatever directory the test has just changed into, and every
# one of them then fails with "No such file or directory" — 53 of them at once,
# which reads as a broken shell rather than a mistyped argument.
case "$SYNSH" in /*) ;; *) SYNSH="$PWD/$SYNSH" ;; esac
[ -x "$SYNSH" ] || { echo "no synsh at $SYNSH — build it first"; exit 2; }

fails=0

claims() {  # claims <language> <line>
    if timeout 5 "$SYNSH" --intent-check "$2" >/dev/null 2>&1; then
        printf '  ok    %-6s %s\n' "$1" "$2"
    else
        printf '  FAIL  %-6s %s — not claimed by any intent\n' "$1" "$2"
        fails=$((fails + 1))
    fi
}

passes() {  # passes <why> <line> — must NOT be claimed
    if timeout 5 "$SYNSH" --intent-check "$2" >/dev/null 2>&1; then
        printf '  FAIL  %-6s %s — CLAIMED, and it is a real command\n' "$1" "$2"
        fails=$((fails + 1))
    else
        printf '  ok    %-6s %s\n' "$1" "$2"
    fi
}

check() {  # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '  ok    %s\n' "$1"
    else
        printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"
        fails=$((fails + 1))
    fi
}

echo "synsh languages — $SYNSH"
echo
echo "  what time is it, in every language it installs in"
claims en "what time is it"
claims de "wie spät ist es"
claims fr "quelle heure est-il"
claims es "¿qué hora es?"
claims pt "que horas são"
claims it "che ore sono"
claims nl "hoe laat is het"
claims pl "która godzina"
claims ru "который час"
claims ja "今何時ですか"
claims zh "现在几点"
claims ko "몇 시야"
claims hi "कितने बजे हैं"
claims ar "كم الساعة"

echo
echo "  folding: case, accents, and the spellings people actually type"
claims de "WIE SPÄT IST ES"          # tolower(3) never touched these bytes
claims de "wie spaet ist es"         # no umlaut key — ae is correct German
claims es "que hora es"              # accents left off, as everyone does
claims es "¿QUÉ HORA ES?"            # opening ¿ is two bytes, and was kept
claims en "  what   time   is   it " # runs of whitespace

echo
echo "  the everyday commands"
claims de "wo bin ich"
claims fr "espace disque"
claims ru "покажи файлы"
claims zh "我在哪"
claims pl "ile miejsca zostało"
claims nl "hoeveel geheugen"

echo
echo "  packages, including the languages that put the verb last"
claims en "install firefox"
claims de "installiere firefox"
claims es "instala firefox"
claims zh "安装 firefox"
claims ja "firefox をインストール"
claims hi "firefox इंस्टॉल करो"
claims de "ist firefox installiert"
claims ru "обнови систему"

echo
echo "  and what must NOT be claimed — every one of these is a real command"
passes sox      "play music.wav"
passes install  "install -m 644 a b"
passes find     "search /tmp -name x"
passes date     "date +%s"
passes time     "time ls"
passes ls       "ls -la"
passes pacman   "uninstall /usr/bin/x"

echo
echo "  classification: prose is prose in any script"
check "de is natural language"  "ai" "$(timeout 5 "$SYNSH" --classify 'wie viele dateien liegen hier')"
check "de keeps its capitals"   "ai" "$(timeout 5 "$SYNSH" --classify 'Wie viele Dateien liegen hier')"
check "ru is natural language"  "ai" "$(timeout 5 "$SYNSH" --classify 'покажи все файлы пожалуйста')"
check "ja has no spaces to count" "ai" "$(timeout 5 "$SYNSH" --classify '何かおすすめの設定はある')"
check "zh has no spaces to count" "ai" "$(timeout 5 "$SYNSH" --classify '现在系统怎么样')"
check "a real command is still shell" "shell" "$(timeout 5 "$SYNSH" --classify 'ls -la')"
check "an operator is still shell"    "shell" "$(timeout 5 "$SYNSH" --classify 'echo hi | wc -l')"

echo
echo "  answering: --lang changes what it says"
for pair in "de:Sprache" "fr:Langue" "es:Idioma" "pl:Język" "ru:Язык" "ja:言語" "zh:语言" "ko:언어"; do
    code=${pair%%:*}; word=${pair#*:}
    out=$(timeout 5 "$SYNSH" --lang "$code" --no-ai -c 'syn lang' 2>&1 | head -1)
    case "$out" in
        "$word"*) printf '  ok    %-3s says %s\n' "$code" "$word" ;;
        *) printf '  FAIL  %-3s — expected a line starting %s, got [%s]\n' "$code" "$word" "$out"
           fails=$((fails + 1)) ;;
    esac
done

# The environment, in the order the C library resolves messages.
check "LANG selects the language"     "fr" \
      "$(LC_ALL= LC_MESSAGES= LANG=fr_FR.UTF-8 timeout 5 "$SYNSH" --no-ai -c 'syn lang' 2>&1 | sed -n 's/.*(\(..\))$/\1/p')"
check "SYNSH_LANG beats LANG"         "ko" \
      "$(LANG=fr_FR.UTF-8 SYNSH_LANG=ko timeout 5 "$SYNSH" --no-ai -c 'syn lang' 2>&1 | sed -n 's/.*(\(..\))$/\1/p')"
check "--lang beats the environment"  "de" \
      "$(LANG=fr_FR.UTF-8 timeout 5 "$SYNSH" --lang de --no-ai -c 'syn lang' 2>&1 | sed -n 's/.*(\(..\))$/\1/p')"
check "C is not a language"           "en" \
      "$(LC_ALL=C timeout 5 "$SYNSH" --no-ai -c 'syn lang' 2>&1 | sed -n 's/.*(\(..\))$/\1/p')"
check "an unknown code falls back to English, not to nothing" "en" \
      "$(LANG=xx_XX.UTF-8 timeout 5 "$SYNSH" --no-ai -c 'syn lang' 2>&1 | sed -n 's/.*(\(..\))$/\1/p')"

# ⚠ THE LANGUAGE MUST NOT REACH THE COMMANDS. A translated `ls` is not a
# feature, it is a broken shell — this is the assertion that keeps the catalog
# on the message side of the line.
check "the language does not change what runs" "hello" \
      "$(timeout 5 "$SYNSH" --lang ja --no-ai -c 'echo hello')"
check "exit codes are not localised" "1" \
      "$(timeout 5 "$SYNSH" --lang ru --no-ai -c 'false'; echo $?)"


# ── ⛔ EVERY WORD synsh PRINTS GOES THROUGH T() ────────────────────────────
#
# The catalog was COMPLETE and this suite PASSED while thirty-eight messages
# were English in all fourteen languages — because a string that never reaches
# T() is not a missing translation, it is not a message. `syn status` translated
# its values through M_STATUS_* and printed the labels beside them in English;
# `syn ai on` printed the same two words as bare literals three lines from the
# T() that already had them.
#
# So this reads the SOURCE. Any printf/fputs literal that looks like a sentence
# and is not a T() is a message somebody forgot to declare.
#
# ⚠ i18n.c AND phrases.c ARE EXEMPT BY NAME. One holds every catalog and the
# other the intent tables — both are nothing BUT foreign-language literals.
bare=$(python3 - "$here/.." <<'PYEOF'
import re, sys, glob, os
root = sys.argv[1]

# ⛔ EACH OF THESE IS SOMETHING A PROGRAM READS OR RE-READS, NOT PROSE.
#   export NAME=…, alias n='v'   — output you can paste back in as input
#   the shell's own diagnostics whose words all arrive as %s (strerror, argv)
SKIP_SUBSTR = ("export %s", "alias %s='%s'", "pacman -", "%s: %s", "%s  Synapse:")
#
# ⚠ AND ONE PER-SITE EXEMPTION, WHICH HAS TO CARRY A REASON.
# `/* i18n-english: why */` on the line or above it holds until the next BLANK
# LINE, so a run of them — the help screen's typeable examples — takes one
# marker and not thirteen. Anything claiming the exemption says in place why the
# English is the right answer; that is the whole point of it being a comment.
bad = []
for f in sorted(glob.glob(os.path.join(root, "src", "*.c"))):
    base = os.path.basename(f)
    if base in ("i18n.c", "phrases.c"):
        continue
    exempt = False
    for n, line in enumerate(open(f, encoding="utf-8"), 1):
        if not line.strip():
            exempt = False
        if "i18n-english:" in line:
            exempt = True
        if exempt:
            continue
        if not re.search(r"\b(printf|fprintf|fputs|puts|dprintf)\s*\(", line):
            continue
        stripped = re.sub(r"T\(M_[A-Z_0-9]+\)", "X", line)
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', stripped):
            if any(k in lit for k in SKIP_SUBSTR):
                continue
            # Two words of lower-case prose is a sentence; "%s\n" is not.
            if re.search(r"[A-Za-z]{2,}\s+[a-z]{2,}", lit):
                bad.append("%s:%d: %s" % (base, n, lit[:48]))
print("\n".join(bad))
PYEOF
)
check "every word synsh prints goes through T()" "" "$(printf '%s' "$bare" | tr '\n' ' ')"

# ── ⛔ AND EVERY CATALOG HAS EVERY SLOT ────────────────────────────────────
#
# A missing designated initialiser is a NULL in the array, and synsh_msg()
# answers the English text for it — which is exactly right at runtime and
# invisible to every other check here, because the shell still works and still
# says something. The only way to see a slot nobody filled is to count them.
gaps=$(python3 - "$here/.." <<'PYEOF'
import re, sys, os
root = sys.argv[1]
ids = re.findall(r"X\((M_[A-Z_0-9]+),", open(os.path.join(root, "include", "i18n.h")).read())
want = set(ids)
src = open(os.path.join(root, "src", "i18n.c")).read()
out = []
for lang in ("DE FR ES PT IT NL PL RU JA ZH KO HI AR").split():
    m = re.search(r"static const char \*const MSG_%s\[M_COUNT\] = \{(.*?)\n\};" % lang,
                  src, re.S)
    if not m:
        out.append("%s(no catalog)" % lang); continue
    have = set(re.findall(r"\[(M_[A-Z_0-9]+)\s*\]\s*=", m.group(1)))
    missing = want - have
    if missing:
        out.append("%s(%d missing: %s)" % (lang, len(missing), ",".join(sorted(missing)[:3])))
print(" ".join(out))
PYEOF
)
check "every catalog fills every message slot" "" "$gaps"

# ── ⛔ AND THE BANNER BOX IS THE SAME WIDTH IN ALL FOURTEEN ────────────────
#
# The tagline row padded to a hard-coded 25 — the length of the English words —
# so the right-hand │ landed wherever the translation happened to end. It is
# measured now, in COLUMNS: "カーネルが考える場所" is 30 bytes, 10 code points
# and 20 columns, and only the last of those three draws a box that closes.
#
# ⚠ THE ASSERTION IS THAT EVERY ROW MATCHES, not that any row is 41 wide. A
# width constant here would be a second place to change.
box=$(for l in en de fr es pt it nl pl ru ja zh ko hi ar; do
    printf '' | timeout 5 "$SYNSH" --lang "$l" -i 2>/dev/null |
    python3 -c '
import sys, unicodedata
def cols(s):
    w = 0
    for ch in s:
        if unicodedata.category(ch) in ("Mn", "Me"): continue
        w += 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
    return w
rows = [r for r in sys.stdin.read().split("\n")
        if r.startswith("  \u2502") or r.startswith("  \u256d") or r.startswith("  \u2570")]
widths = {cols(r) for r in rows}
if len(rows) < 4 or len(widths) != 1:
    print("'"$l"'(" + ",".join(str(w) for w in sorted(widths)) + ")", end="")
'
done)
check "the banner box closes in every language" "" "$box"

echo
if [ "$fails" -eq 0 ]; then echo "all language checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
