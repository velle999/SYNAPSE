import QtQuick
import Quickshell.Io
import "../components"
import ".."

/*
 * Memory, from /proc/meminfo.
 *
 * Used = MemTotal - MemAvailable, NOT MemTotal - MemFree. MemFree excludes the
 * page cache, so a healthy Linux box with warm cache reads as ~95% "used" and
 * the module becomes a permanent red alarm that means nothing. MemAvailable is
 * the kernel's own estimate of what a new allocation could actually get, which
 * is the number a human wants.
 */
BarModule {
    id: root

    property int  percent: 0
    property real usedGiB: 0
    property real totalGiB: 0

    icon: Icons.memory
    iconColor: percent >= 90 ? root.pal.red : (percent >= 75 ? root.pal.clock : root.pal.glyph)
    text: percent + "%"
    tooltipText: "Memory " + usedGiB.toFixed(1) + " / " + totalGiB.toFixed(1) + " GiB"
                 + "\nClick for the task manager"

    onClicked: taskmgr.running = true

    Process {
        id: taskmgr
        command: ["synctl", "dispatch", "taskmgr"]
    }

    FileView {
        id: meminfo
        path: "/proc/meminfo"

        onLoaded: {
            const kv = {}
            for (const line of this.text().split("\n")) {
                const m = line.match(/^(\w+):\s+(\d+)/)
                if (m) kv[m[1]] = parseInt(m[2], 10)   // kB
            }
            if (!kv.MemTotal) return

            const avail = kv.MemAvailable !== undefined ? kv.MemAvailable : kv.MemFree
            const used  = kv.MemTotal - avail

            root.totalGiB = kv.MemTotal / 1048576
            root.usedGiB  = used / 1048576
            root.percent  = Math.round(100 * used / kv.MemTotal)
        }
    }

    Timer {
        interval: 5000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: meminfo.reload()
    }
}
