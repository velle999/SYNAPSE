import QtQuick
import Quickshell.Io
import "../components"
import ".."

/*
 * Somebody is looking at this screen — the indicator for syn-remote(1), and the
 * button that ends it.
 *
 * Why it exists: a remote desktop is the one thing on this machine that can be
 * running with no window, no sound and no trace on screen. `syn-remote on`
 * survives a reboot by design — that is the point of it — so a session started
 * for one afternoon of help is a session still listening a month later, and
 * nothing about the desktop looks any different. The pill is what makes being
 * watched visible, and once it is there the disconnect button is nearly free.
 *
 * ⚠ IT APPEARS WHEN SOMEBODY IS CONNECTED, NOT WHEN THE SERVER IS RUNNING.
 * Recording.qml's rule, for its reason: a pill that is up whenever remote
 * access is enabled is furniture, and furniture stops being read. What matters
 * is the moment a person is on the other end of it. `syn-settings --rec remote`
 * is where "it is listening, and from how far" lives.
 *
 * SHOWN ON EVERY MONITOR, again like Recording: a viewer sees one output, but
 * the person at the machine may be looking at any of them, and a disconnect
 * button you have to go and find is one that does not get pressed. No BarConfig
 * key for the same reason — a transient alert, not something to turn off per
 * monitor.
 *
 * ⛔ THE COUNT IS GATED ON THE SERVER ACTUALLY RUNNING, in syn-remote itself.
 * The number comes from a state file, and a state file outliving the thing that
 * wrote it would leave this pill saying somebody is watching over a server that
 * is not there — which is worse than no pill at all. Same hazard Recording.qml
 * avoids by reading /proc; same answer, one layer down.
 */
BarModule {
    id: root

    property int    viewers: 0
    property string scope: ""        // "local" | "lan" — never translated
    property string port: ""

    // A disconnect is in flight. Stopping the unit is quick, but the poll is
    // two seconds behind it, and a pill that stayed up afterwards would read as
    // a button that did nothing.
    property bool stopping: false

    moduleVisible: root.viewers > 0

    property bool blinkOn: true
    readonly property bool blinking: root.viewers > 0 && !root.stopping && !root.hovered

    // The eye means "being watched"; the crossed eye means "not any more". So
    // it swaps on hover to say what the click will do, and stays swapped while
    // the stop is in flight.
    icon: (root.hovered || root.stopping) ? Icons.remoteEyeStop : Icons.remoteEye

    iconColor: (root.blinking && !root.blinkOn)
               ? Qt.rgba(root.pal.red.r, root.pal.red.g, root.pal.red.b, 0.3)
               : root.pal.red

    // ⚠ THE NUMBER ONLY ONCE THERE IS MORE THAN ONE. A lone "1" beside an eye
    // is a digit people stop to read; two viewers is the case worth counting.
    text: root.stopping ? I18n.tr("ENDING")
                        : (root.viewers > 1 ? String(root.viewers) : "")
    textColor: root.pal.red

    active: true

    tooltipText: {
        if (root.stopping)
            return I18n.tr("Disconnecting…")
        // ⚠ trn(), not tr(). One viewer is drawn as "Somebody" and never as a
        // number, but two is dual in Arabic and 2-4 is its own form in Russian
        // and Polish — a single counted string would be wrong in three of the
        // thirteen and right-looking in the other ten.
        let t = root.viewers > 1
                ? I18n.trn("%1 person is looking at this screen",
                           "%1 people are looking at this screen",
                           root.viewers).arg(root.viewers)
                : I18n.tr("Somebody is looking at this screen")
        // ⛔ `scope` IS A RECORD VALUE and is matched on, never drawn: the two
        // sentences below are the drawn form, and they say the thing that
        // actually matters — a LAN-bound server is reachable by every device on
        // the network, because synnet accepts private-range traffic by design.
        if (root.scope === "lan")
            t += "\n" + I18n.tr("Reachable from the whole network, on port %1").arg(root.port)
        else if (root.scope === "local")
            t += "\n" + I18n.tr("Through an SSH tunnel, on port %1").arg(root.port)
        t += "\n" + I18n.tr("Click to disconnect them")
        return t
    }

    Timer {
        interval: 700
        running: root.blinking
        repeat: true
        onTriggered: root.blinkOn = !root.blinkOn
        onRunningChanged: if (!running) root.blinkOn = true
    }

    // ── Disconnecting ────────────────────────────────────
    // `stop`, not `off`: this ends the session that is happening, and leaves
    // remote access switched on for the next one. Turning it off for good is a
    // decision to make in Settings ▸ Remote Desktop, with the rest of the
    // picture in front of you — not on one click of an alert.
    Process {
        id: stopProc
        command: ["syn-remote", "stop"]
    }

    onClicked: {
        if (root.stopping) return
        root.stopping = true
        stopProc.running = true
        stopFailsafe.restart()
        poll.running = true
    }

    Timer {
        id: stopFailsafe
        interval: 8000
        onTriggered: root.stopping = false
    }

    // ── Polling ──────────────────────────────────────────
    Process {
        id: poll
        command: ["syn-remote", "status", "--rec"]
        stdout: StdioCollector {
            onStreamFinished: {
                // ⚠ BY FIELD NAME, never by line number. That record grows a
                // row on the end when it grows one, and every value in it is a
                // short word that would look plausible in the wrong place.
                let n = 0, sc = "", p = ""
                for (const line of String(this.text).split("\n")) {
                    const tab = line.indexOf("\t")
                    if (tab < 0) continue
                    const k = line.slice(0, tab), v = line.slice(tab + 1)
                    if (k === "connections") n = parseInt(v) || 0
                    else if (k === "scope") sc = v
                    else if (k === "port") p = v
                }
                root.viewers = n
                root.scope = sc
                root.port = p
                if (n === 0) {
                    root.stopping = false
                    stopFailsafe.stop()
                }
            }
        }
    }

    Timer {
        interval: 2000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: poll.running = true
    }
}
