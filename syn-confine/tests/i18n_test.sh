#!/usr/bin/env bash
# i18n_test.sh — syn-confine's own words, and the two things that are not words.
#
# ⚠ MOST OF WHAT COMES OUT OF A SANDBOX IS NOT THIS PROGRAM SPEAKING. The
# confined command inherits the environment and phrases its own errors:
# "Permission denied" comes from ITS libc. tests/syn_confine_test.sh greps that
# child's strerror in seven places, which is why it pins LC_ALL before anything
# runs — a Japanese VM once failed all seven, and a package build with them, on
# a sandbox that was working perfectly.
#
# ⛔ 1. EXIT 78 IS THE PROTOCOL, AND IT IS A NUMBER. "The sandbox could not be
#    built" is told apart from "the command failed" by the status and never by
#    the wording. That is what lets the distinction survive translation, and it
#    is asserted here in a language that is not English.
#
# ⛔ 2. THE FLAG SPELLINGS STAY. `--rw`, `--tcp`, `rw`/`rx`/`ro` are what you
#    type and what `--print` echoes back; a translated flag is a policy summary
#    that names an option the program does not have.
#
# ⚠ 3. `--print` IS PROSE. It is what somebody reads to decide whether a
#    sandbox is tight enough — "(UDP NOT covered)" is the whole point of that
#    line — so it IS translated, and this checks that it changes.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. Everything below parses
# tool output, and the gettext tools are themselves translated.
# ⚠ LANGUAGE is UNSET, not set — gettext reads it before LC_ALL.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/build/syn-confine}
case "$BIN" in /*) ;; *) BIN="$PWD/$BIN" ;; esac
fails=0
check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "syn-confine translations"

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# ── 1. the template is current, and nothing was mangled making it ──────────
err=$("$root/po/pot.sh" "$root" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
if [ -f "$tmp/syn-confine.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/syn-confine.pot" 2>/dev/null)
    now=$(grep -c '^msgid "' "$tmp/syn-confine.pot" 2>/dev/null)
    check "po/syn-confine.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted,
# with no warning — and the two refusals that matter most here are em-dashed. A
# msgid that lost a character never matches the source string.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/syn-confine.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

# ── 2. what must and must not change with the language ────────────────────
#
# ⛔ AND THE LOCALE HAS TO EXIST. LANGUAGE is vetoed under C, so a box with no
# generated locales silently tests nothing. localedef into a scratch LOCPATH —
# never locale-gen, which is root and system-wide.
if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    # ⛔ AND THE BINARY HAS TO FIND A CATALOG. Its compiled-in localedir is
    # under the install prefix, so an uninstalled syn-confine loads nothing and
    # answers English in every language — which is how a check like this passes
    # while proving nothing at all.
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/syn-confine.mo" "$root/po/de.po" 2>/dev/null

    if ! localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        printf '  skip  localedef could not build de_DE (nothing asserted)\n'
    else
        run_de() { SYN_CONFINE_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 "$BIN" "$@"; }
        run_c()  { SYN_CONFINE_LOCALEDIR=$mo LC_ALL=C.UTF-8 "$BIN" "$@"; }

        # ⛔ 78 IN GERMAN TOO. The one thing a caller keys on.
        run_de --net --tcp 80 -- true >/dev/null 2>&1
        check "a refusal still exits 78 in another language" "78" "$?"

        # ⛔ AND THE FLAG SPELLINGS `--print` ECHOES BACK ARE UNTOUCHED. A
        # translated `rw` is a summary naming an option that does not exist.
        kinds=$(run_de --print --rw "$tmp" --ro /usr 2>/dev/null |
                grep -oE '^  (rw|ro|rx) ' | tr -d ' ' | sort -u | tr '\n' ' ')
        # ⚠ ALL THREE, because --rw grants exec too and the base profile is
        # read-execute: a report of one kind would mean the policy collapsed,
        # not that the spelling held.
        check "--print still spells the flags rw/ro/rx" "ro rw rx " "$kinds"

        # ...and the prose around them DOES move, or nothing is translated.
        # ⚠ Only once a catalog has something in it: before that this is a
        # skip, not a failure, because "not translated yet" is legitimate.
        if msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF 'msgid "network        no TCP (UDP NOT covered)'; then
            a=$(run_c  --print --rw "$tmp" 2>&1 | md5sum)
            b=$(run_de --print --rw "$tmp" 2>&1 | md5sum)
            check "...while the policy summary DOES change" "differs" \
                  "$([ "$a" = "$b" ] && echo same || echo differs)"

            # ⛔ AND IT SAID SOMETHING. Every branch of --print is guarded, so a
            # change that made it silent would leave two empty strings equal.
            n=$(run_de --print --rw "$tmp" 2>&1 | grep -c .)
            check "...and --print actually printed a policy" "yes" \
                  "$([ "$n" -ge 3 ] && echo yes || echo "no($n)")"
        else
            printf '  skip  the German catalog is not filled yet\n'
        fi
    fi
else
    printf '  skip  no binary or no localedef (nothing asserted)\n'
fi

# ── 3. ⛔ usage() IS NOT MARKED ────────────────────────────────────────────
#
# One fputs of a manual page, in every sibling. A `_()` around it would put a
# hundred lines of flag spellings and aligned columns in front of a translator
# for no gain, and the decision to move that set is one decision for all of
# them, taken once.
badusage=$(awk '/^static void usage/,/^}/' "$root"/src/*.c | grep -c '[^A-Za-z_0-9]_(')
check "usage() is left in English, as in every sibling" "0" "$badusage"

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

# ── ⛔ AND THE MAIN SUITE STILL PINS THE LOCALE IT ASSERTS IN ─────────────
#
# It greps the CONFINED CHILD's strerror for "denied" — two execs away, and the
# child speaks from its own libc. Losing that pin fails seven assertions and a
# package build on any translated machine, which is what happened on a Japanese
# VM on 2026-09-01.
pin=""
s="$root/tests/syn_confine_test.sh"
grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$s" || pin="$pin LC_ALL"
grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$s" || pin="$pin LANGUAGE"
check "tests/syn_confine_test.sh pins the locale it asserts in" "" "$pin"

echo
if [ "$fails" -eq 0 ]; then echo "all syn-confine translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
