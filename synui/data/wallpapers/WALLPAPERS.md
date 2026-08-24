# `data/wallpapers/` — provenance and licence

Everything in this directory installs **flat into `/usr/share/backgrounds`**,
which is what the Super+W picker (`wppick_scan_dir()` in `src/wppick.c`) reads.
None of it is SynapseOS artwork — our own drawings are `data/synapse-*.png` and
go through `install_data()` in `meson.build`. **This directory is the
third-party one**, and nothing lands here without a grant recorded below.

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

Renaming the file is not a modification of the work. If you ever *do* crop,
recolour or re-encode this picture, it stops being a redistribution and the
paragraph above stops applying — say so here, and ship the result CC BY-SA 4.0.

### Attribution actually has to ship

CC BY-SA 4.0 §3(a) requires the creator's name, the licence notice, the
warranty disclaimer and a link to the material to travel *with* the copy — a
line in a repo file the user never receives does not satisfy it. `package()`
therefore installs this document to
**`/usr/share/licenses/synui/WALLPAPERS.md`**, which is where Arch packages put
a licence that is not one of the common ones. It is not put in
`/usr/share/backgrounds` next to the image: that directory is a picker list, not
a document store.

> The material is provided as-is and without warranties of any kind, to the
> extent permitted by the licence. See the full text at
> <https://creativecommons.org/licenses/by-sa/4.0/legalcode> for the complete
> disclaimer of warranties and limitation of liability.

### It does not affect synui's own licence

synui's code is GPL-2.0-or-later and this is a **CC BY-SA 4.0 photograph that
sits beside it in the same package** — mere aggregation, not linking and not a
derivative work. `license=()` in `PKGBUILD` names `CC-BY-SA-4.0` so the metadata
is honest about what is in the package; no synui source file is affected by it,
and no GPL/CC compatibility question arises, because nothing here combines the
two into one work.
