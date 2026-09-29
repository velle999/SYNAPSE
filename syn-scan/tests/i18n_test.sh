#!/usr/bin/env bash
# i18n_test.sh — syn-scan's words, reachable by a translator, and its records
# untouched.
#
# ⛔ 1. THE RECORD PROTOCOL MUST NEVER BE TRANSLATED. `--rec` emits header rows
#    naming the columns — `engine verdict path detail when`, `id name present
#    runnable path`, `id origin engine detail when` — and data/syn-scan.qml
#    keys off those names.
#
#    ⛔ AND THE `verdict` COLUMN IS MATCHED, NOT READ. The window colours a row
#    red by comparing it against `infected`, and counts what needs a look by
#    `infected` and `suspect`. Every verdict carries an id AND a label one
#    function apart — verdict_id() and verdict_label() — and only the label is
#    marked. A translated verdict is a window that draws an infected file as
#    clean.
#
# ⚠ 2. THE HUMAN PATH MUST BE TRANSLATED, and it is where somebody ends up when
#    the window will not open.
#
# ⛔ 3. AND EVERY CATALOG IS FULL. syn-scan shipped six releases with all
#    thirteen catalogs empty: every file compiled, every check passed, and
#    every word was English. A string added without its thirteen
#    translations fails here, not in front of somebody who reads German.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. The gettext tools are
# translated too, and LANGUAGE is read before LC_ALL — so it is UNSET, or an
# ambient LANGUAGE=ja answers Japanese to the German runs below.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/build/syn-scan}
fails=0
check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "syn-scan translations"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/syn-scan-i18n.XXXXXX"); trap 'rm -rf "$tmp"' EXIT

# ── 1. the template is current, and nothing was mangled making it ──────────
err=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
# ⚠ THE SET, NOT THE COUNT. A changed label keeps the count and moves the msgid.
msgids() { msgcat --no-wrap --sort-output "$1" 2>/dev/null | grep -E '^msgid(_plural)? ' | md5sum; }
if [ -f "$tmp/syn-scan.pot" ]; then
    check "po/syn-scan.pot is current" "$(msgids "$tmp/syn-scan.pot")" \
          "$(msgids "$root/po/syn-scan.pot")"
fi

# ⛔ AND THE NON-ASCII SURVIVED. Half this program's messages carry an em dash;
# a msgid that lost it never matches the source string at runtime.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^"?.*[\x80-\xff]' "$root/po/syn-scan.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

# ── 2. every catalog is FULL, and declares its language's plural forms ─────
#
# ⚠ msgfmt --statistics, not a grep for `msgstr ""`: a long translation is
# written as `msgstr ""` plus continuation lines, and a grep calls it missing.
plurals() {
    case $1 in ja|ko|zh) echo 1 ;; pl|ru) echo 3 ;; ar) echo 6 ;; *) echo 2 ;; esac
}
short=""; wrongpl=""
while IFS= read -r l; do
    po="$root/po/$l.po"
    [ -f "$po" ] || continue
    stats=$(msgfmt --statistics -o /dev/null "$po" 2>&1)
    case $stats in *untranslated*|*fuzzy*) short="$short $l(${stats%%.*})" ;; esac
    n=$(grep -o 'nplurals=[0-9]*' "$po" | head -1 | cut -d= -f2)
    [ "$n" = "$(plurals "$l")" ] || wrongpl="$wrongpl $l(nplurals=$n)"
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every catalog translates every msgid" "" "$short"
check "every catalog declares its language's number of plural forms" "" "$wrongpl"

