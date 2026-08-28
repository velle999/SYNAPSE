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
    // Named so weatherFor() below can reach the glyphs beside it.
    id: root

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

    // The equalizer row in the mixer. Sliders rather than a bar chart: the
    // panel it opens is ten sliders, and the glyph should look like the thing
    // it leads to.
    readonly property string equalizer: "\uF1DE"

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

    // -- Screen recording --
    // fa-circle and fa-stop, from the same fa block as the media glyphs below,
    // so the recording pill sits in the same visual family as the rest of the
    // bar rather than importing a second icon style for one module.
    // -- Updates (syn-update's ping) --
    // nf-fa-arrow_circle_o_down. Chosen over a bare arrow so it reads as an
    // indicator at 14px rather than as part of the module beside it.
    readonly property string updates:    "\uF01A"

    // The assistant. ⚠ A SPEECH BUBBLE AND NOT A ROBOT: F544 (robot) and
    // F5DC (brain) are the obvious glyphs for this and NEITHER IS IN
    // Symbols Nerd Font Mono, which is the only icon font the ISO ships.
    // A missing glyph is a button you cannot see and can still press.
    readonly property string assistant:  "\uF075"

    /* -- Weather --
     *
     * The nf-weather block, one glyph per icon NAME weather.c publishes — so
     * the picture on the bar and the one the lock screen draws in cairo come
     * from the same WMO table, one process apart. `weatherFor` is the only
     * place a name is turned into a glyph.
     *
     * ⚠ Escapes, like every other glyph here: a literal PUA character does not
     * survive being written into a file, and this is exactly the block where
     * that failed silently before — see the header of this file. All seven were
     * checked against SymbolsNerdFontMono-Regular.ttf with
     * `fc-query --format %{charset}` rather than by eye.
     */
    readonly property string wxSun:    "\uE30D"   // nf-weather-day_sunny
    readonly property string wxPartly: "\uE302"   // nf-weather-day_cloudy
    readonly property string wxCloud:  "\uE312"   // nf-weather-cloudy
    readonly property string wxFog:    "\uE313"   // nf-weather-fog
    readonly property string wxRain:   "\uE318"   // nf-weather-rain
    readonly property string wxSnow:   "\uE31A"   // nf-weather-snow
    readonly property string wxStorm:  "\uE31D"   // nf-weather-thunderstorm

    function weatherFor(name) {
        switch (name) {
        case "sun":    return root.wxSun
        case "partly": return root.wxPartly
        case "fog":    return root.wxFog
        case "rain":   return root.wxRain
        case "snow":   return root.wxSnow
        case "storm":  return root.wxStorm
        }
        // Including a name from a newer compositor than this QML: a cloud is
        // the honest answer to "some weather", and a blank would collapse an
        // icon-and-text module to a bare number.
        return root.wxCloud
    }

    readonly property string record:     "\uF111"
    readonly property string recordStop: "\uF04D"

    // -- Media (MPRIS) --
    readonly property string mediaPlay:  "\uF04B"
    readonly property string mediaPause: "\uF04C"
    readonly property string mediaNext:  "\uF051"
    readonly property string mediaPrev:  "\uF048"
    readonly property string mediaNote:  "\uF001"

    // The music widget's library chip: the caret says the card unfolds, and
    // which way it will go. Same fa block as the transport glyphs above, so the
    // chip sits in the family the buttons beside it belong to.
    readonly property string caretDown: "\uF0D7"
    readonly property string caretUp:   "\uF0D8"

    // U+F0200 sits outside the BMP, so QML needs the surrogate pair.
    readonly property string ethernet: "\uDB80\uDE00"
}
