#!/usr/bin/env python3
"""synui-game-status — waybar's game mode indicator (custom/gamemode).

Reads the state synui publishes to $XDG_RUNTIME_DIR/synui-game on every
enter/leave (see synui/src/game.c) and prints one line of JSON for the bar
(quickshell's GameMode.qml parses it; the schema is waybar's custom-module one,
kept as-is so the two bars stay interchangeable).

Python, not shell, for one reason: `app` is the game's WM_CLASS — client-
controlled — and it ends up inside a JSON string. json.dumps escapes it;
a shell here-doc would happily let a window title close the quote.
"""

import json
import os
import subprocess
import sys


def read_state(path):
    # "ai" is absent from files written by synui pkgrel < 198; an empty value
    # means "this synui did not say", which is reported as unknown rather than
    # guessed either way.
    state = {"state": "off", "mode": "auto", "app": "", "ai": ""}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                key, sep, val = line.partition("=")
                if sep and key in state:
                    state[key] = val.strip()
    except FileNotFoundError:
        pass          # synui not running, or never entered game mode: "off"
    except OSError:
        pass
    return state


def synapd_is_active():
    """Ask systemd, rather than repeating what synui hoped for.

    The tooltip used to state "synapd suspended (GPU freed)" for any state=on.
    It was a fixed string, so it read as success in exactly the case that was
    broken: synapd is socket-activated, and a stop that is immediately undone
    by the next client connecting looks identical from synui's side. Costs one
    short-lived subprocess, and only while game mode is on.
    """
    try:
        r = subprocess.run(["systemctl", "is-active", "synapd.service"],
                           capture_output=True, text=True, timeout=2)
        return r.stdout.strip() == "active"
    except (OSError, subprocess.SubprocessError):
        return None       # cannot tell — say so instead of picking a side


def ai_line(ai):
    if ai == "untouched":
        return "synapd left running (game_suspend_ai = off)"
    if ai != "suspended":
        # Either an older synui, or one that decided not to suspend.
        return "synapd running"
    live = synapd_is_active()
    if live is None:
        return "synapd suspend requested (could not verify)"
    if live:
        # The whole point of this line. Socket activation is the usual cause:
        # stopping synapd.service alone leaves synapd.socket listening, and the
        # next client to connect starts it straight back up.
        return "⚠ synapd STILL RUNNING — the suspend did not hold"
    return "synapd suspended (GPU freed)"


def main():
    rtdir = os.environ.get("XDG_RUNTIME_DIR")
    if not rtdir:
        print("{}")
        return 0

    st = read_state(os.path.join(rtdir, "synui-game"))
    on = st["state"] == "on"
    mode = st["mode"]
    app = st["app"]

    if on:
        text = ""   # Nerd Font gamepad (fa-gamepad); CSS colours it yellow
        tooltip = f"Game mode ON — {app}" if app else "Game mode ON"
        tooltip += "\n" + ai_line(st["ai"])
        tooltip += "\nidle timers held off"
    else:
        # Not hidden when off: a bar element that only ever appears when
        # something is on gives you no way to tell "off" from "broken" — which
        # is the exact confusion that sent us looking at game mode to begin
        # with. Dim it instead, and let the CSS carry the difference.
        text = ""   # same glyph; CSS dims it grey so "off" still reads
        tooltip = "Game mode off"

    if mode == "forced-on":
        tooltip += "\nForced ON (Super+G)"
    elif mode == "forced-off":
        tooltip += "\nForced OFF (Super+G)"
    else:
        tooltip += "\nAuto (fullscreen game detection) — Super+G to override"

    print(json.dumps({
        "text": text,
        "tooltip": tooltip,
        "class": "active" if on else "inactive",
        "alt": "on" if on else "off",
    }))
    return 0


if __name__ == "__main__":
    sys.exit(main())
