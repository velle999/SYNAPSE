#!/usr/bin/env bash
# i18n_test.sh — syn-vault's words, reachable by a translator, and its records
# untouched.
#
# ⛔ 1. THE RECORD PROTOCOL MUST NEVER BE TRANSLATED. `--rec` emits a header row
#    naming the columns — `name`, `state`, `mount`, `stray` — and
#    data/syn-vault.qml keys off those names. It matches on a VALUE too:
#    `state === "open"` decides, in seven places, whether a row offers Lock or
#    Unlock, whether the mount path is shown, and what colour the row is drawn.
#
#    ⚠ AND `open`/`locked` ARE DRAWN AS WELL AS MATCHED. That is the trap this
#    component had: `syn-vault status foo` says "'foo' is open." to a person and
#    `--rec` puts `open` in a column a program reads. They are two separate
#    strings in the source, and only one of them is marked.
#
#    ⚠ THIS PROGRAM GUARDS SOMEBODY'S ENCRYPTED FILES. A record that changed
#    shape in one language is a window that believes a locked vault is mounted,
#    on the screen where the alternative is writing plaintext to the disk.
#
# ⚠ 2. THE HUMAN PATH MUST BE TRANSLATED, and it is where somebody ends up when
#    the window will not open.
#
# ⛔ 3. ONE CATALOG SERVES BOTH. po/*.po is compiled to JSON for the window and
#    to a .mo for the binary. ⚠ The .mo is named after the DOMAIN —
#    syn-vault.mo — which is why the build uses meson's i18n module rather than
#    a custom_target: a loop can only name its output de.mo, which installs to
#    the right directory under a name libintl never looks for.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. Everything below parses
# tool output, and the gettext tools are themselves translated: on a Japanese
# desktop xgettext writes `警告:` and a filter matching the English word finds
# nothing.
# ⚠ LANGUAGE is UNSET, not set — gettext reads it before LC_ALL, so an ambient
# LANGUAGE=ja would answer Japanese to the German runs below and the assertion
# that the human path IS translated would compare Japanese with Japanese.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/build/syn-vault}
fails=0
check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "syn-vault translations"

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# ⛔ A SCRATCH HOME WITH TWO FAKE VAULTS, AND NO gocryptfs INVOLVED.
#
# vault_exists() is "is there a gocryptfs.conf in the cipher directory", so a
# vault this suite can list is a directory and an empty file — no FUSE, no
# mount, no /dev/fuse, and nothing that could touch a real vault. The record
# commands then produce the SAME ROWS in both locales, which is what makes a
# byte diff mean anything; run against a real home they would differ because
# somebody opened a vault between the two runs.
#
# ⚠ TWO OF THEM, AND ONE WITH A STRAY FILE. `stray` is the column that says a
# closed vault's mountpoint has unencrypted files in it — the one condition
# this program exists to warn about — so a fixture without it leaves that whole
# branch, and the sentence it prints, unexercised.
H=$tmp/home
mkdir -p "$H/.local/share/syn-vault/alpha.vault" \
         "$H/.local/share/syn-vault/beta.vault" \
         "$H/Vaults/alpha" "$H/Vaults/beta"
: > "$H/.local/share/syn-vault/alpha.vault/gocryptfs.conf"
: > "$H/.local/share/syn-vault/beta.vault/gocryptfs.conf"
: > "$H/Vaults/beta/left-behind.txt"
export HOME="$H"

# ── 1. the template is current, and nothing was mangled making it ──────────
err=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
if [ -f "$tmp/syn-vault.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/syn-vault.pot" 2>/dev/null)
    now=$(grep -c '^msgid "' "$tmp/syn-vault.pot" 2>/dev/null)
    check "po/syn-vault.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted,
# with no warning about the loss — and this program's messages are full of em
# dashes and carry a ⚠ in the two that matter most. A msgid that lost a
# character never matches the source string, so it is permanently English
# however well translated. pot.sh refuses that flag; this asserts the RESULT.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/syn-vault.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

