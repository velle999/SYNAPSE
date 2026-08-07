import QtQuick
import Quickshell
import ".."

/*
 * BarModule — the chrome every bar item shares.
 *
 * One place for the hover wash, the click/scroll plumbing and the tooltip, so
 * a module only has to say what it shows. waybar got this from CSS selectors
 * on #module; here it is a component, which is why the modules below are so
 * short.
 *
 * The tooltip is a real popup window rather than an Item drawn inside the bar:
 * the bar is 28px tall with an exclusive zone, so anything drawn inside it
 * would be clipped to a strip. PopupWindow hangs off the bar's own window and
 * is positioned by the anchor below.
 */
Rectangle {
    id: root

    // ── What a module fills in ───────────────────────────
    // TWO independent reasons to be hidden, kept apart on purpose. barVisible
    // is the per-monitor switch from the bar's right-click menu; moduleVisible
    // is the module's own condition (no battery on a desktop, no MPRIS player).
    // A single `visible` would have one clobber the other — turning the media
    // module on for a monitor would force an empty pill on screen with nothing
    // playing.
    property bool barVisible: true
    property bool moduleVisible: true
    visible: barVisible && moduleVisible

    // Whether right-click means something to THIS module. Modules that decline
    // it let the button fall through to the bar underneath, which opens the
    // per-monitor menu — otherwise right-clicking the CPU readout would be
    // swallowed and look like the menu was broken.
    property bool acceptsRight: false

    property string text: ""
    property string icon: ""                 // optional leading glyph
    property color  iconColor: Theme.cyan
    property color  textColor: Theme.fg
    property string tooltipText: ""
    property bool   active: false            // draws the magenta wash

    // Read-only for modules that need to change what they SAY on hover, not
    // just how they look — the recording pill swaps its blinking dot for a stop
    // square, so the click target announces what the click will do. Exposed
    // here rather than each such module laying its own MouseArea over this one:
    // a second hoverEnabled area on top would starve the wash and the tooltip.
    readonly property alias hovered: mouse.containsMouse

    signal clicked(var mouse)
    signal rightClicked(var mouse)
    signal middleClicked(var mouse)
    signal scrolled(int delta)               // +1 up, -1 down

    implicitWidth: row.implicitWidth + Theme.modulePadH * 2

    // A pill inset from the bar's full height, rather than a full-height block:
    // the hover wash reads as a button instead of a stripe cut out of the bar.
    implicitHeight: Theme.barHeight
    height: Theme.pillHeight
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
    radius: Theme.pillRadius

    color: root.active ? Theme.activeBg
                       : (mouse.containsMouse ? Theme.hoverBg : "transparent")
    Behavior on color { ColorAnimation { duration: Theme.animFast } }

    // A press that only changes colour is easy to miss on a 20px target; the
    // dip is small enough not to read as movement in peripheral vision.
    scale: mouse.pressed ? 0.94 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutQuad } }

    // Active modules get a hairline in the accent so "on" survives a theme
    // whose activeBg wash is subtle against its own bar colour.
    border.width: root.active ? 1 : 0
    border.color: Theme.magenta

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Text {
            visible: root.icon !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: root.icon
            color: root.iconColor
            font.family: Theme.iconFamily
            font.pixelSize: Theme.iconSize
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
        }

        Text {
            visible: root.text !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.textColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                         | (root.acceptsRight ? Qt.RightButton : Qt.NoButton)

        onClicked: (m) => {
            if (m.button === Qt.LeftButton)        root.clicked(m)
            else if (m.button === Qt.RightButton)  root.rightClicked(m)
            else if (m.button === Qt.MiddleButton) root.middleClicked(m)
        }
        // angleDelta is in eighths of a degree; one notch is 120.
        onWheel: (w) => root.scrolled(w.angleDelta.y > 0 ? 1 : -1)
    }

    // ── Tooltip ──────────────────────────────────────────
    // Deliberately not shown while the pointer is merely passing through: a bar
    // full of modules that each flash a popup on the way past is unusable.
    Timer {
        id: hoverDelay
        interval: 450
        onTriggered: if (mouse.containsMouse && root.tooltipText !== "") tip.visible = true
    }
    Connections {
        target: mouse
        function onContainsMouseChanged() {
            if (mouse.containsMouse) hoverDelay.restart()
            else { hoverDelay.stop(); tip.visible = false }
        }
    }

    PopupWindow {
        id: tip
        visible: false
        implicitWidth: tipText.implicitWidth + 24
        implicitHeight: tipText.implicitHeight + 16
        color: "transparent"

        anchor {
            window: root.QsWindow.window
            rect.x: root.mapToItem(null, 0, 0).x + root.width / 2 - tip.width / 2
            rect.y: Theme.barHeight + 4
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.popupBg
            border.color: Theme.magenta
            border.width: 1
            radius: Theme.radius

            Text {
                id: tipText
                anchors.centerIn: parent
                text: root.tooltipText
                color: Theme.fg
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
