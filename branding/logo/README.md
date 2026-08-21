# SynapseOS logo — print masters

Vector masters plus 4096px rasters. The **SVGs are the source of truth**; regenerate
any size with `rsvg-convert -w N synapseos-logo-square-transparent.svg -o out.png`.

The dendrite mark is the original vector from `synui/data/logo.svg`. The wordmark was
traced from `synui/data/synapse-logo.png` with potrace, so the letterforms are the real
ones rather than a substituted font. The CRT scanlines and red/blue fringing of that PNG
are deliberately **not** reproduced: 1px scanlines moiré or disappear in print, and the
chromatic fringe reads as a registration error.

| variant | use |
|---|---|
| `-square-…`        | 1:1, matches the original framing. Stickers, avatars, mug print. |
| (no `-square`)     | tight lockup, less dead space. Mug wraps, banners. |
| `…-dark`           | brand purple on `#0a0a12`. The large version of the original. |
| `…-transparent`    | brand purple `#a78bfa`, alpha. Dark products. |
| `…-white`          | single-colour white. Dark garments, one-colour printing. |
| `…-ink`            | deeper violet `#5b21b6`, triangle opacity lifted 0.4→0.65. **Light products.** |
| `synapseos-mark-…` | mark only, no wordmark. Small applications. |

⚠ `#a78bfa` is a light violet designed for dark grounds — it washes out badly on white
or light stock. Use the `-ink` variant there, not the purple one.

⚠ The containment triangle is a hairline at `opacity 0.4`. It survives at 4096px but
gets faint below roughly 1000px, and on a small sticker it can vanish. The `-ink`
variant already lifts it; do the same for any other small-format use.

## Naming

Every file states its colourway explicitly (`-dark`, `-transparent`, `-white`, `-ink`).
Nothing relies on a bare name meaning a default — an earlier cut had bare `.svg` meaning
transparent while bare `.png` meant the dark ground, which is exactly the kind of thing
that gets the wrong file sent to a printer.

Full matrix: 2 framings (square, tight) x 4 colourways, in SVG and 4096px PNG.
`synapseos-mark-*` is mark-only and ships in transparent purple alone.
Render another from any master:

    rsvg-convert -w 4096 synapseos-mark-transparent.svg -o mark-white-4096.png
