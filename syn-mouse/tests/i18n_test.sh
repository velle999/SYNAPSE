#!/usr/bin/env bash
# i18n_test.sh — syn-mouse's words, reachable by a translator, and its records
# untouched.
#
# ⛔ 1. THE RECORD PROTOCOL MUST NEVER BE TRANSLATED. `--rec` rows open with
#    their kind — `profile`, `bind`, `mouse`, `active`, `button`, `key` — and
#    data/syn-mouse.qml keys off every one of them.
#
#    ⛔ AND THE NAMES ARE COMMANDS. A button name (`back`), a mode (`toggle`)
#    and a key name (`shift`) are what `syn-mouse bind` takes back and what the
#    window sends. Every button carries a name AND a label one struct field
#    apart — `back` and "Thumb button (back)" — and only the second is marked.
#    A translated name is a window binding a button that does not exist.
#
# ⚠ 2. THE HUMAN PATH MUST BE TRANSLATED, and it is where somebody ends up when
#    the window will not open.
#
# ⛔ 3. ONE CATALOG SERVES BOTH. po/*.po is compiled to JSON for the window and
#    to a .mo for the binary, named after the DOMAIN — syn-mouse.mo — which is
#    why the build uses meson's i18n module rather than a custom_target.
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
BIN=${2:-$root/build/syn-mouse}
fails=0
check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

echo "syn-mouse translations"

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# ⛔ A SCRATCH SYNMOUSE_HOME, AND A CONTROL SOCKET NOBODY SERVES. A suite that
# read the real bindings would compare whatever the person running it has
# bound — and one that reached a running daemon would be asking a live service.
#
# ⚠ AND SEEDED, so `--rec profiles` has rows of every kind to compare. An empty
# config is a record two locales agree on perfectly while testing nothing.
mkdir -p "$tmp/cfg"
cat > "$tmp/cfg/bindings.conf" <<'CONF'
notify = on

[Diablo IV]
app = steam_app_2344520
back = toggle 1 every 5
forward = repeat shift+2 every 0.5
middle = latch shift
wheelup = key f
right = off

[Everywhere]
forward = key mouse4
CONF
export SYNMOUSE_HOME="$tmp/cfg"
export SYNMOUSE_SOCKET=/nonexistent/syn-mouse-i18n.sock
export SYNMOUSE_SYNUI_SOCKET=""

# ── 1. the template is current, and nothing was mangled making it ──────────
err=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean" "" "$err"
if [ -f "$tmp/syn-mouse.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/syn-mouse.pot" 2>/dev/null)
    now=$(grep -c '^msgid "' "$tmp/syn-mouse.pot" 2>/dev/null)
    check "po/syn-mouse.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted —
