import QtQuick
import Quickshell
import Quickshell.Io
import ".."

/*
 * Where a post-it is actually typed into.
 *
 * A SECOND surface, mapped only while editing, because mapping is the only
 * moment the compositor hands a layer surface the keyboard (layer.c,
 * layer_surface_map) — there is no click-to-focus path for layer surfaces and a
 * later change of keyboard_interactivity is not acted on. So "start editing"
 * has to mean "map a window", which is exactly the shape StartMenu.qml has and
 * the reason it works.
 *
 * Full-screen for StartMenu's reason too: no Wayland protocol tells a layer
 * surface that the pointer went down somewhere else, so the only way to hear
 * the click that dismisses this is to be the surface it lands on.
 *
 * ONE editor, however many notes there are: PostItState.editing names the note
 * it is open on, and clicking a different note while it is up saves the one it
 * was on and follows.
 */
PanelWindow {
    id: root

    required property var modelData
    screen: modelData

    // Which note this is open on. NOT read straight out of PostItState: the
    // editor has to know what it was editing at the moment that changes, or a
    // note switched away from is a note whose last sentence was never written.
    property string active: ""

    visible: WidgetState.postit
             && PostItState.editing !== ""
             && modelData.name === WidgetState.primaryOutput

    anchors { top: true; left: true; right: true; bottom: true }

    // NEVER reserve space. A transient full-screen panel with an exclusive zone
    // would shove every window on the monitor around each time it opened — the
    // trap Osd.qml and StartMenu.qml both document.
    exclusionMode: ExclusionMode.Ignore

    // The whole point of the second surface. Mapping this grants the keyboard;
    // unmapping it hands focus back to the window underneath.
    focusable: true
    color: "transparent"

    /*
     * One function for every way the editor can change what it is on, because
     * there are three of them and they used to be two handlers that did not
     * quite agree:
     *
     *  - it opened          (visible went true)
     *  - it was dismissed   (editing went "")
     *  - another note was clicked while it was up (editing went A -> B, and
     *    `visible` never changed at all, which is why a visibility handler
     *    alone left the editor showing note A's text under note B's title)
     *
     * Whatever is in the box is saved on the way out of each of them.
     */
    function sync() {
        const want = root.visible ? PostItState.editing : ""
        if (want === root.active) return

        if (root.active !== "") PostItState.save(root.active, edit.text)
        root.active = want

        if (want !== "") {
            // Seeded, not bound. A binding would fight the reload our own save
            // triggers and could overwrite what is being typed.
            edit.text = PostItState.textOf(want)
            edit.cursorPosition = edit.text.length
            edit.forceActiveFocus()
        } else if (PostItState.editing !== "") {
            // Gone while still flagged as editing means the WIDGET was switched
            // off underneath the editor — Space in the manager panel clears the
            // whole desktop, and that is a routine thing to press. Dropping the
            // flag matters: left set, the editor springs open holding the
            // keyboard the next time the widget comes back on, over whatever
            // window was focused. (Re-enters here, and returns immediately —
            // `active` is already "".)
            PostItState.editing = ""
        }
        menu.open = false
    }

    onVisibleChanged: root.sync()
    Connections {
        target: PostItState
        function onEditingChanged() { root.sync() }
    }

    function commit() { PostItState.editing = "" }

    // Everywhere that is not the box: press to save and close. Press rather
    // than click, as in StartMenu, so a press-drag-release that began outside
    // never leaves the editor hanging around.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: {
            // A click away from an open menu closes the MENU. Closing both at
            // once would make every mis-aimed press throw the editor away too.
            if (menu.open) { menu.open = false; return }
            root.commit()
        }
    }

    /*
     * Where the note currently IS, which is no longer a constant — it can be
     * dragged. Resolved the same way WidgetFrame resolves it, from the same
     * store and with the same per-note home corner, so the editor follows the
     * note without the note having to tell it.
     */
    readonly property string noteEdgeH: WidgetLayout.edgeH(active, "left")
    readonly property string noteEdgeV: WidgetLayout.edgeV(active, "bottom")
    readonly property int noteX: WidgetLayout.x(active, PostItState.homeX(active))
    readonly property int noteY: WidgetLayout.y(active, PostItState.homeY(active))

    Rectangle {
        id: box

        // Over the note rather than anywhere else: the point of writing is
        // seeing what you write, and a box that opens elsewhere means hunting
        // for it. Shares the note's corner and margins.
        //
        // The vertical needs the bar added back for a note anchored to the TOP.
        // Widget margins are measured in the usable area; this surface sets
        // ExclusionMode.Ignore and so covers the whole output, and the two
        // differ by exactly the bar's exclusive zone. The bottom edge is common
        // to both, which is why only one branch compensates.
        x: root.noteEdgeH === "left" ? root.noteX
                                     : parent.width - width - root.noteX
        y: root.noteEdgeV === "top" ? root.noteY + Theme.barHeight
                                    : parent.height - height - root.noteY
        width: 320
        height: 260
        radius: 8
        // Solid, not popupBg: this one is being read closely and a translucent
        // panel over a busy wallpaper is where that stops being true.
        color: Theme.bgSolid
        border.color: Theme.yellow
        border.width: 1

        // Swallow presses inside the box, or the catcher above would read
        // "clicked in the editor" as "clicked away from it".
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: {
                if (menu.open) { menu.open = false; return }
                edit.forceActiveFocus()
            }
        }

        Text {
            id: head
            anchors { top: parent.top; left: parent.left; margins: 10 }
            // Which note, once there is more than one to be confused about.
            text: PostItState.ids.length > 1
                  ? "note " + (PostItState.indexOf(root.active) + 1)
                  : "note"
            color: Theme.yellow
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }

        Flickable {
            id: scroll
            anchors {
                top: head.bottom; topMargin: 8
                left: parent.left; leftMargin: 10
                right: parent.right; rightMargin: 10
                bottom: foot.top; bottomMargin: 6
            }
            clip: true
            contentWidth: width
            contentHeight: edit.implicitHeight
            // Keep the caret in view as the note grows past the box.
            onContentHeightChanged: contentY =
                Math.max(0, Math.min(contentY, contentHeight - height))

            TextEdit {
                id: edit
                width: scroll.width
                color: Theme.fg
                selectionColor: Theme.activeBg
                selectedTextColor: Theme.fg
                font.family: Theme.fontFamily
                font.pixelSize: 13
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                persistentSelection: true

                /*
                 * Clipboard, spelled out.
                 *
                 * TextEdit does handle these itself, and on this desktop that
                 * was not enough to make them work — so they are dispatched
                 * here, where what happens on Ctrl+V is a line of code rather
                 * than a chain of defaults through a layer surface that is not
                 * an ordinary window. `matches` is used rather than a raw
                 * keycode so the platform's own idea of these sequences
                 * (Shift+Insert, Ctrl+Insert) keeps working.
                 */
                Keys.onPressed: (e) => {
                    if (e.matches(StandardKey.Paste))          { root.pasteInto(); e.accepted = true }
                    else if (e.matches(StandardKey.Copy))      { edit.copy();      e.accepted = true }
                    else if (e.matches(StandardKey.Cut))       { edit.cut();       e.accepted = true }
                    else if (e.matches(StandardKey.SelectAll)) { edit.selectAll(); e.accepted = true }
                    else if (e.matches(StandardKey.Undo))      { edit.undo();      e.accepted = true }
                    else if (e.matches(StandardKey.Redo))      { edit.redo();      e.accepted = true }
                }

                // Escape saves and closes. Every other panel on this desktop
                // closes on escape, and there is nothing here worth throwing an
                // edit away over.
                Keys.onEscapePressed: root.commit()

                // Not committed per keystroke — that would be one atomic rename
                // per character. Idle for a moment and it lands, so a session
                // that dies mid-note loses a sentence at worst.
                onTextChanged: idle.restart()
            }
        }

        /*
         * The other two mouse buttons over the text.
         *
         * Only those two are accepted, so a left press is not handled here and
         * falls through to the TextEdit underneath — which keeps the caret, the
         * drag-select and the double-click-a-word it already had.
         */
        MouseArea {
            anchors.fill: scroll
            acceptedButtons: Qt.RightButton | Qt.MiddleButton

            onPressed: (m) => {
                edit.forceActiveFocus()
                const at = edit.positionAt(m.x + scroll.contentX,
                                           m.y + scroll.contentY)
                if (m.button === Qt.MiddleButton) {
                    // Middle-click pastes the PRIMARY selection: the thing you
                    // get by dragging over text anywhere else on the desktop.
                    // It is not the clipboard and Qt has no API for it at all,
                    // which is why this is the one path that always goes out to
                    // wl-clipboard (a hard dependency of this package).
                    //
                    // It drops in where the click was and takes nothing away
                    // with it — a middle-click is an insertion, not a replace.
                    edit.deselect()
                    edit.cursorPosition = at
                    root.pasteAt(at, true)
                    return
                }
                if (edit.selectionStart === edit.selectionEnd) edit.cursorPosition = at
                const p = mapToItem(null, m.x, m.y)
                menu.popup(p.x, p.y)
            }
        }

        Timer {
            id: idle
            interval: 1500
            onTriggered: if (root.visible) PostItState.save(root.active, edit.text)
        }

        Text {
            id: foot
            anchors { bottom: parent.bottom; left: parent.left; margins: 8 }
            text: "esc saves and closes · right-click to copy and paste"
            color: Theme.fgDim
            font.family: Theme.fontFamily
            font.pixelSize: 9
        }
    }

    /* ── Paste ──────────────────────────────────────────────────────────────
     *
     * Qt first, because a paste that is already in this process is instant and
     * needs nobody. `canPaste` false does NOT reliably mean the clipboard is
     * empty here: a Wayland client is only offered the selection while it holds
     * keyboard focus, and this surface is a layer surface that takes focus at
     * map time — so an offer that never arrived and a clipboard with nothing in
     * it look exactly the same from inside. Asking the compositor is the only
     * way to tell the difference, and wl-paste asking it is cheap enough that
     * being wrong costs one process.
     */
    function pasteInto() { root.pasteAt(edit.cursorPosition, false) }

    function pasteAt(at, primary) {
        if (!primary && edit.canPaste) { edit.paste(); return }
        clip.at = at
        clip.running = false
        clip.command = primary ? ["wl-paste", "--primary", "--no-newline"]
                               : ["wl-paste", "--no-newline"]
        clip.running = true
    }

    Process {
        id: clip
        // Where the text goes when it arrives. Recorded at request time: this
        // is a round trip through another process, and the caret may have moved
        // by the time it lands.
        property int at: 0

        stdout: StdioCollector {
            onStreamFinished: {
                const t = this.text
                if (t === "") return           // nothing in that selection

                // A paste over a selection replaces it, the same as typing
                // would. Middle-click never gets here with one — it deselects
                // first, because an insertion that ate a selection somewhere
                // else in the note would be a surprise.
                let at = clip.at
                if (edit.selectionStart !== edit.selectionEnd) {
                    at = edit.selectionStart
                    edit.remove(edit.selectionStart, edit.selectionEnd)
                }

                at = Math.max(0, Math.min(at, edit.length))
                edit.insert(at, t)
                edit.cursorPosition = at + t.length
                edit.forceActiveFocus()
            }
        }
    }

    /* ── The right-click menu ───────────────────────────────────────────────
     *
     * Drawn on THIS surface rather than in a window of its own. A popup would
     * be a third layer surface for four lines of text, and this one already
     * covers the whole output — which is also why nothing has to be done about
     * a menu opened near the bottom edge of the box.
     */
    Rectangle {
        id: menu

        property bool open: false

        function popup(px, py) {
            menu.x = Math.max(4, Math.min(px, root.width - menu.width - 4))
            menu.y = Math.max(4, Math.min(py, root.height - menu.height - 4))
            menu.open = true
        }

        visible: menu.open
        width: 132
        height: items.height + 8
        radius: 4
        color: Theme.bgSolid
        border.color: Theme.yellow
        border.width: 1
        opacity: menu.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.animFast } }

        Column {
            id: items
            y: 4
            width: parent.width

            MenuRow {
                label: "cut";        enabled: edit.selectedText !== ""
                onChosen: { edit.cut(); menu.open = false }
            }
            MenuRow {
                label: "copy";       enabled: edit.selectedText !== ""
                onChosen: { edit.copy(); menu.open = false }
            }
            MenuRow {
                label: "paste"
                onChosen: { root.pasteInto(); menu.open = false }
            }
            MenuRow {
                label: "select all"; enabled: edit.text !== ""
                onChosen: { edit.selectAll(); menu.open = false }
            }
        }
    }

    // Self-contained, as WidgetFrame's Chamfered is: an inline component is its
    // own type and must not reach for ids in the file around it.
    component MenuRow: Rectangle {
        id: row

        property string label: ""
        signal chosen()

        width: parent ? parent.width : 0
        height: 20
        color: ma.containsMouse && row.enabled ? Theme.hoverBg : "transparent"

        Text {
            anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
            text: row.label
            color: row.enabled ? Theme.fg : Theme.fgDim
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }

        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            enabled: row.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: row.chosen()
        }
    }
}
