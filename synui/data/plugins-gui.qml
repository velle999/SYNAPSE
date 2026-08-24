//@ pragma UseQApplication
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * Plugin Manager — the window for browsing, installing and turning on bar
 * plugins.
 *
 * ⚠ PLUGINS HERE, WIDGETS ON THE DESKTOP. synui already owns the word widget:
 * `synui-widgets` manages the things that sit ON the desktop (Tuxagotchi and
 * its neighbours). These sit IN the bar, they are somebody else's code running
 * in the bar's process, and Omarchy — whose format and catalogue these are —
 * calls them plugins. One word each, so a sentence about one is never about the
 * other.
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
 *
 * ── And `catalogue` is now a list of hundreds ────────────────────────────────
 *
 * It used to be five rows out of a file shipped in the package, which is why
 * this window had no search: everything fitted on screen. It is now those five
 * plus everything omarchyplugins.com lists that this desktop could host — the
 * community's widgets, the games among them — fetched by the script and cached.
 *
 * So the window grew the two things a list that long needs and nothing else: a
 * search that narrows as you type, and a Refresh that fetches the list again.
 * The FILTERING IS LOCAL — the script already handed over every row, and asking
 * it again per keystroke would put a process spawn behind every letter.
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

    title: "Plugin Manager"
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
    property string query: ""       // what is typed in the search box
    property string category: ""    // the pane's selection; "" is everything
    property bool loading: false
    property string busy: ""        // the id an action is running against
    property string outcome: ""     // what the last action did, or why it failed

    function reload() {
        root.loading = true
        catProc.running = true      // catalogue first; scan chains off it
    }

    /*
     * What the list draws: `rows` narrowed by the search box and put in an
     * order somebody choosing can use.
     *
     * ⚠ EVERY WORD HAS TO MATCH, and it matches across the tags — not only what
     * is on screen. The two dozen games in the registry are filed under the
     * category "Widgets" and say "game" nowhere in their names; typing `games`
     * finds them because the tag is in the haystack. A search over the visible
     * columns would find almost none of them and look broken.
     *
     * The order is: what you have installed, then what synui itself shipped and
     * tested, then whatever most people starred. A list of nine hundred sorted
     * by name opens on somebody's numbered `0xdeadbeef.clock`.
     */
    /*
     * The left pane: every category the catalogue actually uses, counted.
     *
     * ⚠ DERIVED FROM `rows`, NEVER A FIXED LIST. The categories are Omarchy's,
     * they arrive in column six of the same TSV the rows do, and the day they
     * add one a hardcoded list here would file it under nothing and hide the
     * plugins in it. Nine today; the pane does not know that.
     *
     * A row with no category is not dropped — it is collected under "Other",
     * because a plugin the pane cannot file is still a plugin somebody has to
     * be able to reach. Sorted by name so the pane is stable between refreshes:
     * ordering by count would move Appearance up and down the list as the
     * catalogue grew, and a menu whose rows change places is one you have to
     * read every time.
     */
    readonly property var categories: {
        const seen = {}
        for (let i = 0; i < root.rows.length; i++) {
            const r = root.rows[i]
            const name = (r.category || "").trim() || "Other"
            if (!seen[name]) seen[name] = { name: name, total: 0, installed: 0 }
            seen[name].total++
            if (r.installed) seen[name].installed++
        }
        const out = []
        for (const k in seen) out.push(seen[k])
        out.sort(function (a, b) { return a.name.localeCompare(b.name) })
        return out
    }

    function categoryOf(r) { return (r.category || "").trim() || "Other" }

    readonly property var visibleRows: {
        const q = root.query.trim().toLowerCase()
        const words = (q === "") ? [] : q.split(/[\s,]+/)
        const out = []
        for (let i = 0; i < root.rows.length; i++) {
            const r = root.rows[i]
            if (root.category !== "" && root.categoryOf(r) !== root.category) continue
            if (words.length > 0) {
                const hay = (r.id + " " + r.name + " " + r.desc + " " +
                             (r.category || "") + " " + (r.tags || "") + " " +
                             (r.author || "")).toLowerCase()
                let miss = false
                for (let w = 0; w < words.length; w++)
                    if (hay.indexOf(words[w]) < 0) { miss = true; break }
                if (miss) continue
            }
            out.push(r)
        }
        out.sort(function (a, b) {
            if (a.installed !== b.installed) return a.installed ? -1 : 1
            const at = (a.trust === "shipped") ? 0 : 1
            const bt = (b.trust === "shipped") ? 0 : 1
            if (at !== bt) return at - bt
            const as = parseInt(a.stars || "0", 10)
            const bs = parseInt(b.stars || "0", 10)
            if (as !== bs) return bs - as
            return (a.name || "").localeCompare(b.name || "")
        })
        return out
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
                    /* Ten columns since the registry: the five that were here
                     * plus category, tags, author, stars and trust. The guard
                     * is >= rather than == so an older script — one an upgrade
                     * has not replaced yet — still lists, with the extra fields
                     * simply undefined. */
                    if (f.length < 5 || !f[0]) continue
                    out.push({ id: f[0], name: f[1], desc: f[2],
                               category: f[5] || "", tags: f[6] || "",
                               author: f[7] || "", stars: f[8] || "",
                               trust: f[9] || "" })
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
                /* ⚠ THE CATALOGUE'S METADATA HAS TO REACH THE INSTALLED ROWS
                 * TOO. Category, author and stars come from the listing and a
                 * scan knows none of them, so a widget you installed would lose
                 * everything the search matches on — and would then be
                 * unfindable by the words that found it in the first place. */
                const meta = {}
                for (let i = 0; i < root.catalogue.length; i++)
                    meta[root.catalogue[i].id] = root.catalogue[i]

                const lines = this.text.split("\n")
                for (let i = 1; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length < 7 || !f[0]) continue
                    const m = meta[f[0]] || {}
                    const r = { id: f[0], name: f[1], desc: f[2], dir: f[3],
                                installed: true, enabled: f[5] === "on",
                                why: f[6], inCatalogue: meta[f[0]] !== undefined,
                                category: m.category || "", tags: m.tags || "",
                                author: m.author || "", stars: m.stars || "",
                                trust: m.trust || "" }
                    seen[r.id] = r
                    out.push(r)
                }
                /* Then everything in the catalogue that is not on disk yet. */
                for (let i = 0; i < root.catalogue.length; i++) {
                    const c = root.catalogue[i]
                    if (seen[c.id]) continue
                    out.push({ id: c.id, name: c.name, desc: c.desc, dir: "",
                               installed: false, enabled: false, why: "",
                               inCatalogue: true, category: c.category,
                               tags: c.tags, author: c.author, stars: c.stars,
                               trust: c.trust })
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
                    width: parent.width - 380
                    spacing: 3
                    Text {
                        text: "Plugin Manager"
                        color: root.cText
                        font { family: root.uiFont; pixelSize: 16; bold: true }
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        /* The header carries the OUTCOME when there is one:
                         * a refusal is about the row you just pressed, and a
                         * message that appeared beside the row would move as the
                         * list re-sorted under it. */
                        text: root.outcome !== "" ? root.outcome
                            : root.loading ? "reading…"
                            : root.query !== ""
                              ? root.visibleRows.length + " of " + root.rows.length + " match"
                            /* ⚠ THE COUNT HAS TO FOLLOW THE PANE. Left saying
                             * "875 plugin(s)" over a list showing sixty-three,
                             * the header contradicts the only other thing on
                             * screen that counts anything. */
                            : root.category !== ""
                              ? root.visibleRows.length + " in " + root.category
                                + " · " + root.rows.length + " altogether"
                              : root.rows.length + " plugin(s) for the bar, in Omarchy's format"
                        color: root.outcome !== "" ? root.cWarn : root.cDim
                        font { family: root.uiFont; pixelSize: 11 }
                    }
                }

                Row {
                    anchors { right: parent.right; rightMargin: 18
                              verticalCenter: parent.verticalCenter }
                    spacing: 8

                    /* ⚠ A LIST OF HUNDREDS NEEDS THIS, and it is the reason the
                     * window has a header wide enough to hold it. Filtering is
                     * local: the script already handed over every row. */
                    Rectangle {
                        width: 220; height: 28; radius: 4
                        color: root.wash(0.10)
                        border { width: 1
                                 color: qIn.activeFocus ? root.cAccent : "transparent" }

                        Text {
                            anchors { left: parent.left; leftMargin: 9
                                      verticalCenter: parent.verticalCenter }
                            visible: root.query === "" && !qIn.activeFocus
                            text: "search — try games"
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: 12 }
                        }
                        TextInput {
                            id: qIn
                            anchors { left: parent.left; right: clr.left
                                      leftMargin: 9; rightMargin: 4
                                      verticalCenter: parent.verticalCenter }
                            text: root.query
                            /* ⚠ A SEARCH ANSWERS ACROSS EVERY CATEGORY, so
                             * typing clears the pane. Leaving both filters on
                             * means somebody searching for a plugin they know
                             * is there gets an empty list, because it is filed
                             * under a category they are not standing in — and
                             * nothing on screen says so. */
                            onTextChanged: {
                                root.query = text
                                if (text !== "") root.category = ""
                            }
                            color: root.cText
                            selectionColor: root.cAccent
                            selectByMouse: true
                            clip: true
                            font { family: root.uiFont; pixelSize: 12 }
                            Keys.onEscapePressed: { root.query = ""; text = "" }
                        }
                        Text {
                            id: clr
                            anchors { right: parent.right; rightMargin: 9
                                      verticalCenter: parent.verticalCenter }
                            visible: root.query !== ""
                            text: "×"
                            color: clrMa.containsMouse ? root.cText : root.cDim
                            font { family: root.uiFont; pixelSize: 14 }
                            MouseArea {
                                id: clrMa
                                anchors.fill: parent
                                anchors.margins: -6
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { root.query = ""; qIn.text = "" }
                            }
                        }
                    }

                    /* The community list is cached and a week old at most. This
                     * is for the widget somebody published this morning. */
                    Rectangle {
                        id: refreshBtn
                        width: 84; height: 28; radius: 4
                        readonly property bool running: root.busy === "__refresh"
                        color: rfMa.containsMouse && root.busy === "" ? root.wash(0.22)
                                                                      : root.wash(0.10)
                        Text {
                            anchors.centerIn: parent
                            text: refreshBtn.running ? "…" : "Refresh"
                            color: root.cText
                            font { family: root.uiFont; pixelSize: 12 }
                        }
                        MouseArea {
                            id: rfMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: root.busy === "" ? Qt.PointingHandCursor
                                                          : Qt.ArrowCursor
                            onClicked: root.act("__refresh", [root.bin, "refresh"])
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: root.wash(0.10) }

            /*
             * ── The disclaimer ──────────────────────────────────────────
             *
             * ⚠ THIS CATALOGUE IS SOMEBODY ELSE'S DESKTOP'S. These are Omarchy
             * community plugins and Omarchy runs on Hyprland; synui is a
             * wlroots compositor of its own. registry.py already drops the
             * kinds that have no host here — overlays, services, whole shells —
             * and `synui-plugins` refuses a widget that imports
             * Quickshell.Hyprland at install time, by name, with the import
             * that did it.
             *
             * ⛔ BUT NEITHER OF THOSE IS KNOWABLE FROM A LISTING. The filter
             * reads their catalogue's metadata and the refusal reads the
             * source, which arrives only once it is cloned — so between the row
             * on screen and the widget in the bar there is a gap this window
             * cannot close, and saying so up front is the only honest thing it
             * can do about it. A row is an offer, not a promise.
             */
            Rectangle {
                id: notice
                width: parent.width
                height: 44
                color: Qt.rgba(root.cWarn.r, root.cWarn.g, root.cWarn.b, 0.10)

                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 2
                    color: root.cWarn
                }

                Column {
                    anchors { left: parent.left; leftMargin: 18
                              right: parent.right; rightMargin: 18
                              verticalCenter: parent.verticalCenter }
                    spacing: 2
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: "⚠ Written for Omarchy on Hyprland — not all of it runs here."
                        color: root.cWarn
                        font { family: root.uiFont; pixelSize: 11; bold: true }
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: "Quickshell.Hyprland is refused at install, by name. What loads can still have overlay parts that do nothing."
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: 11 }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: root.wash(0.10) }

            // ── Categories, and the list beside them ────────────────────
            Item {
                id: body
                width: parent.width
                height: parent.height - 62 - 1 - notice.height - 1 - 34

                /*
                 * The category column, the same one the software window grew
                 * and for the same reason: browsing by category and typing a
                 * name and hoping are different needs, and a list of nine
                 * hundred only catered for the second.
                 *
                 * It is driven entirely by root.categories, so nothing in here
                 * knows what a category IS — the catalogue decides what exists,
                 * this decides how to draw it.
                 */
                Rectangle {
                    id: catPane
                    anchors { top: parent.top; left: parent.left; bottom: parent.bottom }
                    width: 200
                    color: Qt.rgba(root.cPanel.r, root.cPanel.g, root.cPanel.b, 0.6)

                    ListView {
                        anchors.fill: parent
                        anchors.topMargin: 6
                        clip: true
                        model: root.categories
                        spacing: 1

                        /* "All" is a row and not a clear button: the pane has to
                         * be able to say which of its rows you are standing in,
                         * and "none of them" is one of the answers. */
                        header: Rectangle {
                            readonly property bool current: root.category === ""
                            width: ListView.view.width
                            height: 30
                            color: current ? root.wash(0.16)
                                           : (allMa.containsMouse ? root.wash(0.08) : "transparent")
                            Text {
                                anchors { left: parent.left; leftMargin: 14
                                          verticalCenter: parent.verticalCenter }
                                text: "All plugins"
                                color: root.cAccent
                                font { family: root.uiFont; pixelSize: 12; bold: true }
                            }
                            Text {
                                anchors { right: parent.right; rightMargin: 12
                                          verticalCenter: parent.verticalCenter }
                                text: root.rows.length
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: 10 }
                            }
                            MouseArea {
                                id: allMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.category = ""
                            }
                        }

                        delegate: Rectangle {
                            id: catRow
                            required property var modelData
                            readonly property bool current: catRow.modelData.name === root.category
                            width: ListView.view.width
                            height: 28
                            color: catRow.current ? root.wash(0.16)
                                                  : (catMa.containsMouse ? root.wash(0.08) : "transparent")

                            Text {
                                anchors { left: parent.left; leftMargin: 14
                                          right: catCount.left; rightMargin: 8
                                          verticalCenter: parent.verticalCenter }
                                text: catRow.modelData.name
                                elide: Text.ElideRight
                                color: catRow.current ? root.cAccent : root.cText
                                font { family: root.uiFont; pixelSize: 12 }
                            }
                            /* Installed over total where any are, the way the
                             * software window counts — the number you want from
                             * a category you have already been shopping in is
                             * how much of it you took. */
                            Text {
                                id: catCount
                                anchors { right: parent.right; rightMargin: 12
                                          verticalCenter: parent.verticalCenter }
                                text: catRow.modelData.installed > 0
                                      ? catRow.modelData.installed + "/" + catRow.modelData.total
                                      : catRow.modelData.total
                                color: catRow.modelData.installed > 0 ? root.cAccent : root.cDim
                                font { family: root.uiFont; pixelSize: 10 }
                            }
                            MouseArea {
                                id: catMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                /* Pressing the row you are in steps back out,
                                 * so the pane never becomes a place you can
                                 * only leave by finding the All row. */
                                onClicked: root.category =
                                    catRow.current ? "" : catRow.modelData.name
                            }
                        }
                    }
                }

                Rectangle {
                    anchors { left: catPane.right; top: parent.top; bottom: parent.bottom }
                    width: 1
                    color: root.wash(0.10)
                }

            // ── The list ────────────────────────────────────────────────
            ListView {
                anchors { top: parent.top; left: catPane.right; leftMargin: 1
                          right: parent.right; bottom: parent.bottom }
                clip: true
                model: root.visibleRows
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
                            id: titleRow
                            spacing: 8
                            width: parent.width
                            Text {
                                id: nameText
                                width: Math.max(0, Math.min(implicitWidth, parent.width * 0.45))
                                elide: Text.ElideRight
                                text: row.modelData.name || row.modelData.id
                                color: root.cText
                                font { family: root.uiFont; pixelSize: 13; bold: true }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                /* ⚠ ELIDED, BECAUSE A REGISTRY ID IS LONG. The
                                 * reverse-DNS ones run to forty-odd characters
                                 * and would otherwise push the star count off
                                 * the end of the row. */
                                width: Math.max(0, Math.min(implicitWidth,
                                                parent.width - nameText.width - starText.width - 24))
                                elide: Text.ElideRight
                                text: row.modelData.id
                                color: root.cDim
                                font { family: "monospace"; pixelSize: 10 }
                            }
                            Text {
                                id: starText
                                anchors.verticalCenter: parent.verticalCenter
                                visible: text !== ""
                                text: (parseInt(row.modelData.stars || "0", 10) > 0)
                                      ? "★ " + row.modelData.stars : ""
                                color: root.cDim
                                font { family: root.uiFont; pixelSize: 10 }
                            }
                        }
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            /* The REASON wins over the description. A row that
                             * cannot run and shows only what it would have done
                             * is a row nobody can act on. */
                            /* The category, who wrote it, and who has vouched
                             * for it — `shipped` alone means it was loaded into
                             * a real synui bar before it was listed, and it is
                             * left unsaid on those rows because it is the
                             * baseline the others are being measured against. */
                            text: row.modelData.why !== ""
                                  ? "unsupported — " + row.modelData.why
                                  : row.modelData.desc
                                    + (row.modelData.category ? " · " + row.modelData.category : "")
                                    + (row.modelData.author ? " · " + row.modelData.author : "")
                                    + ((row.modelData.trust && row.modelData.trust !== "shipped")
                                       ? " · " + row.modelData.trust : "")
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
            }

            // ── Footer ──────────────────────────────────────────────────
            Rectangle {
                width: parent.width
                height: 34
                color: root.cPanel
                Text {
                    anchors { left: parent.left; leftMargin: 18
                              verticalCenter: parent.verticalCenter }
                    text: "Shipped plugins, then omarchyplugins.com — a plugin runs inside the bar's own process, and everything is off until you turn it on"
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: 11 }
                }
            }
        }
    }
