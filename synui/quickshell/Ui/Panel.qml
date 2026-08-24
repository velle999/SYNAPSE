import QtQuick
import Quickshell.Io
import qs.Commons

/*
 * Panel — the base for a plugin's popup.
 *
 * Most bar plugins are two things from one entry point: a button in the strip
 * and a panel that opens under it. BarWidget is the contract for the button;
 * this is the contract for the panel. It owns only the lifecycle — open, close,
 * toggle, and the IPC that lets something outside the bar summon it — while the
 * implementation owns its content, its keyboard handling and how it looks.
 *
 * ⚠ `switchPanel` IS A HOST FEATURE AND RETURNS false HERE. On Omarchy, arrowing
 * off the edge of one open panel moves to the next widget's panel along the bar.
 * synui's bar has no such notion, so the call is answered honestly rather than
 * left undefined — a widget checks the return value and simply does not move.
 */
Item {
    id: root

    property QtObject bar: null
    property string moduleName: ""
    property var settings: ({})
    property string ipcTarget: ""
    property bool manageIpc: true
    property alias controller: panelController
    /* Their flags for the moment one panel closes because another is opening,
     * so a widget can suppress a close animation it is about to reverse. */
    property bool popoutSwitching: false
    property bool popoutSwitchClosing: false

    readonly property bool opened: panelController.open
    readonly property color barForeground: root.bar && root.bar.barForeground
                                           ? root.bar.barForeground : Color.foreground

    function open()  { panelController.show() }
    function close() { panelController.hide() }
    function toggle() { if (root.opened) root.close(); else root.open() }

    function closeForPopoutSwitch() {
        root.popoutSwitchClosing = true
        root.close()
        Qt.callLater(function () { root.popoutSwitchClosing = false })
    }

    function switchPanel(direction) {
        if (root.bar && typeof root.bar.switchPanelFrom === "function")
            return root.bar.switchPanelFrom(root, direction)
        return false
    }

    /* Same rule as BarWidget.setting: null and undefined both take the
     * fallback, because a key present-but-null is how "I cleared this" is
     * spelt in a settings file somebody edited by hand. */
    function setting(name, fallback) {
        const value = root.settings ? root.settings[name] : undefined
        return (value === undefined || value === null) ? fallback : value
    }

    PanelController { id: panelController }

    /*
     * ⚠ GUARDED ON A TARGET BEING SET. An IpcHandler with an empty target is a
     * handler registered under no name, and two panels that both left it empty
     * would collide. A widget that wants to be summonable sets `ipcTarget`;
     * one that does not gets no handler at all.
     */
    IpcHandler {
        enabled: root.manageIpc && root.ipcTarget !== ""
        target: root.ipcTarget

        function open(): void   { root.open() }
        function close(): void  { root.close() }
        function show(): void   { root.open() }
        function hide(): void   { root.close() }
        function toggle(): void { root.toggle() }
    }
}
