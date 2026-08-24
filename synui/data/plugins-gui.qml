//@ pragma UseQApplication
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * Bar Plugins — the window for browsing, installing and turning on bar widgets.
 *
 * ⚠ EVERY FACT ON SCREEN COMES FROM `synui-plugins`, the same script the bar
 * reads and the same one the command line is. Nothing here parses a manifest, or
 * decides whether a plugin can be hosted, or knows what Omarchy is. It renders
 * rows. That is the arrangement every other window in this suite has with its
 * own tool, and for a plugin system it is the difference between "it is off" and
 * "it was refused" being one answer instead of two.
 *
 * TSV across that boundary, like synpkg's: parsing here is a split on newline
 * and a split on tab, and the shell side never has to escape JSON.
 *
 * ── Two lists, one window ───────────────────────────────────────────────────
 *
 * `catalogue` is what you could install; `scan` is what is on disk. They are
 * separate commands because they answer separate questions — a plugin cloned
 * from a git URL is in the second and not the first, and a catalogue entry is in
 * the first until you install it. Merged here by id, which is the only thing
 * both sides agree on.
 */
/*
 * ⚠ THE WINDOW IS THE ROOT, not a ShellRoot holding one. `onClosed` is
 * FloatingWindow's signal: on a ShellRoot it matches nothing, so closing the
 * window would leave quickshell running with nothing on screen and every later
 * `synui-plugins gui` would exit 0 having drawn none. Same shape synpkg's
 * window has, and for the same reason.
 */
