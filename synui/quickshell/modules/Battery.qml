import QtQuick
import Quickshell.Services.UPower
import "../components"
import ".."

/*
 * Battery — hidden entirely on a desktop.
 *
 * waybar hid this module by itself when /sys/class/power_supply/BAT* was
 * absent, which is why the waybar config needed no detection and why the same
 * config works on the dev desktop and on a laptop installed from the ISO.
 * UPower always exposes a displayDevice, so the `visible` binding below is
 * what reproduces that: no laptop battery, no module.
 */
BarModule {
    id: root

    readonly property var dev: UPower.displayDevice
    readonly property bool present: dev ? (dev.isLaptopBattery && dev.isPresent) : false
    readonly property int  pct: dev ? Math.round(dev.percentage * 100) : 0
    readonly property bool charging: dev
        ? (dev.state === UPowerDeviceState.Charging
           || dev.state === UPowerDeviceState.PendingCharge)
        : false
    readonly property bool full: dev ? dev.state === UPowerDeviceState.FullyCharged : false

    moduleVisible: present

    // Steps with the level, matching the waybar format-icons ramp.
    icon: charging ? Icons.batCharging
                   : (pct >= 90 ? Icons.batFull
                      : pct >= 70 ? Icons.batThreeQtr
                      : pct >= 45 ? Icons.batHalf
                      : pct >= 20 ? Icons.batQuarter
                      : Icons.batEmpty)
    iconColor: charging ? Theme.barGreen
                        : (pct <= 10 ? Theme.barRed : pct <= 20 ? Theme.barClock : Theme.barGlyph)
    text: full ? "full" : (pct + "%")
    textColor: (!charging && pct <= 10) ? Theme.barRed : Theme.barFg

    tooltipText: {
        if (!dev) return "Battery"
        let s = "Battery " + pct + "%"
        if (charging && dev.timeToFull > 0)
            s += "\n" + fmt(dev.timeToFull) + " until full"
        else if (!charging && dev.timeToEmpty > 0)
            s += "\n" + fmt(dev.timeToEmpty) + " remaining"
        if (dev.changeRate) s += "\n" + dev.changeRate.toFixed(1) + " W"
        if (dev.healthSupported) s += "\nHealth " + Math.round(dev.healthPercentage) + "%"
        return s
    }

    function fmt(seconds) {
        const h = Math.floor(seconds / 3600)
        const m = Math.floor((seconds % 3600) / 60)
        return h > 0 ? (h + "h " + m + "m") : (m + "m")
    }
}
