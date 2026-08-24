/*
 * iconhue.h — the recolour that makes SynapseOS's own app icons follow the theme.
 *
 * Every icon we draw ourselves is built from one violet family (#a78bfa and its
 * shades). That reads as a house style against SYNAPSE's cyan, and as a mistake
 * against Gruvbox's orange — the dock ends up with nine violet tiles sitting in
 * a bar that is not violet anywhere else. This maps that family onto whatever
 * the theme's panel accent currently is.
 *
 * Its own header with no synui.h behind it, for the same reason palette.h has
 * one: the transform is pure — pixels in, pixels out — and that is what makes
 * it testable against images built in the test rather than against a running
 * compositor. icons.c is the only caller.
 *
 * Buffers are native-endian ARGB32 with PREMULTIPLIED alpha, i.e. exactly what
 * cairo_image_surface_get_data() hands back and what icons.c already holds.
 * The premultiply is not a detail to skip: the hue maths has to happen on
 * straight colour or every antialiased edge shifts a different amount from the
 * solid it belongs to, and the icons fringe.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_ICONHUE_H
#define SYN_ICONHUE_H

#include <stdbool.h>

/*
 * Is this one of ours, and is it actually drawn in the house palette?
 *
 * BOTH halves are load-bearing, and neither is sufficient alone:
 *
 *   - The NAME alone is not enough. It is a prefix match, and the prefix is not
 *     ours exclusively — syncthing, synfig and synergy all answer to it.
 *   - The CONTENT alone is much worse than it looks. On a stock install
 *     `nordvpn-tray-blue` is 100% brand-hue, `nordvpn` 83%, `mpv` 71% and
 *     `falkon` 48%: a content-only rule repaints half the dock.
 *
 * Together they are tight. The nine icons we draw in the house palette measure
 * 96–100%; syn-resolve-gui measures 0% and is deliberately left alone, because
 * the clapperboard is DaVinci Resolve's branding and not ours to retint.
 *
 * `data` may be NULL, in which case only the name is judged (the caller has no
 * pixels yet) — that answers "could this ever qualify", not "does it".
 */
bool syn_iconhue_wants(const char *icon_name, const unsigned char *data,
                       int w, int h, int stride);

/*
 * Move the house violet in `data` onto `accent_rgb`, in place.
 *
 * The caller must hand this a FRESH COPY OF THE PRISTINE DECODE every time,
 * never the result of a previous call: hue assignment is not idempotent across
 * accents, so re-tinting an already-tinted icon walks it a little further from
 * itself on every theme switch. icons.c keeps the untouched decode alongside
 * the drawn surface for exactly this reason.
 *
 * What moves is the hue, and the recolour happens in OKLab so that "the same
 * icon in another colour" is what actually comes out. Each pixel keeps the
 * PERCEIVED lightness it was drawn at — not its HSL lightness, which is a
 * channel average and lets a green land 30 points of CIE L* above the violet it
 * replaced — so the icon's internal light/dark structure survives the move on
 * every accent instead of on the ones that happen to sit near the violet's
 * luminance. Chroma moves with the theme: it is scaled by the accent's own
 * chroma over the brand violet's, so a quiet theme gets quiet icons and a vivid
 * one gets vivid ones, which is the difference between an icon that follows the
 * accent and one that merely wears its hue.
 *
 * ⚠ AN ACCENT WITH NO HUE IS AN ANSWER, NOT AN ABSENCE. `wallpaper_palette()`
 * answers a picture with no colour in it in white and greys rather than
 * inventing a hue (palette.c, syn_palette_monochrome), and the panels, the bar
 * and every app window go monochrome with it. So do the icons: chroma goes to
 * zero and each pixel keeps the perceived lightness it was drawn at, which in
 * monochrome is the whole of the drawing. Note what that is NOT — it is not
 * "leave them alone", which puts nine violet tiles in a white-and-grey dock,
 * and it is emphatically not rotating them onto the accent, because a grey
 * reads as h = 0 and the entire family would come out RED.
 *
 * Two things differ from the hue path, and only in this branch. Every colour
 * in the icon goes, not just the two the hue path knows by name: leaving a
 * foreign hue alone protects its meaning when there is another hue to move it
 * to, and in monochrome it only leaves coloured crumbs on a grey icon. And the
 * teal detail gives way in LIGHTNESS rather than hue, since there is no hue
 * circle left to step around — see mono_detail_L() and detail_weight().
 */
void syn_iconhue_apply(unsigned char *data, int w, int h, int stride,
                       const float accent_rgb[3]);

#endif /* SYN_ICONHUE_H */