# ── 2. ⛔ EVERY RECORD IS BYTE-IDENTICAL IN EVERY LANGUAGE ─────────────────
#
# ⚠ RUN, not grepped. A `_()` where the record's own literal belongs is
# invisible to any amount of reading; it shows up the moment the program is
# asked the same question in two languages.
#
# ⛔ AND THE LOCALE HAS TO EXIST. LANGUAGE is vetoed under C, so a box with no
# generated locales silently tests nothing. localedef into a scratch LOCPATH —
# never locale-gen, which is root and system-wide.
#
# ⚠ EVERY --rec COMMAND THAT NEEDS NO gocryptfs AND NO MOUNT. `create`, `open`
# and `close` are absent on purpose: each forks the encrypting backend, and a
# test that ran them twice to compare two records would be a test that makes
# vaults on the machine running it.
REC_CMDS="--rec list
--rec status alpha
--rec status beta
--rec status nosuchvault"

if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    # ⛔ AND THE BINARY HAS TO BE ABLE TO FIND A CATALOG. Its compiled-in
    # localedir is under the install prefix, so an uninstalled syn-vault loads
    # nothing and answers English in every language — which is how the first
    # version of a check like this passes with a _() sitting in a record.
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/syn-vault.mo" "$root/po/de.po" 2>/dev/null

    if ! localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        printf '  skip  localedef could not build de_DE (nothing asserted)\n'
    else
        # ── 2a. against the REAL German catalog ───────────────────────────
        printf '%s\n' "$REC_CMDS" | while IFS= read -r cmd; do
            [ -n "$cmd" ] || continue
            a=$(SYN_VAULT_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN $cmd 2>/dev/null | md5sum)
            b=$(SYN_VAULT_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                $BIN $cmd 2>/dev/null | md5sum)
            [ "$a" = "$b" ] || printf '[%s]' "$cmd"
        done > "$tmp/drift.a"
        check "every --rec command answers the same in German as in C" "" \
              "$(cat "$tmp/drift.a")"

        # ...and the HUMAN path does, or nothing is being translated at all.
        # ⚠ Only once a catalog has something in it: before that this is a skip,
        # not a failure, because "not translated yet" is a legitimate state.
        if msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF "msgid \"'%s' is locked."; then
            h1=$(SYN_VAULT_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN list 2>&1 | md5sum)
            h2=$(SYN_VAULT_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN list 2>&1 | md5sum)
            check "...while the human path DOES change" "differs" \
                  "$([ "$h1" = "$h2" ] && echo same || echo differs)"
        else
            printf '  skip  the German catalog is not filled yet\n'
        fi

        # ── 2b. ⛔ AND AGAINST A CATALOG THAT TRANSLATES EVERYTHING ───────
        #
        # The strongest version of 2a, and it does not depend on anybody having
        # translated anything: a catalog built from the TEMPLATE with every
        # msgstr marked. A record that changes has a string in it reaching
        # gettext, whether or not de.po happens to carry that entry today.
        #
        # ⚠ THIS IS WHAT CATCHES A MARKED `open`. The word is in the catalog on
        # purpose — the CLI's sentence needs it — so a `_()` that slipped onto
        # the RECORD's copy is invisible to 2a until somebody translates the
        # entry, and then it is a window that reads every vault as locked.
        #
        # ⚠ FIVE THINGS HAD TO BE RIGHT HERE AND EACH FAILED SILENTLY when this
        # was first written for a sibling component:
        #
        #   · msgfilter READS A FILE. `-i -` writes an empty catalog and says
        #     nothing, and an empty one turns this into a skip.
        #   · THE MARKER IS A SUFFIX ONLY. A prefix moves the leading "\n" of
        #     every msgid that starts with one and msgfmt drops the entry —
        #     leaving untranslated exactly the strings this exists to catch.
        #   · AND NEVER ON AN EMPTY LINE, for the same reason.
        #   · THE HEADER ENTRY IS REPAIRED AFTERWARDS. msgfilter marks it too,
        #     so `charset=UTF-8` becomes `charset=UTF-8⟧` and nothing loads.
        #   · AND IT IS ADDRESSED AS de_DE, not LANGUAGE=hostile — LANGUAGE is
        #     ignored under the C locale, so the catalog was never opened and
        #     the whole check quietly passed.
        hos=$tmp/hos; mkdir -p "$hos/de/LC_MESSAGES"
        if msgen "$root/po/syn-vault.pot" -o "$tmp/ident.po" 2>/dev/null &&
           msgfilter -i "$tmp/ident.po" -o "$tmp/hostile.raw" \
                     sed -e '/./s/$/⟧/' 2>/dev/null &&
           sed 's/^\("[A-Za-z-]*: .*\)⟧\(\\n"\)$/\1\2/' \
               "$tmp/hostile.raw" > "$tmp/hostile.po" &&
           msgfmt -o "$hos/de/LC_MESSAGES/syn-vault.mo" "$tmp/hostile.po" 2>/dev/null
        then
            printf '%s\n' "$REC_CMDS" | while IFS= read -r cmd; do
                [ -n "$cmd" ] || continue
                a=$(SYN_VAULT_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN $cmd 2>/dev/null | md5sum)
                b=$(SYN_VAULT_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                    $BIN $cmd 2>/dev/null | md5sum)
                [ "$a" = "$b" ] || printf '[%s]' "$cmd"
            done > "$tmp/drift.b"
            check "every --rec command survives a catalog that translates EVERYTHING" \
                  "" "$(cat "$tmp/drift.b")"

            # ...and the catalog WAS reached, or the check above proved nothing.
            g1=$(SYN_VAULT_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN list 2>&1 | md5sum)
            g2=$(SYN_VAULT_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN list 2>&1 | md5sum)
            check "...and that catalog WAS reached (the human path changed)" \
                  "differs" "$([ "$g1" = "$g2" ] && echo same || echo differs)"

            # ⛔ AND THE RECORD STILL HAD ROWS IN IT. Both commands above exit
            # non-zero on one path or another, so a change that made them print
            # NOTHING would leave two empty strings comparing equal and the
            # whole of 2b passing on air.
            rows=$(SYN_VAULT_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                   $BIN --rec list 2>/dev/null | grep -c .)
            check "...and --rec list actually emitted its header and two rows" "3" "$rows"

            # ⛔ AND `open`/`locked` ARE STILL THE RECORD'S WORDS. This is the
            # one value the QML compares, and it is ALSO a msgid because the
            # CLI's sentence needs it — so an assertion on the column itself is
            # the only thing that separates the two copies.
            st=$(SYN_VAULT_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN --rec status alpha 2>/dev/null | tail -1 | cut -f2)
            check "...and the state column is still 'locked'" "locked" "$st"
        else
            printf '  skip  msgen/msgfilter unavailable (nothing asserted)\n'
        fi
    fi
