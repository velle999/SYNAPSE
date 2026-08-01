#!/usr/bin/env python3
"""Render the "Synapse Dendrite — Tux" Wallpaper Engine wallpaper.

Wallpaper Engine's three project types are scene, video and web. `web` renders
100% black under linux-wallpaperengine (upstream CEF->GL bug) and `scene` needs
textures in Valve's proprietary .tex container, so this is a **video**
wallpaper: a seamless H.264 loop the engine plays on a wlr-layer-shell surface.
Everything below just draws the frames.

The loop is built so that every animated quantity is periodic with a period
that divides FRAMES, which is what makes the last frame hand over to the first
without a visible seam:

    pulses along the dendrite   PULSE_CYCLE frames   (3 per loop)
    glow "breathing"            FRAMES/2             (2 per loop)
    starfield drift             FRAMES               (1 wrap per loop)
    Tux's bob                   FRAMES/3
    Tux's blink                 once, entirely inside the loop
    the wordmark's hue sweep    FRAMES               (1 full cycle per loop)

Drawing is done with PIL at 2x and downsampled (SS), because PIL has no
antialiasing of its own. Anything that moves is a small pre-rendered sprite
added into the frame rather than a full-canvas redraw, so a frame costs a few
composites instead of a few hundred draw calls.

Usage:  ./make.py [--portrait] [--frames N] [--wordmark purple|rgb|none] [--out DIR]
"""

import argparse
import colorsys
import math
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
TUX_SVG = os.path.join(HERE, "assets", "tux.svg")

# SynapseOS palette. The dendrite mark is #a78bfa on #0a0a12 everywhere else in
# the OS (Plymouth splash, synui welcome emblem) — keep it that way here.
PURPLE = (167, 139, 250)
PURPLE_DEEP = (108, 78, 214)
PURPLE_DIM = (86, 66, 150)
BG = (10, 10, 18)
BG_EDGE = (5, 5, 10)
SPARK = (232, 226, 255)

FPS = 30
FRAMES = 300  # 10 s
PULSE_CYCLE = 100  # frames; must divide FRAMES
SS = 2  # supersampling factor for the static layers

WORDMARK = "SYNAPSEOS"
# Adwaita Sans is a variable font, so we can ask for a weight the OS ships no
# static cut of; DejaVu Sans Bold is the fallback because it is on every Arch
# box and this has to render on whatever machine builds the wallpaper.
# (path, weight-axis value or None for a static face)
WORDMARK_FONTS = [
    ("/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf", 760),
    ("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", None),
]

# ---------------------------------------------------------------------------
# The dendrite mark, in the source logo's own coordinate space
# ---------------------------------------------------------------------------
# Straight out of synui/data/logo.svg, whose root <g> is translated to the
# centre of a 1024x1024 box: the soma sits at (0,-48) and the branches are
# lines from it. Keeping the same numbers means the mark on the wallpaper is
# the mark on the boot splash, just scaled.

SOMA = (0.0, -48.0)
TRIANGLE = [(0, -464), (400, 272), (-400, 272)]

# (endpoint, stroke width, terminal node radius)
PRIMARY = [((0, -416), 8.0, 17.6), ((288, 192), 8.0, 17.6), ((-288, 192), 8.0, 17.6)]
INNER = [((176, 200), 4.4, 12.8), ((-176, 200), 4.4, 12.8)]


def _lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def _sprout(start, end, at, angle_deg, length, spread=1.0):
    """A secondary branch leaving the segment start->end at fraction `at`."""
    base = _lerp(start, end, at)
    dx, dy = end[0] - start[0], end[1] - start[1]
    n = math.hypot(dx, dy) or 1.0
    a = math.atan2(dy, dx) + math.radians(angle_deg)
    tip = (base[0] + math.cos(a) * length * spread, base[1] + math.sin(a) * length * spread)
    return base, tip


