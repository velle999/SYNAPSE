import QtQuick
import qs.Commons

/*
 * ToggleSwitch — a track and a sliding knob, no label.
 *
 * ⚠ THE CALLER OWNS THE VALUE. This never flips itself: bind `checked` to real
 * state and change that state in response to `toggled()`. A control that
 * toggled optimistically and then had to be corrected is a control that visibly
 * changes its mind, and a service that already tracks a desired state gets the
 * instant throw for free by binding `checked` to it.
 *
 * `busy` swallows further clicks while an operation is in flight but leaves
 * hover, cursor and tooltip alone — a switch that greyed out on every
 * background refresh would flicker its way through the day.
 *
 * ⛔ THE CURSOR IS A RING OUTSIDE THE TRACK, NOT A STATE ON IT. Normal chrome
 * carries a stronger border than the hover-cursor state (0.4 against 0.25), so
 * painting the cursor onto a bordered track would make it go FAINTER as the
 * cursor arrived — the opposite of what a highlight is for. Drawn outside, on
 * the panel background, it reads immediately.
 *
 * `rounded` follows the theme's own corner radius, so the switch is a pill on a
 * rounded desktop and a rectangle on a sharp one without the caller choosing.
 */
Item {
    id: root

    property bool checked: false
    property bool busy: false
    /* Off when the surrounding row owns the click, as Toggle does. */
    property bool interactive: true
    property bool hasCursor: false

    property bool cursorRing: root.interactive
    property int  cursorPad: Style.space(6)
    property bool rounded: Style.cornerRadius > 0
    property color foreground: Color.foreground
    property color accent: Color.accent

    signal toggled()
    signal hovered(bool isHovered)

    readonly property alias containsMouse: mouse.containsMouse
    readonly property bool hot: root.hasCursor || mouse.containsMouse

    /* ⚠ trackHeight IS SETTABLE AND THE REST DERIVE FROM IT. A compact
     * placement — a switch riding a section header — needs a genuinely smaller
     * control, not a big one scaled down: scaling lands the track and the knob
     * on fractional pixels and blurs both edges. The floors are low enough to
     * stay out of an override's way and every default is already above them. */
    property int trackHeight: Math.max(22, Math.round(Style.spacing.controlHeight * 0.55))
    property int trackWidth:  Math.round(root.trackHeight * 1.9)
    property int knobSize:    Math.max(6, Math.round(root.trackHeight * 0.72))
    property int knobInset:   Math.max(1, Math.round((root.trackHeight - root.knobSize) / 2))

    readonly property int pad: root.cursorRing ? root.cursorPad : 0

    implicitWidth:  root.trackWidth  + root.pad * 2
    implicitHeight: root.trackHeight + root.pad * 2

    BorderSurface {
        anchors.fill: parent
        visible: root.cursorRing && root.hot
        color: "transparent"
        radius: Style.cornerRadius
        borderSpec: Border.controlSpec("hover-cursor", root.foreground, root.accent, null)
    }

    BorderSurface {
        id: track
        width: root.trackWidth
        height: root.trackHeight
        anchors.centerIn: parent
        radius: root.rounded ? height / 2 : 0
        color: root.checked ? Style.selectedFillFor(root.foreground, root.accent, null)
                            : Style.normalFillFor(root.foreground, root.accent, null)
        borderSpec: Border.controlSpec(root.checked ? "selected" : "normal",
                                       root.foreground, root.accent, null)

        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            width: root.knobSize
            height: root.knobSize
            radius: root.rounded ? height / 2 : 0
            x: root.checked ? track.width - width - root.knobInset : root.knobInset
            anchors.verticalCenter: parent.verticalCenter
            color: root.checked ? Style.selectedStateColor(root.foreground, root.accent, null)
                                : Qt.darker(root.foreground, 1.25)

            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.interactive
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onContainsMouseChanged: root.hovered(mouse.containsMouse)
        onClicked: if (!root.busy) root.toggled()
    }
}