else
    printf '  skip  no binary or no localedef (nothing asserted)\n'
fi

# ── 3. ⛔ NOTHING MARKED ON A LINE THAT WRITES A RECORD ────────────────────
#
# The static half of check 2. rec_header() names the columns the window keys
# off and rec_row() writes the values it matches; a `_()` anywhere on either is
# wrong outright, because a record must carry the English string.
badrec=$(grep -n 'rec_row([^)]*[^A-Za-z_0-9]_(\|rec_header([^)]*[^A-Za-z_0-9]_(' \
         "$root"/src/*.c | tr '\n' ' ')
check "no _() inside a rec_row() or rec_header() call" "" "$badrec"

# ── 4. ⛔ EVERY .qml THAT TRANSLATES IS IN po/POTFILES ─────────────────────
#
# A file missing from that list still compiles and still looks up at runtime,
# so nothing warns — the strings simply never reach a template and are English
# forever.
missing=""
while IFS= read -r q; do
    rel=${q#"$root/data/"}
    grep -qxF "$rel" "$root/po/POTFILES" || missing="$missing $rel"
done < <(grep -rl 'I18n\.tr(' "$root/data" --include='*.qml')
check "every .qml that calls I18n.tr() is listed in po/POTFILES" "" "$missing"

# ⛔ AND THE SINGLETON IS THE SHARED ONE, BYTE FOR BYTE. data/qml/I18n.qml is
# the same file in every quickshell app in this project; a local edit to one
# copy is a divergence nothing else would notice.
shared=$(ls "$root"/../syn-edit/data/qml/I18n.qml 2>/dev/null)
if [ -n "$shared" ]; then
    a=$(md5sum < "$root/data/qml/I18n.qml"); b=$(md5sum < "$shared")
    check "data/qml/I18n.qml is the shared copy, unedited" "$b" "$a"
else
    printf '  skip  no sibling checkout to compare I18n.qml against\n'
fi

# ── 5. ⛔ THE LIST'S STATE COLUMN LINES UP IN EVERY LANGUAGE ───────────────
#
# It used to pad to the literal "open  " — six characters, the width of the
# English words — so a translation put the ] wherever it happened to end.
# Measured in COLUMNS now, which is the third of three possible answers:
# "已解锁" is 9 bytes, 3 code points and 6 columns.
if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1 &&
   msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
   grep -qF 'msgid "locked"'; then
    ragged=""
    for l in de fr es pt it nl pl ru ja zh ko hi ar; do
        [ -f "$root/po/$l.po" ] || continue
        m2=$tmp/mo-$l; mkdir -p "$m2/x/LC_MESSAGES"
        msgfmt -o "$m2/x/LC_MESSAGES/syn-vault.mo" "$root/po/$l.po" 2>/dev/null || continue
        localedef -i de_DE -f UTF-8 -c "$loc/x.UTF-8" 2>/dev/null
        out=$(SYN_VAULT_LOCALEDIR=$m2 LOCPATH=$loc LC_ALL=x.UTF-8 $BIN list 2>/dev/null)
        n=$(printf '%s' "$out" | python3 -c '
import sys, unicodedata, re
def cols(s):
    w = 0
    for ch in s:
        if unicodedata.category(ch) in ("Mn", "Me"): continue
        w += 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
    return w
# every row brackets its state; the ] has to land in the same column on each
ends = {cols(l[:l.index("]")]) for l in sys.stdin.read().split("\n") if "]" in l}
print(len(ends))')
        [ "$n" = "1" ] || ragged="$ragged $l($n)"
    done
    check "the list's ] lands in one column in every language" "" "$ragged"
else
    printf '  skip  no catalogs to measure the list column with\n'
fi

# ── 6. the catalogs still compile, and to both shapes ─────────────────────
#
# ⚠ msgfmt -c, which is what catches a translation that reordered %s without
# saying %1$s — five catalogs failed exactly that way in another component, and
# the failure at runtime is a crash, not a wrong word.
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
# They assert English against a binary that answers the desktop's language once
# syn-vault is installed. One exported LC_ALL and one unset LANGUAGE; nothing
# else stops them being dropped, and dropping them breaks the build on every
# translated desktop and no English one.
pin=""
for suite in vault_test.sh qml_test.sh; do
    s="$root/tests/$suite"
    [ -f "$s" ] || continue
    grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$s" || pin="$pin $suite(LC_ALL)"
    grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$s" || pin="$pin $suite(LANGUAGE)"
done
check "the other suites pin the locale they assert in" "" "$pin"

echo
if [ "$fails" -eq 0 ]; then echo "all syn-vault translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