def build_arbor():
    """Branch polylines from the soma outward, plus the secondary arbor.

    The canonical five branches carry the identity of the mark; the secondaries
    exist so the wallpaper has motion out where Tux is not standing, and so the
    thing reads as a neuron rather than a logo pasted on black.
    """
    paths = []  # (polyline, width, tip radius, brightness)
    for end, w, r in PRIMARY:
        paths.append(([SOMA, end], w, r, 1.0))
    for end, w, r in INNER:
        paths.append(([SOMA, end], w, r, 1.0))

    for end, w, _r in PRIMARY:
        for at, ang, ln in ((0.55, 34, 130), (0.8, -30, 96), (0.93, 26, 62)):
            base, tip = _sprout(SOMA, end, at, ang, ln)
            paths.append(([SOMA, base, tip], w * 0.42, 7.0, 0.72))
    for end, w, _r in INNER:
        at, ang, ln = 0.7, -32, 84
        base, tip = _sprout(SOMA, end, at, ang, ln)
        paths.append(([SOMA, base, tip], w * 0.5, 5.5, 0.6))
    return paths


ARBOR = build_arbor()


# ---------------------------------------------------------------------------
# small drawing helpers
# ---------------------------------------------------------------------------

def dot(draw, xy, r, fill):
    x, y = xy
    draw.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def stroke(draw, pts, width, fill):
    """A polyline with round caps and joints (ImageDraw gives us neither)."""
    draw.line(pts, fill=fill, width=int(round(width)))
    for p in pts:
        dot(draw, p, width / 2.0, fill)


def polyline_len(pts):
    return [math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]


def point_at(pts, t):
    """Position at arc-length fraction t along a polyline."""
    segs = polyline_len(pts)
    total = sum(segs) or 1.0
    want = max(0.0, min(1.0, t)) * total
    for i, s in enumerate(segs):
        if want <= s or i == len(segs) - 1:
            return _lerp(pts[i], pts[i + 1], (want / s) if s else 0.0)
        want -= s
    return pts[-1]


def glow(mask, radius, color, gain=1.0):
    """An L mask -> a blurred, coloured RGB layer ready to be ADDED to a frame."""
    b = mask.filter(ImageFilter.GaussianBlur(radius))
    if gain != 1.0:
        b = b.point(lambda v: min(255, int(v * gain)))
    return ImageOps.colorize(b, (0, 0, 0), color)


def scaled(img, k):
    """Uniformly scale an RGB layer's intensity (a LUT, so this is cheap)."""
    if k >= 0.999:
        return img
    lut = [int(v * k) for v in range(256)]
    return img.point(lut * len(img.getbands()))


def add_sprite(frame, sprite, cx, cy, k=1.0):
    """Additively blend a small RGB sprite centred at (cx, cy)."""
    if k <= 0.004:
        return
    w, h = sprite.size
    x, y = int(cx - w / 2), int(cy - h / 2)
    fw, fh = frame.size
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(fw, x + w), min(fh, y + h)
    if x0 >= x1 or y0 >= y1:
        return
    piece = sprite.crop((x0 - x, y0 - y, x1 - x, y1 - y))
    if k < 0.999:
        piece = scaled(piece, k)
    box = (x0, y0, x1, y1)
    frame.paste(ImageChops.add(frame.crop(box), piece), box)


def rng(seed):
    """A tiny deterministic PRNG — the same wallpaper every build, no numpy."""
    state = seed & 0xFFFFFFFF

    def nxt():
        nonlocal state
        state = (1103515245 * state + 12345) & 0x7FFFFFFF
        return state / 0x7FFFFFFF

    return nxt


# ---------------------------------------------------------------------------
# layout
# ---------------------------------------------------------------------------

