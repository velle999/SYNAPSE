import QtQuick
import Quickshell
import ".."

/*
 * The "◢ SYNAPSE" start button.
 *
 * It used to be drawn by the COMPOSITOR (src/launcher.c): a cairo buffer in a
 * scene tree placed just above the top layer, pinned to the output's top-left
 * corner, click-hit-tested in server_cursor_button ahead of forwarding. That was
 * a workaround for the bar being a foreign process — and it broke in two ways
 * the moment the bar learned to auto-hide:
 *
 *   - it could not hide WITH the bar. launcher_apply_position only knew about
 *     fullscreen; quickshell slides the bar's *content* inside a window that
 *     stays mapped and full height, so there is nothing in the surface geometry
 *     for the compositor to read. The caret and emblem sat there over the
 *     desktop with the bar gone from under them.
 *   - being above the TOP layer, it drew over ordinary windows.
 *
 * In the bar it slides with the bar, z-orders with the bar, and — the part that
 * is pure subtraction — Workspaces no longer has to mirror launcher.c's width
 * formula to avoid being drawn under an invisible-but-clickable region.
 *
 * The colour is now Theme.barGlyph rather than a hardcoded #05d9e8. Those are the
 * same value on the default palette, so nothing changes until a theme is
 * applied — at which point the button follows it, which the compositor-drawn one
 * never could ("kept in step by eye" was the old comment's admission).
 */
Rectangle {
    id: root

    // The bar this button belongs to, so a click opens the menu on THIS monitor.
    // Required rather than defaulted: a silent fallback to the primary output
    // would put the menu on another screen and look like a compositor bug.
    required property string output

    readonly property bool logo: LauncherStyle.logo

    // 12px padding either side, matching waybar's old #custom-synapse and the
    // LAUNCHER_PAD_X the compositor used, so the wordmark still lines up where
    // it always did.
    implicitWidth:  content.implicitWidth + 24
    implicitHeight: Theme.barHeight

    color: mouse.containsMouse || MenuState.open ? Theme.barHoverBg : "transparent"
    Behavior on color { ColorAnimation { duration: Theme.animFast } }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 6                       // LAUNCHER_CARET_GAP

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "◢"
            color: Theme.barGlyph
            // NOT the picked font, for the same reason Theme.iconFamily isn't:
            // U+25E2 is a geometric symbol, and a picked face that lacks it
            // hands the caret to whatever Qt substitutes — a different weight
            // and advance from one font choice to the next. The wordmark below
            // follows the picker; the caret is a mark, not text.
            font.family: "monospace"
            font.pixelSize: 13           // LAUNCHER_FONT
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
        }

        // Logo mode keeps the caret and swaps only the wordmark for the emblem,
        // so the two styles read as one button in two dresses.
        //
        // logo-bold.svg, not logo.svg: at this size the thin mark's strokes and
        // faint triangle all but vanish into the bar. sourceSize is what makes
        // Qt rasterise the SVG at the drawn size instead of scaling a default
        // 128px render down to 23 and going soft.
        Image {
            visible: root.logo
            anchors.verticalCenter: parent.verticalCenter
            source: "file:///usr/share/synui/logo-bold.svg"
            sourceSize.width:  23        // LAUNCHER_LOGO_SZ
            sourceSize.height: 23
            width: 23
            height: 23
        }

        Text {
            visible: !root.logo
            anchors.verticalCenter: parent.verticalCenter
            text: "SYNAPSE"
            color: Theme.barGlyph
            // Follows the font picker, like every other string on the bar. It
            // was hardcoded to "monospace" back when the compositor drew this
            // button in cairo and the bar could not have known what the picker
            // chose; keeping it meant one word in the old face after a font
            // switch — the same "looks broken" that Theme.uiFont exists to fix.
            //
            // implicitWidth is content.implicitWidth + 24, so a wider or
            // narrower face just moves Workspaces along; nothing mirrors this
            // width any more (see the header).
            font.family: Theme.fontFamily
            font.pixelSize: 13
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onClicked: MenuState.toggle(root.output)
    }
}
