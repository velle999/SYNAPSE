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

    # ⛔ THE FINGERPRINT PANE IS FAKED, AND THAT IS NOT TIDINESS — IT IS THE
    # ONLY REASON THIS CHECK SEES THOSE TEN ROWS AT ALL.
    #
    # fprint.c emits one row per finger, but ONLY on a machine where
    # fprintd-list reports a reader. Every box without one takes an early
    # return two rows in, so ten drawn labels and two sentences sat unmarked
    # through two releases: this check passed here, and failed on velle's
    # ThinkPad — where the reader is enrolled — with 13 unreachable strings,
    # taking `syn-update` down mid-build for every component after it.
    #
    # ⚠ SHADOWED BY PREPENDING TO PATH, WHICH WORKS IN BOTH DIRECTIONS. On a
    # box with no fprintd it ADDS the command; on one that has it, have_cmd()
    # walks PATH in order and finds this first. Either way the pane emits the
    # same twelve rows and this check reads the same words.
    #
    # ⚠ THE MORAL, AGAIN: a check that reads the RECORD reads THIS MACHINE.
    # Rows appear and disappear with the hardware, the installed packages and
    # the bootloader. Anything asserted about them has to hold for rows this
    # box will never emit — which means faking the reader, not hoping for one.
    fake=$tmp/fakebin; mkdir -p "$fake"
    cat > "$fake/fprintd-list" <<'FPL'
#!/bin/sh
echo "found 1 devices"
echo "Device at /net/reactivated/Fprint/Device/0"
echo "Fingerprints for user tester on Synaptics (press):"
echo " - #0: right-index-finger"
echo " - #1: left-thumb"
FPL
    printf '#!/bin/sh\nexit 0\n' > "$fake/fprintd-enroll"
    chmod +x "$fake/fprintd-list" "$fake/fprintd-enroll"
    PATH="$fake:$PATH"

    # ...and it really is emitting them, or the shadowing silently did nothing
    # and this check is back to reading whatever hardware happens to be here.
    nfinger=$("$BIN" --rec fprint 2>/dev/null | grep -c '^finger	')
    check "the fingerprint pane emits its ten rows on any machine" "10" "$nfinger"

    # ⛔ AND THE FOUR ACCELERATOR ROWS, FOR THE SAME REASON. ai.c lists one row
    # per ggml backend library present, so which of them exist is decided by
    # which synapse-llama package this machine happens to have — and the two
    # labels that were NOT marked were exactly the two with no library here.
    # This passed, and failed on velle's box where libggml-vulkan.so exists.
    # Four stub files, four rows, every machine.
    libs=$tmp/libdir; mkdir -p "$libs"
    for so in cuda vulkan hip cpu; do : > "$libs/libggml-$so.so"; done
    export SYN_SETTINGS_LIBDIR="$libs"
    naccel=$("$BIN" --rec ai 2>/dev/null | grep -c '^accel	')
    check "the AI pane emits all four accelerator rows on any machine" "4" "$naccel"

    # ⛔ AND THE apps PANE IS COLLECTED TWICE, THE SECOND TIME WITH NO CONFIG.
    #
    # Third time this class has bitten: the fingerprint rows needed a stubbed
    # fprintd and the accelerator rows a stubbed library directory, both because
    # a pane emits different rows on different machines. The apps pane does it
    # from the CONFIG rather than from the hardware — `terminal_current()` reads
    # synuirc, and only where no `terminal =` is set does it report the built-in
    # fallback in words instead of naming a file. Every developer box has that
    # knob set, so the sentence was unreachable here and perfectly reachable in
    # a clean build root, where it failed the build.
    #
    # An empty HOME and XDG_CONFIG_HOME is what a fresh install looks like.
    empty=$tmp/noconfig; mkdir -p "$empty"

    for pane in display region time power system network bluetooth kernel \
                apps ai speech fprint assistant apps@fresh; do
        case "$pane" in
            apps@fresh) set -- --rec apps ;;
            *)          set -- --rec "$pane" ;;
        esac
        case "$pane" in
            apps@fresh) HOME="$empty" XDG_CONFIG_HOME="$empty" "$BIN" "$@" 2>/dev/null ;;
            *)          "$BIN" "$@" 2>/dev/null ;;
        esac | awk -F'\t' '
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
            # A MASKED ADDRESS, which is not a word and has no translation: the
            # Bluetooth pane's rows are keyed on the address, and the record
            # carries every address masked until --reveal. See addr_mask() in
            # src/util.c.
            *"•"*)                   continue ;;
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
# ── ⛔ AND THE SAME CLASS, WITHOUT RUNNING ANYTHING ────────────────────────
#
# The check above is the strong one, but it can only see rows THIS MACHINE
# emits — that is how ten finger labels survived two releases. This one reads
# the SOURCE: every string literal handed to rec_row() as an ARGUMENT is a cell
# a person reads, so it is either N_() or one of the tokens listed below, and
# there is no third category.
#
# ⚠ ARGUMENTS ONLY, NOT THE FORMAT. The format is the row's shape — tabs,
# `%s`, and the leading `kind` token — and xgettext would extract it whole.
# ⛔ WHICH IS ALSO A TRAP: prose written INTO the format string is drawn and
# reaches no template, because the msgid is the whole format and the cell is
# only part of it. network.c had a 300-character sentence there. The check
# below also refuses a run of English inside a rec_row format.
bare=$(python3 - "$root" <<'PYEOF'
import re, sys, glob, os
root = sys.argv[1]

# ⛔ EVERY ENTRY HERE IS A THING A PROGRAM READS, NOT A WORD ANYBODY TRANSLATES.
#   current            — data/syn-settings.qml compares `f[2] === "current"`
#   toggle:/choice:/mode:/enroll:/forget:/unavailable:  — the action column
#   *.service/.socket/.timer  — systemd unit names
#   wifi               — an argument to strcmp, not a cell
#   override           — a state token the window colours on
#   synapse, computer  — the DEFAULT wake words, i.e. somebody's data
ALLOW = {"current", "wifi", "override", "synapse, computer"}
PREFIX = ("toggle:", "choice:", "mode:", "enroll:", "forget:", "unavailable:")
SUFFIX = (".service", ".socket", ".timer", ".target", ".conf", ".desktop")

def split_args(call):
    args, depth, cur, k = [], 0, "", 0
    while k < len(call):
        c = call[k]
        if c == '"':
            cur += c; k += 1
            while k < len(call) and call[k] != '"':
                if call[k] == "\\": cur += call[k]; k += 1
                cur += call[k]; k += 1
            cur += '"'; k += 1; continue
        if c == "(": depth += 1
        if c == ")": depth -= 1
        if c == "," and depth == 0:
            args.append(cur.strip()); cur = ""; k += 1; continue
        cur += c; k += 1
    args.append(cur.strip())
    return args

bad = []
for f in sorted(glob.glob(os.path.join(root, "src", "*.c"))):
    s = open(f, encoding="utf-8").read()
    rel = os.path.basename(f)
    for m in re.finditer(r"rec_row\(", s):
        i, depth, j = m.end(), 1, m.end()
        while j < len(s) and depth:
            if s[j] == "(": depth += 1
            elif s[j] == ")": depth -= 1
            elif s[j] == '"':
                j += 1
                while j < len(s) and s[j] != '"':
                    if s[j] == "\\": j += 1
                    j += 1
            j += 1
        call = s[i:j - 1]
        line = s[:m.start()].count("\n") + 1
        args = split_args(call)

        # the format: no run of English prose in it
        fmt = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', args[0]))
        if re.search(r"[A-Za-z]{3,} [a-z]{3,} [a-z]{3,}", fmt):
            bad.append("%s:%d: prose in the rec_row FORMAT" % (rel, line))

        for a in (" ".join(x.split()) for x in args[1:]):
            outside = re.sub(r'N_\((?:[^()"]|"(?:[^"\\]|\\.)*")*\)', "X", a)
            for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', outside):
                if len(lit) < 4 or lit in ALLOW: continue
                if lit.startswith(PREFIX) or lit.endswith(SUFFIX): continue
                bad.append("%s:%d: %s" % (rel, line, lit[:50]))
print("\n".join(bad))
PYEOF
)
check "every rec_row cell is N_() or a token a program reads" "" "$(printf '%s' "$bare" | tr '\n' ' ')"

    n=$(grep -c '' "$tmp/unreachable")
    sample=$(head -2 "$tmp/unreachable" | cut -c1-60 | tr '\n' '|')
    check "every word the window draws is in the template" "0" "$n${sample:+ — $sample}"

    # ⛔ AND EVERY CLOUD BACKEND SPELLS ITS KEY LABEL OUT. That label used to be
    # composed — `"%s API key"` — which no draw-time lookup can ever match, so
    # it is a field in the BACKENDS table now. The field is NULL for the local
    # backends, which never emit the row; a new CLOUD one added without it would
    # print "(null)" into the record instead of failing loudly.
    nokey=$(awk '
        /^static const struct .*BACKENDS\[\] = \{/ { inside = 1; next }
        inside && /^\};/ { exit }
        inside && /^\t\{ "/ {
            entry = $0
            while (entry !~ /(true|false) \},?$/ && (getline nxt) > 0) entry = entry nxt
            if (entry ~ /true \},?$/ && entry ~ /NULL/) {
                match(entry, /"[^"]+"/); print substr(entry, RSTART + 1, RLENGTH - 2)
            }
        }
    ' "$root/src/assistant.c" | tr '\n' ' ')
    check "every cloud backend spells out its API-key label" "" "$nokey"
