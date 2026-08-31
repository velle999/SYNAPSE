# syn-arcade

A game assistant: an overlay with frame rate and temperatures, controller
setup and testing, and the shortcuts that make a desktop usable while a game
has the screen.

## The overlay

```bash
syn-arcade hud              # what it is set to right now
syn-arcade hud toggle       # works INSIDE a running game
syn-arcade hud cycle        # move it to the next corner
syn-arcade hud set font_size 20
syn-arcade hud path         # which config is in effect, and who outranks it
```

The overlay is MangoHud, so any MangoHud setting works with `hud set`.
`hud path` exists because MangoHud reads several files in a fixed order and
"my setting did nothing" is nearly always a file further down the list
winning — the command says which one is actually being used.

⚠ **The overlay is not session-wide by default.** It is turned on for a game
by launching through `syn game`, because loading the MangoHud Vulkan layer
into everything segfaulted unrelated Vulkan clients. `syn-arcade hud on`
restores it everywhere for anyone who wants that back.

## Controllers

```bash
syn-arcade pads                    # every controller attached
syn-arcade pads info <pad>         # buttons, axes, deadzones, live sticks
syn-arcade pads test <pad>         # watch it as you press things
syn-arcade pads rumble <pad>       # check the motors
```

⚠ An Xbox controller that pairs and then fails with a GATT error is nearly
always a **stale bond** rather than a driver problem — remove the pairing and
pair it again.

## Requires

`mangohud` for the overlay and `projectm-pulseaudio` for the music
visualiser. Nothing here requires the SynapseOS compositor; it is a Wayland
program and the controller half needs no display at all.
