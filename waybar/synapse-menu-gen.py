#!/usr/bin/env python3
# Regenerates synapse-menu.xml and config.jsonc's custom/synapse.menu-actions
# from installed .desktop files, so the SYNAPSE waybar menu's "Applications"
# submenu always reflects what's actually installed. Run before waybar
# starts (see synuirc's autostart line) — never crashes the caller: any
# failure leaves the existing files untouched and exits 0, so waybar still
# launches with whatever menu it had last.
#
# The two files are a matched pair: waybar looks a clicked item's GtkMenuItem
# id ("app_7") up in menu-actions to find the command to run. Let them drift
# apart and the menu misfires in silence — ids past the end of menu-actions do
# nothing at all, and ids still in range launch whatever app now sits at that
# index. So: parse tolerantly, write both files or neither, and refuse to write
# a pair whose ids don't line up.

import configparser
import glob
import json
import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
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

# XDG main categories → the submenu we file them under, in the order we test
# them. An app usually lists several ("Game;Emulator;"), so precedence matters:
# the catch-alls (Settings/System/Utility) have to come last or they swallow
# half the menu.
CATEGORIES = [
    ("Game",        "Games"),
    ("Development", "Development"),
    ("Graphics",    "Graphics"),
    ("AudioVideo",  "Multimedia"),
    ("Audio",       "Multimedia"),
    ("Video",       "Multimedia"),
    ("Office",      "Office"),
    ("Science",     "Science"),
    ("Education",   "Education"),
    ("Network",     "Internet"),
    ("Settings",    "Settings"),
    ("System",      "System"),
    ("Utility",     "Accessories"),
]
OTHER_CATEGORY = "Other"

# A GTK3 menu renders at its natural height and *ignores* the size the
# compositor grants it in xdg_popup.configure — it even asks for resize_y in
# the positioner's constraint adjustment, then discards the answer. So a
# submenu taller than the monitor is hard-clipped: no scroll arrows, and the
# entries past the bottom edge are simply unreachable. synui already clamps the
# popup surface to the output; it cannot make GTK reflow. The only fix is to
# never build a submenu that doesn't fit.
#
# Rows are ~27px, so this bounds a submenu at ~700px — comfortable even on a
# 768px laptop panel, and it keeps a category scannable besides.
MAX_ITEMS_PER_SUBMENU = 25


def category_of(entry):
    """The submenu an app belongs in, from its .desktop Categories= list."""
    cats = {c for c in (entry.get("Categories") or "").split(";") if c}
    for key, display in CATEGORIES:
        if key in cats:
            return display
    return OTHER_CATEGORY

