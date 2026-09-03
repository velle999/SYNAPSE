#!/usr/bin/env bash
# i18n_test.sh — synnet's words, reachable by a translator, and the three
# things it emits that are NOT words left exactly as they were.
#
# synnet has no window and no `--rec`. What it prints on stdout is read by a
# person; everything else it writes is read by a program, and the whole of this
# file is about that line.
#
# ⛔ 1. THE JOURNAL STAYS ENGLISH. When the firewall fails, syn-settings' own
#    network pane tells somebody "`journalctl -u synnet` has what nft said" —
#    that is the line they will read, paste into a search, and attach to a bug
#    report. A journal that changed language with the desktop is one nobody
#    else can help with.
#
# ⛔ 2. THE STATE FILE IS A PROTOCOL. /run/synnet/firewall.state is key=value,
#    and syn-settings parses `state`, `links` and `reasserts` out of it to
#    decide whether this machine reports itself filtered.
#
# ⛔ 3. AND SO IS AN AI PROMPT. synnet asks synapd "Reply with just BLOCK or
#    ALLOW" and matches on those two words. A translated prompt is a different
#    question, answered in a language nothing here reads.
#
# ⛔ 4. THE nft SCRIPT LEAST OF ALL — it is a program's input.
#
# ⚠ AND THIS TEST NEEDS NO ROOT AND NO nftables. synnet already carries the
# seams: $SYNNET_FW_STATE_FILE, $SYNNET_FW_PREF_FILE, $SYNNET_FW_IFACES_FILE
# and a stub `nft` on PATH that records what it was handed. Everything below is
# driven through them, so the same bytes are compared on any machine — which is
# the failure mode that has bitten three sibling components.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. Everything below parses
# tool output, and the gettext tools are themselves translated.
# ⚠ LANGUAGE is UNSET, not set — gettext reads it before LC_ALL, so an ambient
# LANGUAGE=ja would answer Japanese to the German runs below and the assertion
# that the human path IS translated would compare Japanese with Japanese.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/_b/synnet}
case "$BIN" in /*) ;; *) BIN="$PWD/$BIN" ;; esac
fails=0
check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "synnet translations"

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

export SYNNET_FW_STATE_FILE="$tmp/firewall.state"
export SYNNET_FW_PREF_FILE="$tmp/firewall.pref"
export SYNNET_FW_IFACES_FILE="$tmp/trusted-ifaces"
printf 'on\n' > "$SYNNET_FW_PREF_FILE"
printf 'virbr0\nwaydroid0\n' > "$SYNNET_FW_IFACES_FILE"
printf 'state=active\nreasserts=7\nlinks=1\n' > "$SYNNET_FW_STATE_FILE"

# An nft that works and records the ruleset it was handed on stdin.
mkdir -p "$tmp/bin"
export NFT_LOG="$tmp/nft.log"
cat > "$tmp/bin/nft" <<'STUB'
#!/bin/sh
printf '%s\n' "ARGV: $*" >> "$NFT_LOG"
cat >> "$NFT_LOG" 2>/dev/null
exit 0
STUB
chmod +x "$tmp/bin/nft"

# ── 1. the template is current, and nothing was mangled making it ──────────
err=$("$root/po/pot.sh" "$root" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
if [ -f "$tmp/synnet.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/synnet.pot" 2>/dev/null)
    now=$(grep -c '^msgid "' "$tmp/synnet.pot" 2>/dev/null)
    check "po/synnet.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted,
# with no warning about the loss — and half of synnet's status screen is em
# dashes and ⚠. A msgid that lost a character never matches the source string,
# so it is permanently English however well translated.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/synnet.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

# ── 2. ⛔ NOTHING A PROGRAM READS MOVES WITH THE LANGUAGE ──────────────────
#
# ⚠ RUN, not grepped. A `_()` around a syslog format or an nft fragment is
# invisible to any amount of reading; it shows up the moment the program is
# asked to do the same thing in two languages.
#
# ⛔ AND THE LOCALE HAS TO EXIST. LANGUAGE is vetoed under C, so a box with no
# generated locales silently tests nothing. localedef into a scratch LOCPATH —
# never locale-gen, which is root and system-wide.
if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    # ⛔ AND THE BINARY HAS TO BE ABLE TO FIND A CATALOG. Its compiled-in
    # localedir is under the install prefix, so an uninstalled synnet loads
    # nothing and answers English in every language — which is how a check like
    # this passes with a _() sitting on a protocol string.
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/synnet.mo" "$root/po/de.po" 2>/dev/null

    if ! localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        printf '  skip  localedef could not build de_DE (nothing asserted)\n'
    else
        # ── 2b. ⛔ A CATALOG THAT TRANSLATES EVERYTHING ───────────────────
        #
        # The strong form, and it does not depend on anybody having translated
        # anything: built from the TEMPLATE with every msgstr marked. Anything
        # that changes under it has a string reaching gettext, whether or not
        # de.po happens to carry that entry today.
        #
        # ⚠ FIVE THINGS HAD TO BE RIGHT HERE AND EACH FAILED SILENTLY when this
        # was first written for a sibling:
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
        #     ignored under the C locale, so the catalog was never opened.
        hos=$tmp/hos; mkdir -p "$hos/de/LC_MESSAGES"
        if msgen "$root/po/synnet.pot" -o "$tmp/ident.po" 2>/dev/null &&
           msgfilter -i "$tmp/ident.po" -o "$tmp/hostile.raw" \
                     sed -e '/./s/$/⟧/' 2>/dev/null &&
           sed 's/^\("[A-Za-z-]*: .*\)⟧\(\\n"\)$/\1\2/' \
               "$tmp/hostile.raw" > "$tmp/hostile.po" &&
           msgfmt -o "$hos/de/LC_MESSAGES/synnet.mo" "$tmp/hostile.po" 2>/dev/null
        then
            # ⛔ THE nft SCRIPT THE DAEMON ACTUALLY HANDS OVER. Not the one in
            # the source — the one as cooked, with the trusted links spliced
            # in. A translated fragment anywhere in it is a rule the kernel
            # refuses, on the machine whose firewall this is.
            #
            # ⚠ AND IT IS A GUARD, NOT A DEMONSTRATION. Every line of that
            # script is built by concatenating literals with the SYNNET_NFT_*
            # macros, and xgettext extracts only the FIRST literal of such a run
            # — so a `_()` put around one of them today marks a msgid the
            # runtime string can never equal, and gettext hands it straight
            # back. Tried, and it changed nothing. What this catches is the
            # first plain whole-literal fragment somebody adds to the script,
            # which is exactly when it would start mattering. The state-file
            # check below IS exercised: marking its writer fails it.
            # ⚠ UNDER fakeroot WHEN THERE IS ONE. `--firewall` refuses without
            # root and returns having written nothing, which would leave two
            # empty logs comparing equal — the shape of a check that tests
            # nothing and says ok. fakeroot makes geteuid() answer 0, and every
            # write on this path goes to $tmp or to the stub `nft` on PATH, so
            # nothing outside the fixture is touched. CI runs as real root.
            AS_ROOT=""
            [ "$(id -u)" = 0 ] || AS_ROOT=$(command -v fakeroot || true)

            run_fw() {  # run_fw <localedir> <locale> — writes $NFT_LOG and the state file
                : > "$NFT_LOG"
                rm -f "$SYNNET_FW_STATE_FILE"
                PATH="$tmp/bin:$PATH" SYNNET_LOCALEDIR=$1 LOCPATH=$loc LC_ALL=$2 \
                    $AS_ROOT "$BIN" --firewall >/dev/null 2>&1
            }

            # ⛔ `since=` IS STRIPPED, AND ONLY THAT. It is the epoch second the
            # firewall was asserted, so two runs a second apart differ there for
            # a reason that has nothing to do with language — and a diff that
            # always fails is a diff nobody reads. Every other field is compared
            # whole, including the ones syn-settings parses.
            state_body() { grep -v '^since=' "$SYNNET_FW_STATE_FILE" 2>/dev/null | md5sum; }

            # ⛔ CHECKED FOR EMPTINESS BEFORE BEING COMPARED, AND SKIPPED IF SO.
            # `--firewall` loads an nftables chain, so it refuses without root
            # and returns having written nothing — which would leave two empty
            # logs comparing equal and both assertions below passing on air.
            # That is the shape of a check that tests nothing and says ok, and
            # it is worth more to say "skip" here than to be told twice a day
            # that a thing nobody exercised is fine. CI runs as root.
            run_fw "$mo" C.UTF-8
            if [ -s "$NFT_LOG" ]; then
                a=$(md5sum < "$NFT_LOG")
                sa=$(state_body)
                run_fw "$hos" de_DE.UTF-8
                b=$(md5sum < "$NFT_LOG")
                sb=$(state_body)
                check "the nft script is byte-identical under a hostile catalog" "$a" "$b"
                check "the published state file is byte-identical too" "$sa" "$sb"
            else
                printf '  skip  --firewall needs root and there is no fakeroot — the nft script and the state file were not compared\n'
            fi
            printf 'state=active\nreasserts=7\nlinks=1\n' > "$SYNNET_FW_STATE_FILE"

            # ...and the catalog WAS reached, or none of the above proved
            # anything. `--status` is the human path and must change.
            printf 'state=active\nreasserts=7\nlinks=1\n' > "$SYNNET_FW_STATE_FILE"
            g1=$(SYNNET_LOCALEDIR=$mo  LC_ALL=C.UTF-8 "$BIN" --status 2>&1 | md5sum)
            g2=$(SYNNET_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 "$BIN" --status 2>&1 | md5sum)
            check "...and that catalog WAS reached (--status changed)" "differs" \
                  "$([ "$g1" = "$g2" ] && echo same || echo differs)"

            # ⛔ AND --status PRINTED SOMETHING. Every branch of it is guarded
            # on a file or on privilege, so a change that made it silent would
            # leave two empty strings comparing equal above.
            n=$(SYNNET_LOCALEDIR=$mo LC_ALL=C.UTF-8 "$BIN" --status 2>&1 | grep -c .)
            check "...and --status actually printed its report" "yes" \
                  "$([ "$n" -gt 8 ] && echo yes || echo "no($n)")"
        else
            printf '  skip  msgen/msgfilter unavailable (nothing asserted)\n'
        fi

        # ...and against the REAL German catalog, once it has something in it.
        if msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF 'msgid "  Input firewall'; then
            h1=$(SYNNET_LOCALEDIR=$mo LC_ALL=C.UTF-8 "$BIN" --status 2>&1 | md5sum)
            h2=$(SYNNET_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 "$BIN" --status 2>&1 | md5sum)
            check "the human path changes with a real catalog too" "differs" \
                  "$([ "$h1" = "$h2" ] && echo same || echo differs)"
        else
            printf '  skip  the German catalog is not filled yet\n'
        fi
    fi
else
    printf '  skip  no binary or no localedef (nothing asserted)\n'
fi

# ── 3. ⛔ THE STATIC HALF: NOTHING MARKED ON A LINE A PROGRAM READS ────────
#
# The three destinations, spelled out. A `_()` on any of them is wrong outright
# and does not need a machine to prove it.
badlog=$(grep -n 'syslog([^)]*[^A-Za-z_0-9]_(' "$root"/src/*.c | tr '\n' ' ')
check "no _() inside a syslog() call — the journal stays English" "" "$badlog"

# The nft script is composed with snprintf into `script`/`cmd`; a marked
# fragment anywhere in that composition is a rule the kernel will refuse.
badnft=$(grep -n 'run_nft([^)]*[^A-Za-z_0-9]_(' "$root"/src/*.c | tr '\n' ' ')
check "no _() inside a run_nft() call — the ruleset is not prose" "" "$badnft"

# ⛔ AND THE AI PROMPTS. Two of them, and both ask for BLOCK or ALLOW and then
# match on those two words. A grep for the marker on the same statement is
# enough because both are one snprintf each.
badai=$(awk '/snprintf\(prompt/,/;/' "$root"/src/*.c | grep -c '[^A-Za-z_0-9]_(' )
check "no _() in an AI prompt — the model is asked one question" "0" "$badai"

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

# ── ⛔ AND THE OTHER SUITE PINS THE LOCALE IT ASSERTS IN ──────────────────
#
# tests/firewall_test.sh matches English phrases out of --status against a
# binary that answers the desktop's language once synnet is installed. One
# exported LC_ALL and one unset LANGUAGE; nothing else stops them being
# dropped, and dropping them breaks the build on every translated desktop and
# no English one.
pin=""
s="$root/tests/firewall_test.sh"
grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$s" || pin="$pin LC_ALL"
grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$s" || pin="$pin LANGUAGE"
check "tests/firewall_test.sh pins the locale it asserts in" "" "$pin"

echo
if [ "$fails" -eq 0 ]; then echo "all synnet translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
