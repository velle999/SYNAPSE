#!/usr/bin/env python3
"""synui-clock — waybar's bar clock (custom/clock).

Renders the time using the format the Date & Time settings panel writes to
~/.config/synui/clock.state (see synui/src/clock.c). The panel saves:

    format=12|24        12- or 24-hour
    seconds=0|1         show seconds
    zones=A|B|C         world-clock IANA zones (tooltip only)

The bar polls this once a second (quickshell's Clock.qml, a 1s Timer), so
flipping 12/24-hour or seconds in the panel changes the bar within a second with
no bar reload.
This is the reader half of that pipeline; without it the bar shows a fixed
format and the panel's toggles do nothing — which is the bug this fixes.

Python, not shell: the zones come from state a user can edit, and they end up
inside a JSON string. json.dumps escapes them; strftime does the per-zone
formatting via zoneinfo without shelling TZ around.
"""

import json
import os
import sys
from datetime import datetime

try:
    from zoneinfo import ZoneInfo
except ImportError:  # pragma: no cover — Python < 3.9, not shipped on the ISO
    ZoneInfo = None


def state_path():
    xdg = os.environ.get("XDG_CONFIG_HOME")
    if xdg:
        return os.path.join(xdg, "synui", "clock.state")
    home = os.environ.get("HOME", "")
    return os.path.join(home, ".config", "synui", "clock.state")


def read_state():
    # Defaults must match clock_seed_zones()/the struct defaults in clock.c so
    # the bar and the panel agree before the panel has ever saved.
    st = {"format": "12", "seconds": "0",
          "zones": "America/New_York|Europe/London|Asia/Tokyo"}
    try:
        with open(state_path(), encoding="utf-8", errors="replace") as f:
            for line in f:
                key, sep, val = line.partition("=")
                if sep and key in st:
                    st[key] = val.strip()
    except FileNotFoundError:
        pass          # never saved — keep defaults
    except OSError:
        pass
    return st


def time_format(fmt24, seconds):
    if fmt24:
        return "%H:%M:%S" if seconds else "%H:%M"
    return "%I:%M:%S %p" if seconds else "%I:%M %p"


def main():
    st = read_state()
    fmt24 = st["format"] == "24"
    seconds = st["seconds"] == "1"
    tfmt = time_format(fmt24, seconds)

    now = datetime.now().astimezone()
    text = f"{now.strftime(tfmt)}  {now.strftime('%Y-%m-%d')}"

    # Tooltip: the full local date, then the world clocks the panel manages.
    lines = [now.strftime("%A, %B %-d %Y")]
    zfmt = "%H:%M %a" if fmt24 else "%I:%M %p %a"
    zones = [z.strip() for z in st["zones"].replace(",", "|").split("|") if z.strip()]
    if zones and ZoneInfo is not None:
        lines.append("")
        for z in zones:
            try:
                zt = datetime.now(ZoneInfo(z))
            except Exception:      # unknown/removed zone name — skip it
                continue
            label = z.rsplit("/", 1)[-1].replace("_", " ")
            lines.append(f"{label}: {zt.strftime(zfmt)}")

    print(json.dumps({"text": text, "tooltip": "\n".join(lines)}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
