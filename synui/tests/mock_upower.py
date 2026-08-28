#!/usr/bin/env python3
"""A mock org.freedesktop.UPower — a battery on a machine that has none.

⚠ EXISTS BECAUSE THE DEV BOX HAS NO BATTERY. Every laptop-only path in the bar
and in the desktop monitor is unreachable here, which is exactly how one of them
can break and stay broken: the module hides itself on a desktop, and "hidden
because there is no battery" and "hidden because the check is wrong" look
identical from a chair.

Speaks on whatever bus DBUS_SYSTEM_BUS_ADDRESS names, so the caller runs a
private dbus-daemon and points the shell at that. The real system bus is never
touched.

GDBus (PyGObject) rather than dbus-python, which is not installed here — the
`dbus` module that imports is PyQt6's, and it has no dbus.service.
"""
import sys
from gi.repository import Gio, GLib

DEV_IF = "org.freedesktop.UPower.Device"
BAT = "/org/freedesktop/UPower/devices/battery_BAT0"
DISP = "/org/freedesktop/UPower/devices/DisplayDevice"

DAEMON_XML = """
<node><interface name='org.freedesktop.UPower'>
  <method name='EnumerateDevices'><arg type='ao' direction='out'/></method>
  <method name='GetDisplayDevice'><arg type='o' direction='out'/></method>
  <method name='GetCriticalAction'><arg type='s' direction='out'/></method>
  <signal name='DeviceAdded'><arg type='o'/></signal>
  <signal name='DeviceRemoved'><arg type='o'/></signal>
  <property name='DaemonVersion' type='s' access='read'/>
  <property name='OnBattery' type='b' access='read'/>
  <property name='LidIsClosed' type='b' access='read'/>
  <property name='LidIsPresent' type='b' access='read'/>
</interface></node>
"""

DEVICE_XML = """
<node><interface name='org.freedesktop.UPower.Device'>
  <method name='Refresh'/>
  <property name='Type' type='u' access='read'/>
  <property name='PowerSupply' type='b' access='read'/>
  <property name='IsPresent' type='b' access='read'/>
  <property name='Percentage' type='d' access='read'/>
  <property name='State' type='u' access='read'/>
  <property name='Energy' type='d' access='read'/>
  <property name='EnergyFull' type='d' access='read'/>
  <property name='EnergyFullDesign' type='d' access='read'/>
  <property name='EnergyRate' type='d' access='read'/>
  <property name='TimeToEmpty' type='x' access='read'/>
  <property name='TimeToFull' type='x' access='read'/>
  <property name='IconName' type='s' access='read'/>
  <property name='NativePath' type='s' access='read'/>
  <property name='Model' type='s' access='read'/>
  <property name='Capacity' type='d' access='read'/>
  <property name='Vendor' type='s' access='read'/>
  <property name='Serial' type='s' access='read'/>
  <property name='UpdateTime' type='t' access='read'/>
  <property name='Voltage' type='d' access='read'/>
  <property name='WarningLevel' type='u' access='read'/>
  <property name='BatteryLevel' type='u' access='read'/>
  <property name='Online' type='b' access='read'/>
  <property name='IsRechargeable' type='b' access='read'/>
  <property name='ChargeCycles' type='i' access='read'/>
</interface></node>
"""


# ⚠ THE DESKTOP NUMBERS ARE MEASURED, NOT INVENTED. Read off the dev machine
# (a desktop, no battery) with `gdbus call --system … GetAll`:
#
#     Type 0 (Unknown)   PowerSupply false   IsPresent false   Percentage 0
#
# upower leaves the composite device at its defaults when no power-supply
# battery exists — up_daemon_refresh_battery_props() never fills it in. A guess
# here would make the control half of the test a fiction, and the control is
# what proves the laptop half measures the predicate rather than the weather.
def desktop_props():
    p = device_props(0.0, 0)
    p["Type"] = GLib.Variant("u", 0)
    p["PowerSupply"] = GLib.Variant("b", False)
    p["IsPresent"] = GLib.Variant("b", False)
    return p


