//@ pragma UseQApplication
import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "."

/*
 * wallhaven.qml — the wallpaper picker's other half: where more wallpapers
 * come from.
 *
 * Super+W lists what is already on the disk. This is a grid of what is on
 * wallhaven.cc — filtered by category, sorted by what is popular — and picking
 * one downloads it into ~/Pictures/Wallpapers and hands it to the picker's own
 * setter. So a wallpaper taken from here is a LOCAL wallpaper from then on: it
 * is in Super+W's list afterwards, and nothing has to remember where it came
 * from.
 *
 * Reached three ways, all the same window: Super+Ctrl+W, the "Browse
 * wallhaven.cc…" row in the Super+W picker, and `synui-wallhaven` at a prompt.
 *
 * ⚠ A SECOND QUICKSHELL ENTRY POINT, like the welcome guide, and for the same
 * reason: two bars ship, and a browser living inside the SYNAPSE bar would not
 * exist for anyone running Antiquity. It also costs nothing when closed —
 * dismissing it quits the process — and does not come back when game mode
 * restarts the bar.
 *
 * ⛔ THE NETWORK SWITCH IS THE SCRIPT'S, NOT THIS FILE'S. synui-wallhaven
 * refuses every command while it is off and the launcher checks before it ever
 * starts this, so a window that is on screen is one somebody has already opted
 * in to. Re-asking here would be a second copy of that rule to keep in step.
 *
 * ⚠ NOTHING HERE PARSES JSON. The script emits the same tab-separated records
 * every other --rec table in this project does, and the columns are matched by
 * NAME — so a field wallhaven adds or moves is the script's problem and not a
 * silent mis-read here.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
ShellRoot {
    id: root

    property bool open: true
    property string output: ""

    // The rows of the current page, and where in the listing we are.
    property var items: []
    property int page: 1
    property int lastPage: 1
    property int total: 0
    property bool busy: false
    property string error: ""

    // ⚠ THREE BITS, general/anime/people, which is wallhaven's own spelling and
    // is passed through rather than translated into something friendlier. The
    // chips below are the friendly half; this is what the API takes.
    property bool catGeneral: true
    property bool catAnime: true
    property bool catPeople: false
    readonly property string categories:
        (root.catGeneral ? "1" : "0") + (root.catAnime ? "1" : "0")
        + (root.catPeople ? "1" : "0")

    readonly property var sorts: [
        { id: "toplist",    label: "Popular" },
        { id: "date_added", label: "Latest"  },
        { id: "views",      label: "Most viewed" },
        { id: "random",     label: "Random"  }
    ]
    property int sortIndex: 0
    readonly property string sort: root.sorts[root.sortIndex].id

    property int selected: 0

    // ── asking the script ───────────────────────────────────────────────────
    //
    // ⚠ GUARDED ON `running`. Setting `running = true` on a quickshell Process
    // that is already running is a SILENT no-op, and the category chips are
    // exactly the sort of thing somebody clicks three times in a second.
    function reload() {
        if (searchProc.running) return
        root.busy = true
        root.error = ""
        searchProc.command = ["synui-wallhaven", "search", "--rec",
                              "--categories=" + root.categories,
                              "--sort=" + root.sort,
                              "--page=" + root.page]
        searchProc.running = true
    }

    // ⛔ AT LEAST ONE CATEGORY. "000" is a request wallhaven answers with
    // nothing at all, which on screen is indistinguishable from a network that
    // did not come back — so the last chip standing cannot be switched off.
    function toggleCat(which) {
        const on = [root.catGeneral, root.catAnime, root.catPeople]
        const n = on.filter(x => x).length
        if (n === 1 && on[which]) return
        if (which === 0) root.catGeneral = !root.catGeneral
        if (which === 1) root.catAnime = !root.catAnime
        if (which === 2) root.catPeople = !root.catPeople
        root.page = 1
        root.selected = 0
        root.reload()
    }

    function setSort(i) {
        if (i === root.sortIndex) return
        root.sortIndex = i
        root.page = 1
        root.selected = 0
        root.reload()
    }

    function goPage(d) {
        const want = root.page + d
        if (want < 1 || want > root.lastPage) return
        root.page = want
        root.selected = 0
        root.reload()
    }

    Process {
        id: searchProc
        stdout: StdioCollector {
            onStreamFinished: {
                const rows = []
                let cols = null
                for (const raw of String(this.text).split("\n")) {
                    const line = raw.replace(/\s+$/, "")
                    if (line === "") continue
                    const f = line.split("\t")
                    // The paging facts ride on a last line rather than a second
                    // command — see the script. `#page current last total`.
                    if (f[0] === "#page") {
                        root.page = Number(f[1]) || 1
                        root.lastPage = Number(f[2]) || 1
                        root.total = Number(f[3]) || 0
                        continue
                    }
                    if (cols === null) { cols = f; continue }   // the header
                    const o = {}
                    for (let i = 0; i < cols.length; i++) o[cols[i]] = f[i] || ""
                    rows.push(o)
                }
                root.items = rows
            }
        }
        // ⚠ The script says why on stderr — off, no network, a name that did
        // not resolve — and a grid that is simply empty says none of it.
        stderr: StdioCollector {
            onStreamFinished: {
                const t = String(this.text).replace(/\s+$/, "")
                if (t !== "") root.error = t.replace(/^synui-wallhaven:\s*/, "")
            }
        }
        onExited: root.busy = false
    }

    // Downloading and setting. The script does both; this only has to say that
    // something is happening, because a four-megabyte download over a slow line
    // is long enough for a button to look dead.
    property string applying: ""

    function apply(it) {
        if (setProc.running || !it) return
        root.applying = it.id
        setProc.command = ["synui-wallhaven", "set", it.id]
        setProc.running = true
    }

    Process {
        id: setProc
        stderr: StdioCollector {
            onStreamFinished: {
                const t = String(this.text).replace(/\s+$/, "")
                if (t !== "") root.error = t.replace(/^synui-wallhaven:\s*/, "")
            }
        }
        onExited: root.applying = ""
    }

    function close() { Qt.quit() }

    Component.onCompleted: root.reload()

    /*
     * ⚠ The launcher toggles ACROSS A PROCESS BOUNDARY: closing quits, so
     * "closed" and "not running" are one state. A `toggle` that reaches a
     * running instance means the window is up and the key was pressed again.
     */
    IpcHandler {
        target: "wallhaven"

        function show(output: string): void { root.output = output; root.open = true }
        function hide(): void               { root.close() }
        function toggle(output: string): void {
            if (root.open && (!output || output === root.output)) root.close()
            else { root.output = output; root.open = true }
        }
    }

    Variants {
        model: Quickshell.screens

        PanelWindow {
            id: win
            required property var modelData
            screen: modelData

            visible: root.open
                     && (root.output === modelData.name || root.output === "")

            // The whole screen, so a click on the desktop dismisses: no Wayland
            // protocol tells a layer surface that a click landed elsewhere, so
            // the only way to hear it is to be the surface it lands on.
            anchors { top: true; left: true; right: true; bottom: true }
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.namespace: "synui-glass"
            focusable: true
            color: "transparent"

            readonly property real u: Math.max(10, Math.round(height / 54))

            MouseArea {
                anchors.fill: parent
                onClicked: root.close()
            }

            Rectangle {
                id: card
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.86, win.u * 92)
                height: Math.min(parent.height * 0.86, win.u * 56)
                radius: win.u * 0.9
                color: Theme.popupBg
                border.width: Math.max(1, win.u * 0.06)
                border.color: Theme.wpAccent

                // Swallow clicks that land on the card, or every pick would also
                // be a click on the dismiss catcher underneath.
                MouseArea { anchors.fill: parent }

                Column {
                    anchors.fill: parent
                    anchors.margins: win.u * 1.2
                    spacing: win.u * 0.8

                    // ── the header ──────────────────────────────────────────
                    Item {
                        width: parent.width
                        height: title.height

                        Text {
                            id: title
                            text: "WALLHAVEN"
                            color: Theme.fgDim
                            font.pixelSize: win.u * 0.95
                            font.family: Theme.fontFamily
                            font.letterSpacing: win.u * 0.14
                            font.bold: true
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: title.verticalCenter
                            text: root.busy ? "loading…"
                                : root.error !== "" ? root.error
                                : root.total > 0
                                  ? "page " + root.page + " of " + root.lastPage
                                  : ""
                            color: root.error !== "" ? Theme.red : Theme.fgDim
                            font.pixelSize: win.u * 0.85
                            font.family: Theme.fontFamily
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, card.width * 0.6)
                        }
                    }

                    // ── categories and sorting ──────────────────────────────
                    Flow {
                        width: parent.width
                        spacing: win.u * 0.4

                        Repeater {
                            model: [
                                { label: "General", on: root.catGeneral, i: 0 },
                                { label: "Anime",   on: root.catAnime,   i: 1 },
                                { label: "People",  on: root.catPeople,  i: 2 }
                            ]
                            Chip {
                                required property var modelData
                                u: win.u
                                label: modelData.label
                                on: modelData.on
                                onPicked: root.toggleCat(modelData.i)
                            }
                        }

                        Item { width: win.u * 1.2; height: 1 }

                        Repeater {
                            model: root.sorts
                            Chip {
                                required property var modelData
                                required property int index
                                u: win.u
                                label: modelData.label
                                on: root.sortIndex === index
                                onPicked: root.setSort(index)
                            }
                        }
                    }

                    // ── the grid ────────────────────────────────────────────
                    GridView {
                        id: grid
                        width: parent.width
                        height: parent.height - y - foot.height - win.u * 1.6
                        clip: true
                        // A view that scrolls says so.
                        ScrollBar.vertical: ScrollBar {
                            policy: grid.contentHeight > grid.height
                                    ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        }

                        cellWidth: Math.floor(grid.width / 4)
                        cellHeight: Math.round(grid.cellWidth * 0.62)
                        model: root.items
                        currentIndex: root.selected
                        onCurrentIndexChanged: grid.positionViewAtIndex(
                            grid.currentIndex, GridView.Contain)

                        delegate: Item {
                            id: cell
                            required property var modelData
                            required property int index
                            width: grid.cellWidth
                            height: grid.cellHeight

                            readonly property bool chosen: root.selected === cell.index

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: win.u * 0.25
                                radius: win.u * 0.4
                                color: "#00000000"
                                border.width: cell.chosen ? Math.max(2, win.u * 0.14) : 0
                                border.color: Theme.wpAccent
                                clip: true

                                Image {
                                    id: thumb
                                    anchors.fill: parent
                                    anchors.margins: cell.chosen ? win.u * 0.14 : 0
                                    // ⚠ A REMOTE URL, loaded by Qt itself —
                                    // asynchronously, with its own cache. This
                                    // is the whole reason the browser is QML
                                    // and not compositor C: the alternative was
                                    // a curl worker thread, a JPEG decoder and
                                    // a thumbnail cache inside the process that
                                    // also draws every window on the desktop.
                                    source: cell.modelData.thumb || ""
                                    asynchronous: true
                                    cache: true
                                    fillMode: Image.PreserveAspectCrop
                                    sourceSize.width: Math.round(grid.cellWidth)
                                }

                                // A tile that has not arrived says so, rather
                                // than being an empty rectangle among pictures.
                                Text {
                                    anchors.centerIn: parent
                                    visible: thumb.status !== Image.Ready
                                    text: thumb.status === Image.Error ? "—" : "…"
                                    color: Theme.fgDim
                                    font.pixelSize: win.u * 1.2
                                    font.family: Theme.fontFamily
                                }

                                // The resolution, because a 5000-wide picture
                                // and a phone wallpaper look identical at this
                                // size and only one of them belongs on a
                                // monitor.
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.bottom: parent.bottom
                                    width: res.implicitWidth + win.u * 0.5
                                    height: res.implicitHeight + win.u * 0.25
                                    color: "#c0000000"
                                    visible: thumb.status === Image.Ready
                                    Text {
                                        id: res
                                        anchors.centerIn: parent
                                        text: cell.modelData.resolution || ""
                                        color: "#ffffff"
                                        font.pixelSize: win.u * 0.6
                                        font.family: Theme.fontFamily
                                    }
                                }

                                // While its download is running.
                                Rectangle {
                                    anchors.fill: parent
                                    visible: root.applying === cell.modelData.id
                                    color: "#a0000000"
                                    Text {
                                        anchors.centerIn: parent
                                        text: "setting…"
                                        color: "#ffffff"
                                        font.pixelSize: win.u * 0.7
                                        font.family: Theme.fontFamily
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: { root.selected = cell.index; root.apply(cell.modelData) }
                            }
                        }
                    }

                    // ── the footer ──────────────────────────────────────────
                    Text {
                        id: foot
                        width: parent.width
                        text: "Enter set as wallpaper   ·   ← → page   ·   "
                              + "1 2 3 categories   ·   S sort   ·   Esc close"
                        color: Theme.fgDim
                        font.pixelSize: win.u * 0.75
                        font.family: Theme.fontFamily
                        elide: Text.ElideRight
                    }
                }
            }

            Keys.onPressed: (e) => {
                switch (e.key) {
                case Qt.Key_Escape: root.close(); break
                case Qt.Key_Left:
                    if (root.selected % 4 === 0) root.goPage(-1)
                    else root.selected = Math.max(0, root.selected - 1)
                    break
                case Qt.Key_Right:
                    if (root.selected % 4 === 3 || root.selected === root.items.length - 1)
                        root.goPage(1)
                    else root.selected = Math.min(root.items.length - 1, root.selected + 1)
                    break
                case Qt.Key_Up:
                    root.selected = Math.max(0, root.selected - 4); break
                case Qt.Key_Down:
                    root.selected = Math.min(root.items.length - 1, root.selected + 4); break
                case Qt.Key_PageDown: root.goPage(1); break
                case Qt.Key_PageUp:   root.goPage(-1); break
                case Qt.Key_Return:
                case Qt.Key_Enter:
                case Qt.Key_Space:
                    root.apply(root.items[root.selected]); break
                case Qt.Key_1: root.toggleCat(0); break
                case Qt.Key_2: root.toggleCat(1); break
                case Qt.Key_3: root.toggleCat(2); break
                case Qt.Key_S:
                    root.setSort((root.sortIndex + 1) % root.sorts.length); break
                case Qt.Key_R: root.reload(); break
                default: return
                }
                e.accepted = true
            }
        }
    }

    // A chip: the same shape the settings panels use — a label that is on or
    // off and answers a click.
    component Chip: Rectangle {
        id: chip
        property real u: 12
        property string label: ""
        property bool on: false
        signal picked()

        implicitWidth: chipText.implicitWidth + chip.u * 1.2
        implicitHeight: chipText.implicitHeight + chip.u * 0.5
        radius: height / 2
        // ⛔ A WASH, NOT A FILL, AND THE INK STAYS BRIGHT. Filling with the
        // accent and inking with the popup background is the obvious way round
        // and it is unreadable: the wallpaper accent is measured off whatever
        // picture is on the desktop, so on a dark wallpaper — which is most of
        // them — a chip that is ON becomes dark text on a dark fill. Caught in
        // the rig's first screenshot, where General, Anime and Popular were
        // the three chips nobody could read.
        //
        // The state is carried by the border and the brightness of the ink
        // instead, both of which hold whatever colour the accent turns out to
        // be.
        //
        // ⚠ AND THE WASH IS NOT DERIVED FROM THE ACCENT. Theme.wpAccent is a
        // STRING — "#rrggbb" — not a color, so `.r`/`.g`/`.b` are not on it and
        // Qt.rgba() of them is three undefineds that qmllint reports and QML
        // coerces to something arbitrary. A fixed wash, chosen per theme
        // because a light palette needs the opposite one.
        color: chip.on ? (Theme.isLight ? "#18000000" : "#22ffffff")
                       : "#00000000"
        border.width: Math.max(1, chip.u * 0.06)
        border.color: chip.on ? Theme.wpAccent : Theme.fgDim

        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.on ? Theme.fg : Theme.fgDim
            font.pixelSize: chip.u * 0.8
            font.family: Theme.fontFamily
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.picked()
        }
    }
}