else
    printf '  skip  no binary, so the drawn labels were not checked\n'
fi

# ── ⛔ AND NO DRAWN LABEL HIDES IN A STATIC TABLE ──────────────────────────
#
# The rec_row gate above reads the ARGUMENTS of each call, and `accel[i][1]` is
# not a literal — the words live in a table three lines up. Two of that table's
# four entries were unmarked for two releases because no check could see them:
# not the runtime one (this machine has no library for them) and not the static
# one (they are not literals at the call). So the tables are read too.
#
# ⚠ A TABLE ENTRY THAT IS ALL lower-case, digits, dots, colons or dashes is a
# TOKEN — a path, a unit name, a kind — and those are the record's own words.
# Anything else in a table that can feed a drawn column is a label.
tbl=$(python3 - "$root" <<'PYEOF'
import re, sys, glob, os
bad = []
for f in sorted(glob.glob(os.path.join(sys.argv[1], "src", "*.c"))):
    s = open(f, encoding="utf-8").read()
    for m in re.finditer(r"static const char \*const \w+\[\][^=]*=\s*\{(.*?)\n\s*\};", s, re.S):
        line = s[:m.start()].count("\n") + 1
        stripped = re.sub(r'N_\((?:[^()"]|"(?:[^"\\]|\\.)*")*\)', "X", m.group(1))
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', stripped):
            if len(lit) < 3 or lit.startswith("/"): continue
            if re.match(r"^[a-z0-9_.:-]+$", lit): continue
            bad.append("%s:%d: %s" % (os.path.basename(f), line, lit[:40]))
print(" ".join(bad))
PYEOF
)
check "no drawn label sits unmarked in a static table" "" "$tbl"

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