STATIC_ITEMS = [
    # The control panel is synui's, not a program we can exec: the compositor
    # draws it. wtype speaks virtual-keyboard-v1, which synui wires to the same
    # path a physical keyboard takes, so pressing its bind is how a client asks
    # the compositor for a panel — there is no other IPC into synui.
    # Keep this in step with the `control` bind in synui's config.c.
    ("control", "Control Panel", "wtype -M logo -k c -m logo"),
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


def strip_jsonc(text):
    """Drop // and /* */ comments and trailing commas from JSONC.

    waybar's config is JSONC and ours carries comments, but json.load only
    speaks strict JSON. It used to be handed the raw file: the first comment to
    land in config.jsonc made the load raise, the blanket except in main()
    swallowed it, and the menu quietly stopped regenerating — which is how the
    XML and menu-actions drifted apart to begin with.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':                    # copy strings verbatim, escapes and all
            out.append(c)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n:
            if text[i + 1] == "/":
                nl = text.find("\n", i)
                if nl < 0:
                    break
                i = nl
                continue
            if text[i + 1] == "*":
                end = text.find("*/", i + 2)
                i = n if end < 0 else end + 2
                continue
        out.append(c)
        i += 1
    # Legal in JSONC, fatal in JSON.
    return re.sub(r",(\s*[}\]])", r"\1", "".join(out))


def clickable_ids(menu_xml):
    """Ids of GtkMenuItems that actually dispatch an action when clicked.

    A menu item holding a <child type="submenu"> only opens that submenu, so it
    needs no entry in menu-actions; every other item does.
    """
    root = ET.fromstring(menu_xml)
    ids = set()
    for obj in root.iter("object"):
        if obj.get("class") != "GtkMenuItem":
            continue
        if any(c.get("type") == "submenu" for c in obj.findall("child")):
            continue
        ids.add(obj.get("id"))
    return ids


def write_atomic(path, data):
    """Write to a temp file in the same dir, then rename over the target.

    waybar may be starting while we run; a rename is atomic, so it reads one
    whole version or the other and never a half-written menu.
    """
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".menu-gen-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


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

            apps.append((name, cmd, category_of(e)))

    apps.sort(key=lambda a: a[0].lower())
    return apps


def group_apps(apps):
    """[(name, cmd, category)] → [(category, [(name, cmd), …])], menu order.

    Alphabetical by category, except "Other" which sinks to the bottom.
    """
    buckets = {}
    for name, cmd, cat in apps:
        buckets.setdefault(cat, []).append((name, cmd))
    order = sorted(c for c in buckets if c != OTHER_CATEGORY)
    if OTHER_CATEGORY in buckets:
        order.append(OTHER_CATEGORY)
    return [(c, buckets[c]) for c in order]


def paginate(items):
    """Split an oversized category into pages that each fit on screen.

    Returns None when the category already fits, else [(label, items), …] with
    the pages balanced so you never get a page of one. Labels are initial
    ranges ("A–L"), falling back to item numbers when two pages would otherwise
    carry the same label (a category dominated by one letter).
    """
    n = len(items)
    if n <= MAX_ITEMS_PER_SUBMENU:
        return None

    pages = -(-n // MAX_ITEMS_PER_SUBMENU)   # ceil
    size = -(-n // pages)                    # …then even them out
    chunks = [items[i:i + size] for i in range(0, n, size)]

    def initial(entry):
        for ch in entry[0]:
            if ch.isalnum():
                return ch.upper()
        return "#"

    labels = [f"{initial(c[0])}–{initial(c[-1])}" for c in chunks]
    if len(set(labels)) != len(labels):
        labels, first = [], 1
        for c in chunks:
            labels.append(f"{first}–{first + len(c) - 1}")
            first += len(c)
    return list(zip(labels, chunks))


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

    # A GtkMenuItem's submenu must be <child type="submenu"><object
    # class="GtkMenu">, NOT <property name="submenu">. The latter SIGSEGVs
    # waybar 0.15 inside GtkBuilder's own g_set_error() path.
    def open_submenu(indent, id_, label):
        pad = "  " * indent
        lines.append(f"{pad}<child>")
        lines.append(f'{pad}  <object class="GtkMenuItem" id="{id_}">')
        lines.append(f'{pad}    <property name="label">{escape(label)}</property>')
        lines.append(f'{pad}    <child type="submenu">')
        lines.append(f'{pad}      <object class="GtkMenu" id="{id_}_menu">')

    def close_submenu(indent):
        pad = "  " * indent
        lines.append(f"{pad}      </object>")
        lines.append(f"{pad}    </child>")
        lines.append(f"{pad}  </object>")
        lines.append(f"{pad}</child>")

    for id_, label, _ in STATIC_ITEMS:
        item(2, id_, label)
    separator(2, "sep1")

    # Ids are handed out as items are emitted, and the action map is built in
    # the same pass, so the XML and menu-actions cannot drift out of lockstep
    # no matter how the categories shake out.
    app_actions = {}
    counter = 0

    def app_item(indent, name, cmd):
        nonlocal counter
        aid = f"app_{counter}"
        counter += 1
        app_actions[aid] = cmd
        item(indent, aid, name)

    open_submenu(2, "applications", "Applications")
    for cat, items in group_apps(apps):
        cid = "cat_" + re.sub(r"[^a-z0-9]+", "_", cat.lower()).strip("_")
        open_submenu(6, cid, cat)
        pages = paginate(items)
        if pages is None:
            for name, cmd in items:
                app_item(10, name, cmd)
        else:
            for n, (label, page) in enumerate(pages):
                open_submenu(10, f"{cid}_{n}", label)
                for name, cmd in page:
                    app_item(14, name, cmd)
                close_submenu(10)
        close_submenu(6)
    close_submenu(2)

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
            config = json.loads(strip_jsonc(f.read()))

        actions = {}
        for id_, _, cmd in STATIC_ITEMS:
            actions[id_] = cmd
        actions.update(app_actions)
        for id_, _, cmd in POWER_ITEMS:
            actions[id_] = cmd

        # Every item the XML can emit needs a command behind it, or clicking it
        # is a silent no-op. Cheap check; the bug it catches is invisible at
        # runtime. Items that only open a submenu ("Applications") are skipped —
        # they are containers, and GTK never dispatches an action for them.
        missing = clickable_ids(menu_xml) - set(actions)
        if missing:
            raise ValueError(f"menu items with no action: {sorted(missing)}")

        config["custom/synapse"]["menu-actions"] = actions

        # Both files or neither — they only mean anything as a matched pair.
        write_atomic(MENU_PATH, menu_xml)
        write_atomic(CONFIG_PATH, json.dumps(config, indent=4) + "\n")
    except Exception as exc:  # noqa: BLE001 - never block waybar's launch
        print(f"synapse-menu-gen: {exc}", file=sys.stderr)
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
