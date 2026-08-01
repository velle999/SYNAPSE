# SynapseOS — Dendrite & Tux

A Wallpaper Engine wallpaper: the SynapseOS dendrite mark firing behind Tux,
as a seamless 10 s loop. Landscape (1920x1080) and portrait (1080x1920) cuts.

![preview](build/synapse-dendrite-tux-preview.jpg)

## Why it is a *video* wallpaper

Wallpaper Engine has three project types and only one of them was open to us:

- **web** renders 100% black under `linux-wallpaperengine` — CEF initialises but
  the CEF→GL path hands back an all-black texture. Upstream bug, still open.
- **scene** needs its textures in Valve's proprietary `.tex` container and a
  scene graph authored by the Wallpaper Engine editor, which is Windows-only.
- **video** is just an H.264 file the engine hands to mpv. It works, and 22 of
  velle's subscribed video wallpapers already prove it on this seat.

So `make.py` draws 300 PNG frames with PIL and encodes them.

## Building

    ./make.py                  # 1920x1080 -> build/synapse-dendrite-tux.mp4
    ./make.py --portrait       # 1080x1920 -> build/synapse-dendrite-tux-portrait.mp4
    ./make.py --single 60      # just frame 60 -> build/…-still.png, ~0.6 s
    ./make.py --keep-frames    # leave build/frames-*/ behind

Needs `python3-pillow`, `rsvg-convert` (librsvg) and `ffmpeg`.

`--single N` renders frame N of the **full-length** loop — `frames` stays the
period every animated quantity is derived from, and only the one frame is
drawn. It is also what produces `build/…-still.png`, the static cut for
swaybg/greeter use, so that still is exactly the frame the loop shows at N.

`build/` is **not** tracked (bar the preview JPEGs) — the render is
deterministic: fixed PRNG seeds, no clock input, no `random`, and fixed x264
settings. Verified, not assumed: re-rendering frames 60 and 217 from a clean
checkout reproduces the originals bit-for-bit. Re-run `make.py` rather than
committing 6 MB of derived video.

## Installing

    ./install.sh

`linux-wallpaperengine`'s `--bg` takes a Workshop **id**, not a path, so a
locally made wallpaper has to be installed into Steam's Workshop content tree
under an id of its own. These use `9000000001` / `9000000002`; Steam's real ids
are file ids in the 1e9–4e9 range and never reach that block, so a subscription
cannot land on top of them. Steam does not garbage-collect ids it does not know
about, but *verify integrity* or a reinstall of Wallpaper Engine will take the
whole tree — that is why `install.sh` exists and the packs are in the repo.

Then, per output:

    synui-wpengine set DP-1      9000000001    # landscape
    synui-wpengine set HDMI-A-1  9000000002    # the rotated portrait monitor

`synui-wpengine` runs **one engine process per output** — overrides are global
inside a single process, so they cannot be scoped per screen. It drives the
engine with `--scaling fill`, which crops a 16:9 loop to its central ~31% of
width on the rotated monitor: that is what the portrait cut is for, and why
both cuts keep the mark and the penguin near the horizontal centre.

## How the loop closes

Every animated quantity has a period that divides `FRAMES`, so the last frame
hands over to the first with no seam:

| quantity                  | period          |
| ------------------------- | --------------- |
| pulses along the dendrite | `PULSE_CYCLE` (100) — 3 per loop |
| glow "breathing"          | `FRAMES/2` |
| Tux's bob                 | `FRAMES/3` |
| starfield drift           | `FRAMES` — exactly one canvas of travel |
| Tux's blink               | once, entirely inside the loop |

Measured on the rendered frames, the wrap `f299 → f0` differs *less* than an
ordinary consecutive pair (mean 0.48 vs 0.56), so the handover is invisible.

## The mark

The dendrite geometry is lifted from `synui/data/logo.svg` in its own
coordinate space — soma at `(0, -48)` in a 1024x1024 box, the five canonical
branches as lines from it — so the mark on the wallpaper is the mark on the
Plymouth splash and the synui welcome emblem, just scaled. The palette
(`#a78bfa` on `#0a0a12`) is the same one.

The *secondary* arbor is invented: it exists so there is motion out where Tux
is not standing, and so the thing reads as a neuron rather than a logo pasted
on black.

## Files

    make.py                    the renderer
    assets/tux.svg             Tux, drawn to match the flat SynapseOS style
    pack/<id>/project.json     Wallpaper Engine project manifests
    install.sh                 copies build output + manifests into the Workshop tree
    build/                     derived, untracked