class Layout:
    """Where the mark and the penguin sit, for a given canvas.

    Both are kept near the horizontal centre on purpose: linux-wallpaperengine
    is driven with `--scaling fill`, so on velle's rotated HDMI-A-1 a 16:9 loop
    is cropped to its central ~31% of width. Anything decorative can live out
    at the edges; the subject cannot.
    """

    def __init__(self, w, h):
        self.w, self.h = w, h
        if w >= h:
            self.scale = 0.93 * h / 1080.0
            self.cx, self.cy = w / 2.0, h * 0.42
            self.tux_h = h * 0.47
            self.tux_bottom = h * 0.865
            self.mark_h = h * 0.055
            self.mark_cy = h * 0.915
        else:
            self.scale = 1.11 * w / 1080.0
            self.cx, self.cy = w / 2.0, h * 0.325
            self.tux_h = h * 0.39
            self.tux_bottom = h * 0.855
            self.mark_h = w * 0.052
            self.mark_cy = h * 0.903

    def to_px(self, p, ss=1):
        return ((self.cx + p[0] * self.scale) * ss, (self.cy + p[1] * self.scale) * ss)


# ---------------------------------------------------------------------------
# static layers
# ---------------------------------------------------------------------------

def make_background(lay):
    """Base gradient: a purple bloom behind the soma, falling off to near-black."""
    w, h = lay.w, lay.h
    small = 160  # the gradient is smooth, so build it small and scale it up
    sw, sh = small, max(1, int(small * h / w))
    img = Image.new("RGB", (sw, sh))
    px = img.load()
    gx, gy = lay.cx / w * sw, lay.cy / h * sh
    far = math.hypot(sw, sh) * 0.62
    for y in range(sh):
        for x in range(sw):
            d = min(1.0, math.hypot(x - gx, y - gy) / far)
            t = (1.0 - d) ** 2.2
            px[x, y] = (
                int(BG_EDGE[0] + (BG[0] - BG_EDGE[0]) * (1 - d) + 30 * t),
                int(BG_EDGE[1] + (BG[1] - BG_EDGE[1]) * (1 - d) + 22 * t),
                int(BG_EDGE[2] + (BG[2] - BG_EDGE[2]) * (1 - d) + 62 * t),
            )
    return img.resize((w, h), Image.BICUBIC)


def make_web(lay):
    """The faint neural web filling the frame behind everything.

    Nodes are scattered on a jittered grid and joined to their nearest
    neighbours, which gives a network that looks organic but is identical on
    every build (see rng()).
    """
    w, h = lay.w * SS, lay.h * SS
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    r = rng(0x5EED)

    cols, rows = 9, 6 if lay.w >= lay.h else 10
    nodes = []
    for i in range(cols):
        for j in range(rows):
            x = (i + 0.5 + (r() - 0.5) * 0.85) * w / cols
            y = (j + 0.5 + (r() - 0.5) * 0.85) * h / rows
            nodes.append((x, y))

    reach = max(w / cols, h / rows) * 1.02
    for i, a in enumerate(nodes):
        for b in nodes[i + 1:]:
            if math.dist(a, b) < reach:
                d.line([a, b], fill=34, width=int(1.4 * SS))
    for n in nodes:
        dot(d, n, (1.8 + r() * 2.6) * SS, 85)

    mask = mask.resize((lay.w, lay.h), Image.LANCZOS)
    layer = ImageOps.colorize(mask, (0, 0, 0), PURPLE_DIM)
    return ImageChops.add(scaled(layer, 0.60), glow(mask, 6, PURPLE_DEEP, 0.45))


def make_dendrite(lay):
    """(core, glow) for the mark itself, plus the pixel-space arbor for pulses."""
    w, h = lay.w * SS, lay.h * SS
    core = Image.new("L", (w, h), 0)
    hot = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(core)
    dh = ImageDraw.Draw(hot)

    tri = [lay.to_px(p, SS) for p in TRIANGLE]
    d.line(tri + [tri[0]], fill=96, width=int(2.2 * SS * lay.scale))

    for pts, width, tip, bright in ARBOR:
        px = [lay.to_px(p, SS) for p in pts]
        v = int(230 * bright)
        stroke(d, px, max(1.0, width * lay.scale * SS), v)
        dot(d, px[-1], tip * lay.scale * SS, 255)
        dot(dh, px[-1], tip * lay.scale * SS * 0.8, 255)

    soma = lay.to_px(SOMA, SS)
    dot(d, soma, 36 * lay.scale * SS, 255)
    dot(dh, soma, 36 * lay.scale * SS, 255)

    core_l = core.resize((lay.w, lay.h), Image.LANCZOS)
    hot_l = hot.resize((lay.w, lay.h), Image.LANCZOS)
    core_rgb = ImageOps.colorize(core_l, (0, 0, 0), PURPLE)
    aura = ImageChops.add(
        glow(core_l, 9 * lay.scale, PURPLE_DEEP, 0.85),
        glow(hot_l, 30 * lay.scale, PURPLE_DEEP, 0.7),
    )
    return core_rgb, aura


