#!/usr/bin/env bash
# i18n_test.sh — a translated string in synfiles must be reachable by a translator.
#
# ⛔ 1. qsTr() IS A TRAP AND IT IS THE FIRST THING CHECKED. Qt's translation
#    path needs someone to call QTranslator::load() and installTranslator()
#    before the QML engine starts. quickshell does neither — there is no
#    installTranslator anywhere in the binary and no way to reach one from QML.
#    So qsTr("Copy") compiles, returns "Copy", and translates nothing, in every
#    language, forever. It reads in a diff exactly like a marked string.
#
# ⚠ 2. A NON-LITERAL ARGUMENT extracts nothing. tools/qml-xgettext.py reads the
#    source, not the running program, so I18n.tr(someVariable) is marked-looking
#    and English.
#
# ⛔ 3. AND THE KEYS BESIDE THE LABELS WERE NOT SWEPT UP. A context-menu row
#    carries the action it runs in `act:` beside its label, and a property row
#    carries the record key the binary emitted in `key:`. Translating either
#    does not make a German file manager — it makes one whose menu items do
#    nothing and whose properties panel stops decoding paths. Same rule as
#    ctlpanel.c's settings keys, asserted rather than trusted.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

# ⛔ THE AMBIENT LOCALE IS NOT THIS TEST'S TO INHERIT. Everything below parses
# tool output, and the gettext tools are themselves translated: on a Japanese
# desktop xgettext writes `警告:` and a filter matching the English word finds
# nothing. ⚠ LANGUAGE is UNSET, not set — gettext reads it before LC_ALL.
export LC_ALL=C.UTF-8
unset LANGUAGE

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${2:-$root/build/syn-settings}
QML="$root/data/syn-settings.qml"
fails=0

check() {
    if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"
    else printf '  FAIL  %s — expected [%s], got [%s]\n' "$1" "$2" "$3"; fails=$((fails+1)); fi
}

# The Plural-Forms header of a .po, joined across its continuation lines.
#
# ⚠ SINGLE-QUOTED PYTHON, DELIBERATELY. Written inline with double quotes the
# shell eats the backslashes the regex needs and it matches nothing — which
# reported all thirteen catalogs as disagreeing with the desktop when every one
# of them agreed. Same escaping trap the bar's gate hit.
plural_of() {
    python3 -c 'import re,sys
t = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r"Plural-Forms: (.*?)\\n", t, re.S)
print(re.sub(r"\"\s*\n\s*\"", "", m.group(1)).strip() if m else "")' "$1"
}

echo "syn-settings translations"

# ── 1. qsTr() is never used ───────────────────────────────
# ⚠ COMMENTS STRIPPED FIRST. A checker that describes what it forbids finds
# itself — synui's equivalent matched its own I18n.qml header on the first run.
qstr=$(sed -e 's://.*::' "$QML" | grep -n 'qsTr[[:space:]]*(\|qsTranslate[[:space:]]*(' | tr '\n' ' ')
check "no qsTr() — quickshell has no translator to load it" "" "$qstr"

# ── 2. every marked argument is a literal, and the template is current ──
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
# ⚠ THE TEMPLATE IS BOTH LANGUAGES NOW, so pot.sh regenerates it —
# qml-xgettext.py over the QML and real xgettext over src/*.c, merged. Counting
# only the QML half would call the template current while every N_() label
# added since was missing from it.
err=$("$root/po/pot.sh" "$root" "$root/po" "$tmp" 2>&1 >/dev/null | grep -v '^ ' | grep -v 'warning:')
check "po/pot.sh runs clean (and every I18n.tr() argument is literal)" "" "$err"

if [ -f "$tmp/syn-settings.pot" ]; then
    have=$(grep -c '^msgid "' "$root/po/syn-settings.pot" 2>/dev/null || echo 0)
    now=$(grep -c '^msgid "' "$tmp/syn-settings.pot" 2>/dev/null || echo 0)
    check "po/syn-settings.pot is current ($have msgids)" "$now" "$have"
fi

