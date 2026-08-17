import QtQuick
import Quickshell.Io
import "../components"
import ".."

/*
 * Screen recording indicator — a blinking red dot and a running clock while
 * wf-recorder is capturing, and the button that stops it.
 *
 * Why it exists: Super+Shift+R is a toggle with NO on-screen state. Start a
 * take, tab away, forget, and it records until the disk fills — the only
 * evidence is a notification that has long since dismissed itself. The pill is
 * that missing evidence, and once it is on screen the stop button is nearly
 * free.
 *
 * SHOWN ON EVERY MONITOR, unlike most modules. wf-recorder captures exactly one
 * output, but the pointer can be on any of them, and a stop button you have to
 * go and find on another screen is a stop button that does not get pressed. The
 * tooltip names which output is actually being captured, so nothing is lost by
 * showing it everywhere. For the same reason it has no BarConfig key: like
 * GameMode this is a transient alert, not furniture someone would turn off per
 * monitor — it is invisible whenever nothing is recording.
 *
 * State comes from the wf-recorder PROCESS (synui-record-status reads /proc),
 * not from a file synui-record writes. That is deliberate and is the property
 * this module was careful not to break: a recorder killed by a full disk, a
 * refused screencopy or the OOM killer takes its pill down with it. A state
 * file would leave the bar claiming to record over nothing at all.
 */
BarModule {
    id: root

    // Everything below is what the last poll saw. `recording` is the only one
    // that gates visibility; the rest is tooltip material.
    property bool   recording: false
    property int    elapsed: 0           // seconds
    property string file: ""
    property string outputName: ""
    property string audioDev: ""

    // A stop is in flight. wf-recorder is given SIGTERM and then FINISHES
    // WRITING — up to a few seconds on a long take — so the pill must not just
    // vanish on click, or a recording still being flushed to disk looks like
    // one that was thrown away. See the bounded wait in synui-record.
    property bool stopping: false

    moduleVisible: recording

    // Blink, but only while actually recording and not while the pointer is on
    // it: a target that is dimming in and out under the cursor reads as
    // unresponsive, and it is exactly then that the icon has swapped to the
    // stop square that needs to be legible.
    property bool blinkOn: true
    readonly property bool blinking: root.recording && !root.stopping && !root.hovered

    // The dot means "capturing"; the square means "not any more". So it swaps
    // on hover (to say what the click will do) and stays swapped while the file
    // is being finished — a red dot over "SAVING" would claim the take is still
    // running when it has already ended.
    icon: (root.hovered || root.stopping) ? Icons.recordStop : Icons.record

    // The dot fades rather than flicking, because BarModule already animates
    // iconColor — the timer only has to flip the target and the 120ms
    // ColorAnimation carries it. A hand-rolled per-frame pulse would fight it.
    iconColor: (root.blinking && !root.blinkOn)
               ? Qt.rgba(root.pal.red.r, root.pal.red.g, root.pal.red.b, 0.3)
               : root.pal.red

    text: root.stopping ? "SAVING" : root.clock
    textColor: root.pal.red

    // The magenta wash and hairline BarModule draws for "on". A recording IS
    // the on state, so it is on for the pill's whole life.
    active: true

    tooltipText: root.tip

    Timer {
        interval: 700
        running: root.blinking
        repeat: true
        onTriggered: root.blinkOn = !root.blinkOn
        // Leaving it mid-fade would strand a half-dim dot on a pill that has
        // stopped blinking, which reads as a rendering glitch rather than a
        // state.
        onRunningChanged: if (!running) root.blinkOn = true
    }

    // ── The clock ────────────────────────────────────────
    // Ticked locally and RESYNCED from every poll, rather than derived purely
    // from either. Polling once a second to move a digit spawns a process a
    // second for the length of a recording; ticking alone drifts, and would be
    // flatly wrong for a recorder that was already running when the bar started
    // (a bar restart, or a second monitor plugged in mid-take). The poll is the
    // authority; the tick is what makes it look alive between polls.
    Timer {
        interval: 1000
        running: root.recording && !root.stopping
        repeat: true
        onTriggered: root.elapsed++
    }

    readonly property string clock: {
        const t = Math.max(0, root.elapsed)
        const h = Math.floor(t / 3600)
        const m = Math.floor(t / 60) % 60
        const s = t % 60
        const pad = (n) => (n < 10 ? "0" + n : "" + n)
        // Hours only once there are any: "00:01:07" on a one-minute clip wastes
        // three characters of a 28px bar that the tray and the media title are
        // already competing for.
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s)
                     : pad(m) + ":" + pad(s)
    }

    readonly property string tip: {
        if (root.stopping)
            return "Stopping — finishing the file…\nwf-recorder is still writing"
        let t = root.outputName !== "" ? "Recording " + root.outputName
                                       : "Recording"
        t += " · " + root.clock
        if (root.file !== "")
            t += "\n" + root.file
        // Named, not just "on": which device is recorded is the difference
        // between capturing the game and capturing the room, and it is the one
        // thing that cannot be checked after the fact without playing the file.
        t += root.audioDev !== "" ? "\nAudio: " + root.audioDev
                                  : "\nNo audio"
        t += "\nClick to stop (or Super+Shift+R)"
        return t
    }

    // ── Stopping ─────────────────────────────────────────
    // `--stop`, NOT a bare `synui-record`. The bare form is a toggle, and this
    // pill can be up to one poll stale — if the keybind already stopped the
    // take, a toggle here would start a brand new recording from a button
    // labelled stop. --stop refuses to do that and says so instead.
    Process {
        id: stopProc
        command: ["synui-record", "--stop"]
    }

    onClicked: {
        if (root.stopping) return          // it is already on its way out
        root.stopping = true
        stopProc.running = true
        stopFailsafe.restart()
        poll.running = true                // don't wait out the poll interval
    }

    // If the recorder is wedged, "SAVING" must not be the pill's last word
    // forever — synui-record gives up waiting after 10s, so a little past that
    // the button becomes pressable again rather than leaving a dead control on
    // the bar. Cleared early by the poll below the moment the process is gone.
    Timer {
        id: stopFailsafe
        interval: 13000
        onTriggered: root.stopping = false
    }

    // ── Polling ──────────────────────────────────────────
    Process {
        id: poll
        command: ["synui-record-status"]
        stdout: StdioCollector {
            onStreamFinished: {
                let j
                try {
                    j = JSON.parse(this.text)
                } catch (e) {
                    // Unparseable output is not evidence of a recording. Say
                    // nothing is running rather than freezing the last state on
                    // screen, which is the failure that would outlive the bug.
                    root.recording = false
                    root.stopping = false
                    return
                }
                root.recording  = j.recording === true
                root.outputName = j.output || ""
                root.file       = j.file || ""
                root.audioDev   = j.audio || ""
                if (root.recording) {
                    root.elapsed = j.elapsed || 0
                } else {
                    root.elapsed  = 0
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
