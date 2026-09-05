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

## Big screen mode

A ten-foot interface for a television and a controller — `syn-arcade big start`,
or `Super`+`F10`. Full documentation is in the
[wiki](https://github.com/velle999/SYNAPSE/wiki/Big-Screen); two things it does
that are easy to miss:

```bash
syn-arcade big disc              # a Blu-ray, DVD or audio CD in the drive
syn-arcade big disc play         # play it, full screen, through mpv
syn-arcade big transport forward # thirty seconds on, on whatever is playing
```

A disc in the drive appears as a tile named after the disc and disappears when
you take it out — while `mpv` is installed, which on SynapseOS is a choice
rather than a given. A commercial DVD also needs `libdvdcss` and a Blu-ray needs
`libaacs`; `big disc` names whichever is missing.

A Media Center remote works as a remote: its buttons drive the interface, and
the five that reach no application at all — OK, ⏭, ⏮, Guide and Power — keep
working with a film or a browser in front of it.

## Requires

`mangohud` for the overlay and `projectm-pulseaudio` for the music
visualiser. Nothing here requires the SynapseOS compositor; it is a Wayland
program and the controller half needs no display at all.
