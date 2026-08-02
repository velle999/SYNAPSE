import QtQuick
import Quickshell
import ".."

/*
 * Where the post-it is actually typed into.
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
 */
PanelWindow {
    id: root

    required property var modelData
    screen: modelData

    visible: WidgetState.postit
             && PostItState.editing
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

    onVisibleChanged: {
        if (visible) {
            // Seeded, not bound. A binding would fight the reload our own save
            // triggers and could overwrite what is being typed.
            edit.text = PostItState.note
            edit.cursorPosition = edit.text.length
            edit.forceActiveFocus()
        } else if (PostItState.editing) {
            // Gone while still flagged as editing means the WIDGET was switched
            // off underneath the editor — Space in the manager panel clears the
            // whole desktop, and that is a routine thing to press. Save and drop
            // the flag: left set it loses whatever was typed (the idle timer
            // below refuses to fire once this is invisible) and, worse, the
            // editor springs open holding the keyboard the next time the widget
            // comes back on, over whatever window was focused.
            root.commit()
        }
    }

    function commit() {
        PostItState.save(edit.text)
        PostItState.editing = false
    }

    // Everywhere that is not the box: press to save and close. Press rather
    // than click, as in StartMenu, so a press-drag-release that began outside
    // never leaves the editor hanging around.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: root.commit()
    }

    /*
     * Where the note currently IS, which is no longer a constant — it can be
     * dragged. Resolved the same way WidgetFrame resolves it, from the same
     * store, so the editor follows the note without the note having to tell it.
     */
    readonly property string noteEdgeH: WidgetLayout.edgeH("postit", "left")
    readonly property string noteEdgeV: WidgetLayout.edgeV("postit", "bottom")
    readonly property int noteX: WidgetLayout.x("postit", 20)
    readonly property int noteY: WidgetLayout.y("postit", WidgetState.visualizer ? 124 : 24)

    Rectangle {
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
            onPressed: edit.forceActiveFocus()
        }

        Text {
            id: head
            anchors { top: parent.top; left: parent.left; margins: 10 }
            text: "note"
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

        Timer {
            id: idle
            interval: 1500
            onTriggered: if (root.visible) PostItState.save(edit.text)
        }

        Text {
            id: foot
            anchors { bottom: parent.bottom; left: parent.left; margins: 8 }
            text: "esc saves and closes"
            color: Theme.fgDim
            font.family: Theme.fontFamily
            font.pixelSize: 9
        }
    }
}
