# `data/wallpapers/` — provenance and licence

Everything in this directory installs **flat into `/usr/share/backgrounds`**,
which is what the Super+W picker (`wppick_scan_dir()` in `src/wppick.c`) reads.
**This is the third-party directory** — SynapseOS's own drawings are
`data/synapse-*.png` and go through `install_data()` in `meson.build`. Nothing
lands here without a grant recorded below. That includes work of ours that is
*derived* from someone else's: the noir grade below is SynapseOS's edit, and it
lives here rather than with our own artwork precisely because it carries
somebody else's licence with it.

The filename prefix says where a file came from, because that list is flat and
shared with every other package: `antiquity-` for diinki's, `commons-` for
Wikimedia Commons.

## The three `antiquity-*` files

From diinki's [linux-antiquity](https://github.com/diinki/linux-antiquity),
under its repository-root **MIT** licence — the same grant the QML in
`quickshell-antiquity/` ships under. Two of the three are collages of
out-of-copyright printed artwork, and one was re-encoded from PNG to JPEG.

The full write-up — the grant reasoning, the underlying artwork each collage is
built from, and the re-encoding measurements — is
**[`../../quickshell-antiquity/WALLPAPERS.md`](../../quickshell-antiquity/WALLPAPERS.md)**.
Read that before touching them; it is not duplicated here.

## `commons-st-louis-night.jpg`

| | |
|---|---|
| **Title** | *St. Louis on the Mississippi river by night. Jefferson National Expansion Memorial aka. Gateway Arch and Old Courthouse are visible.* |
| **Author** | **Daniel Schwen** (Wikimedia Commons: `Dschwen`) |
| **Source** | <https://commons.wikimedia.org/wiki/File:St_Louis_night_expblend.jpg> |
| **Licence** | **CC BY-SA 4.0** — <https://creativecommons.org/licenses/by-sa/4.0/> |
| **Taken** | 27 January 2008 |
| **Original** | 3000×1929, JPEG, 1.35 MB |

A Commons *featured picture* and *quality image*; picture of the day on
3 May 2008. An exposure blend, hence upstream's `expblend` filename — several
exposures combined so the lit skyline and the sky both hold detail, which is
also why it survives being stretched to a desktop.

### What we changed: nothing

**The file is byte-for-byte upstream's**, at full resolution — verify with

    sha256sum data/wallpapers/commons-st-louis-night.jpg
    4f716ae463ea703d40e740acf7e37625dcf1f963ad96450d209e1cba20d95181

This is deliberate and is the whole reason it was not re-encoded the way the
antiquity collages were. **CC BY-SA 4.0 treats a modified copy as an adapted
work**, which we would then have to license under CC BY-SA 4.0 ourselves and
mark as changed. A verbatim copy is a plain redistribution: attribution and the
licence notice are the entire obligation. At 1.35 MB for 3000×1929 there was
nothing to win by re-encoding it — the collages were re-encoded because they
were 23 MB of PNG, and this already is a JPEG at a sane size.

Renaming the file is not a modification of the work.

## `commons-st-louis-night-noir.jpg` — an ADAPTED work

A black-and-white grade of the photograph above, made for SynapseOS. **This one
IS a modified copy, and the paragraph directly above therefore does not apply to
it.**

| | |
|---|---|
| **Original photograph** | **Daniel Schwen**, as above, CC BY-SA 4.0 |
| **Adaptation** | greyscale noir grade, the SynapseOS project, 2026 |
| **Licence of the adaptation** | **CC BY-SA 4.0** — *required*, not chosen |
| **Changes made** | converted to greyscale and re-graded for contrast; alpha channel dropped; re-encoded PNG → JPEG |

**The ShareAlike term is why the licence is not a decision.** CC BY-SA 4.0 §3(b)
requires an adaptation to be released under the same licence or a compatible
one, so this grade is CC BY-SA 4.0 whatever we would have preferred, and §3(a)
still requires Daniel Schwen to be credited as the author of the underlying
photograph and the modification to be **indicated** — which is what this section
is. Both obligations are inherited; neither is discretionary.

