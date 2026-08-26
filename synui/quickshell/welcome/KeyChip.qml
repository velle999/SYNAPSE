import QtQuick
import ".."

/*
 * KeyChip — a chord, drawn as a key.
 *
 * ⚠ IT IS NOT A HARDCODED COLOUR. The welcome menu this replaces drew its key
 * column with a literal `cairo_set_source_rgba(cr, 0.45, 0.45, 0.55, 1.0)` —
 * a fixed blue-grey that no theme could move and that measured under 3:1 on the
 * dark panel the desktop actually draws. It read as switched-off text next to
 * the label it belongs to, which was the complaint that started this rewrite.
 *
 * Theme.fgDim is the same ink 65% of the way from the surface to the
 * foreground, recomputed per theme, so the chip tracks the palette instead of
 * arguing with it. The chip's own fill is the accent at low alpha, which is
 * what makes it read as a key rather than as more prose.
 *
 * An empty chord draws NOTHING rather than an empty box: several rows here are
 * things you can only reach from a menu, and a blank key cap beside them would
 * be a promise of a shortcut that does not exist.
 */
Rectangle {
    id: root

    property string text: ""
    /* A live VALUE — the AI backend's "GPU"/"CPU"/"off" — rather than a chord.
     * Same chip, different meaning, so it gets the accent's own ink: a reader
     * who has learnt that a dim cap is a key should not read a state as one. */
    property bool value: false

    visible: root.text !== ""
    implicitWidth: visible ? label.implicitWidth + 16 : 0
    implicitHeight: visible ? label.implicitHeight + 8 : 0

    radius: Math.min(4, Theme.panelRadius)
    color: root.value ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.16)
                      : Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.07)
    border.width: 1
    border.color: root.value ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.45)
                             : Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.14)

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.value ? Theme.cyan : Theme.fgDim
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
}