# ⛔ AND THE NON-ASCII SURVIVED. xgettext with --omit-header writes the template
# as ASCII and DROPS every non-ASCII character from the msgids it extracted with
# no warning — a msgid that lost a character can never match the source string.
src_nonascii=$(LC_ALL=C grep -cP 'N_\("[^"]*[\x80-\xff]' "$root"/src/*.c | awk -F: '{s+=$2} END{print (s>0)}')
pot_nonascii=$(LC_ALL=C grep -cP '^msgid ".*[\x80-\xff]' "$root/po/syn-settings.pot" | awk '{print ($1>0)}')
check "non-ASCII msgids survived into the template" "$src_nonascii" "$pot_nonascii"

# ⛔ NOTHING IN THIS BINARY TRANSLATES AT RUNTIME. syn-settings prints only its
# record, and the record is never translated — see include/i18n.h. A `_()` here
# would be a column that changes language with the desktop, which the GUI
# matches on and `--rec | column -t` is meant to stay stable for.
badgettext=$(grep -n '[^A-Za-z_]_("' "$root"/src/*.c | tr '\n' ' ')
check "no _() in the C — the record is never translated" "" "$badgettext"

# ⛔ AND EVERY WORD THE WINDOW WILL DRAW IS REACHABLE BY A TRANSLATOR.
#
# This is the one that matters, and it has to RUN the program: the labels are
# not in the QML at all — they arrive in the record, and the window translates
# the `key` and `detail` columns at the draw site. A label the C forgot to mark
# is drawn in English forever with nothing to say so, because the template looks
# complete: the string was never in it to be missing.
#
# ⚠ THE WINDOW LOOKS UP THE WHOLE CELL. So whatever a cell ends up being has to
# BE a msgid, punctuation included — `rec_row("…%s.\t-", how)` appended a full
# stop that no msgid had, and those three sentences were English however well
# translated. That is what this check caught.
#
# ⚠ msgcat --no-wrap, and the value escaped the way a .po escapes it. A long
# msgid is WRAPPED across lines in the template and one containing a quote is
# written \" — grepping for the raw string finds neither, and the check reports
# strings that are perfectly reachable.
if [ -x "$BIN" ]; then
    # ⚠ THE TEMPLATE JUST GENERATED, NOT THE CHECKED-IN ONE. Reading the
    # committed .pot asks "was this string ever marked", which stays true after
    # someone deletes the N_() — the answer has to come from the sources as they
    # are now. (Verified: unmarking one label fails this; against the committed
    # copy it passed.)
    msgcat --no-wrap -o "$tmp/flat.pot" "$tmp/syn-settings.pot" 2>/dev/null
    for pane in display region time power system network bluetooth kernel \
                apps ai speech fprint assistant; do
        "$BIN" --rec "$pane" 2>/dev/null | awk -F'\t' '
            NR == 1 { for (i = 1; i <= NF; i++)
                          if ($i == "key" || $i == "detail" ||
                              $i == "role" || $i == "covers") w[i] = 1
                      next }
            { for (i in w) if ($i != "" && $i != "-") print $i }'
    done | sort -u | while IFS= read -r v; do
        # Data, not words: a path, anything carrying a number, a bare token.
        case "$v" in /*) continue ;; *[0-9]*) continue ;; esac
        [ "${#v}" -lt 3 ] && continue
        # ⛔ STRUCTURALLY UNREACHABLE, EACH FOR A STATED REASON. A draw-time
        # lookup can only find a cell that IS a msgid, so a cell COMPOSED at
        # runtime never matches one however its parts are marked; and a command,
        # a unit name or a driver name is not a word anybody translates.
        case "$v" in
            "Bootloader: "*)         continue ;;  # composed: "Bootloader: %s (%s). %s."
            *" API key")             continue ;;  # composed: "%s API key"
            "nmcli "*)               continue ;;  # a command to type
            nvidia|nouveau|amdgpu|i915) continue ;;  # kernel driver names
            *.service|*.socket|*.timer|*.target|*.mount|*.path) continue ;;
            *-sleep-hook|*-gpu-sleep)   continue ;;  # systemd unit names
            *.conf|*.desktop)        continue ;;  # file names
        esac
        # ⛔ A PREFIX COUNTS, BECAUSE THIS CODE COMPOSES CELLS BY APPENDING.
        # kernel.c builds its detail as `what` plus up to three clauses —
        # "  ⚠ headers MISSING …" and friends — so the drawn cell is a marked
        # sentence with more marked sentences stuck on the end, and demanding
        # the WHOLE cell be one msgid fails on a machine that happens to append
        # one. It did: this passed here and failed on a CachyOS box, because
        # those kernel rows only exist where those kernels are installed.
        #
        # ⚠ THE MORAL: a check that reads the RECORD reads THIS MACHINE. Rows
        # appear and disappear with the hardware, the installed packages and
        # the bootloader, so anything asserted about them has to hold for rows
        # this box will never emit.
        esc=$(printf '%s' "$v" | sed 's/\\/\\\\/g; s/"/\\"/g')
        if grep -qF "msgid \"$esc\"" "$tmp/flat.pot"; then continue; fi
        # …otherwise: is any msgid a prefix of it?
        awk -v cell="$v" '
            /^msgid "/ {
                m = substr($0, 8, length($0) - 8)
                gsub(/\\"/, "\"", m)
                if (length(m) > 8 && substr(cell, 1, length(m)) == m) { found = 1; exit }
            }
            END { exit !found }
        ' "$tmp/flat.pot" || printf '%s\n' "$v"
    done > "$tmp/unreachable"
    n=$(grep -c '' "$tmp/unreachable")
    sample=$(head -2 "$tmp/unreachable" | cut -c1-60 | tr '\n' '|')
    check "every word the window draws is in the template" "0" "$n${sample:+ — $sample}"
else
    printf '  skip  no binary, so the drawn labels were not checked\n'
fi

# ── 3. the language set matches the desktop's ─────────────
# A file manager in English on a German desktop reads as the application being
# broken, so the two lists are the same list.
mine=$(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS" | sort | tr '\n' ' ')
desk=$(grep -vE '^\s*#|^\s*$' /home/velle/SYNAPSE/synui/po/LINGUAS 2>/dev/null | sort | tr '\n' ' ')
if [ -n "$desk" ]; then
    check "po/LINGUAS matches the desktop's" "$desk" "$mine"
else
    printf '  skip  the desktop tree is not beside this one\n'
fi

absent=""
while IFS= read -r l; do
    [ -f "$root/po/$l.po" ] || absent="$absent $l"
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every language in po/LINGUAS has a .po" "" "$absent"

# ── 4. ⛔ THE KEYS BESIDE THE LABELS ARE UNTOUCHED ─────────
badkey=$(grep -n 'act: *I18n\.\|key: *I18n\.\|icon: *I18n\.' "$QML" | tr '\n' ' ')
check "no menu action, record key or icon name is translated" "" "$badkey"

# ⛔ AND THE PANE IDS ARE UNTOUCHED. Every row of the panes[] table carries the
# pane it opens in `id:` beside its label and blurb — SYNSETTINGS_PANE names it,
# the state file stores it, and every `root.pane === "…"` branch matches on it.
# A translated id is a sidebar whose entries open a pane that does not exist.
badid=$(grep -n 'id: *I18n\.' "$QML" | tr '\n' ' ')
check "no pane id is translated" "" "$badid"

# ── 5. the catalogs compile, and their plural rules are the desktop's ──
bad=""
while IFS= read -r l; do
    po="$root/po/$l.po"
    [ -f "$po" ] || continue
    msgfmt -c -o /dev/null "$po" 2>/dev/null || bad="$bad $l(msgfmt)"
    "$root/tools/po2json.py" "$po" -o "$tmp/$l.json" >/dev/null 2>&1 || bad="$bad $l(po2json)"
    # ⛔ `msginit -l ar` and `-l zh` cannot resolve those bare codes and fall
    # back to the template's English default — Arabic gets 2 plural forms
    # instead of 6, Chinese 2 instead of 1. Both are VALID rules that pass every
    # property check, so the only way to catch it is to compare against the
    # reviewed set next door.
    ref="/home/velle/SYNAPSE/synui/po/$l.po"
    if [ -f "$ref" ]; then
        a=$(plural_of "$po"); b=$(plural_of "$ref")
        [ -n "$a" ] && [ "$a" = "$b" ] || bad="$bad $l(plural)"
    fi
done < <(grep -vE '^\s*#|^\s*$' "$root/po/LINGUAS")
check "every catalog compiles and uses the desktop's plural rule" "" "$bad"

echo
if [ "$fails" -eq 0 ]; then echo "all syn-settings translation checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
