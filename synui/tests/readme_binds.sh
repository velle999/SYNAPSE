#!/usr/bin/env bash
# readme_binds.sh — every default keybinding is documented in the README
#
# The README's hotkey table is the only place most people ever read the
# bindings, and nothing kept it honest. It drifted: Super+Space moved from the
# command bar to rofi and the command bar moved to Super+=, and the table went
# on saying "Super+Space | Command bar" for both of them. Super+, / Super+. —
# the two moves that make the niri layout a niri layout — were never documented
# at all, and neither was Super+/ , the palette that lists the rest.
#
# None of that is a build error, and none of it is visible from inside synui.
# It is only visible by reading two files side by side, which is what this does.
#
# The check is one-directional on purpose: every combo in seed_default_binds()
# must appear in the table. It does NOT insist the table mention only real
# binds — rows like "Super (tapped alone)" and the workspace ranges are real
# behaviour that is not in that table, and a check that banned them would be
# fighting the documentation instead of verifying it.
#
# Usage: readme_binds.sh [path/to/config.c] [path/to/README.md]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
config=${1:-$here/../src/config.c}
readme=${2:-$here/../../README.md}

[ -f "$config" ] || { echo "  ABORT no config.c at $config"; exit 1; }
[ -f "$readme" ] || { echo "  ABORT no README.md at $readme"; exit 1; }

fails=0

# THE TABLE, not the whole README. Scoping matters more than it looks: the
# prose under the table mentions Super+Space and Super+= while explaining how
# to swap them, so a whole-file grep is satisfied by the explanation of a key
# that has no row. The table is what people scan; the table is what must be
# complete.
table=$(sed -n '/^| Key | Action |/,/^$/p' "$readme")
if [ "$(printf '%s\n' "$table" | grep -c '^|')" -lt 30 ]; then
    echo "  ABORT the hotkey table was not found in $readme (looked for a"
    echo "        '| Key | Action |' header) — every check below would be vacuous."
    exit 1
fi

# How a combo token is written in the table. The keysym names config.c uses are
# xkb's; the README writes what is printed on the key.
key_label() {
    case "$1" in
        super)     echo 'Super' ;;
        shift)     echo 'Shift' ;;
        ctrl)      echo 'Ctrl'  ;;
        alt)       echo 'Alt'   ;;
        return)    echo 'Return' ;;
        space)     echo 'Space' ;;
        escape)    echo 'Escape' ;;
        tab)       echo 'Tab' ;;
        backspace) echo 'Backspace' ;;
        delete)    echo 'Delete' ;;
        print)     echo 'Print' ;;
        equal)     echo '=' ;;
        # The keys the desktop-scale binds are printed with. Without
        # these they fell through to the default and the table had to
        # write `minus`, which is not what is on the key.
        minus)     echo '-' ;;
        plus)      echo '+' ;;
        slash)     echo '/' ;;
        question)  echo '?' ;;
        comma)     echo ',' ;;
        period)    echo '.' ;;
        # The arrow keys. Without these they fell through to the default and the
        # table had to write `up` in lower case to satisfy this check, while the
        # prose two rows below it wrote `Up` — the label is what is printed on
        # the key, and that is capitalised.
        left)      echo 'Left' ;;
        right)     echo 'Right' ;;
        up)        echo 'Up' ;;
        down)      echo 'Down' ;;
        [a-z])     echo "${1^^}" ;;
        *)         echo "$1" ;;
    esac
}

# The table is markdown, so a combo is a run of `code` spans joined by +.
combo_pattern() {
    local out="" tok
    local IFS=+
    for tok in $1; do
        out="$out\`$(key_label "$tok")\`+"
    done
    echo "${out%+}"
}

# Combos that are real but are documented as a CLASS rather than one row each.
# Each is listed with the phrase that covers it, so an exemption cannot quietly
# become "we stopped documenting this".
#
# Several phrases per combo, separated by ';' — ANY of them counts. The README
# writes these in prose ("Volume keys") and the wiki's Keybindings page writes
# the xkb symbols (`XF86AudioRaiseVolume`); both are correct for their audience,
# and this file is run against both.
declare -A COVERED=(
    # Super+? IS Super+Shift+/ on a US layout, and config.c binds both spellings
    # because xkb hands over the shifted keysym. Docs write the one a hand
    # actually presses, which is the right call for a table people read.
    [super+shift+question]='`Super`+`?`;`Super`+`/`'
    [xf86monbrightnessup]='Brightness keys;XF86MonBrightnessUp'
    [xf86monbrightnessdown]='Brightness keys;XF86MonBrightness'
    [xf86audioraisevolume]='Volume keys;XF86AudioRaiseVolume'
    [xf86audiolowervolume]='Volume keys;LowerVolume'
    [xf86audiomute]='Volume keys;Mute'
    # The laptop screen key. Same split as the volume and brightness keys:
    # the README writes what is printed on the key, the wiki writes the xkb
    # symbol, and either satisfies this.
    [xf86display]='Display key;XF86Display'
    # Super+Ctrl++ IS Super+Ctrl+Shift+= on a US layout and config.c binds
    # both spellings, because xkb hands over the SHIFTED keysym. The docs
    # write the one a hand actually presses, as they do for Super+? above.
    [super+ctrl+shift+plus]='`Super`+`Ctrl`+`=`'
)

# Pull the combos out of the seed table: { "super+x", "action" },
combos=$(sed -n '/^static void seed_default_binds/,/^}/p' "$config" |
         grep -oE '\{ *"[a-z0-9+]+" *,' |
         sed -e 's/{ *"//' -e 's/" *,//')

n=$(printf '%s\n' "$combos" | grep -c .)
echo "=== $n default binds ==="
if [ "$n" -lt 40 ]; then
    echo "  ABORT only $n combos parsed out of $config — the extractor is broken,"
    echo "        and every check below would pass for the wrong reason."
    exit 1
fi

for combo in $combos; do
    if [ -n "${COVERED[$combo]+set}" ]; then
        hit=""
        while IFS= read -r phrase; do
            [ -n "$phrase" ] || continue
            if grep -qF -- "$phrase" <<<"$table"; then hit=$phrase; break; fi
        done <<<"${COVERED[$combo]//;/$'\n'}"
        if [ -n "$hit" ]; then
            printf '  ok    %-26s (covered by "%s")\n' "$combo" "$hit"
        else
            printf '  FAIL  %-26s — no row covers it (tried: %s)\n' \
                   "$combo" "${COVERED[$combo]}"
            fails=$((fails + 1))
        fi
        continue
    fi

    pat=$(combo_pattern "$combo")
    if grep -qF -- "$pat" <<<"$table"; then
        printf '  ok    %-26s %s\n' "$combo" "$pat"
    else
        printf '  FAIL  %-26s not in the hotkey table (looked for %s)\n' "$combo" "$pat"
        fails=$((fails + 1))
    fi
done

# The rebind syntax the README tells people to use has to be the one the parser
# accepts. It documented `bind = <combo>, <action>` for a parser that splits on
# WHITESPACE, so the comma landed inside the combo and every example in the
# README was a bind that logged "bad key" and did nothing.
echo ""
echo "=== rebind syntax ==="
if grep -qE 'bind = <combo>,' "$readme"; then
    echo "  FAIL  the README documents 'bind = <combo>, <action>' — config.c splits"
    echo "        the combo from the action on WHITESPACE, so the comma is parsed"
    echo "        as part of the key name and the bind is silently dropped."
    fails=$((fails + 1))
else
    echo "  ok    no comma in the documented bind syntax"
fi

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