Nothing else in the package is affected. An adaptation of a CC BY-SA photograph
is still just a picture sitting beside the code (see the aggregation note
below), and ShareAlike reaches the adapted *work*, never the software shipped
alongside it.

### Re-encoded, and here the licence did not object

velle's master is a 4.03 MB PNG at 3000×1929 with a fully opaque alpha channel.
Shipped as **JPEG, quality 92, 4:4:4 (no chroma subsampling)**, matching the
treatment the antiquity collages got and for the same reason — `syn-update`
pushes this package to every install:

    magick St_Louis_night_expblend-noir.png \
        -alpha off -strip -quality 92 -sampling-factor 1x1 \
        data/wallpapers/commons-st-louis-night-noir.jpg

4.03 MB → **1.21 MB** at **44.70 dB PSNR**, maximum per-channel deviation
16/255, 0.42% of pixels differing at all — the antiquity collages measured
43–45 dB and 14/255, so this sits in the same band. The dropped alpha was
uniformly opaque (`min == max == 65535`) and carried nothing.

Re-encoding was licence-neutral here **only because the work was already an
adaptation** — there was no verbatim-copy exemption left to lose. Do not read
this as permission to re-encode the colour original.

⚠ **It stays exactly greyscale through the JPEG**, which matters more than it
looks: HSL saturation measures `max = 0` on the encoded file, so every pixel is
still `r == g == b` to the byte. Had 4:2:0 subsampling or a lower quality
introduced a faint chroma drift, `syn_palette_from_pixels()` would have found a
*hue* in a black-and-white picture and tinted the whole desktop off it. That is
the reason for 4:4:4 here, not sharpness.

### What it does to the desktop accent

**It gives a grey desktop, and that is correct.** Verified by linking the real
`src/imgdec.c` + `src/palette.c` + `src/contrast.c` into a harness rather than
guessing: `syn_palette_from_pixels()` returns **`ok = false`** on this file — the
documented "nothing worth taking" answer — while `wallpaper.c` still sets
`wp_measured = true`, because it *did* look. That pair is what routes it to
`syn_palette_monochrome()`: white and greys on a dark panel, deep greys on a
pale one. `ok = false` alone would have meant "I could not look" and would have
given the theme's own accent — a colour from nowhere near the screen, which is
the bug that answer was written to fix.

⚠ It also decodes as a **1-component greyscale JPEG**, not 3-component — the
encoder picks that on its own for a grey image. `syn_decode_jpeg()` asks libjpeg
for `JCS_EXT_BGRA` and gets a correct opaque BGRA surface out of it (3000×1929,
`CAIRO_FORMAT_RGB24`, stride 12000). Tested, not assumed.

On a multi-monitor desktop this picture does not drag the accent grey: per the
comment on `wallpaper_palette()`, a greyscale wallpaper on the first output
hands the question to the next one instead of averaging toward grey.

## Attribution actually has to ship

CC BY-SA 4.0 §3(a) requires the creator's name, the licence notice, the
warranty disclaimer and a link to the material to travel *with* the copy — a
line in a repo file the user never receives does not satisfy it. `package()`
therefore installs this document — the notice for **both** St. Louis files, the
verbatim one and the adaptation — to
**`/usr/share/licenses/synui/WALLPAPERS.md`**, which is where Arch packages put
a licence that is not one of the common ones. It is not put in
`/usr/share/backgrounds` next to the image: that directory is a picker list, not
a document store.

> The material is provided as-is and without warranties of any kind, to the
> extent permitted by the licence. See the full text at
> <https://creativecommons.org/licenses/by-sa/4.0/legalcode> for the complete
> disclaimer of warranties and limitation of liability.

## None of it affects synui's own licence

synui's code is GPL-2.0-or-later, and these are **two CC BY-SA 4.0 photographs
sitting beside it in the same package** — mere aggregation, not linking and not
a derivative work. ShareAlike reaches the adapted picture and stops there; it
does not reach software merely shipped in the same package. `license=()` in `PKGBUILD` names `CC-BY-SA-4.0` so the metadata
is honest about what is in the package; no synui source file is affected by it,
and no GPL/CC compatibility question arises, because nothing here combines the
two into one work.
