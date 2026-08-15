import QtQuick
import Quickshell.Io
import Quickshell.Bluetooth
import "../components"
import ".."

/*
 * Bluetooth.
 *
 * Left click hands off to synui's own Super+B panel for scan/pair/connect —
 * that panel already exists and is compositor-drawn, so duplicating it in QML
 * would mean two Bluetooth UIs that disagree. Middle click toggles the radio,
 * which is the one thing the panel cannot do from a dead adapter. bluez's
 * polkit rules let the active local session drive the adapter, so no root.
 */
BarModule {
    id: root

    acceptsRight: true

    readonly property var adapter: Bluetooth.defaultAdapter
    readonly property bool on: adapter ? adapter.enabled : false
    readonly property var connectedDevices: {
        if (!adapter || !adapter.devices) return []
        return adapter.devices.values.filter(d => d.connected)
    }
    readonly property bool anyConnected: connectedDevices.length > 0

    icon: !on ? Icons.btOff : (anyConnected ? Icons.btConnected : Icons.btOn)
    iconColor: !on ? Theme.barDim : (anyConnected ? Theme.barBlue : Theme.barGlyph)
    active: anyConnected

    tooltipText: {
        if (!adapter) return "No Bluetooth adapter"
        if (!on) return "Bluetooth off\nMiddle-click to switch it on"
        let s = adapter.name || "Bluetooth"
        if (anyConnected) {
            s += "\n" + connectedDevices.length + " connected"
            for (const d of connectedDevices) s += "\n  " + (d.name || d.address)
        } else {
            s += "\nno devices connected"
        }
        return s + "\nClick for the Bluetooth panel · middle-click toggles the radio"
    }

    onClicked: panel.running = true
    onMiddleClicked: if (adapter) adapter.enabled = !adapter.enabled
    onRightClicked: console_.running = true

    Process { id: panel;    command: ["synctl", "dispatch", "bluetooth"] }
    Process { id: console_; command: ["syntty", "-e", "bluetoothctl"] }
}
