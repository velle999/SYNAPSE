import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Services.UPower
import ".."

/*
 * SYS://MONITOR — CPU, memory and battery as segmented meters on the desktop.
 *
 * Deliberately the same numbers the bar already shows. The bar is for a glance
 * down; this is for reading across the room, and the two disagreeing would be
 * worse than either alone — so both derive from /proc and UPower directly
 * rather than one copying the other.
 *
 * Slower than the bar's modules on purpose: 3s here against the bar's 2s and
 * 5s. A desktop widget nobody is looking at should not be the reason a core
 * wakes up.
 *
 * The meters are segments rather than a filled bar because a segment reads at a
 * distance — which is the entire reason this widget exists — where the end of a
 * smooth bar does not. Everything about being a widget (the panel, the shadow,
 * the drag) is in WidgetFrame.
 */
WidgetFrame {
    id: root

    widgetId: "sysmon"
    shown: WidgetState.sysmon
    label: "SYS://MONITOR"
    accent: Theme.magenta

    homeEdgeH: "right"; homeEdgeV: "top"
    homeMarginX: 18
    homeMarginY: Theme.barHeight + 18

    cardWidth: 248
    bodyHeight: col.implicitHeight

    // ── Readings ─────────────────────────────────────────
    property int  cpu: 0
    property real prevTotal: -1
    property real prevIdle: 0

    property int  mem: 0
    property real memUsedGiB: 0
    property real memTotalGiB: 0

    readonly property var batDev: UPower.displayDevice
    readonly property bool hasBattery:
        batDev ? (batDev.isLaptopBattery && batDev.isPresent) : false
    readonly property int bat: batDev ? Math.round(batDev.percentage * 100) : 0

    Column {
        id: col
        width: parent.width
        spacing: 8

        Repeater {
            model: [
                { key: "CPU", value: root.cpu, show: true },
                { key: "MEM", value: root.mem, show: true },
                { key: "BAT", value: root.bat, show: root.hasBattery }
            ]

            delegate: Item {
                id: row
                required property var modelData

                readonly property int value: modelData.value
                // Meaning, not style: yellow is warm, red is a machine in
                // trouble. Same thresholds the filled bar used.
                readonly property color tint: value >= 90 ? Theme.red
                                            : value >= 70 ? Theme.yellow
                                                          : Theme.cyan

                visible: modelData.show
                height: modelData.show ? 15 : 0
                width: col.width

                Text {
                    id: key
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: row.modelData.key
                    color: Theme.fgDim
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    font.letterSpacing: 1.2
                    width: 30
                }

                Row {
                    id: meter
                    anchors {
                        left: key.right; right: pct.left
                        rightMargin: 9
                        verticalCenter: parent.verticalCenter
                    }
                    height: 9
                    spacing: 2

                    readonly property int segments: 18
                    // How many segments are lit. Rounded up so that any load at
                    // all lights one — a meter reading empty on a machine doing
                    // something is a broken meter.
                    readonly property int lit:
                        Math.min(segments, Math.ceil(segments * Math.max(0, Math.min(100, row.value)) / 100))

                    Repeater {
                        model: meter.segments
                        delegate: Rectangle {
                            required property int index
                            readonly property bool on: index < meter.lit
                            // The leading segment runs hot, so the eye finds the
                            // end of the meter without reading the number.
                            readonly property bool head: index === meter.lit - 1

                            width: (meter.width - (meter.segments - 1) * meter.spacing) / meter.segments
                            height: parent.height
                            color: on ? (head ? Qt.lighter(row.tint, 1.5) : row.tint)
                                      : Theme.fg
                            opacity: on ? (head ? 1.0 : 0.85) : 0.08
                            Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }
                            Behavior on color   { ColorAnimation  { duration: Theme.animNormal } }
                        }
                    }
                }

                Text {
                    id: pct
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: row.value + "%"
                    color: row.tint
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    width: 36
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        // Both numbers were already being computed and neither was ever shown.
        // A percentage says how full memory is; this says how much there is,
        // which is the question anybody watching a build actually has.
        Text {
            width: col.width
            horizontalAlignment: Text.AlignRight
            text: root.memTotalGiB > 0
                  ? root.memUsedGiB.toFixed(1) + " / " + root.memTotalGiB.toFixed(1) + " GiB"
                  : ""
            color: Theme.fgDim
            font.family: Theme.fontFamily
            font.pixelSize: 9
        }
    }

    // ── Sources ──────────────────────────────────────────
    // /proc/stat is cumulative since boot, so usage is the DELTA between two
    // reads; the first tick has no previous sample and shows 0 rather than a
    // meaningless spike. Same reasoning as modules/Cpu.qml.
    FileView {
        id: stat
        path: "/proc/stat"
        onLoaded: {
            const f = this.text().split("\n")[0].trim().split(/\s+/).slice(1).map(Number)
            if (f.length < 4) return
            const idle = f[3] + (f[4] || 0)
            const total = f.reduce((a, b) => a + b, 0)
            if (root.prevTotal >= 0) {
                const dt = total - root.prevTotal
                const di = idle - root.prevIdle
                if (dt > 0) root.cpu = Math.round(100 * (dt - di) / dt)
            }
            root.prevTotal = total
            root.prevIdle = idle
        }
    }

    // Used = MemTotal - MemAvailable. MemFree would read ~95% used on any
    // healthy box with a warm page cache and turn this into a permanent alarm.
    FileView {
        id: meminfo
        path: "/proc/meminfo"
        onLoaded: {
            const kv = {}
            for (const line of this.text().split("\n")) {
                const m = line.match(/^(\w+):\s+(\d+)/)
                if (m) kv[m[1]] = parseInt(m[2], 10)
            }
            if (!kv.MemTotal) return
            const avail = kv.MemAvailable !== undefined ? kv.MemAvailable : kv.MemFree
            root.memTotalGiB = kv.MemTotal / 1048576
            root.memUsedGiB = (kv.MemTotal - avail) / 1048576
            root.mem = Math.round(100 * (kv.MemTotal - avail) / kv.MemTotal)
        }
    }

    Timer {
        interval: 3000
        // Nothing is read while the widget is off.
        running: root.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: { stat.reload(); meminfo.reload() }
    }
}
