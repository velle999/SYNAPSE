import QtQuick
import qs.Commons

/*
 * WidgetButton — the base every INTERACTIVE Omarchy bar widget extends.
 *
 * BarWidget is the contract for a widget that reports something. This is the
 * one for a widget you can click: a label, a hover cursor, a press signal and a
 * wheel signal, sized to the bar it is in.
 *
 * ⚠ REIMPLEMENTED, NOT VENDORED, and unlike BarWidget that is a practical
 * decision rather than only a courteous one. Omarchy is MIT (© David Heinemeier
 * Hansson) so a verbatim copy would be perfectly legal — but theirs calls into
 * THEIR host for tooltips, click registration and an indicator reveal, and a
 * copy would be a file full of calls to functions this bar does not have. What
 * is portable is the shape: the property names, the two signals, and how the
 * size is derived. Those are what a widget written against it depends on, and
 * those are what is here.
 *
 * ⚠ EVERY HOST CALL IN THEIRS IS GUARDED, which is what makes this possible at
 * all — `if (root.bar)` before showTooltip, `&& registeredBar.unregisterClick
 * Target` before that. A host that offers none of them is a supported case in
 * their own design, so a widget on this bar simply has no tooltip rather than
 * failing. The guards are kept for the same reason: `bar` is null until the
 * loader has placed the widget.
 *
 * ⛔ IT IS THE ONE TYPE THAT UNBLOCKS MOST OF THEM. Of the eight bar widgets
 * Omarchy ships, three resolve against BarWidget and qs.Commons alone; the
 * other five all fail on this single name. It is worth having for that reason
 * and no other.
 */
Item {
    id: root

    /* ── The host, and what a widget reads off it ────────────────────────── */
    property var    bar: null
    property string text: ""
    property string fontFamily: root.bar && root.bar.fontFamily
                                ? root.bar.fontFamily : Style.font.family
    property real   fontSize: Style.font.body
    property color  foreground: root.bar && root.bar.barForeground
                                ? root.bar.barForeground : Color.foreground
    property color  activeColor: root.bar && root.bar.urgent
                                 ? root.bar.urgent : Color.urgent

    /* ── What the widget says about itself ──────────────────────────────── */
    property bool active: false
    property real horizontalMargin: 8.5
    property real verticalPadding: 6
    property real fixedWidth: -1
    property real fixedHeight: -1
    property real textRotation: 0
    property bool keepSpace: false
    property bool dimmed: false
    property bool concealed: false
    property bool interactive: true
    property bool pressable: true
    property bool useActiveColor: true
    property bool labelVisible: true
    property bool hasVisualContent: root.text !== ""
    property string tooltipText: ""

    signal pressed(int button)
    signal wheelMoved(int delta)

    /*
     * Their two host hooks, kept as guarded no-ops rather than dropped.
     *
     * ⚠ A WIDGET CALLS triggerPress() ITSELF in places — it is not only the
     * MouseArea below — so removing it would break a widget that never touches
     * a tooltip. hideOwnTooltip() is called from four of their own property
     * handlers for the same reason.
     */
    function triggerPress(button) {
        root.hideOwnTooltip()
        root.pressed(button)
    }
    function hideOwnTooltip() {
        if (root.bar && typeof root.bar.hideTooltip === "function")
            root.bar.hideTooltip(root)
    }

    onVisibleChanged:     if (!root.visible)     root.hideOwnTooltip()
    onInteractiveChanged: if (!root.interactive) root.hideOwnTooltip()
    onConcealedChanged:   if (root.concealed)    root.hideOwnTooltip()

    readonly property bool vertical: root.bar ? root.bar.vertical : false
    readonly property int  barSize: root.bar && root.bar.barSize > 0
                                    ? root.bar.barSize : 28
    readonly property real scaledHorizontalMargin: Style.spaceReal(root.horizontalMargin)
    readonly property real scaledVerticalPadding:  Style.spaceReal(root.verticalPadding)
    /* The painted label's width, for chrome that lines up with the text rather
     * than with the slot it sits in. Zero on an icon-only button. */
    readonly property real labelWidth: label.visible ? label.implicitWidth : 0

    /* `keepSpace` is how a widget holds its slot while showing nothing — a
     * spacer, or a readout with no reading yet. Without it an empty widget
     * would collapse and the row beside it would shuffle. */
    visible: root.hasVisualContent || root.keepSpace
    opacity: !root.hasVisualContent || root.concealed ? 0 : (root.dimmed ? 0.45 : 1)

    implicitWidth:  root.fixedWidth > 0 ? root.fixedWidth
                  : (root.vertical ? root.barSize
                     : Math.max(12, label.implicitWidth + root.scaledHorizontalMargin * 2))
    implicitHeight: root.fixedHeight > 0 ? root.fixedHeight
                  : (root.vertical
                     ? Math.max(12, label.implicitHeight + root.scaledVerticalPadding * 2)
                     : root.barSize)

    Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    Text {
        id: label
        visible: root.labelVisible
        anchors.centerIn: parent
        text: root.text
        color: root.active && root.useActiveColor ? root.activeColor : root.foreground
        font.family: root.fontFamily
        font.pixelSize: root.fontSize
        /* NativeRendering, as theirs does: these labels are mostly Nerd Font
         * glyphs at bar sizes, where the distance-field renderer smears a
         * one-pixel stem. */
        renderType: Text.NativeRendering
        rotation: root.textRotation
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        Behavior on color { ColorAnimation { duration: 160 } }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        enabled: root.interactive
        hoverEnabled: true
        cursorShape: root.pressable ? Qt.PointingHandCursor : Qt.ArrowCursor

        onEntered: if (root.bar && typeof root.bar.showTooltip === "function")
                       root.bar.showTooltip(root, root.tooltipText)
        onExited:  root.hideOwnTooltip()
        onClicked: (mouse) => { if (root.pressable) root.triggerPress(mouse.button) }
        onWheel:   (wheel) => root.wheelMoved(wheel.angleDelta.y)
    }
}
