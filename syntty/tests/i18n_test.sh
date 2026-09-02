#!/usr/bin/env bash
# i18n_test.sh — syntty's words, reachable by a translator, and its diagnostics
# left alone.
#
# syntty has ONE front-end, and it draws its own pixels — there is no QML window
# and so no JSON half to this catalog. What there is instead is a hard line down
# the middle of the program:
#
# ⛔ 1. EVERY SUBCOMMAND'S STDOUT IS PROTOCOL. `dump`, `about`, `win --stats`,
#    `render`, `fit`, `mouse`, `key`, `paste` and `config` exist so that a
#    terminal drawing in pixels can be asked questions from a shell, and
#    tests/syntty_test.sh parses the answers — column labels included:
#
#        sz=$("$ST" about | awk '/^  cell/{print $2}')
#
#    A translated label is a suite that fails in German and passes in English,
#    which is the worst possible way to find out. Check 3 tracks brace depth
#    through every one of those writers and fails on a `_()` inside.
#
# ⛔ 1b. AND SO IS `config --example`. It is a CONFIG FILE, written to disk and
#    read back by the parser; its comments are prose, but a translator who
#    reflows one line of `# font = monospace` hands somebody a file that no
#    longer shows the setting it documents. It stays English, as usage() does.
#
# ⚠ 2. WHAT GOES TO stderr IS FOR A PERSON. die(), warn(), and the handful of
#    fprintf(stderr, "syntty: …") warnings are the whole translatable surface,
#    and it is the surface somebody hits on the worst day they have with this
#    program — a font that will not open, a compositor missing a global, a
#    config file with a typo in it.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. Everything below parses
# tool output, and the gettext tools are themselves translated: on a Japanese
# desktop xgettext writes `警告:`, a `grep -v 'warning:'` filter matches
# nothing, and "po/pot.sh runs clean" fails on a warning already judged fine.
# ⚠ LANGUAGE is UNSET, not set — gettext reads it BEFORE LC_ALL, so an ambient
# LANGUAGE=ja would answer Japanese to the German runs below, and check 2 would
# compare Japanese with Japanese and pass with nothing translated. The
# deliberate foreign-locale runs set LC_ALL per command and still win.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/build/syntty}
fails=0
check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "syntty translations"