FloatingWindow {
    id: root

    title: "Bar Plugins"
    implicitWidth: 720
    implicitHeight: 560
    color: root.cBg

    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNUI_PLUGINS_BIN") || "synui-plugins"

    // ── Palette ─────────────────────────────────────────────────────────────
    //
    // theme.json plus the wallpaper's own accent, exactly as synpkg's window
    // reads them and for the same reason: this is a different package from
    // synui, so the contract across the two is the FILE and never an import of
    // the bar's Theme singleton, which would break the moment either is
    // installed alone.
    property var p: ({})
    readonly property bool isLight: root.p.scheme === "light"

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/theme.json"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: { try { root.p = JSON.parse(this.text()) } catch (e) { root.p = ({}) } }
        onLoadFailed: root.p = ({})
    }

    property string wpAccent: ""
    property FileView wpPaletteFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/palette.state"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const t   = this.text()
            /* ⚠ `ok` AND `use` BOTH HAVE TO HOLD — the picture's answer and the
             * setting. Reading the colour without checking both is how the bar
             * came to wear the wallpaper on themes that never asked. */
            const ok  = /^\s*ok\s*=\s*yes\s*$/m.test(t)
            const use = !/^\s*use\s*=\s*no\s*$/m.test(t)
            const m   = t.match(/^\s*accent\s*=\s*(#[0-9A-Fa-f]{6})\s*$/m)
            root.wpAccent = (ok && use && m) ? m[1] : ""
        }
        onLoadFailed: root.wpAccent = ""
    }

    function themed(key, r, g, b, a) {
        const c = root.p[key]
        return (c && c.length === 3) ? Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, a)
                                     : Qt.rgba(r / 255, g / 255, b / 255, a)
    }
    function pick(dark, light) { return root.isLight ? light : dark }

    readonly property color cBg:     root.themed("bar",   11, 11, 20, 1.0)
    readonly property color cPanel:  root.themed("popup", 17, 17, 28, 1.0)
    readonly property color cText:   root.pick("#c8e3ee", "#1a1d24")
    readonly property color cDim:    root.pick("#7d8e97", "#5e6675")
    readonly property color cAccent: root.wpAccent !== "" ? Qt.color(root.wpAccent)
                                                          : root.pick("#05d9e8", "#00727e")
    readonly property color cWarn:   root.pick("#ffd319", "#8a6d00")
    function wash(a) { return root.pick(Qt.rgba(1, 1, 1, a), Qt.rgba(0, 0, 0, a * 0.6)) }

    readonly property string uiFont: "monospace"

    // ── State ───────────────────────────────────────────────────────────────
    property var  rows: []          // merged: {id,name,desc,state,why,inCatalogue}
    property bool loading: false
    property string busy: ""        // the id an action is running against
    property string outcome: ""     // what the last action did, or why it failed

    function reload() {
        root.loading = true
        catProc.running = true      // catalogue first; scan chains off it
    }

    /* The catalogue: what could be installed. Read first because it is the
     * shorter list and the scan below merges INTO it. */
    property var catalogue: []
    Process {
        id: catProc
        command: [root.bin, "catalogue"]
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                const lines = this.text.split("\n")
                for (let i = 1; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 5 || !f[0]) continue
                    out.push({ id: f[0], name: f[1], desc: f[2] })
                }
                root.catalogue = out
                scanProc.running = true
            }
        }
    }

    Process {
        id: scanProc
        command: [root.bin, "scan"]
        stdout: StdioCollector {
            onStreamFinished: {
                /* id -> row, so a plugin that is BOTH in the catalogue and on
                 * disk is one row rather than two. */
                const seen = {}
                const out = []
                const lines = this.text.split("\n")
                for (let i = 1; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 7 || !f[0]) continue
                    const r = { id: f[0], name: f[1], desc: f[2], dir: f[3],
                                installed: true, enabled: f[5] === "on",
                                why: f[6], inCatalogue: false }
                    seen[r.id] = r
                    out.push(r)
                }
                /* Then everything in the catalogue that is not on disk yet. */
                for (let i = 0; i < root.catalogue.length; i++) {
                    const c = root.catalogue[i]
                    if (seen[c.id]) { seen[c.id].inCatalogue = true; continue }
                    out.push({ id: c.id, name: c.name, desc: c.desc, dir: "",
                               installed: false, enabled: false, why: "",
                               inCatalogue: true })
                }
                root.rows = out
                root.loading = false
            }
        }
    }

    /*
     * One action, and the window is BUSY until it finishes.
     *
     * ⚠ `add` CAN TAKE SECONDS — it is a network fetch — and a window that
     * looked idle through it would invite a second click on a row already being
     * installed. The row says what is happening and the buttons stop taking
     * presses.
     *
     * stderr is collected because that is where every refusal goes: "needs
     * Hyprland", "already installed", "clone failed". A window that showed only
     * the exit code would report failure without ever saying why, which is the
     * one thing this whole tool is built not to do.
     */
    Process {
        id: actProc
        property string errText: ""
        stdout: StdioCollector { onStreamFinished: {} }
        stderr: StdioCollector { onStreamFinished: actProc.errText = this.text }
        onExited: (code) => {
            root.busy = ""
            root.outcome = code === 0 ? ""
                         : (actProc.errText.trim().split("\n")[0] || "that did not work")
            actProc.errText = ""
            root.reload()
        }
    }

    function act(id, argv) {
        if (root.busy !== "") return
        root.busy = id
        root.outcome = ""
        actProc.command = argv
        actProc.running = true
    }

    Component.onCompleted: root.reload()

    Column {
        anchors.fill: parent
        spacing: 0

            // ── Header ──────────────────────────────────────────────────
            Rectangle {
                width: parent.width
                height: 62
                color: root.cPanel

                Column {
                    anchors { left: parent.left; leftMargin: 18
                              verticalCenter: parent.verticalCenter }
                    spacing: 3
                    Text {
                        text: "Bar Plugins"
                        color: root.cText
                        font { family: root.uiFont; pixelSize: 16; bold: true }
                    }
                    Text {
                        /* The header carries the OUTCOME when there is one:
                         * a refusal is about the row you just pressed, and a
                         * message that appeared beside the row would move as the
                         * list re-sorted under it. */
                        text: root.outcome !== "" ? root.outcome
                            : root.loading ? "reading…"
                            : root.rows.length + " plugin(s) · widgets for the bar, in Omarchy's format"
                        color: root.outcome !== "" ? root.cWarn : root.cDim
                        font { family: root.uiFont; pixelSize: 11 }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: root.wash(0.10) }

            // ── The list ────────────────────────────────────────────────
            ListView {
                width: parent.width
                height: parent.height - 62 - 1 - 34
                clip: true
                model: root.rows
                spacing: 0

                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    height: 58
                    color: rowMa.containsMouse ? root.wash(0.06) : "transparent"
                    /* A refused plugin is legible but visibly not part of what
                     * you can act on — the same dimming a held package takes in
                     * the software window. */
                    opacity: row.modelData.why !== "" ? 0.62 : 1.0

                    MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true }

                    Rectangle {
                        id: dot
                        anchors { left: parent.left; leftMargin: 18
                                  verticalCenter: parent.verticalCenter }
                        width: 8; height: 8; radius: 4
                        color: row.modelData.enabled ? root.cAccent : "transparent"
                        border { width: row.modelData.enabled ? 0 : 1; color: root.cDim }
                    }

                    Column {
                        anchors { left: dot.right; leftMargin: 14
                                  right: btn.left; rightMargin: 12
                                  verticalCenter: parent.verticalCenter }
                        spacing: 3

                        Row {
                            spacing: 8
                            width: parent.width
                            Text {
                                width: Math.max(0, Math.min(implicitWidth, parent.width * 0.55))
                                elide: Text.ElideRight
                                text: row.modelData.name || row.modelData.id
                                color: root.cText
                                font { family: root.uiFont; pixelSize: 13; bold: true }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.modelData.id
                                color: root.cDim
                                font { family: "monospace"; pixelSize: 10 }
                            }
                        }
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            /* The REASON wins over the description. A row that
                             * cannot run and shows only what it would have done
                             * is a row nobody can act on. */
                            text: row.modelData.why !== ""
                                  ? "unsupported — " + row.modelData.why
                                  : row.modelData.desc
                            color: row.modelData.why !== "" ? root.cWarn : root.cDim
                            font { family: root.uiFont; pixelSize: 11 }
                        }
                    }

                    // ── The one button this row needs ───────────────
                    //
                    // Not installed -> Install. Installed and refused -> nothing
                    // to press. Otherwise the on/off it actually has.
                    Rectangle {
                        id: btn
                        anchors { right: parent.right; rightMargin: 18
                                  verticalCenter: parent.verticalCenter }
                        visible: row.modelData.why === ""
                        width: 84; height: 28; radius: 4
                        readonly property bool running: root.busy === row.modelData.id
                        readonly property string label:
                            btn.running ? "…"
                          : !row.modelData.installed ? "Install"
                          : row.modelData.enabled ? "On" : "Off"
                        color: btnMa.containsMouse && root.busy === "" ? root.wash(0.22)
                                                                       : root.wash(0.10)
                        border {
                            width: 1
                            color: row.modelData.enabled ? root.cAccent : "transparent"
                        }
                        Text {
                            anchors.centerIn: parent
                            text: btn.label
                            color: row.modelData.enabled ? root.cAccent : root.cText
                            font { family: root.uiFont; pixelSize: 12 }
                        }
                        MouseArea {
                            id: btnMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: root.busy === "" ? Qt.PointingHandCursor
                                                          : Qt.ArrowCursor
                            onClicked: {
                                if (!row.modelData.installed)
                                    root.act(row.modelData.id,
                                             [root.bin, "add", row.modelData.id])
                                else
                                    root.act(row.modelData.id,
                                             [root.bin, row.modelData.id, "toggle"])
                            }
                        }
                    }

                    /* Remove, only on the ones you installed yourself — the
                     * script refuses the other two directories, and a button
                     * that always errors is worse than one that is not there. */
                    Text {
                        anchors { right: btn.left; rightMargin: 10
                                  verticalCenter: parent.verticalCenter }
                        visible: row.modelData.installed &&
                                 row.modelData.dir.indexOf("/.config/synui/plugins/") >= 0
                        text: "remove"
                        color: rmMa.containsMouse ? root.cWarn : root.cDim
                        font { family: root.uiFont; pixelSize: 11; underline: rmMa.containsMouse }
                        MouseArea {
                            id: rmMa
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.act(row.modelData.id,
                                                [root.bin, "remove", row.modelData.id])
                        }
                    }

                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 1
                        color: root.wash(0.06)
                    }
                }
            }

            // ── Footer ──────────────────────────────────────────────────
            Rectangle {
                width: parent.width
                height: 34
                color: root.cPanel
                Text {
                    anchors { left: parent.left; leftMargin: 18
                              verticalCenter: parent.verticalCenter }
                    text: "A plugin runs inside the bar's own process — everything is off until you turn it on"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: 11 }
                }
            }
        }
    }
