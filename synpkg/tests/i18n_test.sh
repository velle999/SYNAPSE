#!/usr/bin/env bash
# i18n_test.sh — synpkg's words, reachable by a translator, and its TSV untouched.
#
# synpkg has TWO front-ends over one set of code paths, and they need opposite
# things from a catalog:
#
# ⛔ 1. THE --tsv PATH MUST NEVER BE TRANSLATED. It is what data/synpkg.qml
#    parses and what these tests parse. A translated column or status word makes
#    the GUI's behaviour depend on the user's locale — the bug `pacman -Qi`
#    taught this project twice, in chibi and in syn-arsenal. This suite proves it
#    the only way worth proving: by RUNNING the TSV commands under a real foreign
#    locale and diffing against C.
#
# ⚠ 2. THE HUMAN PATH MUST BE. The CLI and the TUI are what somebody reads.
#
# ⛔ 3. AND ONE CATALOG SERVES BOTH FRONT-ENDS. po/*.po is compiled to JSON for
#    the window and to a .mo for the binary, so a word they share is translated
#    once. ⚠ The .mo is named after the DOMAIN — synpkg.mo — which is why the
#    build uses meson's i18n module rather than a custom_target: a loop can only
#    name its output de.mo, which installs to the right directory under a name
#    libintl never looks for, and every string silently falls back to English.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. Everything below parses
# tool output — xgettext diagnostics, msgfmt, msgattrib — and the gettext tools
# are themselves translated. On a Japanese desktop xgettext writes `警告:`, the
# `grep -v 'warning:'` filter below matches nothing, and "po/pot.sh runs clean"
# fails with a warning this project has decided is acceptable. It is the same
# bug `pacman -Qi` taught us: LC_ALL=C anything you parse.
#
# ⚠ LANGUAGE too, and it has to be UNSET rather than set: gettext consults it
# BEFORE LC_ALL, so an ambient LANGUAGE=ja would answer Japanese to the German
# runs below and the one assertion that proves the human path IS translated
# would be comparing Japanese against Japanese.
#
# The deliberate foreign-locale runs set LC_ALL per command, which wins over
# this.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/build/synpkg}
fails=0
check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "synpkg translations"

# ── 1. the template is current, and nothing was mangled making it ──────────
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
err=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
if [ -f "$tmp/synpkg.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/synpkg.pot" 2>/dev/null)
    now=$(grep -c '^msgid "' "$tmp/synpkg.pot" 2>/dev/null)
    check "po/synpkg.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted, with
# no warning about the loss — `%s.pacnew — merge it` came out `%s.pacnew  merge
# it`, a msgid that can never match the source string. pot.sh refuses that now;
# this asserts the result rather than the flag.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/synpkg.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

# ── 2. ⛔ THE TSV PATH IS LOCALE-INDEPENDENT ───────────────────────────────
#
# ⚠ RUN, not grepped. A `_()` on the wrong side of a `g_out == OUT_TSV` branch
# is invisible to any amount of reading; it shows up the moment the program is
# asked the same question in two languages.
#
# ⛔ AND THE LOCALE HAS TO EXIST. LANGUAGE is vetoed under C, so a box with no
# generated locales silently tests nothing. localedef into a scratch LOCPATH —
# never locale-gen, which is root and system-wide.
if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    # ⛔ AND THE BINARY HAS TO BE ABLE TO FIND A CATALOG. Its compiled-in
    # localedir is under the install prefix, so an uninstalled synpkg loads
    # nothing and answers English in every language — which is how the first
    # version of this check passed with a _() sitting in a TSV row.
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/synpkg.mo" "$root/po/de.po" 2>/dev/null
    if localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        drift=""
        # ⚠ EVERY --tsv SUBCOMMAND THAT RUNS OFFLINE. The runtime check is the
        # only one that can tell a TSV branch from its else, so it has to be
        # the broad one.
        for cmd in "config" "--tsv config" "--tsv about" \
                   "--tsv suggest categories" "--tsv groups"; do
            a=$(SYNPKG_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN $cmd 2>/dev/null | md5sum)
            b=$(SYNPKG_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                $BIN $cmd 2>/dev/null | md5sum)
            case "$cmd" in
              --tsv*) [ "$a" = "$b" ] || drift="$drift [$cmd]" ;;
            esac
        done
        check "every --tsv command answers the same in German as in C" "" "$drift"

        # ...and the HUMAN path does not, or nothing is being translated at all.
        # ⚠ Only once a catalog has something in it: before that this is a skip,
        # not a failure, because "not translated yet" is a legitimate state.
        # ⚠ A **C** MSGID, not just any: the QML half of this catalog was full
        # long before the C half existed, so a bare count is always over the
        # threshold and this check would claim the CLI was translated when not
        # one of its strings was.
        if msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF 'msgid "upgrade_system"' ||
           msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF 'msgid "nothing here"'; then
            h1=$(SYNPKG_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN config 2>/dev/null | md5sum)
            h2=$(SYNPKG_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN config 2>/dev/null | md5sum)
            check "...while the human path DOES change" "differs" \
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

# ── 3. (there is no static version of check 2, deliberately) ──────────────
#
# ⛔ A GREP CANNOT TELL A TSV BRANCH FROM ITS else. The first version of this
# tracked braces after a `g_out == OUT_TSV` and reported five hits — all five
# were the `else` arm, because the common shape here is
#
#     if (g_out == OUT_TSV) tsv_row(...);
#     else warn(_("..."));
#
# with no braces at all, so "inside the TSV branch" is one statement wide and
# brace counting never leaves it. A check that is wrong five times out of five
# teaches people to ignore it, so it is gone: check 2 RUNS the program, which is
# the only thing that can answer this question, and it runs every --tsv
# subcommand that works offline.

# ── 4. the catalogs still compile, and to both shapes ─────────────────────
bad=""
while IFS= read -r l; do
    po="$root/po/$l.po"
    [ -f "$po" ] || { bad="$bad $l(missing)"; continue; }
    msgfmt -c -o /dev/null "$po" 2>/dev/null || bad="$bad $l(msgfmt)"
    "$root/tools/po2json.py" "$po" -o "$tmp/$l.json" >/dev/null 2>&1 \
        || bad="$bad $l(po2json)"
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every catalog compiles to both a .mo and a JSON" "" "$bad"

echo
if [ "$fails" -eq 0 ]; then echo "all synpkg translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
