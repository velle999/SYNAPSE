#!/usr/bin/env python3
"""synui-clock — waybar's bar clock (custom/clock).

Renders the time using the format the Date & Time settings panel writes to
~/.config/synui/clock.state (see synui/src/clock.c). The panel saves:

    format=12|24        12- or 24-hour
    seconds=0|1         show seconds
    date=<layout>       which date layout — see LAYOUTS below
    zones=A|B|C         world-clock IANA zones (tooltip only)

THIS FILE IS THE AUTHORITY ON THE DATE LAYOUTS. It is what actually renders the
bar and the desktop widget, so `synui-clock --layouts` is what syn-settings asks
rather than keeping a list of its own — a second copy in another package is a
list that drifts and then offers a layout nothing renders. clock.c keeps a copy
for its cycle row only, in the same repo and the same package, and an id it does
not recognise is preserved rather than reset.

Two flags, both for syn-settings' Date & Time pane:

    --layouts   `id<TAB>label<TAB>example` for every date layout
    --preview   the bar's own string, plain, so the pane's preview cannot
                disagree with the bar it is previewing

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
    st = {"format": "12", "seconds": "0", "date": DEFAULT_LAYOUT,
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


# id -> (label, strftime pattern). None means "no date at all", which is a real
# answer: a bar clock is not obliged to be a calendar.
#
# The date is the half of this that is not universal. %Y-%m-%d was hardcoded
# here, which is unambiguous and is also not how most of the world writes a
# date — and 08/12 means two different days depending on who is reading it,
# which is the entire reason "Day first" and "Month first" are separate entries
# rather than one "international" checkbox.
# The fourth column is the LONG form, for the tooltip. It exists because the
# tooltip was "%A, %B %-d %Y" for everyone — so somebody who chose Day first
# still got "Wednesday, August 12 2026" one hover away from the bar they just
# fixed. Spelled out per layout rather than derived from the short pattern:
# deriving it means parsing a strftime string to guess an intent that is
# already known here.
LAYOUTS = [
    ("iso",      "ISO 8601",           "%Y-%m-%d",    "%A, %-d %B %Y"),
    ("dmy",      "Day first",          "%d/%m/%Y",    "%A, %-d %B %Y"),
    ("mdy",      "Month first",        "%m/%d/%Y",    "%A, %B %-d %Y"),
    ("dmy-text", "Day first, named",   "%-d %b %Y",   "%A, %-d %B %Y"),
    ("mdy-text", "Month first, named", "%b %-d, %Y",  "%A, %B %-d %Y"),
    ("locale",   "Follow the locale",  "%x",          "%A, %x"),
    # No date on the BAR still deserves one in the tooltip — that is where you
    # look when the bar does not say.
    ("off",      "No date",            None,          "%A, %-d %B %Y"),
]

DEFAULT_LAYOUT = "iso"


def date_format(layout):
    """The strftime pattern for a layout id, or None for no date.

    An id this build does not know falls back to the default rather than
    printing nothing: a clock that loses its date because a settings file names
    a layout from a newer version is a worse answer than a date in the wrong
    order.
    """
    for lid, _label, fmt, _long in LAYOUTS:
        if lid == layout:
            return fmt
    return date_format(DEFAULT_LAYOUT)


def long_format(layout):
    """The spelled-out form the tooltip uses, in the layout's own order."""
    for lid, _label, _fmt, long_fmt in LAYOUTS:
        if lid == layout:
            return long_fmt
    return long_format(DEFAULT_LAYOUT)


def print_layouts():
    """`id<TAB>label<TAB>example` — the shape syn-settings' choice lists take.

    The EXAMPLE is the point. "Day first" and "Month first" are the same words
    to somebody who has to guess which order they mean; today's date written
    out cannot be misread.
    """
    now = datetime.now().astimezone()
    for lid, label, fmt, _long in LAYOUTS:
        example = now.strftime(fmt) if fmt else "time only"
        print(f"{lid}\t{label}\t{example}")
    return 0


def time_format(fmt24, seconds):
    if fmt24:
        return "%H:%M:%S" if seconds else "%H:%M"
    return "%I:%M:%S %p" if seconds else "%I:%M %p"


def main():
    if "--layouts" in sys.argv[1:]:
        return print_layouts()

    # `--preview` is the bar's own string on stdout with no JSON around it, for
    # syn-settings' Date & Time pane. It exists so the preview there is
    # literally what the bar draws: a settings pane that rendered its own
    # example with its own strftime call could disagree with the bar, and a
    # preview that can be wrong is worse than none.
    preview = "--preview" in sys.argv[1:]

    st = read_state()
    fmt24 = st["format"] == "24"
    seconds = st["seconds"] == "1"
    tfmt = time_format(fmt24, seconds)

    now = datetime.now().astimezone()
    dfmt = date_format(st["date"])
    # Two spaces between them is what BigClock.qml SPLITS ON to separate the
    # time from the date, so "no date" must leave no trailing separator behind
    # for it to split at.
    text = f"{now.strftime(tfmt)}  {now.strftime(dfmt)}" if dfmt \
        else now.strftime(tfmt)

    if preview:
        print(text)
        return 0

    # Tooltip: the full local date, then the world clocks the panel manages.
    long_date = now.strftime(long_format(st["date"]))
    lines = [long_date]
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

    # `date` is for the desktop BigClock widget, which has room for the long
    # form and used to take it from the tooltip's first line. That made "No
    # date" a setting the widget ignored — the tooltip always has a date, on
    # purpose — so the widget kept printing one after you turned it off. An
    # explicit key can be EMPTY, which the tooltip cannot. waybar ignores keys
    # it does not know, so this costs the bar nothing.
    print(json.dumps({"text": text, "tooltip": "\n".join(lines),
                      "date": long_date if dfmt else ""}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