def make_stars(lay, seed, count, rmax, bright):
    w, h = lay.w * SS, lay.h * SS
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    r = rng(seed)
    for _ in range(count):
        dot(d, (r() * w, r() * h), (0.6 + r() * rmax) * SS, int(90 + r() * 165))
    mask = mask.resize((lay.w, lay.h), Image.LANCZOS)
    return scaled(ImageOps.colorize(mask, (0, 0, 0), SPARK), bright)


def make_tux(lay):
    """Render tux.svg at the layout's size, and a purple rim glow behind him."""
    hpx = int(lay.tux_h)
    wpx = int(round(hpx * 420 / 520.0))
    png = os.path.join(tempfile.gettempdir(), "syn-tux-%d.png" % hpx)
    subprocess.run(
        ["rsvg-convert", "-w", str(wpx * SS), "-h", str(hpx * SS), TUX_SVG, "-o", png],
        check=True,
    )
    tux = Image.open(png).convert("RGBA").resize((wpx, hpx), Image.LANCZOS)
    os.unlink(png)

    # The rim is the silhouette blurred and tinted: light spilling around him
    # from the mark he is standing in front of. It has to be blurred on a
    # PADDED canvas — blurring the silhouette at its own size clips the falloff
    # at the image edge, and the clip line shows up as a glowing rectangle
    # around him.
    pad = int(hpx * 0.22)
    sil = Image.new("L", (wpx + 2 * pad, hpx + 2 * pad), 0)
    sil.paste(tux.getchannel("A"), (pad, pad))
    rim = ImageChops.add(
        glow(sil, max(5, hpx * 0.030), PURPLE_DEEP, 0.55),
        glow(sil, max(16, hpx * 0.085), PURPLE_DEEP, 0.45),
    )
    return tux, rim, (int(lay.cx - wpx / 2), int(lay.tux_bottom - hpx))


def _wordmark_font(px):
    for path, weight in WORDMARK_FONTS:
        if not os.path.exists(path):
            continue
        font = ImageFont.truetype(path, px)
        if weight is not None:
            try:
                # axis order is (optical size, weight) — see the font's fvar
                font.set_variation_by_axes([32, weight])
            except Exception:
                continue  # not the variable build we expected; try the next
        return font
    raise SystemExit("no usable font for the wordmark; install ttf-dejavu")


