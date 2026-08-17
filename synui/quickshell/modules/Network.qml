import QtQuick
import Quickshell.Io
import "../components"
import ".."

/*
 * Network.
 *
 * Polls nmcli rather than binding Quickshell.Networking, for one reason: the
 * waybar module distinguished "disconnected" from "radio switched off", and
 * the config comment records why — an rfkill'd wlan reading as plain "offline"
 * once sent someone hunting for a driver bug when the radio was just off.
 * `nmcli radio wifi` answers that directly.
 *
 * Left click opens nmtui-connect (pick a network); the actions match the
 * waybar menu's, minus the GTK menu itself.
 */
BarModule {
    id: root

    acceptsRight: true

    property string kind: "none"      // wifi | ethernet | disabled | none
    property string ssid: ""
    property int    signal_: 0
    property string ipaddr: ""

    icon: {
        switch (kind) {
        case "wifi":     return Icons.wifi
        case "ethernet": return Icons.ethernet
        case "disabled": return Icons.netDisabled
        default:         return Icons.netDisconnected
        }
    }
    iconColor: kind === "none" ? root.pal.accent
             : kind === "disabled" ? root.pal.dim
             : root.pal.glyph
    text: kind === "wifi" ? (signal_ + "%") : ""

    tooltipText: {
        switch (kind) {
        case "wifi":     return ssid + " (" + signal_ + "%)\n" + ipaddr
                              + "\nClick to pick a network"
        case "ethernet": return "Wired\n" + ipaddr + "\nClick for network settings"
        case "disabled": return "Wi-Fi is switched off\nMiddle-click to turn it back on"
        default:         return "Disconnected\nClick to pick a network"
        }
    }

    onClicked: pick.running = true
    onRightClicked: settings.running = true
    onMiddleClicked: radioOn.running = true

    Process { id: pick;     command: ["syntty", "-e", "nmtui-connect"] }
    Process { id: settings; command: ["syntty", "-e", "nmtui"] }
    // Also clears an rfkill soft block, and needs no root: polkit lets the
    // active local session do network-control.
    Process { id: radioOn;  command: ["nmcli", "radio", "wifi", "on"] }

    // ── Poll ─────────────────────────────────────────────
    Process {
        id: radio
        command: ["nmcli", "-t", "radio", "wifi"]
        stdout: StdioCollector {
            onStreamFinished: {
                if (this.text.trim() === "disabled") {
                    root.kind = "disabled"
                } else {
                    conn.running = true      // radio is on; ask what is connected
                }
            }
        }
    }

    Process {
        id: conn
        // TYPE:STATE:CONNECTION per device, active ones first.
        command: ["nmcli", "-t", "-f", "TYPE,STATE,CONNECTION", "device", "status"]
        stdout: StdioCollector {
            onStreamFinished: {
                let found = "none"
                for (const line of this.text.split("\n")) {
                    const p = line.split(":")
                    if (p.length < 3 || p[1] !== "connected") continue
                    if (p[0] === "ethernet") { found = "ethernet"; break }
                    if (p[0] === "wifi" && found === "none") found = "wifi"
                }
                root.kind = found
                if (found === "wifi") wifi.running = true
                else if (found === "ethernet") addr.running = true
            }
        }
    }

    Process {
        id: wifi
        command: ["nmcli", "-t", "-f", "ACTIVE,SSID,SIGNAL", "device", "wifi", "list"]
        stdout: StdioCollector {
            onStreamFinished: {
                for (const line of this.text.split("\n")) {
                    const p = line.split(":")
                    if (p[0] === "yes") {
                        root.ssid = p[1] || "(hidden)"
                        root.signal_ = parseInt(p[2] || "0", 10)
                        break
                    }
                }
                addr.running = true
            }
        }
    }

    Process {
        id: addr
        command: ["sh", "-c",
                  "ip -4 -o addr show scope global | awk '{print $4}' | cut -d/ -f1 | head -1"]
        stdout: StdioCollector {
            onStreamFinished: root.ipaddr = this.text.trim() || "no address"
        }
    }

    Timer {
        interval: 5000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: radio.running = true
    }
}
