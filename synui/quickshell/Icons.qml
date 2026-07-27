pragma Singleton

import QtQuick

/*
 * Icons.qml -- the Nerd Font glyphs the bar draws.
 *
 * Written as \u escapes, NOT as literal characters. These live in the Unicode
 * Private Use Area, and carrying them as literals proved fragile: two earlier
 * versions of this file lost every glyph in transit and shipped modules whose
 * icon was the empty string -- CPU and memory rendered as a bare percentage,
 * and Bluetooth, being icon-only, collapsed to zero width and vanished. The
 * escapes are plain ASCII and survive any editor, diff or transport, which is
 * worth the loss of readability.
 *
 * Codepoints come from synui/config/waybar/config.jsonc, so this bar is
 * glyph-for-glyph identical to the waybar it replaces. Needs the
 * "Symbols Nerd Font Mono" family (ttf-nerd-fonts-symbols-mono).
 */
QtObject {
    // -- System --
    readonly property string cpu:    "\uF4BC"
    readonly property string memory: "\uF2DB"
    readonly property string game:   "\uF11B"

    // -- Audio --
    readonly property string volMuted: "\uF026"
    readonly property string volLow:   "\uF026"
    readonly property string volMed:   "\uF027"
    readonly property string volHigh:  "\uF028"

    // Mixer only: a speaker over a microphone row says the wrong thing.
    readonly property string mic:      "\uF130"
    readonly property string micMuted: "\uF131"

    // -- Bluetooth --
    readonly property string btOff:       "\uF293"
    readonly property string btOn:        "\uF293"
    readonly property string btConnected: "\uF294"

    // -- Network --
    readonly property string wifi:            "\uF1EB"
    readonly property string netDisconnected: "\uF127"
    readonly property string netDisabled:     "\uF05E"

    // -- Battery --
    readonly property string batEmpty:    "\uF244"
    readonly property string batQuarter:  "\uF243"
    readonly property string batHalf:     "\uF242"
    readonly property string batThreeQtr: "\uF241"
    readonly property string batFull:     "\uF240"
    readonly property string batCharging: "\uF0E7"

    // -- Brightness (OSD only; there is no bar module for it) --
    readonly property string brightnessLow:  "\uF186"
    readonly property string brightnessHigh: "\uF185"

    // -- Media (MPRIS) --
    readonly property string mediaPlay:  "\uF04B"
    readonly property string mediaPause: "\uF04C"
    readonly property string mediaNext:  "\uF051"
    readonly property string mediaPrev:  "\uF048"
    readonly property string mediaNote:  "\uF001"

    // U+F0200 sits outside the BMP, so QML needs the surrogate pair.
    readonly property string ethernet: "\uDB80\uDE00"
}
