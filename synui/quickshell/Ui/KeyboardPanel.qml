import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.Commons

/*
 * KeyboardPanel — the popup a bar widget opens under its own icon.
 *
 * ⛔ THE MOST-NEEDED TYPE IN THE MODULE AFTER BarIconButton. Probed across 40 of
 * the most-installed community widgets, 22 name it: for anything that is more
 * than a readout — a game, a picker, a list — this IS the widget, and the
 * button in the strip is just the handle.
 *
 * ── Why a layer surface and not an xdg-popup ────────────────────────────────
 *
 * ⚠ AN xdg-popup ONLY RECEIVES KEYS AFTER A CLICK OR HOVER HAS ROUTED FOCUS
 * THROUGH ITS PARENT SURFACE. A panel summoned from the keyboard would open and
 * then ignore every key — which is the entire point of this type missed. So it
 * is a full-screen layer surface that asks for keyboard focus itself, with the
 * visible card placed inside it.
 *
 * ⚠ AND THE FOCUS IS PRIMED IN TWO STEPS, which looks like superstition and is
 * not. `OnDemand` is granted when a surface first maps — but NOT when a surface
 * that is still mapped for its fade-out is reopened, and not when the previously
 * focused client has the pointer constrained (a game). A brief `Exclusive` takes
 * focus in both of those cases; settling back to `OnDemand` immediately after is
 * what releases compositor-wide pointer capture so a click can still reach
 * anything else on screen.
 *
 * ⚠ FOCUS FOLLOWS `open`, NOT `visible`. The window stays mapped through the
 * fade-out so there is something to animate; if focus went with it, the keyboard
 * would be locked out of everything else for the length of the animation.
 *
 * ⚠ AND Qt STILL NEEDS AN ACTIVE-FOCUS ITEM INSIDE THE SURFACE. Layer-shell
 * granting the surface focus is not enough for `Keys.onPressed` to fire — hence
 * `focusTarget`, and hence setting it through Qt.callLater, after the surface is
 * mapped and the content has laid out.
 *
 * ── What is not here ────────────────────────────────────────────────────────
 *
 * Their `mask` subtracts the bar strip so a click on another bar icon reaches
 * the bar and swaps panels in one click. That needs a popout coordinator this
 * bar does not have, so a click on the bar closes this panel and the second
 * click opens the other — honest, and one click worse.
 */
