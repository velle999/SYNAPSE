import QtQuick
import Quickshell
import Quickshell.Io
import "../components"
import ".."

/*
 * Weather — the temperature, on the bar.
 *
 * ⚠ IT DOES NO FETCHING, for the reason Updates.qml sets out at length and
 * which is if anything sharper here: this module is instantiated once per
 * MONITOR, inside the shell process, and the weather is the one thing on this
 * desktop that goes to the network. Three bars would be three requests, and a
 * connect stalling behind a captive portal would be a stalled desktop.
 * src/weather.c fetches on a thread, twenty minutes apart, and publishes the
 * answer; WeatherState watches that file; this draws it.
 *
 * So the two switches are not duplicates of each other, and this is the same
 * split the update notifier already has:
 *
 *   `synctl weather on`  is NETWORK — whether the machine asks Open-Meteo
 *   this module          is FURNITURE — whether the bar shows the answer
 *
 * INVISIBLE WHEN THERE IS NOTHING TO SAY, like Recording and Updates. A machine
 * that has never fetched — the default, and the whole of a fresh install — gets
 * no gap where a temperature would be, so the bar of a user who never turns
 * this on is byte-identical to the one they have now.
 *
 * ⚠ THERE IS A widgets/Weather.qml TOO — the desktop card, the same reading
 * with room around it. The two names are only unambiguous because Bar.qml
 * imports "modules" and shell.qml imports "widgets", and neither imports both.
 *
 * The PLACE is not on the bar. It is one location per machine and it does not
 * change; printing "Oslo" beside the number every day costs the width of a word
 * to say something nobody is reading by the second week. It is in the tooltip,
 * where a thing you check once belongs.
 */
BarModule {
    id: root

    moduleVisible: WeatherState.have

    icon: Icons.weatherFor(WeatherState.icon)
    text: WeatherState.label

    /* A stale reading is DIMMED, never hidden and never silently drawn as
     * current. The lock screen makes the same call in cairo: an eight-hour-old
     * temperature presented as now is worse than no temperature, and hiding it
     * would make a bar module appear and disappear with the network — which
     * reads as a broken bar rather than an old reading. Dim says "this is what
     * I last knew" without taking the width back.
     */
    iconColor: WeatherState.stale ? root.pal.dim : root.pal.glyph
    textColor: WeatherState.stale ? root.pal.dim : root.pal.fg

    tooltipText: {
        let t = WeatherState.label
        if (WeatherState.cond !== "")  t += "  ·  " + WeatherState.cond
        if (WeatherState.place !== "") t += "\n" + WeatherState.place
        t += "\n" + (WeatherState.stale ? "last reading " + WeatherState.ageText
                                        : "updated " + WeatherState.ageText)
        return t + "\nClick to refresh"
    }

    /*
     * A click asks for a reading NOW rather than at the next twenty-minute
     * tick. That is the only useful thing a click can do here — there is
     * nothing to open, the location lives in one file that
     * `omarchy-weather-location` owns, and a bar module that opened a settings
     * window to change a city nobody changes would be furniture pretending to
     * be an app.
     *
     * Through synctl rather than by writing anything: the compositor owns the
     * fetch, and the refresh has to be rate-limited and unit-corrected on its
     * side anyway (see weather_refresh's note about the unit being re-read).
     */
    onClicked: refresh.running = true

    Process {
        id: refresh
        command: ["synctl", "weather", "refresh"]
    }
}