# ── 1. the template is current, and nothing was mangled making it ──────────
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
err=$("$root/po/pot.sh" "$root" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
if [ -f "$tmp/syntty.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/syntty.pot" 2>/dev/null)
    now=$(grep -c '^msgid "' "$tmp/syntty.pot" 2>/dev/null)
    check "po/syntty.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted,
# with no warning about the loss — and syntty's messages are full of them: the
# em dashes and the `→` in its die() text. A msgid that lost a character never
# matches the source string, so it is permanently English however well
# translated. pot.sh refuses --omit-header now; this asserts the RESULT.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/syntty.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

# ── 2. ⛔ THE DIAGNOSTIC SUBCOMMANDS ANSWER THE SAME IN EVERY LANGUAGE ─────
#
# ⚠ RUN, not grepped. A `_()` reached only from one branch is invisible to any
# amount of reading; it shows up the moment the program is asked the same
# question in two languages.
#
# ⛔ AND THE LOCALE HAS TO EXIST. LANGUAGE is vetoed under C, so a box with no
# generated locales silently tests nothing. localedef into a scratch LOCPATH —
# never locale-gen, which is root and system-wide.
if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    # ⛔ AND THE BINARY HAS TO BE ABLE TO FIND A CATALOG. Its compiled-in
    # localedir is under the install prefix, so an UNINSTALLED syntty loads
    # nothing and answers English in every language — which is how the first
    # version of a check like this passed with a _() sitting in a stats row.
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/syntty.mo" "$root/po/de.po" 2>/dev/null
    if localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        drift=""
        # ⚠ EVERY DIAGNOSTIC SUBCOMMAND THAT RUNS WITHOUT A COMPOSITOR. The
        # runtime check is the only one that can tell a printf from its
        # neighbour, so it has to be the broad one.
        printf 'hello\033[31m world\n' > "$tmp/in"
        run() { SYNTTY_LOCALEDIR=$mo ${2:+LOCPATH=$loc} LC_ALL=$1 \
                $BIN $3 < "$tmp/in" 2>/dev/null | md5sum; }
        while IFS= read -r cmd; do
            a=$(run C.UTF-8 "" "$cmd")
            b=$(run de_DE.UTF-8 1 "$cmd")
            [ "$a" = "$b" ] || drift="$drift [$cmd]"
        done <<'CMDS'
about
dump -
dump --stats -
fit 1600x900 --cell=8x16
mouse press:left@10,5
key ctrl+shift+left
paste hello
config --example
CMDS
        check "every diagnostic subcommand answers the same in German as in C" "" "$drift"

        # ...and the HUMAN path does, or nothing is being translated at all.
        # ⚠ Only once a catalog has something in it: before that this is a skip,
        # not a failure, because "not translated yet" is a legitimate state.
        # ⚠ stderr, and stderr ONLY — this is the whole point of the split. The
        # command is a die(): two texts handed to `paste`, which is refused.
        if msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF 'msgid "paste: one text at a time"'; then
            h1=$(SYNTTY_LOCALEDIR=$mo LC_ALL=C.UTF-8 \
                 $BIN paste a b 2>&1 >/dev/null | md5sum)
            h2=$(SYNTTY_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN paste a b 2>&1 >/dev/null | md5sum)
            check "...while what a person reads DOES change" "differs" \
                  "$([ "$h1" = "$h2" ] && echo same || echo differs)"
        else
            printf '  skip  the German catalog is not filled yet\n'
        fi
    else
        printf '  skip  localedef could not build de_DE (nothing asserted)\n'
    fi
else
    printf '  skip  no binary or no localedef (nothing asserted)\n'
fi

# ── 3. ⛔ A TRANSLATED STRING REACHES stderr, NEVER A printf ──────────────
#
# The static half of check 2, and worth having both: check 2 cannot reach a
# printf behind a flag no test passes, and this one cannot tell a translated
# string from a helper called by two paths. Between them the line holds.
#
# ⚠ THE RULE IS ENFORCEABLE BECAUSE IT IS A RULE ABOUT TWO FUNCTIONS, not about
# intent. die() and warn() are what a person reads; printf, fprintf, puts and
# fputs are what something PARSES — print_stats() writes to stderr too, and
# tests/syntty_test.sh greps it. "Only the fprintf calls that are for a person"
# is not checkable by anything, which is why warn() exists at all.
#
# ⚠ awk over accumulated STATEMENTS, not lines: every one of these calls is
# wrapped across three or four lines, and a per-line grep sees the `_()` on a
# continuation line with no writer name in sight. String literals are stripped
# before the parens are counted — half these messages contain "(%zu bytes)".
badwriter=$(awk '
    function strip(s) {
        gsub(/\\\\/, "", s); gsub(/\\"/, "", s)     # escaped backslash, escaped quote
        gsub(/"[^"]*"/, "", s); gsub(/\x27[^\x27]*\x27/, "", s)
        return s
    }
    # ⚠ COMMENTS FIRST. This very file explains the rule in a comment that
    # names both `fprintf(stderr,` and `_()`, and without this the check
    # reports itself.
    /^[ \t]*(\/\*|\*|\/\/)/ { next }
    !acc && /[^A-Za-z0-9_](printf|fprintf|puts|fputs|fwrite)[ ]*\(/ {
        acc = 1; depth = 0; buf = ""; start = FNR
    }
    acc {
        buf = buf $0
        s = strip($0)
        depth += gsub(/\(/, "(", s) - gsub(/\)/, ")", s)
        if (depth <= 0) {
            if (buf ~ /[^A-Za-z0-9_]_\(/ || buf ~ /[^A-Za-z0-9_]P_\(/)
                print FILENAME ":" start
            acc = 0
        }
    }' "$root"/src/*.c | tr '\n' ' ')
check "no translated string inside a printf — only die() and warn()" "" "$badwriter"

# ⛔ AND NOTHING MARKED IN THE USAGE TEXT. It is one 60-line string constant
# naming every flag and key binding syntty has; the flags cannot be translated
# and a half-translated one is worse than none. It is not marked, and this is
# what keeps somebody from marking it in a tidy-up.
badusage=$(grep -n 'usage_text\|usage_opts' "$root"/src/*.c | grep '_(' | tr '\n' ' ')
check "the usage text is not marked" "" "$badusage"

# ── 4. the catalogs still compile ─────────────────────────────────────────
#
# ⚠ msgfmt -c, which is what catches a translation that reordered %s without
# saying %1$s — five catalogs failed exactly that way in another component, and
# the failure at runtime is a crash, not a wrong word.
bad=""
while IFS= read -r l; do
    po="$root/po/$l.po"
    [ -f "$po" ] || { bad="$bad $l(missing)"; continue; }
    msgfmt -c -o /dev/null "$po" 2>/dev/null || bad="$bad $l(msgfmt)"
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every catalog compiles" "" "$bad"

# ── ⛔ AND THE MAIN SUITE PINS THE LOCALE IT ASSERTS IN ───────────────────
#
# tests/syntty_test.sh asserts English against a binary that answers the
# desktop's language once syntty is installed. The fix is one exported LC_ALL
# and one unset LANGUAGE; nothing else stops them being dropped, and dropping
# them breaks the build on every translated desktop and no English one.
suite="$root/tests/syntty_test.sh"
pin=""
grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$suite" || pin="$pin LC_ALL"
grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$suite" || pin="$pin LANGUAGE"
check "the main suite pins the locale it asserts in" "" "$pin"

echo
if [ "$fails" -eq 0 ]; then echo "all syntty translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
