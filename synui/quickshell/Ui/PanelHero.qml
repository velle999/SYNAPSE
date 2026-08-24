import QtQuick
import qs.Commons

/*
 * PanelHero — the headline row at the top of a panel. An icon, a title, a
 * subtitle in small caps, an optional pill on the right of the title, and an
 * optional control pinned to the trailing edge.
 *
 * ⚠ THE HERO RESERVES THE TRAILING SPACE ITSELF. `trailingInset` is subtracted
 * from the label column's width, so a title long enough to run under a toggle
 * elides instead. Callers that placed their own control on the right had to do
 * that arithmetic and mostly did not, which is how a hero ends up with its
 * title crossed out by a switch.
 */
Item {
    id: root

    property Component iconComponent: null
    property string title: ""
    property string meta: ""
    property string detail: ""
    property color  foreground: Color.foreground
    property string fontFamily: Style.font.family
    property real   iconSize: Style.font.display
    property real   iconOpacity: 1.0
    property alias  metaOpacity: metaText.opacity

    /* A ToggleSwitch, a small button — anything pinned to the right and centred
     * against the labels. */
    property Component trailingControl: null

    readonly property color dim: Qt.darker(root.foreground, 1.4)
    readonly property real trailingInset:
        (trailingLoader.item && trailingLoader.item.visible)
        ? trailingLoader.width + Style.space(12) : 0

    width: parent ? parent.width : root.implicitWidth
    implicitHeight: Math.max(iconLoader.implicitHeight,
                             heroLabels.implicitHeight,
                             trailingLoader.implicitHeight)

    Loader {
        id: iconLoader
        sourceComponent: root.iconComponent
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        opacity: root.iconOpacity
    }

    Column {
        id: heroLabels
        anchors {
            left: iconLoader.right; leftMargin: Style.space(14)
            right: parent.right;    rightMargin: root.trailingInset
            verticalCenter: parent.verticalCenter
        }
        spacing: Style.space(2)

        Row {
            id: titleRow
            visible: root.title !== "" || detailPill.visible
            width: parent.width

            Text {
                id: titleText
                visible: root.title !== ""
                text: root.title
                /* Gives way to the pill rather than pushing it off the end. */
                width: Math.min(implicitWidth,
                                Math.max(0, parent.width - (detailPill.visible
                                         ? detailPill.implicitWidth + Style.space(8) : 0)))
                color: root.foreground
                font { family: root.fontFamily; pixelSize: Style.font.title; bold: true }
                elide: Text.ElideRight
            }

            Item {
                width: Math.max(0, parent.width - titleText.width - detailPill.implicitWidth)
                height: 1
            }

            BorderSurface {
                id: detailPill
                visible: root.detail !== ""
                implicitWidth:  detailText.implicitWidth + Style.space(10)
                implicitHeight: detailText.implicitHeight + Style.space(4)
                anchors.verticalCenter: parent.verticalCenter
                color: "transparent"
                borderSpec: Border.controlSpec("normal", root.foreground, Color.accent, null)
                radius: Style.cornerRadius

                Text {
                    id: detailText
                    anchors.centerIn: parent
                    text: root.detail
                    color: root.dim
                    font { family: root.fontFamily; pixelSize: Style.font.body; bold: true }
                }
            }
        }

        Text {
            id: metaText
            width: parent.width
            text: root.meta.toUpperCase()
            visible: text !== ""
            color: root.dim
            font { family: root.fontFamily; pixelSize: Style.font.caption
                   bold: true; letterSpacing: 1.2 }
            elide: Text.ElideRight
        }
    }

    Loader {
        id: trailingLoader
        sourceComponent: root.trailingControl
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
    }
}
