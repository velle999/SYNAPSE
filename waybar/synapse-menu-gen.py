#!/usr/bin/env python3
# Regenerates synapse-menu.xml and config.jsonc's custom/synapse.menu-actions
# from installed .desktop files, so the SYNAPSE waybar menu's "Applications"
# submenu always reflects what's actually installed. Run before waybar
# starts (see synuirc's autostart line) — never crashes the caller: any
# failure leaves the existing files untouched and exits 0, so waybar still
# launches with whatever menu it had last.

import configparser
import glob
import json
import os
import re
import sys
from xml.sax.saxutils import escape

WAYBAR_DIR = os.path.expanduser("~/.config/waybar")
CONFIG_PATH = os.path.join(WAYBAR_DIR, "config.jsonc")
MENU_PATH = os.path.join(WAYBAR_DIR, "synapse-menu.xml")

# XDG application dirs, in precedence order (first match for a given
# .desktop basename wins, per the XDG spec).
APP_DIRS = [
    os.path.expanduser("~/.local/share/applications"),
    "/usr/local/share/applications",
    "/usr/share/applications",
]

FIELD_CODE_RE = re.compile(r"%[fFuUdDnNickvm]")

STATIC_ITEMS = [
    ("terminal", "Terminal", "foot"),
    ("aishell", "AI Shell (synsh)", "foot synsh"),
    ("status", "System Status", "foot --hold syn status"),
    ("network", "Network Setup", "foot -e nmtui"),
    ("monitor", "Process Monitor", "foot -e top"),
]
POWER_ITEMS = [
    ("logout", "Log Out", "pkill -x synui"),
    ("reboot", "Reboot", "sudo systemctl reboot"),
    ("poweroff", "Shut Down", "sudo systemctl poweroff"),
]


def find_apps():
    seen_ids = set()
    apps = []
    for d in APP_DIRS:
        for path in sorted(glob.glob(os.path.join(d, "*.desktop"))):
            entry_id = os.path.basename(path)
            if entry_id in seen_ids:
                continue
            seen_ids.add(entry_id)

            cp = configparser.RawConfigParser(strict=False)
            try:
                with open(path, encoding="utf-8", errors="replace") as f:
                    cp.read_file(f)
            except (OSError, configparser.Error):
                continue
            if "Desktop Entry" not in cp:
                continue
            e = cp["Desktop Entry"]

            if e.get("Type", "Application") != "Application":
                continue
            if e.getboolean("NoDisplay", fallback=False):
                continue
            if e.getboolean("Hidden", fallback=False):
                continue

            name = e.get("Name")
            exec_ = e.get("Exec")
            if not name or not exec_:
                continue

            cmd = FIELD_CODE_RE.sub("", exec_)
            cmd = re.sub(r"\s+", " ", cmd).strip()
            if not cmd:
                continue
            if e.getboolean("Terminal", fallback=False):
                cmd = f"foot -e {cmd}"

            apps.append((name, cmd))

    apps.sort(key=lambda a: a[0].lower())
    return apps


def build_menu_xml(apps):
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        "<interface>",
        '  <object class="GtkMenu" id="menu">',
    ]

    def item(indent, id_, label):
        pad = "  " * indent
        lines.append(f"{pad}<child>")
        lines.append(f'{pad}  <object class="GtkMenuItem" id="{id_}">')
        lines.append(f'{pad}    <property name="label">{escape(label)}</property>')
        lines.append(f"{pad}  </object>")
        lines.append(f"{pad}</child>")

    def separator(indent, id_):
        pad = "  " * indent
        lines.append(f"{pad}<child>")
        lines.append(f'{pad}  <object class="GtkSeparatorMenuItem" id="{id_}"/>')
        lines.append(f"{pad}</child>")

    for id_, label, _ in STATIC_ITEMS:
        item(2, id_, label)
    separator(2, "sep1")

    app_actions = {}
    lines.append("    <child>")
    lines.append('      <object class="GtkMenuItem" id="applications">')
    lines.append('        <property name="label">Applications</property>')
    lines.append('        <child type="submenu">')
    lines.append('          <object class="GtkMenu" id="applications_menu">')
    for i, (name, cmd) in enumerate(apps):
        aid = f"app_{i}"
        app_actions[aid] = cmd
        item(6, aid, name)
    lines.append("          </object>")
    lines.append("        </child>")
    lines.append("      </object>")
    lines.append("    </child>")

    separator(2, "sep2")
    for id_, label, _ in POWER_ITEMS:
        item(2, id_, label)

    lines.append("  </object>")
    lines.append("</interface>")
    return "\n".join(lines) + "\n", app_actions


def main():
    try:
        apps = find_apps()
        menu_xml, app_actions = build_menu_xml(apps)

        with open(CONFIG_PATH, encoding="utf-8") as f:
            config = json.load(f)

        actions = {}
        for id_, _, cmd in STATIC_ITEMS:
            actions[id_] = cmd
        actions.update(app_actions)
        for id_, _, cmd in POWER_ITEMS:
            actions[id_] = cmd
        config["custom/synapse"]["menu-actions"] = actions

        with open(MENU_PATH, "w", encoding="utf-8") as f:
            f.write(menu_xml)
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=4)
            f.write("\n")
    except Exception as exc:  # noqa: BLE001 - never block waybar's launch
        print(f"synapse-menu-gen: {exc}", file=sys.stderr)
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
