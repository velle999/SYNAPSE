pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * Theme.qml — the SYNAPSE bar palette.
 *
 * Colours come from ~/.config/synui/theme.json, which synui-apply-theme writes
 * on every theme switch alongside the waybar CSS. Before that file existed this
 * palette was hardcoded, and the result was a win95 desktop — silver and navy
 * everywhere — with one neon magenta strip across it that no theme switch could
 * move.
 *
 * The bar CANNOT read the generated waybar-style.css instead: QML's colour type
 * parses "#RRGGBB" and SVG names but not CSS rgba(), so every themed colour
 * would silently fall back and the bug would look fixed while doing nothing.
 * Hence a JSON of plain numbers, written atomically by rename.
 *
 * Every colour falls back to the original SYNAPSE palette, so a system that has
 * never applied a theme — a fresh install, or the live ISO — looks exactly as
 * it did before this file learned to read anything.
 */
QtObject {
    id: root

    // Parsed palette, or {} when the file is missing or malformed.
    property var p: ({})

    readonly property bool isLight: p.scheme === "light"

    // QtObject has no default property, so the watcher is held by a named one.
    property FileView paletteFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/theme.json"
        watchChanges: true
        // The generator writes a temp file and renames it, so this fires once,
        // with a complete file — never on a half-written palette.
        onFileChanged: reload()
        onLoaded: {
            try {
                root.p = JSON.parse(this.text())
            } catch (e) {
                // A malformed palette must not take the bar down, and must not
                // leave it half-themed either: fall back wholesale.
                root.p = ({})
            }
        }
        onLoadFailed: root.p = ({})
    }

    // Reading root.p inside these makes every colour below a live binding on
    // it, so a theme switch repaints the bar without a restart.
    function themed(key, r, g, b, a) {
        const c = root.p[key]
        return (c && c.length === 3) ? Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, a)
                                     : Qt.rgba(r / 255, g / 255, b / 255, a)
    }
    function pick(dark, light) { return root.isLight ? light : dark }

    // ── Surfaces ─────────────────────────────────────────
    readonly property real  bgAlpha:    p.barAlpha   !== undefined ? p.barAlpha   : 0.85
    readonly property real  popupAlpha: p.popupAlpha !== undefined ? p.popupAlpha : 0.97

    readonly property color bg:      themed("bar",   11, 11, 20, bgAlpha)
    readonly property color bgSolid: themed("bar",   11, 11, 20, 1.0)
    readonly property color popupBg: themed("popup", 11, 11, 20, popupAlpha)

    // The washes track the colours they tint, so a hover on a Gruvbox bar is
    // Gruvbox rather than the old cyan.
    readonly property color hoverBg:  themed("glyph",   5, 217, 232, 0.15)
    readonly property color activeBg: themed("accent", 255,  41, 109, 0.20)

    // ── Ink ──────────────────────────────────────────────
    readonly property color fg:      p.fg      ? Qt.color(p.fg)      : "#c8e3ee"
    readonly property color cyan:    themed("glyph",   5, 217, 232, 1.0)   // module glyphs
    readonly property color magenta: themed("accent", 255,  41, 109, 1.0)  // accent + underline
    readonly property color yellow:  p.clockFg ? Qt.color(p.clockFg) : "#ffd319"

    // Not themed, because these carry MEANING rather than style: green is
    // charging, red is a battery about to die. They only switch on light/dark,
    // where the pastels are unreadable on a pale bar — the same reason
    // synui-apply-theme darkens the clock's yellow for light schemes.
    readonly property color fgDim:  pick("#3a4a52", "#6b7280")
    readonly property color blue:   pick("#4dabff", "#1d4ed8")
    readonly property color green:  pick("#a6e3a1", "#166534")
    readonly property color red:    pick("#f38ba8", "#b91c1c")
    readonly property color orange: pick("#f9e2af", "#8a6d00")

    // ── Metrics ──────────────────────────────────────────
    // 28px matches the waybar height the compositor already lays out around:
    // synui's launcher (launcher.c) draws "◢ SYNAPSE" over the bar's top-left
    // corner, and its geometry assumes this height.
    readonly property int  barHeight:     28
    readonly property int  accentHeight:  2      // the accent underline
    readonly property int  modulePadH:    9
    readonly property int  moduleGap:     2
    readonly property int  radius:        4

    // Module pills are inset from the bar's height so the hover wash reads as a
    // button rather than a full-height stripe. 20 of 28 leaves 4px above and
    // below; the radius is half the height, which is what makes it a pill and
    // not a rounded box.
    readonly property int  pillHeight:    20
    readonly property int  pillRadius:    10

    readonly property string fontFamily:  "DejaVu Sans Mono"
    readonly property string iconFamily:  "Symbols Nerd Font Mono"
    readonly property int    fontSize:    12
    readonly property int    iconSize:    13

    // Shared easing so every module animates identically.
    readonly property int  animFast:   120
    readonly property int  animNormal: 180
}
