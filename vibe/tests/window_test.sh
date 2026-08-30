#!/bin/bash
# window_test.sh — the assistant window, loaded for real and measured.
#
# ⛔ WHAT THIS EXISTS TO CATCH. The header shipped as one Row anchored to the
# right edge of the window. That fits at 820 wide and runs off the LEFT at 420:
# dragged to its minimum the window drew `efault ▾` where the persona chip was,
# and the model name — anchored between the left edge and a Row wider than the
# window — had negative width and was not drawn at all. Nothing failed, nothing
# was logged, and every static check passed, because a QML Row is perfectly
# happy to be wider than its parent.
#
# So the header is MEASURED here, at the size the window actually opens at.
#
# ⚠ `ReferenceError|TypeError`, NOT `Error:`. A QML file with a missing property
# loads, draws, and reports it as a warning on stderr; grepping for "Error:"
# finds nothing and the test goes green over a window that came up empty.
#
# ⚠ QT_ASSUME_STDERR_HAS_CONSOLE=1 or console.log() prints NOTHING and every
# check below reads as an empty answer — a green suite that tested silence.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

HERE=$(cd "$(dirname "$0")/.." && pwd)
QML="$HERE/data/vibe.qml"
fails=0

check() {  # check <what> <want> <got>
    if [ "$2" = "$3" ]; then
        echo "  ok    $1"
    else
        echo "  FAIL  $1"
        echo "        want: $2"
        echo "        got:  $3"
        fails=$((fails + 1))
    fi
}

if ! command -v quickshell >/dev/null 2>&1; then
    echo "  skip  quickshell not installed, cannot load the window"
    exit 0
fi

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/home/.config/synui" "$T/run"

# ⛔ THE DESKTOP FONT IS THE CONDITION, AND WITHOUT IT THIS TEST IS THEATRE.
# Offscreen Qt defaults to a narrow sans at 100%, and the header that shipped
# FITS under those: measured against the default font the broken layout came
# back at ctrls.x = 118, comfortably inside the window. velle's desktop is
# JetBrains Mono at 115% — every label half again as wide — and the same header
# came back at ctrls.x = -8, drawn off the left edge, which is the screenshot.
# A window test that does not wear the desktop's font tests a window nobody has.
printf 'family=JetBrains Mono\nsize=12\nmono=yes\nscale=115\n' \
    > "$T/home/.config/synui/font.state"

# A stand-in engine. It speaks the protocol and loads no model: what is under
# test is the window, and a window that needs a GPU to be checked is a window
# nobody checks.
cat > "$T/vibe" <<'STUB'
#!/bin/bash
printf 'S\tbackend\tsynapd\n'
printf 'S\tmodel\tsynapd (local)\n'
printf 'S\tcloud\tno\n'
printf 'S\tmode\tauto\n'
printf 'S\tpersona\tdefault\n'
printf 'S\tpersonas\tdefault coach mentor\n'
printf 'V\tspeak\tyes\n'
printf 'V\tlisten\tyes\n'
printf 'V\treading\tno\n'
printf 'V\twake\toff\n'
printf 'P\ttodos\t[{"id":1,"text":"buy milk","status":"todo","prio":2,"due":"","project":"inbox"},{"id":2,"text":"ship 25","status":"done","prio":1,"due":"","project":"inbox"}]\n'
printf 'P\thabits\t[{"id":1,"name":"read","icon":"*","streak":3,"today":true,"week":"..#.#.#"}]\n'
printf 'P\tgoals\t[{"id":1,"title":"ship the release","progress":40}]\n'
printf 'P\tstats\t{"total":2,"done":1,"active":1,"overdue":0,"today_done":1,"completion_rate":50.0}\n'
cat > /dev/null
STUB
chmod +x "$T/vibe"

# The probe: the shipping file, with a timer appended that reports and quits.
# ⚠ The report is taken from the REAL file — a replica of the header would be a
# second layout, and the one that goes wrong is always the one nobody copied.
probe() {  # probe <width> <height>
    awk -v w="$1" -v h="$2" 'BEGIN{RS="\0"} {
        n = match($0, /}[ \t\r\n]*$/)
        printf "%s\n    Component.onCompleted: { root.width = %s; root.height = %s }\n    Timer { running: true; interval: 1500; repeat: false; onTriggered: {\n", substr($0,1,n-1), w, h
        printf "        console.log(\"WIDTH=\"  + root.width)\n"
        printf "        console.log(\"HEAD=\"   + Math.round(ctrls.x + ctrls.width) + \"/\" + Math.round(head.width))\n"
        printf "        console.log(\"LEFT=\"   + Math.round(ctrls.x))\n"
        printf "        console.log(\"BURGER=\" + Math.round(burger.x + burger.width))\n"
        printf "        console.log(\"PANEL=\"  + root.panelOn)\n"
        printf "        console.log(\"TODOS=\"  + todos.count + \",\" + habits.count + \",\" + goals.count)\n"
        printf "        console.log(\"GOAL=\"   + (goals.count ? goals.get(0).progress : -1))\n"
        printf "        console.log(\"FS=\"     + fsBtn.visible + \",\" + Math.round(fsBtn.width))\n"
        printf "        console.log(\"FSLABEL=\" + root.roomForLabels)\n"
        # ⛔ A MENU THAT ELIDES ITS OWN LABELS. `Hide the companion p…` is a row
        # that has stopped saying what it does — and a Menu here does NOT grow
        # for its rows, so the width is computed from the text and every part of
        # the padding has to be counted. It came out short three separate times,
        # each looking fixed until it was looked at.
        printf "        var bad = 0\n"
        printf "        var menus = [mainMenu, modeMenu]\n"
        printf "        for (var mi = 0; mi < menus.length; mi++) {\n"
        printf "            menus[mi].measure()\n"
        printf "            for (var i = 0; i < menus[mi].count; i++) {\n"
        printf "                var it = menus[mi].itemAt(i)\n"
        printf "                if (it && it.text && it.contentItem.width < it.contentItem.contentWidth) bad++\n"
        printf "            } }\n"
        printf "        console.log(\"MENUFIT=\" + bad)\n"
        printf "        Qt.quit() } }\n%s", substr($0,n)
    }' "$QML" > "$T/probe.qml"

    HOME="$T/home" XDG_RUNTIME_DIR="$T/run" VIBE_BIN="$T/vibe" \
    QT_QPA_PLATFORM=offscreen GSETTINGS_BACKEND=memory \
    QT_ASSUME_STDERR_HAS_CONSOLE=1 \
        timeout 40 quickshell -p "$T/probe.qml" 2>&1
}

