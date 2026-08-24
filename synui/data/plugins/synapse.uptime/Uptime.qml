import QtQuick
import Quickshell.Io
import qs.Ui

/*
 * Uptime — a bar plugin, and the worked example for writing one.
 *
 * It is shipped OFF, like every plugin: `synui-plugins synapse.uptime on`.
 *
 * ── What makes this a plugin rather than a module ───────────────────────────
 *
 * synui's own bar modules live in the bar's QML tree and are instantiated by
 * name in Bar.qml. A plugin is a DIRECTORY somewhere else — this one is in
 * /usr/share/synui/plugins, yours would be in ~/.config/synui/plugins — holding
 * a manifest.json and some QML, and the bar finds it by scanning.
 *
 * The format is Omarchy's, because their desktop is quickshell too and it is
 * the only format already describing this exact thing. A widget written to the
 * contract below loads on either desktop.
 *
 * ── The contract ────────────────────────────────────────────────────────────
 *
 * Root at BarWidget (from qs.Ui — quickshell resolves `qs.` against the bar's
 * own tree) and you are handed:
 *
 *   bar         the host bar, for anything not covered below
 *   moduleName  this plugin's manifest id, filled in by the host
 *   settings    per-widget overrides; empty on synui today
 *   vertical    is the bar a column (false here; synui's bar is a strip)
 *   barSize     the bar's thickness in pixels
 *   broadcast() run one of your methods on every instance of this widget,
 *               which on a multi-monitor desk is one per screen
 *   setting()   one settings value with a fallback
 *
 * ⚠ SET implicitWidth. The bar's Row lays widgets out by it, and a widget that
 * leaves it at 0 is loaded, running and invisible — which reads as the plugin
 * having failed rather than as it having no width.
 *
 * ⛔ AND KEEP TO THAT CONTRACT. Importing qs.Commons or Quickshell.Hyprland is
 * how Omarchy's own widgets reach their theme and their compositor, and neither
 * exists here — synui-plugins refuses those before they reach the bar, with the
 * import named. `synui-plugins list` is where that shows up.
 */
BarWidget {
    id: root

    property string label: "—"

    implicitWidth: text.implicitWidth + 12
    implicitHeight: root.barSize

    /* Refreshed by the host through broadcast() as well as by the timer, so a
     * plugin with something to say can reach every screen at once. Nothing
     * calls it that way here; it is wired up because the example is what
     * somebody copies. */
    function refresh() { uptime.reload() }

    Text {
        id: text
        anchors.centerIn: parent
        text: root.label
        /* ⚠ NOT the theme's ink. A plugin has no access to the bar's per-strip
         * palette — that is resolved per monitor and per module against the
         * wallpaper under it — so a plugin picks a colour that reads on
         * anything, or asks `bar` for one. White at 0.85 is the honest simple
         * answer for an example. */
        color: Qt.rgba(1, 1, 1, 0.85)
        font.pixelSize: Math.max(9, Math.round(root.barSize * 0.42))
    }

    FileView {
        id: uptime
        path: "/proc/uptime"
        onLoaded: {
            /* "12345.67 89012.34" — seconds since boot, then idle. */
            const secs = parseFloat(this.text().split(" ")[0])
            if (isNaN(secs)) { root.label = "—"; return }
            const d = Math.floor(secs / 86400)
            const h = Math.floor((secs % 86400) / 3600)
            const m = Math.floor((secs % 3600) / 60)
            root.label = d > 0 ? (d + "d " + h + "h")
                       : h > 0 ? (h + "h " + m + "m")
                               : (m + "m")
        }
        /* /proc/uptime cannot really be absent, but a widget that assumes its
         * file is there is a widget that shows a stale number for ever when it
         * is not. */
        onLoadFailed: root.label = "—"
    }

    /* A minute, not a second: the shortest thing this can display is a minute,
     * so anything faster is a re-read nobody can see. */
    Timer {
        interval: 60000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: root.refresh()
    }
}
