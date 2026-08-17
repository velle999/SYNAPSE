import QtQuick
import ".."

/*
 * One row of the mixer: a mute glyph, a name, a drag/scroll track and a
 * percentage, bound straight to one PipeWire node's audio interface.
 *
 * Shared by devices and application streams because the control is the same;
 * only the label differs. Devices additionally get the `selectable` radio,
 * which is what picks the default sink or source.
 *
 * `audio` is allowed to be null and every read below guards for it. A stream
 * node can be destroyed between the model listing it and this row binding to
 * it — closing a browser tab is enough — and an unguarded `audio.volume` would
 * take the whole bar's QML down with a TypeError, not just this row.
 */
Item {
    id: row

    // PwNodeAudioIface, or null.
    required property var audio

    property string label: ""
    property string sublabel: ""

    // Device rows only: a radio the user clicks to make this the default.
    property bool selectable: false
    property bool selected: false
    signal selectRequested()

    property color accent: Theme.magenta

    // Capture side. Only changes the glyph — a speaker drawn over a microphone
    // row reads as "this is where the sound comes out", which is backwards.
    property bool input: false

    // Held while a drag is in progress. The panel watches this so its
    // pointer-left dismissal timer cannot close the mixer out from under a
    // slider the user is still dragging — a drag that leaves the popup keeps
    // the implicit grab, so "pointer exited" fires while the gesture is live.
    property bool held: false

    readonly property bool muted: audio ? audio.muted : false
    // PipeWire will report above 1.0 when something else applied a software
    // boost, so the readout is the true value and only the fill is clamped.
    readonly property real level: audio ? audio.volume : 0

    implicitHeight: 34

    // Writes are clamped to 1.0 even though PipeWire would accept more: a
    // stray scroll on a bar popup is a bad way to discover software boost.
    function setLevel(v) {
        if (audio) audio.volume = Math.max(0, Math.min(1, v))
    }

    // ── Top line: mute glyph, radio, name ────────────────
    Text {
        id: glyph
        anchors { left: parent.left; top: parent.top }
        width: 16
        horizontalAlignment: Text.AlignHCenter
        text: row.input ? (row.muted ? Icons.micMuted : Icons.mic)
                        : (row.muted ? Icons.volMuted
                        : (row.level >= 0.66 ? Icons.volHigh
                        : (row.level >= 0.33 ? Icons.volMed : Icons.volLow)))
        color: row.muted ? Theme.fgDim : Theme.cyan
        font.family: Theme.iconFamily
        font.pixelSize: 12

        MouseArea {
            anchors { fill: parent; margins: -3 }
            onClicked: if (row.audio) row.audio.muted = !row.audio.muted
        }
    }

    // A radio, not a colour cue: on a light theme "dim vs bright" is nearly
    // invisible, and this mark is the only thing saying which device sound
    // actually comes out of. Same reasoning as BarMenu's [x] checkboxes.
    //
    // ⚠ It carries its OWN MouseArea. It used to have none — only the device
    // NAME beside it was clickable — so the one thing in the panel drawn as a
    // button was the one thing that was not one, and clicking the mark to pick
    // an output did nothing at all, silently. Reported as the mixer not letting
    // you choose an output; the radio was there the whole time and was inert.
    // Margins widen the target past the ~14px glyph, which is under the 20px
    // floor a pointer can reliably hit.
    Text {
        id: radio
        visible: row.selectable
        anchors { left: glyph.right; leftMargin: 6; top: parent.top }
        text: row.selected ? "(•)" : "( )"
        color: row.selected ? Theme.cyan : Theme.fgDim
        font.family: Theme.fontFamily
        font.pixelSize: 11

        MouseArea {
            anchors { fill: parent; margins: -4 }
            enabled: row.selectable
            onClicked: row.selectRequested()
        }
    }

    Text {
        id: name
        anchors {
            left: radio.visible ? radio.right : glyph.right
            leftMargin: 6
            right: parent.right
            top: parent.top
        }
        text: row.label + (row.sublabel !== "" ? "  ·  " + row.sublabel : "")
        color: row.muted ? Theme.fgDim : Theme.fg
        font.family: Theme.fontFamily
        font.pixelSize: 11
        elide: Text.ElideRight

        MouseArea {
            anchors.fill: parent
            enabled: row.selectable
            onClicked: row.selectRequested()
        }
    }

    // ── Bottom line: track and readout ───────────────────
    Text {
        id: pct
        anchors { right: parent.right; bottom: parent.bottom; bottomMargin: 2 }
        width: 32
        horizontalAlignment: Text.AlignRight
        text: Math.round(row.level * 100) + "%"
        color: row.muted ? Theme.fgDim : Theme.fg
        font.family: Theme.fontFamily
        font.pixelSize: 10
    }

    // The hit area is the full 16px strip, not the 5px groove: a 5px drag
    // target on a popup that dismisses itself on pointer-leave is a good way
    // to close the mixer every time you miss.
    Item {
        id: trackArea
        anchors {
            left: glyph.right; leftMargin: 6
            right: pct.left;   rightMargin: 6
            bottom: parent.bottom
        }
        height: 16

        Rectangle {
            id: groove
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            height: 5
            radius: 2.5
            color: Theme.hoverBg

            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                // Deliberately unanimated: this is direct manipulation, and a
                // fill that eases towards the pointer reads as lag.
                width: parent.width * Math.min(1, row.level)
                radius: 2.5
                color: row.muted ? Theme.fgDim : row.accent
            }
        }

        Rectangle {
            width: 10
            height: 10
            radius: 5
            anchors.verticalCenter: parent.verticalCenter
            x: Math.max(0, Math.min(groove.width - width,
                                    groove.width * Math.min(1, row.level) - width / 2))
            color: row.muted ? Theme.fgDim : row.accent
            border.color: Theme.popupBg
            border.width: 1
        }

        MouseArea {
            anchors.fill: parent
            // The panel scrolls when it outgrows the screen, and without this
            // the Flickable steals a vertical wobble mid-drag and the slider
            // stops following the pointer.
            preventStealing: true

            onPressed: (m) => { row.held = true; row.setLevel(m.x / width) }
            onPositionChanged: (m) => { if (pressed) row.setLevel(m.x / width) }
            onReleased: row.held = false
            onCanceled: row.held = false
            onWheel: (w) => row.setLevel(row.level + (w.angleDelta.y > 0 ? 0.05 : -0.05))
        }
    }
}
