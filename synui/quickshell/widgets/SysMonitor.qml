import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Services.UPower
import ".."

/*
 * SYS://MONITOR — CPU, memory and battery as bars on the desktop.
 *
 * Deliberately the same numbers the bar already shows. The bar is for a glance
 * down; this is for reading across the room, and the two disagreeing would be
 * worse than either alone — so both derive from /proc and UPower directly
 * rather than one copying the other.
 *
 * Slower than the bar's modules on purpose: 3s here against the bar's 2s and
 * 5s. A desktop widget nobody is looking at should not be the reason a core
 * wakes up.
 */
PanelWindow {
    id: root

    required property var modelData
    screen: modelData

    visible: WidgetState.sysmon && modelData.name === WidgetState.primaryOutput

    WlrLayershell.layer: WlrLayer.Bottom
    anchors { top: true; right: true }
    margins { top: Theme.barHeight + 18; right: 18 }

    implicitWidth: 230
    implicitHeight: col.implicitHeight + 24

    exclusiveZone: 0
    focusable: false
    mask: Region {}
    color: "transparent"

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

    Rectangle {
        anchors.fill: parent
        radius: 8
        color: Theme.popupBg
        border.color: Theme.magenta
        border.width: 1

        Column {
            id: col
            anchors {
                left: parent.left; right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: 12; rightMargin: 12
            }
            spacing: 7

            Text {
                text: "SYS://MONITOR"
                color: Theme.magenta
                font.family: Theme.fontFamily
                font.pixelSize: 11
                font.letterSpacing: 1.5
            }

            Repeater {
                model: [
                    { key: "CPU", value: root.cpu, show: true },
                    { key: "MEM", value: root.mem, show: true },
                    { key: "BAT", value: root.bat, show: root.hasBattery }
                ]

                delegate: Item {
                    required property var modelData
                    visible: modelData.show
                    height: modelData.show ? 14 : 0
                    width: col.width

                    Text {
                        id: key
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.key
                        color: Theme.fg
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        width: 34
                    }

                    Rectangle {
                        anchors {
                            left: key.right; right: pct.left
                            rightMargin: 8
                            verticalCenter: parent.verticalCenter
                        }
                        height: 5
                        radius: 2.5
                        color: Theme.hoverBg

                        Rectangle {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                            width: parent.width * Math.max(0, Math.min(1, parent.parent.modelData.value / 100))
                            radius: 2.5
                            color: parent.parent.modelData.value >= 90 ? Theme.red
                                 : parent.parent.modelData.value >= 70 ? Theme.yellow
                                                                       : Theme.cyan
                            Behavior on width { NumberAnimation { duration: Theme.animNormal } }
                        }
                    }

                    Text {
                        id: pct
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: parent.modelData.value + "%"
                        color: Theme.fg
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        width: 34
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
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
