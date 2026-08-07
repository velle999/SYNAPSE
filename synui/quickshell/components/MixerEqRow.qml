import QtQuick
import ".."

/*
 * The mixer's equalizer row: a switch, what the curve currently is, and the way
 * through to the ten-band editor.
 *
 * The editor itself is synui's (Control panel ▸ Sound ▸ Equalizer, src/eq.c),
 * compositor-drawn and already reachable from a keybind. This row does not
 * reimplement any of it — a second band editor would be a second copy of
 * synui-eq's preset table to keep in step, and the day one moved a band the
 * other would be labelling the wrong frequency. It offers the one control that
 * belongs next to a volume slider (on/off) and a door to the rest.
 *
 * TWO TARGETS, SAID OUT LOUD
 *
 * The row toggles; the button opens. That split is only readable because the
 * button is drawn as a button — an "Adjust" that looked like the rest of the
 * row would make the whole thing a coin toss between switching the equalizer
 * off and opening a panel.
 *
 * A checkbox and not a colour cue, for the reason BarMenu's rows carry one: on
 * a light theme "dim versus bright" is nearly invisible, and this mark is the
 * only thing saying whether the equalizer is in the path.
 */
Item {
    id: root

    // What eq.state asked for. The checkbox follows THIS, not `status`, so the
    // box goes down the instant it is clicked rather than waiting for a chain
    // that takes a moment to come up.
    property bool on: false

    // "off" | "on" | "bypassed" | "not running" — see EqState.
    property string status: "off"
    property string preset: "flat"
    property int preamp: 0
    // The state file and the graph disagree: the row is reporting a fault, not
    // a setting, and says so instead of showing a curve nothing is applying.
    property bool warning: false

    // Watched by the mixer, whose pointer-left dismissal timer must not fire
    // while the pointer is over one of these hover-enabled children — they take
    // the hover off the panel-wide MouseArea that would otherwise hold it off.
    readonly property bool hovered: rowMouse.containsMouse || adjustMouse.containsMouse

    signal toggleRequested()
    signal openRequested()

    implicitHeight: 22

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.radius
        color: rowMouse.containsMouse ? Theme.hoverBg : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }

    Text {
        id: box
        anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
        text: root.on ? "[x]" : "[ ]"
        color: root.on ? Theme.cyan : Theme.fgDim
        font.family: Theme.fontFamily
        font.pixelSize: 11
    }

    Text {
        id: glyph
        anchors { left: box.right; leftMargin: 6; verticalCenter: parent.verticalCenter }
        text: Icons.equalizer
        color: root.on ? Theme.cyan : Theme.fgDim
        font.family: Theme.iconFamily
        font.pixelSize: 12
    }

    Text {
        id: name
        anchors { left: glyph.right; leftMargin: 6; verticalCenter: parent.verticalCenter }
        text: "Equalizer"
        color: root.on ? Theme.fg : Theme.fgDim
        font.family: Theme.fontFamily
        font.pixelSize: 11
    }

    // The curve, or the complaint. With the box already saying on or off, the
    // useful thing to spend this space on while it is on is WHICH curve —
    // except when the answer would be a lie, and then it is what is wrong.
    Text {
        id: value
        anchors {
            left: name.right; leftMargin: 8
            right: adjust.left; rightMargin: 6
            verticalCenter: parent.verticalCenter
        }
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        text: {
            if (root.warning) return root.status
            if (!root.on) return "off"
            return root.preset + (root.preamp !== 0
                                  ? "  ·  " + (root.preamp > 0 ? "+" : "") + root.preamp + " dB"
                                  : "")
        }
        color: root.warning ? Theme.yellow : (root.on ? Theme.fg : Theme.fgDim)
        font.family: Theme.fontFamily
        font.pixelSize: 10
    }

    // Everything but the button. Placed BELOW the button in the file so the
    // button's own MouseArea is the one on top where they meet.
    MouseArea {
        id: rowMouse
        anchors { left: parent.left; right: adjust.left; top: parent.top; bottom: parent.bottom }
        hoverEnabled: true
        onClicked: root.toggleRequested()
    }

    Rectangle {
        id: adjust
        anchors { right: parent.right; rightMargin: 2; verticalCenter: parent.verticalCenter }
        width: 56
        height: 18
        radius: Theme.radius
        color: adjustMouse.containsMouse ? Theme.activeBg : "transparent"
        border.color: adjustMouse.containsMouse ? Theme.magenta : Theme.hoverBg
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: "Adjust"
            color: Theme.fg
            font.family: Theme.fontFamily
            font.pixelSize: 10
        }

        MouseArea {
            id: adjustMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.openRequested()
        }
    }
}
