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
    property string text: ""
    property string icon: ""                 // optional leading glyph
    property color  iconColor: Theme.cyan
    property color  textColor: Theme.fg
    property string tooltipText: ""
    property bool   active: false            // draws the magenta wash

    signal clicked(var mouse)
    signal rightClicked(var mouse)
    signal middleClicked(var mouse)
    signal scrolled(int delta)               // +1 up, -1 down

    implicitWidth: row.implicitWidth + Theme.modulePadH * 2
    implicitHeight: Theme.barHeight
    radius: Theme.radius

    color: root.active ? Theme.activeBg
                       : (mouse.containsMouse ? Theme.hoverBg : "transparent")
    Behavior on color { ColorAnimation { duration: Theme.animFast } }

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
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton

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
