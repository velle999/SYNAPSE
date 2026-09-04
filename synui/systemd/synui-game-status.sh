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


def ai_line(ai):
    """What actually happened to the AI, from the value synui published.

    ⛔ THIS NO LONGER ASKS systemd WHETHER synapd IS STOPPED. It used to, and
    warned "⚠ synapd STILL RUNNING — the suspend did not hold" when it was:
    correct while game mode stopped the unit, and exactly backwards now that it
    does not. synapd stays up through a game and re-fits its model to the VRAM
    left, so a running synapd IS the success case and that warning would fire
    every single time.

    ⚠ "yielded" is set from synapd's own reply, not from synui's intention —
    which is the property the systemd check was there to provide, kept without
    the subprocess. The distinction that mattered is still drawn: "asked"
    means the daemon never answered.
    """
    if ai == "untouched":
        return "synapd left alone (game_suspend_ai = off)"
    if ai == "yielded":
        return "synapd yielded the GPU — still answering, from RAM"
    if ai == "asked":
        return "⚠ synapd did not answer — the GPU was not handed over"
    if ai == "suspended":
        # A synui old enough to have stopped the unit outright.
        return "synapd suspended (GPU freed)"
    # Either an older synui still, or one that decided not to act.
    return "synapd running"


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