# and this program's messages are full of em dashes. A msgid that lost a
# character never matches the source string, so it is permanently English.
src_nonascii=$(LC_ALL=C grep -cP 'N?_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/syn-mouse.pot" | awk '{print ($1>0)}')
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
# ⚠ EVERY --rec COMMAND THAT CHANGES NOTHING. `bind`, `add` and the rest write
# the file and are covered by tests/cli_test.sh; `apps` needs a compositor.
# `devices` reads this machine's /sys, which is fine here: both runs read the
# same machine seconds apart.
REC_CMDS="--rec profiles
--rec buttons
--rec keys
--rec status
--rec devices"

if [ -x "$BIN" ] && command -v localedef >/dev/null 2>&1; then
    loc=$tmp/loc; mkdir -p "$loc"
    # ⛔ AND THE BINARY HAS TO BE ABLE TO FIND A CATALOG. Its compiled-in
    # localedir is under the install prefix, so an uninstalled syn-mouse loads
    # nothing and answers English in every language — which is how the first
    # version of a check like this passes with a _() sitting in a record.
    mo=$tmp/mo; mkdir -p "$mo/de/LC_MESSAGES"
    msgfmt -o "$mo/de/LC_MESSAGES/syn-mouse.mo" "$root/po/de.po" 2>/dev/null

    if ! localedef -i de_DE -f UTF-8 -c "$loc/de_DE.UTF-8" 2>/dev/null; then
        printf '  skip  localedef could not build de_DE (nothing asserted)\n'
    else
        # ── 2a. against the REAL German catalog ───────────────────────────
        printf '%s\n' "$REC_CMDS" | while IFS= read -r cmd; do
            [ -n "$cmd" ] || continue
            a=$(SYN_MOUSE_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN $cmd 2>/dev/null | md5sum)
            b=$(SYN_MOUSE_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                $BIN $cmd 2>/dev/null | md5sum)
            [ "$a" = "$b" ] || printf '[%s]' "$cmd"
        done > "$tmp/drift.a"
        check "every --rec command answers the same in German as in C" "" \
              "$(cat "$tmp/drift.a")"

        # ...and the HUMAN path does, or nothing is being translated at all.
        # ⚠ Only once a catalog has something in it: before that this is a skip,
        # not a failure, because "not translated yet" is a legitimate state.
        if msgattrib --translated --no-obsolete --no-fuzzy "$root/po/de.po" 2>/dev/null |
           grep -qF 'msgid "Thumb button (back)"'; then
            h1=$(SYN_MOUSE_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN buttons 2>&1 | md5sum)
            h2=$(SYN_MOUSE_LOCALEDIR=$mo LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN buttons 2>&1 | md5sum)
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
        # ⚠ THIS IS WHAT CATCHES A MARKED NAME. "Thumb button (back)" is in the
        # catalog on purpose and `back` sits one field away from it; a `_()`
        # that slipped onto the name is invisible to 2a until somebody
        # translates the entry, and then the window binds nothing.
        #
        # ⚠ FIVE THINGS HAD TO BE RIGHT HERE AND EACH FAILED SILENTLY when this
        # was first written for a sibling component:
        #
        #   · msgfilter READS A FILE. `-i -` writes an empty catalog and says
        #     nothing, and an empty one turns this into a skip.
        #   · THE MARKER IS A SUFFIX ONLY. A prefix moves the leading "\n" of
        #     every msgid that starts with one and msgfmt drops the entry.
        #   · AND NEVER ON AN EMPTY LINE, for the same reason.
        #   · THE HEADER ENTRY IS REPAIRED AFTERWARDS. msgfilter marks it too,
        #     so `charset=UTF-8` becomes `charset=UTF-8⟧` and nothing loads.
        #   · AND IT IS ADDRESSED AS de_DE, not LANGUAGE=hostile — LANGUAGE is
        #     ignored under the C locale, so the catalog was never opened.
        hos=$tmp/hos; mkdir -p "$hos/de/LC_MESSAGES"
        if msgen "$root/po/syn-mouse.pot" -o "$tmp/ident.po" 2>/dev/null &&
           msgfilter -i "$tmp/ident.po" -o "$tmp/hostile.raw" \
                     sed -e '/./s/$/⟧/' 2>/dev/null &&
           sed 's/^\("[A-Za-z-]*: .*\)⟧\(\\n"\)$/\1\2/' \
               "$tmp/hostile.raw" > "$tmp/hostile.po" &&
           msgfmt -o "$hos/de/LC_MESSAGES/syn-mouse.mo" "$tmp/hostile.po" 2>/dev/null
        then
            printf '%s\n' "$REC_CMDS" | while IFS= read -r cmd; do
                [ -n "$cmd" ] || continue
                a=$(SYN_MOUSE_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN $cmd 2>/dev/null | md5sum)
                b=$(SYN_MOUSE_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                    $BIN $cmd 2>/dev/null | md5sum)
                [ "$a" = "$b" ] || printf '[%s]' "$cmd"
            done > "$tmp/drift.b"
            check "every --rec command survives a catalog that translates EVERYTHING" \
                  "" "$(cat "$tmp/drift.b")"

            # ...and the catalog WAS reached, or the check above proved nothing.
            g1=$(SYN_MOUSE_LOCALEDIR=$mo LC_ALL=C.UTF-8 $BIN buttons 2>&1 | md5sum)
            g2=$(SYN_MOUSE_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                 $BIN buttons 2>&1 | md5sum)
            check "...and that catalog WAS reached (the human path changed)" \
                  "differs" "$([ "$g1" = "$g2" ] && echo same || echo differs)"

            # ⛔ AND THE RECORD STILL HAD ROWS IN IT. A change that made these
            # commands print NOTHING would leave two empty strings comparing
            # equal and the whole of 2b passing on air.
            rows=$(SYN_MOUSE_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                   $BIN --rec profiles 2>/dev/null | grep -c .)
            check "...and --rec profiles emitted its header, settings, profiles and binds" "11" "$rows"

            # ⛔ AND THE NAME COLUMN IS STILL THE COMMAND. It sits one field
            # away from a label that IS a msgid, so an assertion on the column
            # itself is the only thing that separates the two.
            names=$(SYN_MOUSE_LOCALEDIR=$hos LOCPATH=$loc LC_ALL=de_DE.UTF-8 \
                    $BIN --rec buttons 2>/dev/null | tail -n +2 | cut -f2 | tr '\n' ' ')
            check "...and every button name is still the English one" \
                  "left right middle back forward button6 button7 button8 wheelup wheeldown wheelleft wheelright " "$names"
        else
            printf '  skip  msgen/msgfilter unavailable (nothing asserted)\n'
        fi
    fi
else
    printf '  skip  no binary or no localedef (nothing asserted)\n'
fi

# ── 3. ⛔ NOTHING MARKED ON A LINE THAT WRITES A RECORD ────────────────────
#
# The static half of check 2. rec_header() names the columns and rec_row()
# writes the values the window matches; and the daemon's status() writes its
# rows with fprintf. A `_()` on any of them is wrong outright.
badrec=$(grep -n 'rec_row([^)]*[^A-Za-z_0-9]_(\|rec_header([^)]*[^A-Za-z_0-9]_(\|fprintf(f, "[a-z]*\\t[^;]*[^A-Za-z_0-9]_(' \
         "$root"/src/*.c | tr '\n' ' ')
check "no _() inside a record-writing call" "" "$badrec"

# ⛔ AND THE JOURNAL STAYS ENGLISH: the daemon's say() lines are what
# `journalctl --user -u syn-mouse` shows and what a bug report carries.
badsay=$(grep -n 'say([^)]*[^A-Za-z_0-9]_(' "$root"/src/*.c | tr '\n' ' ')
check "no _() inside a journal line" "" "$badsay"

# ── 4. ⛔ EVERY .qml THAT TRANSLATES IS IN po/POTFILES ─────────────────────
missing=""
while IFS= read -r q; do
    rel=${q#"$root/data/"}
    grep -qxF "$rel" "$root/po/POTFILES" || missing="$missing $rel"
done < <(grep -rl 'I18n\.tr(' "$root/data" --include='*.qml')
check "every .qml that calls I18n.tr() is listed in po/POTFILES" "" "$missing"

# ⛔ AND THE SINGLETON IS THE SHARED ONE, BYTE FOR BYTE.
shared=$(ls "$root"/../syn-edit/data/qml/I18n.qml 2>/dev/null)
if [ -n "$shared" ]; then
    a=$(md5sum < "$root/data/qml/I18n.qml"); b=$(md5sum < "$shared")
    check "data/qml/I18n.qml is the shared copy, unedited" "$b" "$a"
else
    printf '  skip  no sibling checkout to compare I18n.qml against\n'
fi

# ── 5. the catalogs still compile, and to both shapes ─────────────────────
#
# ⚠ msgfmt -c, which is what catches a translation that reordered %s without
# saying %1$s — the failure at runtime is a crash, not a wrong word.
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
# syn-mouse is installed. One exported LC_ALL and one unset LANGUAGE.
pin=""
for suite in cli_test.sh daemon_test.sh qml_test.sh; do
    s="$root/tests/$suite"
    [ -f "$s" ] || continue
    grep -qE '^[[:space:]]*export[[:space:]]+LC_ALL=' "$s" || pin="$pin $suite(LC_ALL)"
    grep -qE '^[[:space:]]*unset[[:space:]]+LANGUAGE' "$s" || pin="$pin $suite(LANGUAGE)"
done
check "the other suites pin the locale they assert in" "" "$pin"

echo
if [ "$fails" -eq 0 ]; then echo "all syn-mouse translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
