import QtQuick
import Quickshell
import Quickshell.Io
import "../components"
import ".."

/*
 * IsoDownloads — how many people have downloaded the SynapseOS ISO.
 *
 * ⚠ IT FETCHES NOTHING, for the reason Updates.qml and WeatherState.qml give at
 * length: this module is instantiated once per MONITOR inside the compositor's
 * shell process, so an HTTP request here would be one request per screen and a
 * stalled connect would be a stalled bar. `syn downloads` asks GitHub from a
 * systemd user timer and leaves the answer in ~/.cache/syn/downloads; this reads
 * the file.
 *
 * Two switches, and neither is a copy of the other — the same division the
 * update notifier documents:
 *
 *   the TIMER   is network traffic — `syn downloads --watch 12h | off`
 *   this MODULE is furniture — BarConfig key `isodl`, per monitor
 *
 * ⚠ INVISIBLE UNTIL SOMETHING HAS COUNTED. The background check is OFF by
 * default — counting our own downloads is not worth a request to GitHub from
 * every installed SynapseOS — so the file is absent on an ordinary machine and
 * this draws nothing. That is not an error state, and it is why the BarConfig
 * default can be `true` without putting a row on anybody's bar: the same shape
 * Weather uses, so nobody has to find two switches on the machine that did ask
 * for it.
 *
 * ── WHAT THE NUMBER ON THE BAR IS ───────────────────────────────────────────
 *
 * The LIFETIME total, across every release, of COMPLETE downloads — and both
 * halves of that were chosen against the alternative.
 *
 * ⚠ LIFETIME RATHER THAN THE CURRENT RELEASE, because a per-release badge
 * resets to 0 on the day a release is published, which is the one day somebody
 * is watching it, and a counter that reads 0 the morning after a release reads
 * as broken. The release's own number is in the tooltip, where a 0 is a fact
 * rather than an alarm.
 *
 * ⚠ COMPLETE RATHER THAN STARTED, because the ISO ships in parts — .part00,
 * .part01, .part02 — and the parts do not agree: 0.3.1 sits at 6, 6, 4.
 * `complete` is the least-downloaded part of each set, so the most people who
 * could have assembled an ISO; `started` is the most. syn-downloads computes
 * both and says why. What neither of them is, is the SUM: that would report a
 * three-part release three times over and jump whenever the part count changed.
 */
