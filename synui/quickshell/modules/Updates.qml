import QtQuick
import Quickshell
import Quickshell.Io
import "../components"
import ".."

/*
 * Updates — how many SynapseOS updates are waiting, and one click to the window
 * that installs them.
 *
 * ⚠ IT DOES NO CHECKING OF ITS OWN, and that is the whole design. A git fetch
 * per bar is a git fetch per MONITOR — this module is instantiated once per
 * screen — and it would run inside the compositor's shell process, where a
 * network stall is a stalled bar. `syn-update ping` does the work from a
 * systemd user timer and leaves the answer in a small file; this reads the
 * file. The interval is that timer's business (`syn-update ping --every 6h`),
 * and turning the checking off is `syn-update ping --off`.
 *
 * So there are two switches and they are not duplicates of each other:
 *
 *   the TIMER   is network traffic — how often the machine asks upstream
 *   this MODULE is furniture — whether the bar shows the answer
 *
 * Which is why the module's own switch is an ordinary BarConfig key in the
 * bar's right-click menu, per monitor, beside the tray and the clock, and not
 * a second copy of the timer's setting.
 *
 * INVISIBLE WHEN THERE IS NOTHING TO SAY. A row that reads "0 updates" all day
 * is a row that gets ignored on the day it says something else — the same
 * reason Recording and GameMode are not permanent furniture.
 */
BarModule {
    id: root

    // Everything below is what the state file last said.
    property int  updates: 0
    property int  newComps: 0
    property int  held: 0
    property bool errored: false
    property string reason: ""
    property string rev: ""
    property double checkedAt: 0

    // "held" is not an update waiting to be installed — it is one the user has
    // deliberately pinned (`synpkg ignore`), and counting it here would make
    // the badge insist on work nobody is going to do. It is tooltip material.
    readonly property int pending: root.updates + root.newComps

    moduleVisible: root.pending > 0 || root.errored

    icon: Icons.updates
    text: root.errored ? "!" : String(root.pending)

    /*
     * Ordinary bar ink when there is something to install; the warning hue only
     * when the check itself failed, because "there are updates" is not a
     * problem and colouring it like one trains people to ignore the colour.
     *
     * ⚠ THIS ASKED FOR `pal.yellow`, WHICH THE BAR PALETTE HAS NEVER HAD.
     *
     * `yellow` is a Theme property; the per-strip palette these modules read is
     * a different object with its own vocabulary, and its warm slot is
     * `orange`. The binding therefore evaluated to undefined on the errored
     * branch — `Unable to assign [undefined] to QColor`, once per module per
     * monitor, in a log nobody reads.
     *
     * ⚠ AND THE FAILURE MODE IS THE QUIET ONE. A refused assignment does not
     * blank the property, it LEAVES THE PREVIOUS VALUE STANDING — BarModule's
     * own defaults, `pal.glyph` and `pal.fg`. So the errored badge was drawn in
     * ordinary bar ink: not missing, not obviously wrong, simply identical to
     * the working state. Which is exactly what the paragraph above says must
     * never happen — the colour IS the message here, and a machine whose update
     * check was failing looked like a machine with an update waiting.
     *
     * ⚠ AND `orange` RATHER THAN `clock`, which is what the three sibling
     * modules use for their warm tier. `clock` collapses to plain ink on a
     * clear bar, where it would be the same colour as the ordinary state — and
     * this badge is a bare "!" with no number beside it, so its colour carries
     * the entire message. `orange` is defined with a real hue in BOTH palette
     * branches, which makes it the only slot that still says "warning" over a
     * wallpaper. (Battery, Cpu and Memory have the same weakness on a clear
     * bar; they get away with it because they print a number too.)
     */
    iconColor: root.errored ? root.pal.orange : root.pal.fg
    textColor: root.errored ? root.pal.orange : root.pal.fg

    tooltipText: {
        if (root.errored)
            return "Could not check for updates\n"
                 + (root.reason !== "" ? root.reason + "\n" : "")
                 + "syn-update ping   to try now"
        const bits = []
        if (root.updates > 0)  bits.push(root.updates + " to rebuild")
        if (root.newComps > 0) bits.push(root.newComps + " new")
        if (root.held > 0)     bits.push(root.held + " held back")
        let t = "SynapseOS updates: " + bits.join(", ")
        if (root.rev !== "") t += "\nupstream at " + root.rev
        if (root.checkedAt > 0) {
            const mins = Math.floor((Date.now() / 1000 - root.checkedAt) / 60)
            t += "\nchecked " + (mins < 1 ? "just now"
                               : mins < 60 ? mins + " min ago"
                               : Math.floor(mins / 60) + "h ago")
        }
        return t + "\nClick to open Updates"
    }

    // The window that actually installs them. `syn-update-gui` and not
    // `syn-update apply`: applying is a long sudo build that belongs in a
    // window with a log pane, and starting it from a bar click with no visible
    // output is how a machine ends up rebuilding itself in silence.
    onClicked: updatesWindow.running = true

    Process {
        id: updatesWindow
        command: ["syn-update-gui"]
    }

    /* ── The state file ──────────────────────────────────────────────────────
     *
     * ~/.cache/syn-update/pending, written by `syn-update ping`. Watched rather
     * than polled: the timer writes it every few hours, and a bar module that
     * re-read a file on a tick would be doing nothing all day to catch an event
     * that has a perfectly good notification already.
     *
     * ⚠ THE WRITER RENAMES INTO PLACE, which is what makes watching safe: a
     * FileView on a path being written in place would see it empty for a frame
     * and the badge would blink to nothing every time the timer fired.
     */
    FileView {
        id: state
        path: Quickshell.env("HOME") + "/.cache/syn-update/pending"
        watchChanges: true
        // Nothing has checked yet on a machine where the timer has not run —
        // the ordinary case for the first hours of a fresh install, and not
        // worth a warning per bar per monitor.
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const t = this.text()
            const num = (k) => {
                const m = t.match(new RegExp("^" + k + "\\s*=\\s*(\\d+)\\s*$", "m"))
                return m ? parseInt(m[1]) : 0
            }
            const str = (k) => {
                const m = t.match(new RegExp("^" + k + "\\s*=\\s*(.*?)\\s*$", "m"))
                return m ? m[1] : ""
            }
            root.errored   = str("status") === "error"
            root.reason    = str("reason")
            root.updates   = num("updates")
            root.newComps  = num("new")
            root.held      = num("held")
            root.rev       = str("rev")
            root.checkedAt = num("checked")
        }
        // A file that cannot be read is not an error to report — it is a
        // machine that has not checked yet. The module simply stays hidden.
        onLoadFailed: {
            root.errored = false
            root.updates = 0; root.newComps = 0; root.held = 0
        }
    }
}
