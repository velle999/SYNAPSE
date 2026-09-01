pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * WeatherState — the reading, read from the one place the compositor puts it.
 *
 * ⚠ NOTHING HERE FETCHES ANYTHING, and that is the design rather than an
 * omission. This singleton feeds a bar module that is instantiated once per
 * MONITOR and a desktop widget beside it, all inside the shell process — so a
 * fetch here would be N requests on a hotplug and a stalled connect would be a
 * stalled bar. src/weather.c does the network on a thread of its own, off the
 * compositor's event loop, and leaves the answer in ~/.config/synui/weather.state.
 * This watches that file. Same arrangement, and the same reasoning, as
 * Updates.qml and `syn-update ping`.
 *
 * A singleton and not a component per consumer: one FileView, one parse, one
 * copy of the truth, however many bars and widgets are looking at it.
 *
 * ⚠ THE FILE IS ABSENT ON A MACHINE THAT HAS NEVER FETCHED — which is every
 * machine until `synctl weather on`, because the weather is the only part of
 * this desktop that goes to the network and it is off by default. That is not
 * an error state: `have` stays false and every consumer draws nothing.
 */
QtObject {
    id: root

    // False until there has ever been a reading. Consumers hide on it.
    property bool   have: false

    property int    temp: 0
    property string unit: "C"          // "C" or "F", resolved by the compositor
    property string cond: ""           // "Overcast", "Rain showers", …
    property string icon: "cloud"      // sun partly cloud fog rain snow storm
    property string place: ""
    property double when: 0            // unix seconds of the reading
    property int    staleAfter: 3 * 60 * 60

    // Ticked so `age` and `stale` are live bindings rather than values sampled
    // once at parse. A minute is fine: the only thing either drives is the word
    // "2h ago" and a dimmer ink.
    property int tick: 0

    readonly property int age: {
        root.tick                       // named so this re-runs on the timer
        return root.when > 0 ? Math.max(0, Math.floor(Date.now() / 1000 - root.when)) : 0
    }
    /* ⚠ THE SAME THRESHOLD THE LOCK SCREEN USES, and it comes out of the file
     * rather than being written here. A reading the compositor considers
     * current while the bar calls it old is two surfaces disagreeing about one
     * fact, which is the whole class of bug this file exists to avoid. */
    readonly property bool stale: root.have && root.age > root.staleAfter

    // "12°C". One place, because the bar, the widget and every tooltip say it.
    readonly property string label: root.have ? root.temp + "°" + root.unit : ""

    // "just now" / "18 min ago" / "4h ago" — for the tooltip and the card, and
    // only worth saying at all once it is not now.
    readonly property string ageText: {
        if (!root.have) return ""
        const m = Math.floor(root.age / 60)
        if (m < 1)  return I18n.tr("just now")
        if (m < 60) return I18n.trn("%1 min ago", "%1 min ago", m).arg(m)
        return I18n.trn("%1h ago", "%1h ago", Math.floor(m / 60)).arg(Math.floor(m / 60))
    }

    property Timer ageTimer: Timer {
        interval: 60000
        repeat: true
        running: root.have
        onTriggered: root.tick++
    }

    property FileView stateFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/weather.state"
        watchChanges: true
        /* Absent is the documented default — the weather is off until it is
         * turned on — so a missing file is not worth a warning per session, let
         * alone one per bar. Same call LauncherStyle.qml makes about synuirc. */
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.parse(this.text())
        onLoadFailed: root.have = false
    }

    function parse(text) {
        const get = (k) => {
            const m = String(text).match(new RegExp("^" + k + "\\s*=\\s*(.*?)\\s*$", "m"))
            return m ? m[1] : ""
        }

        const when = parseInt(get("when"))
        // `when` is 0 until there has ever been a reading, and the compositor
        // writes this file only after a successful fetch — but a truncated or
        // hand-edited file must not put NaN on the bar.
        if (!isFinite(when) || when <= 0) { root.have = false; return }

        const t = parseFloat(get("temp"))
        if (!isFinite(t)) { root.have = false; return }

        const u = get("unit")
        const sa = parseInt(get("stale_after"))

        root.temp  = Math.round(t)
        root.unit  = (u === "F" || u === "f") ? "F" : "C"
        root.cond  = get("cond")
        // The compositor derives the icon from the WMO code through the one
        // table that also produces the word. An unknown name here means a
        // newer compositor than this QML, which is a cloud rather than nothing.
        root.icon  = get("icon") || "cloud"
        root.place = get("place")
        root.staleAfter = (isFinite(sa) && sa > 0) ? sa : 3 * 60 * 60
        root.when  = when
        root.tick++
        root.have  = true
    }
}