# ── 3. ⛔ EVERY RECORD IS BYTE-IDENTICAL IN EVERY LANGUAGE ─────────────────
#
# ⚠ RUN, not grepped. A `_()` where a record's own literal belongs is invisible
# to any amount of reading; it shows up the moment the program is asked the
# same question in two languages.
#
# ⛔ STUB ENGINES AND A SCRATCH STATE DIR, for the reasons tests/scan_test.sh
# gives: PATH holds only the stubs, the clamd socket cannot exist, and the
# signature database is a fixture. Nothing here reaches a real engine, a real
# quarantine or the machine running it.
STUBS=$tmp/stubs; SCANME=$tmp/scanme; DB=$tmp/clamav-db
mkdir -p "$STUBS" "$SCANME" "$DB"
printf 'stub\n' > "$DB/main.cvd"
printf 'harmless\n' > "$SCANME/clean.txt"
printf 'not really\n' > "$SCANME/eicar.com"
cat > "$STUBS/clamscan" <<STUB
#!/bin/sh
echo "$SCANME/eicar.com: Win.Test.EICAR_HDB-1 FOUND"
echo "$SCANME/clean.txt: OK"
echo "$SCANME/big.zip: Heuristics.Limits.Exceeded ERROR"
exit 1
STUB
# ⚠ A "Warning:" is a finding and anything else is the engine's own trouble —
# so both kinds of row, suspect and incomplete, are in the records compared.
cat > "$STUBS/rkhunter" <<'STUB'
#!/bin/sh
echo "Warning: Hidden directory found: /dev/.udev"
echo "Unable to write to the log file"
exit 1
STUB
chmod +x "$STUBS"/*

export SYNSCAN_CLAMD_SOCKET=$tmp/no-such-clamd.sock
export SYNSCAN_CLAMAV_DBDIR=$DB

# The saved state `status` and `quarantine list` read — written ONCE, in C, so
# that every run below reads the same thing.
STATE=$tmp/state
SYNSCAN_HOME=$STATE PATH=$STUBS "$BIN" --rec scan "$SCANME" >/dev/null 2>&1
SYNSCAN_HOME=$STATE PATH=$STUBS "$BIN" --rec scan --system >/dev/null 2>&1
printf 'kept aside\n' > "$tmp/aside.txt"
SYNSCAN_HOME=$STATE PATH=$STUBS "$BIN" quarantine take "$tmp/aside.txt" >/dev/null 2>&1

# ⚠ A scan writes its findings and stamps each row with the clock, so every
# scan gets a fresh state dir and the `when` column is cut off before the diff
# — two runs a second apart are not two languages disagreeing.
run() {   # run <localedir> <locale> <args...>
    local dir=$1 loc=$2; shift 2
    local home=$STATE
    case " $* " in *" scan "*) home=$(mktemp -d "$tmp/scanhome.XXXXXX") ;; esac
    SYNSCAN_HOME=$home SYN_SCAN_LOCALEDIR=$dir LOCPATH=${LOCPATH_DE:-} LC_ALL=$loc \
        PATH=$STUBS "$BIN" "$@" 2>/dev/null
}
REC_CMDS="--rec engines
--rec scan $SCANME
--rec scan --system
--rec status
--rec status --weekly
--rec quarantine list"

drift() {   # drift <localedir-under-test> — every --rec command, C vs de_DE
    printf '%s\n' "$REC_CMDS" | while IFS= read -r cmd; do
        [ -n "$cmd" ] || continue
        # shellcheck disable=SC2086
        a=$(run "$mo" C.UTF-8 $cmd | cut -f1-5 | md5sum)
        # shellcheck disable=SC2086
        b=$(run "$1" de_DE.UTF-8 $cmd | cut -f1-5 | md5sum)
        [ "$a" = "$b" ] || printf '[%s]' "$cmd"
    done
}

if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    # ⛔ AND THE LOCALE HAS TO EXIST. LANGUAGE is vetoed under C, so a box with
    # no generated locales silently tests nothing. localedef into a scratch
    # LOCPATH — never locale-gen, which is root and system-wide.
    export LOCPATH_DE=$tmp/loc; mkdir -p "$LOCPATH_DE"
    # ⛔ AND THE BINARY HAS TO FIND A CATALOG. Its compiled-in localedir is
    # under the install prefix: an uninstalled syn-scan loads nothing — or the
    # INSTALLED catalog — and a _() sitting in a record passes unseen.
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/syn-scan.mo" "$root/po/de.po" 2>/dev/null

    if ! localedef -i de_DE -f UTF-8 -c "$LOCPATH_DE/de_DE.UTF-8" 2>/dev/null; then
        printf '  skip  localedef could not build de_DE (nothing asserted)\n'
    else
        # ── 3a. against the REAL German catalog ───────────────────────────
        check "every --rec command answers the same in German as in C" "" "$(drift "$mo")"

        # ...and the human path does not, or nothing is being translated.
        h1=$(run "$mo" C.UTF-8 status | md5sum)
        h2=$(run "$mo" de_DE.UTF-8 status | md5sum)
        check "...while the human path IS German" "differs" \
              "$([ "$h1" = "$h2" ] && echo same || echo differs)"

        # ⚠ AND THE ROWS STILL LINE UP. The verdict column was `%-11s`, which
        # pads by BYTES: "Nicht lesbar" overflowed it and 感染 (6 bytes, 4
        # columns) was padded short, so the paths started in different columns
        # row to row. Measured here in display columns, in German and Japanese.
        for l in ja; do
            mkdir -p "$mo/$l/LC_MESSAGES"
            msgfmt -o "$mo/$l/LC_MESSAGES/syn-scan.mo" "$root/po/$l.po" 2>/dev/null
        done
        localedef -i ja_JP -f UTF-8 -c "$LOCPATH_DE/ja_JP.UTF-8" 2>/dev/null
        misaligned=""
        for loc in de_DE ja_JP; do
            cols=$(run "$mo" "$loc.UTF-8" scan "$SCANME" | grep -F "$SCANME/" |
                   python3 -c '
import sys, unicodedata
root = sys.argv[1]
w = lambda s: sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in s)
print(" ".join(sorted({str(w(l.split(root)[0])) for l in sys.stdin})))
' "$SCANME/")
            case $cols in *" "*|"") misaligned="$misaligned $loc($cols)" ;; esac
        done
        check "every finding's path starts in the same column (de, ja)" "" "$misaligned"

        # ── 3b. ⛔ AND AGAINST A CATALOG THAT TRANSLATES EVERYTHING ───────
        #
        # A catalog built from the TEMPLATE with every msgstr marked, so a
        # record that changes has a string reaching gettext whether or not
        # de.po's translation of it happens to differ from the English. Five
        # things each failed silently the first time, in syn-clean:
        #   · msgfilter READS A FILE (`-i -` writes an empty catalog);
        #   · the marker is a SUFFIX, on non-empty lines only — a prefix moves
        #     a leading "\n" and msgfmt drops the entry;
        #   · the header is repaired afterwards, or charset=UTF-8⟧ loads nothing;
        #   · it is addressed as de_DE — LANGUAGE is ignored under C.
        hos=$tmp/hos; mkdir -p "$hos/de/LC_MESSAGES"
        if msgen "$root/po/syn-scan.pot" -o "$tmp/ident.po" 2>/dev/null &&
           msgfilter -i "$tmp/ident.po" -o "$tmp/hostile.raw" \
                     sed -e '/./s/$/⟧/' 2>/dev/null &&
           sed 's/^\("[A-Za-z-]*: .*\)⟧\(\\n"\)$/\1\2/' \
               "$tmp/hostile.raw" > "$tmp/hostile.po" &&
           msgfmt -o "$hos/de/LC_MESSAGES/syn-scan.mo" "$tmp/hostile.po" 2>/dev/null
        then
            check "every --rec command survives a catalog that translates EVERYTHING" \
                  "" "$(drift "$hos")"

            # ...and that catalog WAS reached, or the check above proved nothing.
            g=$(run "$hos" de_DE.UTF-8 status | grep -c '⟧')
            check "...and that catalog WAS reached (the human path is marked)" \
                  "yes" "$([ "${g:-0}" -gt 0 ] && echo yes || echo no)"

            # ⛔ AND THE RECORDS STILL HAD ROWS. A change that made them print
            # nothing would leave two empty outputs comparing equal.
            rows=$(run "$hos" de_DE.UTF-8 --rec scan "$SCANME" | grep -c '^finding')
            # (the stub's OK line is not a finding: infected and unreadable only)
            check "...and --rec scan still emitted its two finding rows" "2" "$rows"

            # ⛔ AND THE VERDICT COLUMN IS STILL THE WORD THE WINDOW MATCHES.
            v=$( { run "$hos" de_DE.UTF-8 --rec scan "$SCANME"
                   run "$hos" de_DE.UTF-8 --rec scan --system; } |
                 grep '^finding' | cut -f3 | sort -u | tr '\n' ' ')
            check "...and every verdict is still an English id" \
                  "error incomplete infected suspect " "$v"
        else
            printf '  skip  msgen/msgfilter unavailable (nothing asserted)\n'
        fi
    fi
else
    printf '  skip  no binary or no localedef (nothing asserted)\n'
fi

# ── 4. ⛔ THE WINDOW'S HALF ────────────────────────────────────────────────
#
# A .qml or .js missing from po/POTFILES still compiles and still looks up at
# runtime, so nothing warns — its strings never reach a template.
missing=""
while IFS= read -r q; do
    rel=${q#"$root/data/"}
    grep -qxF "$rel" "$root/po/POTFILES" || missing="$missing $rel"
done < <(grep -rlE 'I18n\.trn?\(' "$root/data" --include='*.qml' --include='*.js')
check "every file that calls I18n.tr() is listed in po/POTFILES" "" "$missing"

# ⚠ A LOOKUP OF A NON-LITERAL IS ENGLISH FOREVER unless its msgids come from
# somewhere the template reads. The one allowed today is the engine name,
# marked N_() in src/engine.c — so the count is pinned, and every name in that
# table must be a msgid.
dyn=$(grep -c 'i18n-dynamic:' "$root/data/syn-scan.qml")
check "exactly one dynamic lookup in the window (the engine name)" "1" "$dyn"
names=$(grep -oE '^\s*\{ "[a-z]+",\s+N_\("[^"]+"\)' "$root/src/engine.c" |
        sed -E 's/.*N_\("([^"]+)"\)/\1/')
absent=""
while IFS= read -r n; do
    [ -n "$n" ] || continue
    grep -qxF "msgid \"$n\"" "$root/po/syn-scan.pot" || absent="$absent [$n]"
done <<< "$names"
check "every engine name the window looks up is a msgid" "" "$absent"

# ⛔ AND THE SINGLETON IS THE SHARED ONE, BYTE FOR BYTE.
shared=$(ls "$root"/../syn-edit/data/qml/I18n.qml 2>/dev/null)
if [ -n "$shared" ]; then
    check "data/qml/I18n.qml is the shared copy, unedited" \
          "$(md5sum < "$shared")" "$(md5sum < "$root/data/qml/I18n.qml")"
else
    printf '  skip  no sibling checkout to compare I18n.qml against\n'
fi

# ── 5. the catalogs compile, to both shapes ───────────────────────────────
#
# ⚠ msgfmt -c catches a translation that reordered %s without %1$s, which at
# runtime is a crash, not a wrong word.
bad=""
while IFS= read -r l; do
    po="$root/po/$l.po"
    [ -f "$po" ] || { bad="$bad $l(missing)"; continue; }
    msgfmt -c -o /dev/null "$po" 2>/dev/null || bad="$bad $l(msgfmt)"
    "$root/tools/po2json.py" "$po" -o "$tmp/$l.json" >/dev/null 2>&1 \
        || bad="$bad $l(po2json)"
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every catalog compiles to both a .mo and a JSON" "" "$bad"

# ── ⛔ AND THE OTHER SUITES PIN THE LOCALE THEY ASSERT IN ─────────────────
#
# They assert English against a binary that answers in the desktop's language
# once syn-scan is installed — so dropping the pin breaks check() on every
# translated desktop that builds syn-scan, and on no English one.
pin=""
for suite in scan_test.sh qml_test.sh; do
    s="$root/tests/$suite"
    [ -f "$s" ] || continue
    grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$s" || pin="$pin $suite(LC_ALL)"
    grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$s" || pin="$pin $suite(LANGUAGE)"
done
check "the other suites pin the locale they assert in" "" "$pin"

echo
if [ "$fails" -eq 0 ]; then echo "all syn-scan translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
