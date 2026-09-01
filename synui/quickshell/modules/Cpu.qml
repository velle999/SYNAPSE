import QtQuick
import Quickshell.Io
import "../components"
import ".."

/*
 * CPU load, from /proc/stat.
 *
 * /proc/stat is cumulative jiffies since boot, so a single read tells you the
 * average since power-on, which is useless and never changes visibly. Usage is
 * the DELTA between two reads — hence the previous-sample state below. The
 * first tick after start has no previous sample and deliberately shows 0
 * rather than a garbage spike.
 */
BarModule {
    id: root

    property int  usage: 0
    property real prevTotal: -1
    property real prevIdle: 0

    icon: Icons.cpu
    iconColor: usage >= 90 ? root.pal.red : (usage >= 60 ? root.pal.clock : root.pal.glyph)
    text: usage + "%"
    tooltipText: I18n.tr("CPU %1%\nClick for the task manager").arg(usage)

    onClicked: taskmgr.running = true

    Process {
        id: taskmgr
        command: ["synctl", "dispatch", "taskmgr"]
    }

    FileView {
        id: stat
        path: "/proc/stat"

        onLoaded: {
            // First line: "cpu  user nice system idle iowait irq softirq steal"
            const line = this.text().split("\n")[0]
            const f = line.trim().split(/\s+/).slice(1).map(Number)
            if (f.length < 4) return

            const idle  = f[3] + (f[4] || 0)          // idle + iowait
            const total = f.reduce((a, b) => a + b, 0)

            if (root.prevTotal >= 0) {
                const dt = total - root.prevTotal
                const di = idle - root.prevIdle
                if (dt > 0) root.usage = Math.round(100 * (dt - di) / dt)
            }
            root.prevTotal = total
            root.prevIdle  = idle
        }
    }

    Timer {
        interval: 2000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: stat.reload()
    }
}