def _wordmark_mask(text, px, track):
    """`text` as a tight L mask, letter-spaced by `track` pixels."""
    font = _wordmark_font(px)
    scratch = Image.new("L", (px * (len(text) + 2) * 2, px * 3), 0)
    d = ImageDraw.Draw(scratch)
    x = px
    for ch in text:
        d.text((x, px // 2), ch, font=font, fill=255)
        x += font.getlength(ch) + track
    return scratch.crop(scratch.getbbox())


def _rainbow(cycle, total, h):
    """A hue ramp `total` px wide that repeats every `cycle` px.

    The cycle is the width of the LETTERS, not of the padded canvas, so one
    full spectrum spans the word exactly. Cropping a window out of this and
    walking the offset from 0 to `cycle` over the loop gives a sweep that
    closes on itself.
    """
    row = Image.new("RGB", (cycle, 1))
    px = row.load()
    for x in range(cycle):
        r, g, b = colorsys.hsv_to_rgb(x / float(cycle), 0.72, 1.0)
        px[x, 0] = (int(r * 255), int(g * 255), int(b * 255))
    strip = row.resize((cycle, h), Image.NEAREST)
    out = Image.new("RGB", (total, h))
    for x in range(0, total, cycle):
        out.paste(strip, (x, 0))
    return out


class Wordmark:
    """The glowing SYNAPSEOS lockup under Tux.

    "purple" keeps the OS palette and breathes with the mark. "rgb" runs a hue
    sweep along the letters; the sweep advances exactly one cycle over the loop,
    which is what keeps it seamless (see the note at the top of the file).

    The glow is blurred on a PADDED canvas for the same reason Tux's rim is —
    blurring at the mask's own size clips the falloff into a visible rectangle.
    """

    def __init__(self, lay, mode, frames):
        self.mode, self.frames = mode, frames
        px = int(lay.mark_h * SS)
        mask = _wordmark_mask(WORDMARK, px, int(px * 0.20))

        # Keep the lockup inside the region that survives `--scaling fill` on a
        # rotated output (see Layout): never wider than 70% of the short side.
        cap = 0.70 * min(lay.w, lay.h) * SS
        if mask.width > cap:
            k = cap / mask.width
            mask = mask.resize((int(mask.width * k), int(mask.height * k)), Image.LANCZOS)
        mask = mask.resize((mask.width // SS, mask.height // SS), Image.LANCZOS)
        self.cycle = mask.width

        self.pad = int(mask.height * 1.7)
        padded = Image.new("L", (mask.width + 2 * self.pad, mask.height + 2 * self.pad), 0)
        padded.paste(mask, (self.pad, self.pad))
        self.mask = padded
        self.rgbmask = Image.merge("RGB", (padded, padded, padded))
        self.blur_near = max(3.0, mask.height * 0.30)
        self.blur_far = max(9.0, mask.height * 0.85)

        w, h = self.mask.size
        self.cx, self.cy = lay.cx, lay.mark_cy

        if mode == "rgb":
            self.strip = _rainbow(self.cycle, w + self.cycle, h)
        else:
            self.core = ImageOps.colorize(self.mask, (0, 0, 0), SPARK)
            self.aura = ImageChops.add(
                glow(self.mask, self.blur_near, PURPLE, 0.95),
                glow(self.mask, self.blur_far, PURPLE_DEEP, 0.60),
            )

    def draw(self, frame, f, breath):
        if self.mode == "rgb":
            w, h = self.mask.size
            off = int((f / float(self.frames)) * self.cycle) % self.cycle
            core = ImageChops.multiply(self.strip.crop((off, 0, off + w, h)), self.rgbmask)
            aura = ImageChops.add(
                scaled(core.filter(ImageFilter.GaussianBlur(self.blur_near)), 0.95),
                scaled(core.filter(ImageFilter.GaussianBlur(self.blur_far)), 0.85),
            )
        else:
            core, aura = self.core, self.aura
        add_sprite(frame, aura, self.cx, self.cy, 0.55 + 0.45 * breath)
        add_sprite(frame, core, self.cx, self.cy, 1.0)


def make_pulse_sprite(radius, color, gain=1.0):
    n = int(radius * 2) | 1
    m = Image.new("L", (n * 4, n * 4), 0)
    d = ImageDraw.Draw(m)
    dot(d, (n * 2, n * 2), radius * 4 * 0.28, 255)
    m = m.resize((n, n), Image.LANCZOS)
    return ImageChops.add(
        ImageOps.colorize(m, (0, 0, 0), color),
        glow(m, radius * 0.30, color, gain),
    )


# ---------------------------------------------------------------------------
# animation
# ---------------------------------------------------------------------------

def pulse_state(f, phase):
    """(t along the branch, brightness) for one pulse at frame f.

    The signal travels over the first 62% of the cycle and the branch is dark
    for the rest, which is what makes it read as a discrete spike rather than a
    conveyor belt. Ease-out so it decelerates into the terminal node.
    """
    u = ((f / PULSE_CYCLE) + phase) % 1.0
    if u > 0.62:
        return None
    s = u / 0.62
    t = 1.0 - (1.0 - s) ** 1.7
    fade = min(1.0, s / 0.12, (1.0 - s) / 0.22 + 0.35)
    return t, fade


def render(w, h, frames, outdir, title, wordmark="purple", only=None):
    """Draw `frames` frames into outdir.

    `only` renders just that one frame *without changing the loop*: `frames`
    stays the period every animated quantity is derived from, so the frame is
    the one the real loop shows at that index. Rendering 0..N with frames=N+1
    instead would silently be a different animation.
    """
    lay = Layout(w, h)
    print("layout %dx%d  scale=%.3f  soma=(%.0f,%.0f)" % (w, h, lay.scale, *lay.to_px(SOMA)))

    base = make_background(lay)
    base = ImageChops.add(base, make_web(lay))
    core, aura = make_dendrite(lay)
    stars_far = make_stars(lay, 0xC0FFEE, 220, 1.5, 0.55)
    stars_near = make_stars(lay, 0xBEEF, 90, 2.6, 0.9)
    tux, rim, tux_at = make_tux(lay)
    mark = Wordmark(lay, wordmark, frames) if wordmark != "none" else None

    unit = lay.scale * (h / 1080.0 if w >= h else w / 1080.0)
    spark = make_pulse_sprite(max(14, 26 * unit), SPARK, 1.5)
    flash = make_pulse_sprite(max(26, 52 * unit), PURPLE, 1.2)

    # Pixel-space branches, longest first so the eye-catching ones lead.
    arbor_px = []
    for i, (pts, _wdt, tip, bright) in enumerate(ARBOR):
        px = [lay.to_px(p) for p in pts]
        arbor_px.append((px, tip * lay.scale, bright, (i * 0.2379) % 1.0))

    # Tux's eye whites, in his own image space, so the blink can cover them.
    # These MUST track assets/tux.svg — the lids are drawn blind, and a stale
    # copy here just paints two dark blobs next to his eyes.
    eyes = [(178 / 420.0, 106 / 520.0, 31 / 420.0, 47 / 520.0),
            (242 / 420.0, 106 / 520.0, 31 / 420.0, 47 / 520.0)]
    tw, th = tux.size

    os.makedirs(outdir, exist_ok=True)
    for f in (range(frames) if only is None else [only % frames]):
        breath = 0.78 + 0.22 * math.sin(2 * math.pi * f / (frames / 2))
        twinkle = 0.72 + 0.28 * math.sin(2 * math.pi * f / (frames / 3) + 1.1)

        frame = base.copy()

        # Two starfields drifting at different speeds, wrapped so the loop
        # closes: over `frames` each layer travels exactly one canvas.
        for layer, speed, k in ((stars_far, 1.0, twinkle), (stars_near, -1.0, 1.0)):
            dx = int((f / frames) * w * speed) % w
            dy = int((f / frames) * h * 0.35 * speed) % h
            shifted = ImageChops.offset(layer, dx, dy)
            frame = ImageChops.add(frame, scaled(shifted, k))

        frame = ImageChops.add(frame, scaled(aura, breath))
        frame = ImageChops.add(frame, core)

        # Pulses running out along every branch, and the terminal node lighting
        # up as each one lands. A landing on one of the two branches that reach
        # down past Tux also brightens his rim, so the light on him comes from
        # the mark rather than from nowhere.
        landed = 0.0
        for px, tip, bright, phase in arbor_px:
            st = pulse_state(f, phase)
            if st is None:
                continue
            t, fade = st
            x, y = point_at(px, t)
            add_sprite(frame, spark, x, y, fade * bright)
            if t > 0.86:
                hit = (t - 0.86) / 0.14
                add_sprite(frame, flash, px[-1][0], px[-1][1], hit * fade * bright * 0.9)
                if px[-1][1] > lay.cy:
                    landed = max(landed, hit * fade * bright)

        # Tux: a slow bob, a rim that breathes with the mark behind him, and one
        # blink per loop.
        bob = int(round(3.0 * unit * math.sin(2 * math.pi * f / (frames / 3))))
        tx, ty = tux_at[0], tux_at[1] + bob
        add_sprite(frame, rim, tx + tw / 2, ty + th / 2, 0.50 + 0.40 * breath + 0.45 * landed)

        blink = 0.0
        if 140 <= f < 152:
            blink = math.sin(math.pi * (f - 140) / 12.0)
        if blink > 0.02:
            him = tux.copy()
            d = ImageDraw.Draw(him)
            for ex, ey, er, eb in eyes:
                cx, cy = ex * tw, ey * th
                rx, ry = er * tw, eb * th * blink
                d.ellipse([cx - rx, cy - ry - ry * 0.1, cx + rx, cy + ry], fill=(38, 38, 49, 255))
        else:
            him = tux
        frame.paste(him, (tx, ty), him)

        # A little of the mark's glow spills back over him, so he sits IN the
        # scene instead of on top of it.
        frame = ImageChops.add(frame, scaled(aura, 0.13 * breath))

        if mark is not None:
            mark.draw(frame, f, breath)

        frame.save(os.path.join(outdir, "f%05d.png" % f))
        if f % 25 == 0:
            print("  frame %d/%d" % (f, frames), flush=True)
    return lay


def encode(framedir, frames, out_mp4, preview_frame, preview_jpg, w, h):
    subprocess.run([
        "ffmpeg", "-y", "-loglevel", "error",
        "-framerate", str(FPS), "-i", os.path.join(framedir, "f%05d.png"),
        "-c:v", "libx264", "-preset", "slow", "-crf", "18",
        "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.2",
        "-g", str(FPS), "-movflags", "+faststart",
        out_mp4,
    ], check=True)

    pv = Image.open(os.path.join(framedir, "f%05d.png" % preview_frame)).convert("RGB")
    pv.thumbnail((900, 900), Image.LANCZOS)
    pv.save(preview_jpg, quality=92)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--portrait", action="store_true", help="render 1080x1920 instead")
    ap.add_argument("--frames", type=int, default=FRAMES)
    ap.add_argument("--out", default=os.path.join(HERE, "build"))
    ap.add_argument("--keep-frames", action="store_true")
    ap.add_argument("--wordmark", choices=("purple", "rgb", "none"), default="purple",
                    help="how to light the SYNAPSEOS lockup under Tux")
    ap.add_argument("--single", type=int, default=None, help="render ONE frame and stop")
    a = ap.parse_args()

    w, h = (1080, 1920) if a.portrait else (1920, 1080)
    # The wordmark treatment is part of the identity: purple and rgb are two
    # wallpapers under two Workshop ids, not one wallpaper with a switch.
    name = ("synapse-dendrite-tux"
            + ("-rgb" if a.wordmark == "rgb" else "")
            + ("-portrait" if a.portrait else ""))
    os.makedirs(a.out, exist_ok=True)

    if a.single is not None:
        tmp = os.path.join(a.out, "single")
        f = a.single % a.frames
        render(w, h, a.frames, tmp, name, wordmark=a.wordmark, only=f)
        shutil.copy(os.path.join(tmp, "f%05d.png" % f), os.path.join(a.out, name + "-still.png"))
        shutil.rmtree(tmp)
        print("still ->", os.path.join(a.out, name + "-still.png"))
        return

    framedir = os.path.join(a.out, "frames-" + name)
    if os.path.isdir(framedir):
        shutil.rmtree(framedir)
    render(w, h, a.frames, framedir, name, wordmark=a.wordmark)

    mp4 = os.path.join(a.out, name + ".mp4")
    jpg = os.path.join(a.out, name + "-preview.jpg")
    encode(framedir, a.frames, mp4, min(60, a.frames - 1), jpg, w, h)
    if not a.keep_frames:
        shutil.rmtree(framedir)
    print("wrote", mp4, "(%.1f MiB)" % (os.path.getsize(mp4) / 1048576.0))
    print("wrote", jpg)


if __name__ == "__main__":
    sys.exit(main())
