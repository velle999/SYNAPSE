import QtQuick
import Quickshell
import Quickshell.Wayland

/*
 * Osd — the volume / brightness popup.
 *
 * One of these exists per screen (shell.qml Variants), but only the one on the
 * focused output is ever visible. OsdState decides which; see the note there
 * about why the state is a singleton and the window is not.
 *
 * Sits just under the bar rather than at the bottom of the screen: it reflects
 * the same number the bar's volume module shows, so the two belong together.
 */
PanelWindow {
    id: osd

    required property var modelData
    screen: modelData

    // Frosted on a glass theme — see StartMenu.qml for what the namespace does
    // and why the bar does not carry it.
    WlrLayershell.namespace: "synui-glass"

    // Empty output means the focus probe has not answered yet — fall back to
    // the primary screen so the very first keypress after login still shows
    // something rather than nothing.
    visible: OsdState.showing
             && (OsdState.output === modelData.name
                 || (OsdState.output === "" && modelData.name === Quickshell.screens[0].name))

    // One vertical anchor only: with neither left nor right set, layer-shell
    // centres the surface horizontally, which is exactly what is wanted.
    //
    // It rides the SAME edge as the bar and sits just clear of it. Pinned to the
    // top on a bottom-bar desktop it would be the one piece of chrome left up
    // there, floating against nothing.
    anchors.top:    !BarConfig.atBottom
    anchors.bottom:  BarConfig.atBottom
    margins.top:    Theme.barHeight + 10
    margins.bottom: Theme.barHeight + 10

    implicitWidth: 300
    implicitHeight: 54

    // MUST NOT reserve space. An OSD with an exclusive zone would shove every
    // window down the screen for a second and a half, every time the volume
    // changed.
    //
    // Ignore rather than a zone of 0, for the same reason as the start menu: a
    // zone of 0 asks for nothing but still respects the bar's, so `margins.top`
    // stacked on top of the reserved 28px and the popup sat a bar's height too
    // low whenever auto-hide was off.
    exclusionMode: ExclusionMode.Ignore

    // Nor take keyboard focus: the OSD appears while the user is typing or
    // gaming, and EXCLUSIVE focus here would eat their keys.
    focusable: false

    // An empty mask means the window is invisible to the POINTER. Without it
    // this transparent 300x54 rectangle swallows every click under it for the
    // life of the popup — the same trap as synui's decorative buffers, which
    // have to set point_accepts_input=false for exactly this reason.
    mask: Region {}

    color: "transparent"

    Rectangle {
        id: card
        anchors.fill: parent
        // Was a fixed 10, from before any of the bar's panels followed the
        // desktop. It is one of them: a volume card still rounded on a desktop
        // whose every window and menu has gone square is the same split this
        // change closes everywhere else.
        radius: Theme.panelRadius
        color: Theme.popupBg
        border.color: Theme.magenta
        border.width: 1

        opacity: OsdState.showing ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }

        Row {
            anchors {
                fill: parent
                leftMargin: 16
                rightMargin: 16
            }
            spacing: 12

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 22
                horizontalAlignment: Text.AlignHCenter
                text: OsdState.icon
                color: OsdState.muted ? Theme.fgDim : Theme.cyan
                font.family: Theme.iconFamily
                font.pixelSize: 17
            }

            // The track. Width is what is left after the icon, the gaps and the
            // readout — stated rather than anchored so the bar cannot end up
            // with a negative width on a narrow OSD and refuse to draw.
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 22 - 12 - 12 - readout.width
                height: 6
                radius: 3
                color: Theme.hoverBg

                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: parent.width * OsdState.value
                    radius: 3
                    color: OsdState.muted ? Theme.fgDim : Theme.magenta
                    Behavior on width { NumberAnimation { duration: Theme.animFast } }
                }
            }

            Text {
                id: readout
                anchors.verticalCenter: parent.verticalCenter
                text: OsdState.label
                color: OsdState.muted ? Theme.fgDim : Theme.fg
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }
}
