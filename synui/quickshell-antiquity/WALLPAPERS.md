# Antiquity wallpapers — provenance and licence

Three wallpapers ship with the Antiquity shell. They come from
[diinki/linux-antiquity](https://github.com/diinki/linux-antiquity)
(`configs/hypr/wallpapers_bundled/`), the same repository this QML tree is
ported from, and they are the ones the theme was designed against.

Written for the same reason `FONTS.md` is: upstream vendored nine fonts, three
of which SYNAPSE could not redistribute, and that was only discoverable by
opening each file. Anything we ship that we did not draw gets its provenance
recorded here, in one place, before it goes on an ISO.

## The grant

`linux-antiquity` carries **`LICENSE` (MIT), © 2026 diinki**, at the repository
root with no carve-out for any subdirectory, and these files are inside that
repository. That is the same grant the QML in this directory is redistributed
under (vendored here as `LICENSE.antiquity`), and MIT permits redistribution and
modification with the notice retained.

diinki also publishes them in [diinki/wallpapers](https://github.com/diinki/wallpapers)
(`june2026/`, under the heading "Celestial Antiquity"), described there as
"the wallpapers that I've made & published, in case any of you want to use
them!". **That repository has no LICENSE file**, so it is not the grant we rely
on — it is corroboration of authorship. The MIT licence in `linux-antiquity` is.

## The underlying artwork

Two of the three are collages of much older printed artwork. Neither is a
straight scan — the arrangement, colour grading and composition are diinki's —
but the source material is worth naming, because "it's in an MIT repo" is not by
itself an answer for a photograph of someone else's painting.

| file | what it is | source material |
|---|---|---|
| `antiquity-carnation-collage.jpg` | three botanical/astronomical copperplate engravings side by side | 18th–19th century plate books. Two are hand-titled *Carnation* (Pl. 31, July) and *French Marygold* (Pl. 51, Nov.), the centre plate a Copernican orrery over a fortified town. Long out of copyright everywhere. |
| `antiquity-georges-riom-collage.jpg` | an Art Nouveau lily-and-violet panel, signed "Riom" in the plate | Georges Riom, French decorative designer; his colour lithographs are consistently catalogued as *c. 1900*. Published well before 1930, so public domain in the US on publication date. His death date is not recorded in the sources consulted, so the EU life+70 term is **not** independently confirmed here — the MIT grant above is what we ship on. |
| `antiquity-the-blackboard.png` | a dark star chart with constellations, orbital diagrams and a polar grid | Original digital artwork by diinki. Upstream's filename prefixes it `oc_` — original character/creation. Nothing third-party in it. |

## What we changed

Renamed with an `antiquity-` prefix, because they install into
`/usr/share/backgrounds` alongside SynapseOS's own artwork and `wppick.c` lists
that directory flat — a file called `carnation_collage.png` in that list says
nothing about where it came from.

**The two collages were re-encoded from 4K PNG to JPEG (quality 92, 4:4:4, no
chroma subsampling).** They are scans of printed paper, which PNG stores
appallingly: 23 MB of PNG became 4.0 MB of JPEG at 43–45 dB PSNR with a maximum
per-channel deviation of 14/255 — invisible at any viewing distance, and the
difference between a 5.5 MB synui package and a 29 MB one that `syn-update`
would push to every install, including the ones not running this shell.

`antiquity-the-blackboard.png` **stays PNG and is bit-identical** to upstream's.
It is flat-colour line art, where JPEG rings visibly along the thin strokes, and
it costs nothing anyway: `optipng -o2` took it from 871 KB to 600 KB losslessly
(verified pixel-for-pixel).

## Where they land

`/usr/share/backgrounds/`, **flat, not in a subdirectory**. `wppick_scan_dir()`
in `src/wppick.c` reads one directory and does not recurse, so a tidy
`backgrounds/antiquity/` would have made all three invisible to the Super+W
picker — which is the entire reason for shipping them.

They are therefore available to *both* bars and to the compositor's own picker,
not just to the Antiquity shell. That is deliberate: a wallpaper is not a
property of a bar.
