#!/usr/bin/env python3
"""synui-game-status — waybar's game mode indicator (custom/gamemode).

Reads the state synui publishes to $XDG_RUNTIME_DIR/synui-game on every
enter/leave (see synui/src/game.c) and prints one line of waybar JSON.

Python, not shell, for one reason: `app` is the game's WM_CLASS — client-
controlled — and it ends up inside a JSON string. json.dumps escapes it;
a shell here-doc would happily let a window title close the quote.
"""

import json
import os
import sys


def read_state(path):
    state = {"state": "off", "mode": "auto", "app": ""}
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
        tooltip += "\nsynapd suspended (GPU freed), idle timers held off"
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
