#!/usr/bin/env bash
# clock_faces.sh — the analog clock's faces, in the five places that list them.
#
# ⛔ FIVE LISTS THAT HAVE TO AGREE, and none of them fails loudly on its own:
#
#   src/synui.h          the enum, and its order
#   src/ctlpanel.c       the option NAMES the panel writes to synuirc — folded
#                        to lower case by ctl_format(), so these ARE the legal
#                        config values and renaming one renames a setting
#   src/config.c         the parser that reads them back
#   quickshell/BarConfig.qml    the validator between the file and the widget
#   quickshell/widgets/AnalogClock.qml   the validator in the widget itself
#
# Add a face to four of the five and it is silently unreachable: the panel
# offers it, the file records it, and one of the two QML guards falls back to
# `minimal` with nothing said. That is exactly how it would be found — a person
# picks the new face and the clock does not change.
#
# ⚠ The panel's names are NOT translated, and this checks that too. They are
# config values wearing a capital letter; a translated one writes a word
# config.c cannot parse. See src/ctlpanel.c's own note on the array.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
fails=0
ok()  { printf '  ok    %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1" >&2; fails=$((fails + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 — expected [$2], got [$3]"; fi; }

echo "the analog clock's faces"

# The enum is the order everything else is compared against.
# ⚠ The first entry carries ` = 0`, so the pattern has to allow it — a version
# of this that did not silently dropped `minimal` and then reported every other
# list as having one too many.
enum=$(sed -n 's/^ *SYN_CLOCK_FACE_\([A-Z]*\)\( = [0-9]*\)\?,$/\1/p' "$root/src/synui.h" |
       grep -v '^COUNT$' | tr 'A-Z' 'a-z' | tr '\n' ' ')
enum=${enum% }
[ -n "$enum" ] && ok "the enum lists: $enum" || bad "no faces found in src/synui.h"

# ⚠ THE PANEL'S NAMES, FOLDED — which is what ctl_format() persists.
panel=$(sed -n '/ctl_names_clock_face\[\]/,/;/p' "$root/src/ctlpanel.c" |
        grep -o '"[A-Za-z]*"' | tr -d '"' | tr 'A-Z' 'a-z' | tr '\n' ' ')
panel=${panel% }
check "the control panel offers exactly those, in that order" "$enum" "$panel"

# The parser, in whatever order it happens to test them.
# ⚠ The arms are column-aligned, so the spacing between `)` and `==` varies.
parsed=$(grep -oE 'strcmp\(val, "[a-z]+"\) +== 0\) cfg->widget_clock_face' "$root/src/config.c" |
         sed 's/.*"\([a-z]*\)".*/\1/' | sort | tr '\n' ' ')
parsed=${parsed% }
want=$(printf '%s\n' $enum | sort | tr '\n' ' '); want=${want% }
check "config.c parses every one of them back" "$want" "$parsed"

# Both QML guards. ⚠ A face missing from EITHER is a face that silently becomes
# `minimal`, and they are two separate lists in two files.
for f in quickshell/BarConfig.qml quickshell/widgets/AnalogClock.qml; do
    qml=$(grep -o '\["minimal"[^]]*\]' "$root/$f" | head -1 |
          grep -o '"[a-z]*"' | tr -d '"' | sort | tr '\n' ' ')
    qml=${qml% }
    check "$(basename "$f") accepts every face" "$want" "$qml"
done

# ⛔ AND NOT TRANSLATED. N_() around one of these would write a translated word
# into synuirc that config.c cannot read back — the row would appear to work and
# the setting would be lost at the next start.
marked=$(sed -n '/ctl_names_clock_face\[\]/,/;/p' "$root/src/ctlpanel.c" | grep -c 'N_(' || true)
check "the option names are config values, not translated labels" "0" "$marked"

# The documented value list in config.c's own header comment, which is what
# somebody editing synuirc by hand reads.
doc=$(grep -c "widget_clock_face = $(printf '%s' "$enum" | tr ' ' '|')" "$root/src/config.c")
[ "$doc" -ge 1 ] && ok "config.c documents the full list" \
                 || bad "config.c's comment does not list every face"

echo ""
if [ "$fails" -eq 0 ]; then echo "all clock-face checks passed"; else echo "$fails failed"; fi
exit $(( fails > 0 ))