BarModule {
    id: root

    // Everything below is what the state file last said.
    property int    total: 0         // complete downloads, every release
    property int    started: 0       // started downloads, every release
    property int    releases: 0      // releases carrying an ISO
    property string latest: ""       // "1.0.0", the newest release with one
    property int    latestFull: 0
    property int    latestStarted: 0
    property string repo: ""
    property bool   errored: false
    property string reason: ""
    property double checkedAt: 0

    // A reading exists. Not "total > 0": a project whose ISO nobody has
    // downloaded yet has a perfectly good answer, and hiding on the number
    // would make the row appear only once it had something flattering to say.
    readonly property bool have: root.checkedAt > 0 && !root.errored

    moduleVisible: root.have || root.errored

    icon: Icons.isoDownloads
    text: root.errored ? "!" : String(root.total)

    /* The warning hue only when the COUNT failed, exactly as Updates does it —
     * and `orange` for the same reason that module's comment gives at length:
     * it is the only warm slot with a real hue in both palette branches, so it
     * still says "warning" over a wallpaper, and on the errored branch the
     * colour is carrying the whole message. */
    iconColor: root.errored ? root.pal.orange : root.pal.glyph
    textColor: root.errored ? root.pal.orange : root.pal.fg

    tooltipText: {
        if (root.errored)
            // ⛔ `syn downloads` IS A COMMAND TO TYPE and stays spelled that way
            // in every language — a translated one names a binary that is not on
            // the system. Same rule Updates.qml and the compositor's footers
            // follow.
            return I18n.tr("Could not count downloads") + "\n"
                 + (root.reason !== "" ? root.reason + "\n" : "")
                 + I18n.tr("syn downloads   to try now")

        let t = I18n.tr("ISO downloads")
        // ⛔ A LITERAL · AND A LITERAL —, never "\xc2\xb7" or "\xe2\x80\x94". In
        // QML \xNN is a UNICODE escape, not a byte, so the C idiom for these
        // spells U+00C2 and ships mojibake on every desktop — which is exactly
        // what happened to the Updates tooltip and stood for months.
        if (root.latest !== "")
            t += "\n" + I18n.tr("%1 — %2 complete, %3 started")
                             .arg(root.latest).arg(root.latestFull).arg(root.latestStarted)
        t += "\n" + I18n.tr("all releases — %1 complete").arg(root.total)
        if (root.checkedAt > 0)
            t += "\n" + I18n.tr("checked %1").arg(root.ago(root.checkedAt))
        return t + "\n" + I18n.tr("Click for the breakdown · right-click for more")
    }

    /* The per-release table, from the cache rather than the network: everything
     * it prints is already in the file this module is reading, and a click that
     * went to GitHub would be a second fetcher in the one process that must not
     * have one. --hold, so the table is still there to read afterwards. */
    onClicked: breakdown.running = true

    /*
     * ── Right-click ─────────────────────────────────────────────────────────
     *
     * ⚠ THE MODULE HAS TO CLAIM THE BUTTON. BarModule declines right-click by
     * default so it falls through to the bar's own per-monitor menu, which is
     * right for a readout and wrong for anything with actions of its own.
     */
    acceptsRight: true
    /* ⚠ MAPPED, NOT `root.x` — a module's x is its position inside the bar's
     * Row, and the popup anchors against the bar WINDOW. See Updates.qml. */
    onRightClicked: menu.openAt(root.mapToItem(null, root.width / 2, 0).x)

    ModuleMenu {
        id: menu
        // A PopupWindow cannot find its own window — see ModuleMenu.barWindow.
        barWindow: root.QsWindow.window
        title: I18n.tr("ISO downloads")

        // ⛔ THE `key` IS NOT A LABEL. onTriggered switches on it and it never
        // reaches a screen — translating one silently disconnects the row from
        // what it does.
        rows: [
            { key: "table", label: I18n.tr("All releases"),
              detail: root.releases > 0 ? String(root.releases) : "" },
            { key: "check", label: I18n.tr("Check now"),
              detail: root.checkedAt > 0 ? root.ago(root.checkedAt) : "" },
            { key: "page",  label: I18n.tr("Releases page"),
              // The row still says the command exists where the repository is
              // not known yet; it just cannot be followed.
              enabled: root.repo !== "" }
        ]

        onTriggered: (key) => {
            if (key === "table")      breakdown.running = true
            else if (key === "check") checkNow.running = true
            else if (key === "page")  releasesPage.running = true
        }
    }

    Process {
        id: breakdown
        command: ["syntty", "--hold", "-e", "syn", "downloads", "--cached"]
    }

    // Writes the state file this module reads, so the badge corrects itself a
    // second or two later with nothing else asked of it. Quiet by design:
    // --refresh prints nothing and reports its failures into the file.
    Process {
        id: checkNow
        command: ["syn", "downloads", "--refresh"]
    }

    /* ⚠ THE URL IS BUILT FROM THE STATE FILE'S `repo`, not written here. The
     * repository name already exists in syn-downloads.sh, and a second copy in
     * QML is a second thing to forget on the day the project moves. */
    Process {
        id: releasesPage
        command: ["xdg-open", "https://github.com/" + root.repo + "/releases"]
    }

    /* ── The state file ──────────────────────────────────────────────────────
     *
     * ~/.cache/syn/downloads, written by `syn downloads`. Watched rather than
     * polled: it changes every twelve hours at most, and a module that re-read a
     * file on a tick would be doing nothing all day to catch an event that has a
     * perfectly good notification already.
     *
     * ⚠ THE WRITER RENAMES INTO PLACE, which is what makes watching safe — a
     * FileView on a path written in place sees it empty for a frame and the
     * badge blinks to nothing every time the timer fires.
     *
     * ⚠ XDG_CACHE_HOME IS ASKED FOR FIRST, because syn-downloads honours it and
     * a module that only ever looked in ~/.cache would read nothing on a machine
     * that moved its cache — with no error, because an absent file is the
     * ordinary state here.
     */
    FileView {
        id: state
        path: (Quickshell.env("XDG_CACHE_HOME") || (Quickshell.env("HOME") + "/.cache"))
              + "/syn/downloads"
        watchChanges: true
        // Nothing has counted on a machine that never asked to — which is every
        // machine by default, and not worth a warning per bar per monitor.
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const t = this.text()
            // ⛔ `\s` IN A QML STRING REGEX TAKES ONE BACKSLASH. "\\s" here is a
            // literal backslash followed by s, every field parses as 0, the
            // badge reads 0 and the row looks like a feature that was never
            // wired up. Updates.qml shipped exactly that bug for a release.
            const num = (k) => {
                const m = t.match(new RegExp("^" + k + "\\s*=\\s*(\\d+)\\s*$", "m"))
                return m ? parseInt(m[1]) : 0
            }
            const str = (k) => {
                const m = t.match(new RegExp("^" + k + "\\s*=\\s*(.*?)\\s*$", "m"))
                return m ? m[1] : ""
            }
            root.errored       = str("status") === "error"
            root.reason        = str("reason")
            root.repo          = str("repo")
            root.latest        = str("latest")
            root.latestFull    = num("latest_full")
            root.latestStarted = num("latest_starts")
            root.total         = num("total_full")
            root.started       = num("total_starts")
            root.releases      = num("counted")
            root.checkedAt     = num("checked")
        }
        // A file that cannot be read is not an error to report — it is a machine
        // that has never counted. The module simply stays hidden.
        onLoadFailed: {
            root.errored = false
            root.checkedAt = 0
            root.total = 0; root.started = 0; root.releases = 0
            root.latest = ""; root.latestFull = 0; root.latestStarted = 0
        }
    }
}