def device_props(pct, state):
    # ⚠ Type 2 = Battery and PowerSupply TRUE together are what make
    # quickshell's isLaptopBattery answer yes (type == Battery && powerSupply).
    # upower's own display device sets power-supply unconditionally — see
    # up_daemon_refresh_battery_props() in up-daemon.c.
    return {
        "Type": GLib.Variant("u", 2),
        "PowerSupply": GLib.Variant("b", True),
        "IsPresent": GLib.Variant("b", True),
        "Percentage": GLib.Variant("d", pct),
        "State": GLib.Variant("u", state),
        "Energy": GLib.Variant("d", 40.0),
        "EnergyFull": GLib.Variant("d", 50.0),
        "EnergyFullDesign": GLib.Variant("d", 56.0),
        "EnergyRate": GLib.Variant("d", 9.5),
        "TimeToEmpty": GLib.Variant("x", 7200 if state == 2 else 0),
        "TimeToFull": GLib.Variant("x", 3600 if state == 1 else 0),
        "IconName": GLib.Variant("s", "battery-good-symbolic"),
        "NativePath": GLib.Variant("s", "BAT0"),
        "Model": GLib.Variant("s", "45N1127"),
        "Capacity": GLib.Variant("d", 87.0),
        "Vendor": GLib.Variant("s", "SANYO"),
        "Serial": GLib.Variant("s", "1234"),
        "UpdateTime": GLib.Variant("t", 0),
        "Voltage": GLib.Variant("d", 11.4),
        "WarningLevel": GLib.Variant("u", 1),
        "BatteryLevel": GLib.Variant("u", 1),
        "Online": GLib.Variant("b", False),
        "IsRechargeable": GLib.Variant("b", True),
        "ChargeCycles": GLib.Variant("i", 120),
    }


DAEMON_PROPS = {
    "DaemonVersion": GLib.Variant("s", "1.90.9"),
    "OnBattery": GLib.Variant("b", True),
    "LidIsClosed": GLib.Variant("b", False),
    "LidIsPresent": GLib.Variant("b", True),
}


def register(conn, path, xml, props, methods):
    node = Gio.DBusNodeInfo.new_for_xml(xml)

    def call(c, sender, obj, iface, method, params, inv):
        if method in methods:
            inv.return_value(methods[method]())
        else:
            inv.return_value(None)

    def get(c, sender, obj, iface, name):
        return props.get(name)

    def getall(c, sender, obj, iface):
        # GDBus builds GetAll from repeated get() when this is not given, but
        # quickshell issues a real GetAll and an empty answer would look like a
        # device with no properties rather than like a missing mock.
        return props

    conn.register_object(path, node.interfaces[0], call, get, None)


def main():
    shape = sys.argv[1] if len(sys.argv) > 1 else "laptop"
    pct = float(sys.argv[2]) if len(sys.argv) > 2 else 62.0
    state = int(sys.argv[3]) if len(sys.argv) > 3 else 2

    addr = GLib.getenv("DBUS_SYSTEM_BUS_ADDRESS")
    if not addr:
        print("refusing to run without DBUS_SYSTEM_BUS_ADDRESS — this mock "
              "must never take the name on the real system bus", flush=True)
        sys.exit(1)

    conn = Gio.DBusConnection.new_for_address_sync(
        addr,
        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
        | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)

    register(conn, "/org/freedesktop/UPower", DAEMON_XML, DAEMON_PROPS, {
        "EnumerateDevices": lambda: GLib.Variant("(ao)", ([BAT],)),
        "GetDisplayDevice": lambda: GLib.Variant("(o)", (DISP,)),
        "GetCriticalAction": lambda: GLib.Variant("(s)", ("HybridSleep",)),
    })
    props = device_props(pct, state) if shape == "laptop" else desktop_props()
    for path in (BAT, DISP):
        register(conn, path, DEVICE_XML, props, {})

    Gio.bus_own_name_on_connection(
        conn, "org.freedesktop.UPower", Gio.BusNameOwnerFlags.NONE,
        lambda c, n: print(f"mock upower up: {shape} {pct}% state={state}",
                           flush=True),
        lambda c, n: print("mock upower: LOST the name", flush=True))

    GLib.MainLoop().run()


main()