PanelWindow {
    id: root

    required property Item anchorItem
    required property QtObject bar
    /* The widget that owns this panel. Its `close()` wins when it has one, so
     * a widget tracking its own open state is not fought with. */
    property var owner: null

    property int margin:  Style.gapsOut
    property int padding: Style.spacing.popupPadding
    property int gap:     Style.gapsOut
    property int contentWidth:  Style.space(280)
    property int contentHeight: Style.space(200)
    property bool centerOnBar: false
    property bool open: false
    property bool popoutSwitching: false
    property bool popoutSwitchClosing: false
    property bool focusPrimed: false
    property var borderSpec: Border.surfaceSpec("popups", "border", Color.popups.border,
                                                Math.max(1, Style.space(2)))

    /* The item keyboard focus lands on once the surface maps — typically a
     * PanelKeyCatcher inside the content. */
    property Item focusTarget: null

    default property alias contentItem: contentHolder.children

    readonly property var anchorWindow: root.anchorItem ? root.anchorItem.QsWindow.window : null
    readonly property string barPos: root.bar && root.bar.position ? root.bar.position : "top"
    readonly property real screenW: root.screen ? root.screen.width : 0
    readonly property real screenH: root.screen ? root.screen.height : 0
    readonly property real barThickness: root.bar && root.bar.barSize > 0 ? root.bar.barSize : 28

    function close() {
        if (root.owner && ("close" in root.owner)) root.owner.close()
        else root.open = false
    }

    // ── The surface ─────────────────────────────────────────────────────────

    screen: root.anchorWindow ? root.anchorWindow.screen : null
    visible: root.open || card.opacity > 0 || root.popoutSwitching
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore

    WlrLayershell.namespace: "synui-plugin-panel"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: root.open
        ? (root.focusPrimed ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.Exclusive)
        : WlrKeyboardFocus.None

    anchors { top: true; bottom: true; left: true; right: true }

    Timer {
        id: primeTimer
        interval: 40
        onTriggered: root.focusPrimed = true
    }

    onOpenChanged: {
        if (root.open) {
            root.focusPrimed = false
            primeTimer.restart()
            /* ⚠ callLater, NOT now. The surface is not mapped yet and the
             * content has not laid out, so forcing focus here lands on an item
             * with no size and Qt drops it again. */
            Qt.callLater(function () {
                if (root.focusTarget) root.focusTarget.forceActiveFocus()
            })
        } else {
            root.focusPrimed = false
        }
    }

    // ── Where the card goes ─────────────────────────────────────────────────
    //
    // ⚠ THE ANCHOR IS TRACKED, NOT SAMPLED. `mapToItem` is a one-shot: read
    // once, it is right until the bar's layout moves — a widget appearing to
    // its left, a monitor change — and then the panel opens somewhere the icon
    // no longer is. TransformWatcher gives the binding something that actually
    // changes when anything between the bar and the anchor moves.
    TransformWatcher {
        id: anchorWatcher
        a: root.anchorWindow ? root.anchorWindow.contentItem : null
        b: root.anchorItem
    }

    readonly property point anchorPos: {
        anchorWatcher.transform    // the reactive dependency; do not remove
        if (!root.anchorItem || !root.anchorWindow) return Qt.point(0, 0)
        return root.anchorItem.mapToItem(root.anchorWindow.contentItem, 0, 0)
    }
    readonly property real anchorW: root.anchorItem ? root.anchorItem.width : 0

    readonly property real availableCardWidth:
        root.screenW > 0 ? Math.max(120, root.screenW - root.margin * 2) : 0
    readonly property real availableCardHeight:
        root.screenH > 0 ? Math.max(120, root.screenH - root.barThickness - root.gap - root.margin) : 0
    readonly property real verticalContentInset:
        root.padding * 2 + Border.top(root.borderSpec) + Border.bottom(root.borderSpec)

    /* The two helpers their own panels size themselves with: what a widget
     * wants, capped by what is actually on screen. */
    function fittedContentWidth(width, cap) {
        let desired = Math.max(1, Number(width) || 1)
        let max = root.availableCardWidth > 0 ? root.availableCardWidth : desired
        if (cap !== undefined && Number(cap) > 0) max = Math.min(max, Number(cap))
        return Math.round(Math.min(desired, max))
    }
    function fittedContentHeight(height, cap) {
        let desired = Math.max(root.verticalContentInset,
                               (Number(height) || 0) + root.verticalContentInset)
        let max = root.availableCardHeight > 0 ? root.availableCardHeight : desired
        if (cap !== undefined && Number(cap) > 0) max = Math.min(max, Number(cap))
        return Math.round(Math.min(desired, max))
    }
    function cappedContentHeight(height) { return root.fittedContentHeight(height) }

    /* ⚠ CLAMPED, OR A WIDGET AT THE RIGHT-HAND END OPENS ITS PANEL OFF SCREEN.
     * Centred under the icon where there is room, pushed back inside where
     * there is not — never anchored to an edge it cannot see. */
    readonly property real cardX: root.centerOnBar
        ? Math.round((root.screenW - card.width) / 2)
        : Math.round(Math.max(root.margin,
              Math.min(root.screenW - card.width - root.margin,
                       root.anchorPos.x + root.anchorW / 2 - card.width / 2)))
    readonly property real cardY: (root.barPos === "bottom")
        ? Math.round(root.screenH - root.barThickness - root.gap - card.height)
        : Math.round(root.barThickness + root.gap)

    /* Anywhere outside the card dismisses. Below the card in the stacking
     * order, so a click that lands on content never reaches it. */
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onClicked: root.close()
    }

    BorderSurface {
        id: card
        x: root.cardX
        y: root.cardY
        width:  root.contentWidth
        height: root.contentHeight
        radius: Style.cornerRadius
        color: Color.popups.background
        borderSpec: root.borderSpec
        padding: root.padding

        opacity: root.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

        /* Eats the click so the dismissal area underneath never sees it. */
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

        Item {
            id: contentHolder
            anchors {
                fill: parent
                topMargin:    card.contentTopInset
                rightMargin:  card.contentRightInset
                bottomMargin: card.contentBottomInset
                leftMargin:   card.contentLeftInset
            }
        }
    }
}
