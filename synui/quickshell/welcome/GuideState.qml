pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import "pages.js" as Pages
import ".."

/*
 * GuideState — which page the welcome guide is on, which monitor it is on, and
 * the two facts it has to ask the rest of the system for.
 *
 * Same split as MenuState: the STATE is a singleton and the WINDOW is per
 * screen, because the guide is one logical thing shown on whichever monitor was
 * focused when it opened. Three monitors must not mean three guides on three
 * different pages.
 *
 * ⚠ THIS IS ITS OWN PROCESS, NOT PART OF THE BAR. `synui-welcome` starts a
 * second quickshell on the same QML tree with welcome.qml as its entry point.
 * That is deliberate and it is the whole reason the guide is portable: the bar
 * is swappable (`bar_shell = synapse|antiquity`) and a guide living inside it
 * would simply not exist for anyone running the other one — which is what the
 * compositor-drawn welcome menu this replaces never had to worry about, because
 * it was drawn by synui itself.
 */
QtObject {
    id: root

    // ⛔ CALLED, not read, and handed the SINGLETON. pages.js is a
    // `.pragma library` and cannot see a QML singleton of its own, so I18n is
    // passed in — named I18n there too, so every call site in it spells itself
    // `I18n.tr("…")` and tools/qml-xgettext.py needs no special case. Named
    // `tr` instead, the extractor read the file and took nothing from it while
    // reporting success.
    readonly property var pages: Pages.pages(I18n)

    property bool   open:  true

    // Which page to open on. `synui-welcome page N` exports it when it has to
    // START the guide rather than turn an already-running one; on the common
    // path (no variable) that is page one, which is what a guide opens on.
    property int    page: parseInt(Quickshell.env("SYNUI_WELCOME_PAGE") || "0") || 0

    /*
     * Which monitor. Named by whoever asked for the guide — synui knows the
     * focused output and passes it to `synui-welcome`, which exports it here,
     * because there is no Wayland protocol that lets a layer-shell client ask.
     *
     * An environment variable rather than an argument: `quickshell -p file.qml`
     * has no way to hand a config a positional argument, and the IPC path
     * (which DOES take one) only exists once a process is already running — so
     * the very first window, the one this variable is for, could not be told
     * any other way. Empty means "no idea", and Guide.qml shows on every screen
     * rather than on none.
     */
    property string output: Quickshell.env("SYNUI_WELCOME_OUTPUT") || ""

    readonly property var  current:   root.pages[root.page]
    readonly property bool onFirst:   root.page === 0
    readonly property bool onLast:    root.page === root.pages.length - 1

    function show(outputName) {
        if (outputName) root.output = outputName
        else if (!root.output) outputProbe.running = true
        root.open = true
    }

    /*
     * Closing the guide ENDS THE PROCESS.
     *
     * A dedicated quickshell that stayed resident holding a full-screen layer
     * surface after you dismissed it would be a second shell on the session for
     * no benefit — it has no bar to keep, no tray to hold and no state anybody
     * asks it for. `synui-welcome` toggles by talking to a running instance and
     * starting one when nothing answers, so "gone" and "not running" being the
     * same state is what makes that toggle work.
     *
     * Deferred by one event loop turn so a click that closes the guide finishes
     * being delivered first; quitting inside the handler that is still running
     * is how a click ends up looking like a crash in the log.
     */
    function close() {
        root.open = false
        Qt.callLater(Qt.quit)
    }

    function goTo(i) {
        const n = Math.max(0, Math.min(root.pages.length - 1, i))
        if (n === root.page) return
        root.page = n
        // A new page starts on its first selectable row, never on the checkbox
        // and never on the index the previous page happened to leave behind —
        // which on a shorter page would have been past the end.
        root.selected = root.firstSelectable()
    }
    function next() { if (root.onLast) root.close(); else root.goTo(root.page + 1) }
    function back() { root.goTo(root.page - 1) }

    // ── The selection ────────────────────────────────────
    /*
     * An index into the CURRENT page's rows, with one past the end meaning the
     * "Don't show again" checkbox in the footer — which is where the old menu
     * put it as well (WELCOME_CHECK == synui_welcome_menu_len). A `note` row is
     * prose and is skipped: arrowing onto a line Enter does nothing with is how
     * a keyboard interface teaches people it is unreliable.
     */
    property int selected: 0

    readonly property var rows: root.current ? root.current.rows : []
    readonly property int rowCount: root.rows.length

    function isSelectable(i) {
        const r = root.rows[i]
        return r !== undefined && r.kind !== "note"
    }

    function firstSelectable() {
        for (let i = 0; i < root.rowCount; i++) if (root.isSelectable(i)) return i
        return root.rowCount        // a page of nothing but prose: the checkbox
    }

    /*
     * Step by `d`, skipping prose, wrapping through the checkbox.
     *
     * Bounded by the item count so a page whose rows are ALL notes cannot spin
     * here — the checkbox is always a valid stop, so the loop always terminates,
     * but the bound is what makes that true by construction rather than by
     * argument.
     */
    function move(d) {
        const items = root.rowCount + 1        // rows, then the checkbox
        let i = root.selected
        for (let n = 0; n < items; n++) {
            i = (i + d + items) % items
            if (i === root.rowCount || root.isSelectable(i)) { root.selected = i; return }
        }
    }

    /*
     * Do item `i`. Both spellings of "activate" — Enter and a left click — come
     * through here, exactly as they did in the compositor.
     *
     * ⚠ OPENING SOMETHING CLOSES THE GUIDE, and that is not a style choice.
     * This window is the whole screen, so a guide left standing is a guide
     * covering the thing it just opened — and one that has silently gone deaf,
     * since synui grants a layer surface the keyboard at MAP and hands it to the
     * next toplevel that appears. The user's first act on their new desktop
     * would be to open something they then could not see or type into. The
     * compositor-drawn menu could stay up because synui was drawing both and
     * routing its own input.
     *
     * The AI backend row is the exception: it toggles a value this page is
     * REPORTING, so closing would hide the answer the press was asking for.
     */
    function activate(i) {
        if (i === root.rowCount) { root.toggleStartup(); return }

        const r = root.rows[i]
        if (!r || r.kind === "note" || !r.action) return

        // argv, never a shell string. Nothing here needs a shell, and every
        // action name comes from pages.js — but a menu that runs /bin/sh -c is
        // one hostile string away from being an injection, and the rule is
        // cheaper to keep than to re-argue.
        Quickshell.execDetached(r.arg ? ["synctl", "dispatch", r.action, r.arg]
                                      : ["synctl", "dispatch", r.action])

        if (r.live === undefined) root.close()
    }

    /*
     * The checkbox. It dispatches rather than writing welcome.state, because
     * synui is the single writer of its own config — see `startupFile` below,
     * which only ever reads.
     */
    function toggleStartup() {
        Quickshell.execDetached(["synctl", "dispatch", "welcome_startup"])
    }

    // ── The version, in the rail ─────────────────────────
    // Asked of the running compositor rather than compiled in: this tree is
    // installed by the synui package but it is QML, and a version baked into a
    // string here would be one more thing to bump and forget.
    property string version: ""

    property Process versionProbe: Process {
        running: true
        command: ["synctl", "version"]
        stdout: StdioCollector {
            onStreamFinished: {
                try { root.version = JSON.parse(this.text).version || "" }
                catch (e) { root.version = "" }
            }
        }
    }

    // ── The live chords ──────────────────────────────────
    /*
     * ⚠ THE KEYS IN pages.js ARE FALLBACKS. This is the answer.
     *
     * `synctl binds` reports the compositor's own bind table with each chord
     * already rendered by ctlpanel_combo_str(), so the guide, the shortcuts
     * palette and the control panel all spell "Super+Shift+C" identically and
     * a rebound key shows up here without anybody editing a string.
     *
     * FIRST bind whose action matches AND which carries no argument: an action
     * can legitimately be bound several times (`control` bare and
     * `control audio` are two rows of the control panel's world) and the bare
     * one is what a menu row runs. That is the rule welcome_hint() applied
     * inside the compositor, kept verbatim on the way out.
     */
    property var binds: ({})

    property Process bindProbe: Process {
        running: true
        command: ["synctl", "binds"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const map = {}
                    for (const b of JSON.parse(this.text)) {
                        if (b.arg) continue
                        if (map[b.action] === undefined) map[b.action] = b.key
                    }
                    root.binds = map
                } catch (e) {
                    // No synctl, or a synui too old to answer `binds`: the
                    // fallback chords in pages.js are what the rows show, which
                    // is the state the guide shipped in anyway. Never fatal —
                    // an unreadable key column must not cost the guide.
                    root.binds = ({})
                }
            }
        }
    }

    function keyFor(row) {
        if (!row || !row.action) return row && row.key ? row.key : ""
        /*
         * ⚠ A ROW WITH AN ARGUMENT TAKES NO LIVE CHORD. The probe above keeps
         * only the binds that carry NO argument, so `binds[action]` is by
         * construction the chord for the BARE action — and the bare action is
         * not what this row runs. The layouts page's "Pick a layout" dispatches
         * `control Desktop`; showing Super+C beside it would promise a key that
         * opens the panel's front door instead. Its own `key` still shows, for
         * a row that genuinely has a chord of its own.
         */
        if (row.arg) return row.key || ""
        const live = root.binds[row.action]
        return live !== undefined && live !== "" ? live : (row.key || "")
    }

    // ── "Show this guide at startup" ─────────────────────
    /*
     * READ here, WRITTEN by the compositor. The checkbox dispatches
     * `welcome_startup`, which flips config.welcome_at_startup and writes
     * welcome.state — synui is the single writer of its own config, and a
     * FileView that wrote this file would be the second one.
     *
     * The tick therefore comes back the long way round: click → dispatch →
     * synui writes → this reloads. That is a frame or two of latency on a
     * checkbox, and it is worth it for the box never disagreeing with the file.
     *
     * ⚠ ABSENT MEANS ON. A fresh install has never written welcome.state, and
     * reading a missing file as "off" would tick a box nobody ticked. Note the
     * sense: the file holds the opt-IN and the checkbox is the opt-OUT.
     */
    property bool atStartup: true

    property FileView startupFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/welcome.state"
        watchChanges: true
        printErrors: false      // absent on a box that never changed it
        onFileChanged: reload()
        onLoaded: {
            const m = this.text().match(/^\s*show_at_startup\s*=\s*(\d+)\s*$/m)
            root.atStartup = m ? m[1] !== "0" : true
        }
        onLoadFailed: root.atStartup = true
    }

    // ── The AI backend, for the row that reports it ──────
    /*
     * synui-ai-backend(1) writes "gpu", "cpu" or "off" here when it toggles
     * synapd. Absent means nothing has toggled it, and synapd's own default is
     * to auto-detect — so "auto" is the honest label, not "off".
     *
     * Two paths because the state moved: /etc/synapd/backend is where it lives
     * now and /run/synapd/backend is where it used to, and a box upgraded
     * mid-session can still be holding the old one.
     */
    property string aiBackend: "auto"

    function aiLabel(v) {
        return v === "gpu" ? "GPU" : v === "cpu" ? "CPU" : v === "off" ? "off"
                                                                      : "auto"
    }

    property FileView aiFile: FileView {
        path: "/etc/synapd/backend"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.aiBackend = root.aiLabel(this.text().trim())
        onLoadFailed: aiLegacy.reload()
    }

    property FileView aiLegacy: FileView {
        path: "/run/synapd/backend"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.aiBackend = root.aiLabel(this.text().trim())
        onLoadFailed: root.aiBackend = "auto"
    }

    // Fallback only, for a caller that named no output. synui is the only
    // process that knows which monitor has focus — there is no Wayland protocol
    // that tells a layer-shell client — so the normal path is told, and this is
    // what happens when nothing told it.
    property Process outputProbe: Process {
        command: ["synctl", "outputs"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    for (const o of JSON.parse(this.text))
                        if (o.focused) { root.output = o.name; return }
                } catch (e) { /* leave it empty: the guide shows everywhere */ }
            }
        }
    }
}