field() { sed -n "s/.*$1=\([^ ]*\).*/\1/p" <<<"$2" | head -1; }

echo "the assistant window"

# ── the small box, which is the size it opens at ────────────────────────────
small=$(probe 460 380)

bad=$(grep -cE 'ReferenceError|TypeError|Cannot assign|is not a type' <<<"$small")
check "the window loads with no QML errors" "0" "$bad"

# ⛔ THE ONE THAT WOULD HAVE CAUGHT IT. `ctrls.x` is where the right-hand
# controls begin; below zero they are drawn off the left edge of the window and
# the first of them is cut in half, which is exactly what shipped.
left=$(field LEFT "$small")
if [ -n "$left" ] && [ "$left" -gt 0 ] 2>/dev/null; then
    check "the header controls start inside the window at 460 wide" "yes" "yes"
else
    check "the header controls start inside the window at 460 wide" "yes" "no (x=$left)"
fi

# …and clear of the menu button, so the model name between them has room.
burger=$(field BURGER "$small")
if [ -n "$left" ] && [ -n "$burger" ] && [ "$left" -gt "$burger" ] 2>/dev/null; then
    check "the model name has room between the menu and the controls" "yes" "yes"
else
    check "the model name has room between the menu and the controls" "yes" \
          "no (menu ends $burger, controls start $left)"
fi

check "the controls end at the window's right edge" "$(field HEAD "$small" | cut -d/ -f2)" \
      "$(field HEAD "$small" | cut -d/ -f1 | awk '{print $1 + 12}')"

check "full size is a button, and it is on screen" "true" \
      "$(field FS "$small" | cut -d, -f1)"

# ⛔ A BUTTON IS ITS OWN LABEL. `⤢` alone in the dim colour every other chip
# uses is what shipped, and velle looked at it and asked for a full-size button
# — so at the size the window opens at, it carries the word.
check "full size carries its word at the default size" "true" \
      "$(field FSLABEL "$small")"

check "no menu row is cut off by its own menu" "0" "$(field MENUFIT "$small")"

check "the companion records reached the window" "2,1,1" "$(field TODOS "$small")"
check "a goal's progress survives the wire" "40" "$(field GOAL "$small")"

# ⚠ NO PANEL ON THE SMALL BOX. 320 pixels of sidebar in a 460-pixel window is
# the window; the ☰ menu is the way in at this size and it names every list.
check "no companion panel on the small box" "false" "$(field PANEL "$small")"

# ── the smallest the window goes ────────────────────────────────────────────
#
# ⚠ THE MINIMUM IS THE CASE THAT BROKE. velle's screenshot was the window
# dragged until it stopped, which is this size and not the default one.
tiny=$(probe 360 260)
left=$(field LEFT "$tiny")
if [ -n "$left" ] && [ "$left" -gt 0 ] 2>/dev/null; then
    check "the header fits at the window's minimum width" "yes" "yes"
else
    check "the header fits at the window's minimum width" "yes" "no (x=$left)"
fi
check "full size is still reachable at the minimum" "true" \
      "$(field FS "$tiny" | cut -d, -f1)"

# ── full size ───────────────────────────────────────────────────────────────
big=$(probe 1200 800)
bad=$(grep -cE 'ReferenceError|TypeError|Cannot assign|is not a type' <<<"$big")
check "the window loads at full size with no QML errors" "0" "$bad"
check "the companion panel opens where there is room" "true" "$(field PANEL "$big")"

left=$(field LEFT "$big")
if [ -n "$left" ] && [ "$left" -gt 0 ] 2>/dev/null; then
    check "the header still fits at 1200 wide" "yes" "yes"
else
    check "the header still fits at 1200 wide" "yes" "no (x=$left)"
fi

echo ""
if [ "$fails" -eq 0 ]; then echo "all checks passed"; else echo "$fails check(s) failed"; exit 1; fi
