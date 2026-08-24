import QtQuick
import qs.Commons

/*
 * PanelSlider — a track, a fill and a knob. Volume, brightness, anything
 * continuous.
 *
 * ⚠ `liveValue` IS NOT `value`, AND THE SPLIT IS THE WHOLE DESIGN. `value` is
 * what the caller believes; `liveValue` is where the knob is while a drag is in
 * progress. Without the split, every frame of a drag would round-trip through
 * whatever the slider controls — a Pipewire volume, a backlight — and the knob
 * would stutter along behind its own pointer at the speed of the slowest
 * setter. `moved()` fires continuously for the live preview, `released()` once
 * at the end for the commit, and on release the knob goes back to following
 * `value` so a rejected change snaps back rather than lying.
 *
 * ⚠ THE TICKS ARE PAINTED IN THE PANEL'S BACKGROUND COLOUR, so only the part
 * crossing the track shows and they read as notches CUT INTO it rather than as
 * marks drawn on top. They are decoration only — snapping is the caller's job
 * through `integer` or `step`.
 *
 * Right-click is a secondary action on the whole track (audio mutes the channel
 * with it); dragging stays left-button only so the two never race.
 */
Item {
    id: root

    property QtObject bar: null
    property real value: 0
    property real minimum: 0
    property real maximum: 1
    property real step: 0.05
    property bool integer: false

    property color trackColor: root.bar
        ? Style.selectedFillFor(root.bar.barForeground, Color.accent, null)
        : Style.selectedFillFor(Color.foreground, Color.accent, null)
    property color fillColor: root.bar && root.bar.barForeground ? root.bar.barForeground : Color.foreground
    property color knobColor: root.bar && root.bar.barForeground ? root.bar.barForeground : Color.foreground

    property bool dragging: false
    property real trackHeight: Math.max(4, Math.round(Style.spacing.controlHeight * 0.11))
    property real knobSize: Math.max(14, Math.round(Style.spacing.controlHeight * 0.38))
    property real liveValue: root.value

    property int tickCount: 0
    property color tickColor: Color.bar.background

    onValueChanged: if (!root.dragging) root.liveValue = root.value

    signal moved(real value)
    signal released(real value)
    signal rightClicked()

    implicitWidth: Style.space(200)
    implicitHeight: Math.max(Style.space(22), root.knobSize + Style.spacing.md)

    /* Never zero: a caller that sets minimum === maximum would divide by it. */
    readonly property real range: Math.max(0.0001, root.maximum - root.minimum)
    readonly property real progress:
        Math.max(0, Math.min(1, (root.liveValue - root.minimum) / root.range))
    readonly property bool hot: mouseArea.containsMouse || root.dragging

    Rectangle {
        id: track
        anchors { verticalCenter: parent.verticalCenter; left: parent.left; right: parent.right }
        height: root.trackHeight
        radius: height / 2
        color: root.trackColor
    }

    Rectangle {
        id: fill
        anchors { verticalCenter: track.verticalCenter; left: track.left }
        height: track.height
        radius: track.radius
        color: root.fillColor
        width: track.width * root.progress
        /* ⚠ THE ANIMATION IS OFF WHILE DRAGGING. Easing the fill toward the
         * pointer means the bar trails the finger by its own duration. */
        Behavior on width {
            enabled: !root.dragging
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
    }

    Repeater {
        model: root.tickCount > 1 ? root.tickCount : 0
        Rectangle {
            required property int index
            width: Math.max(1, Style.space(2))
            height: root.trackHeight + Style.space(4)
            radius: 1
            color: root.tickColor
            anchors.verticalCenter: track.verticalCenter
            x: Math.max(0, Math.min(track.width - width,
                                    track.width * (index / (root.tickCount - 1)) - width / 2))
        }
    }

    BorderSurface {
        id: knob
        width: root.knobSize
        height: root.knobSize
        radius: root.knobSize / 2
        color: root.knobColor
        /* Ringed in the panel's own background so the knob stays visible where
         * it crosses the filled part of the track. */
        borderSpec: Border.flat(Color.bar.background, Math.max(1, Style.space(2)))
        anchors.verticalCenter: track.verticalCenter
        x: Math.max(0, Math.min(track.width - width, track.width * root.progress - width / 2))
        scale: root.hot ? 1.15 : 1.0

        Behavior on x {
            enabled: !root.dragging
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        function valueFromX(x) {
            const clamped = Math.max(0, Math.min(track.width, x))
            let raw = root.minimum + (clamped / track.width) * root.range
            if (root.integer) raw = Math.round(raw)
            return Math.max(root.minimum, Math.min(root.maximum, raw))
        }

        onPressed: (mouse) => {
            if (mouse.button !== Qt.LeftButton) return
            root.dragging = true
            const next = mouseArea.valueFromX(mouse.x)
            root.liveValue = next
            root.moved(next)
        }
        onClicked: (mouse) => { if (mouse.button === Qt.RightButton) root.rightClicked() }
        onPositionChanged: (mouse) => {
            if (!root.dragging) return
            const next = mouseArea.valueFromX(mouse.x)
            root.liveValue = next
            root.moved(next)
        }
        onReleased: (mouse) => {
            if (mouse.button !== Qt.LeftButton) return
            root.dragging = false
            root.released(root.liveValue)
            /* Back to following the caller. If it refused the change, the knob
             * returns to where the truth is. */
            root.liveValue = root.value
        }
        onWheel: (wheel) => {
            const delta = wheel.angleDelta.y > 0 ? root.step : -root.step
            let next = Math.max(root.minimum, Math.min(root.maximum, root.liveValue + delta))
            if (root.integer) next = Math.round(next)
            root.liveValue = next
            /* A notch is a whole gesture: moved AND released, so a wheel commits
             * where a drag would still be in progress. */
            root.moved(next)
            root.released(next)
        }
    }
}
