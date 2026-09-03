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
 * Reached four ways, all the same window: Super+Ctrl+W, `w` or the [w] button
 * in the Super+W picker, the "Wallhaven" row at the bottom of that picker's
 * built-ins, and `synui-wallhaven` at a prompt. `w` HERE goes back to the
 * picker, so one key flips between what is on the disk and what is not.
 *
 * ⚠ A SECOND QUICKSHELL ENTRY POINT, like the welcome guide, and for the same
 * reason: two bars ship, and a browser living inside the SYNAPSE bar would not
 * exist for anyone running Antiquity. It also costs nothing when closed —
 * dismissing it quits the process — and does not come back when game mode
 * restarts the bar.
 *
 * ⛔ THE NETWORK SWITCH IS STILL THE SCRIPT'S — THIS IS ONLY WHERE IT IS ASKED.
 * `synui-wallhaven status` and `on` are the whole of it here; the state file,
 * what counts as off and which commands refuse are all the script's, so there
 * is one copy of the rule and this is its face. The launcher used to refuse
 * before it ever started this window, which meant Super+Ctrl+W spawned a
 * process that printed to a stderr nobody was reading and the key looked dead.
 * So: the window opens on the question while the switch is off, and asks for
 * nothing from wallhaven.cc until it has been answered.
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

    /*
     * ⛔ WHICH MONITOR, AND WHY IT COMES FROM OUTSIDE. No Wayland protocol tells
     * a layer-shell client where keyboard focus is, so this window cannot work
     * it out; synui knows, because it is answering the keypress that asked for
     * the browser, and synui-wallhaven passes the name on.
     *
     * ⚠ AN ENVIRONMENT VARIABLE FOR THE FIRST WINDOW, IPC FOR EVERY ONE AFTER.
     * `quickshell -p file.qml` cannot be handed a positional argument, and the
     * IPC path only exists once a process is running — so the first window, the
     * one this is for, could not be told any other way. Same split as the
     * welcome guide's SYNUI_WELCOME_OUTPUT.
     *
     * Empty means nobody said (started from a prompt), and that resolves to the
     * FIRST screen rather than to all of them — see `visible` below.
     */
    property string output: Quickshell.env("SYNUI_WALLHAVEN_OUTPUT") || ""

    // The rows of the current page, and where in the listing we are.
    property var items: []
    property int page: 1
    property int lastPage: 1
    property int total: 0
    property bool busy: false
    property string error: ""

    // ⛔ THE NETWORK SWITCH. -1 while `status` is still running: an "unknown"
    // that is neither on nor off matters, because treating it as off for the
    // instant before the answer lands would flash the consent pane in the face
    // of somebody who turned this on months ago.
    property int netOn: -1

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
        // ⛔ Nothing leaves this machine while the switch is off — the script
        // would refuse anyway, but a refusal rendered as a red error string is
        // not what "you have not turned this on" should look like.
        if (root.netOn !== 1) return
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

    // ── the switch ──────────────────────────────────────────────────────────
    Process {
        id: statusProc
        command: ["synui-wallhaven", "status"]
        stdout: StdioCollector {
            onStreamFinished: {
                root.netOn = String(this.text).trim() === "on" ? 1 : 0
            }
        }
    }

    Process {
        id: onProc
        command: ["synui-wallhaven", "on"]
        // ⚠ The script is the one that writes the state file; this only asks
        // for it and believes the exit status.
        onExited: (code) => { if (code === 0) root.netOn = 1 }
    }

    function turnOn() {
        if (onProc.running || root.netOn === 1) return
        onProc.running = true
    }

    // The first search waits for the answer rather than racing it.
    onNetOnChanged: if (root.netOn === 1) root.reload()

    // ── back to the picker ──────────────────────────────────────────────────
    //
    // `w` here opens the Super+W wallpaper picker, and `w` there opens this —
    // one key flips between what is already on the disk and where more comes
    // from.
    //
    // ⛔ AND THIS WINDOW GOES AWAY. It is a focusable full-screen layer surface
    // and the picker is a compositor-drawn modal: leave both up and two
    // full-screen surfaces are asking for the keyboard, which is a picker
    // nobody can drive. The quit waits for synctl to exit — a detached child
    // holding this process's pipes is a child that gets SIGPIPE'd.
    Process {
        id: pickProc
        command: ["synctl", "dispatch", "wallpaper"]
        onExited: root.close()
    }

    function openPicker() {
        if (pickProc.running) return
        pickProc.running = true
    }

    function close() { Qt.quit() }

    Component.onCompleted: statusProc.running = true

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

            /*
             * ⛔ ONE WINDOW, NOT ONE PER SCREEN. `root.output === ""` used to
             * mean EVERY monitor, and on a multi-monitor desk that is what it
             * did: the browser opened on all of them at once, each with its own
             * grid and its own keyboard focus to lose. An unknown output is one
             * screen's worth of not knowing, not three windows — so it falls
             * back to the first screen, which is the rule Osd.qml already uses
             * for the same reason.
             */
            visible: root.open
                     && (root.output === modelData.name
                         || (root.output === ""
                             && modelData.name === Quickshell.screens[0].name))

            // The whole screen, so a click on the desktop dismisses: no Wayland
            // protocol tells a layer surface that a click landed elsewhere, so
            // the only way to hear it is to be the surface it lands on.
            anchors { top: true; left: true; right: true; bottom: true }
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.namespace: "synui-glass"
            focusable: true
            color: "transparent"

            // ⚠ Qt.callLater, not a direct call: the item has to exist and the
            // surface has to be mapped before it can take focus. `visible` is
            // already true when this window is built, so the completion handler
            // is the one that fires on the first open and the change handler is
            // for every toggle after it.
            Component.onCompleted: Qt.callLater(keys.forceActiveFocus)
            onVisibleChanged: if (visible) Qt.callLater(keys.forceActiveFocus)

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
                        height: Math.max(title.height, wpBtn.height)

                        Text {
                            id: title
                            anchors.verticalCenter: parent.verticalCenter
                            text: "WALLHAVEN"
                            color: Theme.fgDim
                            font.pixelSize: win.u * 0.95
                            font.family: Theme.fontFamily
                            font.letterSpacing: win.u * 0.14
                            font.bold: true
                        }

                        // The way back. A BUTTON and its own label: it opens
                        // the wallpaper picker, and it says so whatever the
                        // state of anything else on this window.
                        Chip {
                            id: wpBtn
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            u: win.u
                            label: "Wallpapers"
                            onPicked: root.openPicker()
                        }

                        Text {
                            anchors.right: wpBtn.left
                            anchors.rightMargin: win.u * 0.6
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.netOn === -1 ? "checking…"
                                : root.netOn === 0 ? "off"
                                : root.busy ? "loading…"
                                : root.error !== "" ? root.error
                                : root.total > 0
                                  ? "page " + root.page + " of " + root.lastPage
                                  : ""
                            color: root.error !== "" ? Theme.red : Theme.fgDim
                            font.pixelSize: win.u * 0.85
                            font.family: Theme.fontFamily
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, card.width * 0.45)
                        }
                    }

                    // ── categories and sorting ──────────────────────────────
                    Flow {
                        width: parent.width
                        spacing: win.u * 0.4
                        visible: root.netOn === 1

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
                        visible: root.netOn === 1
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

                    // ── the switch, while it is off ─────────────────────────
                    //
                    // ⛔ THE WHOLE POINT OF THIS PANE. Everything else in this
                    // window talks to wallhaven.cc; this is the one screen that
                    // does not, and it is what a first Super+Ctrl+W lands on.
                    // The button is deliberately the only way past it, and it
                    // says exactly what saying yes costs.
                    Item {
                        width: parent.width
                        height: parent.height - y - foot.height - win.u * 1.6
                        visible: root.netOn === 0

                        Column {
                            anchors.centerIn: parent
                            width: Math.min(parent.width * 0.8, win.u * 40)
                            spacing: win.u * 0.9

                            Text {
                                width: parent.width
                                text: "Wallhaven is off"
                                color: Theme.fg
                                font.pixelSize: win.u * 1.3
                                font.family: Theme.fontFamily
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                width: parent.width
                                text: "Browsing here asks wallhaven.cc for "
                                      + "thumbnails, and the one you pick is "
                                      + "downloaded into ~/Pictures/Wallpapers. "
                                      + "It is the only part of the wallpaper "
                                      + "picker that leaves this machine, so it "
                                      + "is off until you say otherwise — "
                                      + "nothing has been asked for yet."
                                color: Theme.fgDim
                                font.pixelSize: win.u * 0.85
                                font.family: Theme.fontFamily
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Item {
                                width: parent.width
                                height: turnOnBtn.height
                                Chip {
                                    id: turnOnBtn
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    u: win.u * 1.3
                                    label: onProc.running ? "turning on…"
                                                          : "Turn on"
                                    on: true
                                    onPicked: root.turnOn()
                                }
                            }
                            Text {
                                width: parent.width
                                text: "The wallpapers already on this machine "
                                      + "are in the Super+W picker — press w."
                                color: Theme.fgDim
                                font.pixelSize: win.u * 0.75
                                font.family: Theme.fontFamily
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    // ── the footer ──────────────────────────────────────────
                    Text {
                        id: foot
                        width: parent.width
                        text: root.netOn === 1
                              ? "Enter set as wallpaper   ·   ← → page   ·   "
                                + "1 2 3 categories   ·   S sort   ·   "
                                + "w wallpapers   ·   Esc close"
                              : "Enter turn on   ·   w wallpapers   ·   Esc close"
                        color: Theme.fgDim
                        font.pixelSize: win.u * 0.75
                        font.family: Theme.fontFamily
                        elide: Text.ElideRight
                    }
                }
            }

            /*
             * ⛔ THE KEYS HAVE TO HANG OFF AN ITEM WITH ACTIVE FOCUS, NOT OFF
             * THE WINDOW. Layer-shell handing this surface keyboard focus is
             * not enough for `Keys.onPressed` to fire — Qt needs a focused item
             * INSIDE it, which Ui/KeyboardPanel.qml documents and StartMenu.qml
             * does the same way. On the window, as this shipped in 590, every
             * key the footer advertises answered nothing: Escape did not close
             * it, Enter set no wallpaper, the arrows did not move. Nothing said
             * so — a dead key handler is not a warning, a crash, or a lint.
             *
             * ⛔ AND `Keys.BeforeItem`, because the grid is a Flickable and its
             * built-in arrow scrolling would otherwise eat exactly the arrows
             * that move the selection.
             */
            Item {
                id: keys
                anchors.fill: parent
                focus: true
                Keys.priority: Keys.BeforeItem

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
                        // While the switch is off there is nothing to set, and the
                        // one thing on screen is the button this answers.
                        if (root.netOn !== 1) root.turnOn()
                        else root.apply(root.items[root.selected])
                        break
                    case Qt.Key_W:
                        // The picker's `w` opens this window; this one goes back.
                        root.openPicker(); break
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
