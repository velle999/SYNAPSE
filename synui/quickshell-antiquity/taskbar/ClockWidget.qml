import QtQuick
import ".."

/*
 * `textLight`, not `text` — the one-line change that makes the bar readable.
 *
 * In every palette, `text` is the colour for glyphs drawn ON an accent fill:
 * the "T" on a theme swatch, a numeral on a gold chip. Four of the five themes
 * set it to near-black (#121212) for exactly that job. The taskbar is not an
 * accent fill — it is `glassTintColor` at 20% over the wallpaper — so painting
 * its clock, its workspace numerals and its launcher glyph with `text` put
 * near-black on a translucent strip. Upstream got away with it because
 * linux-antiquity ships pale botanical wallpapers and the strip reads light;
 * over SynapseOS's dark default the whole bar was very nearly invisible, and on
 * `eros` (glass tint #3c2d66) it disappeared outright.
 *
 * `textLight` is light in all five palettes and is what the rest of the shell
 * already uses for text on dark chrome. `accent` still marks the active
 * desktop and hover, so nothing about the highlight changes.
 */
Text {
    text: Time.time
    color: Config.colors.textLight
    font.pixelSize: 11
    font.family: Config.fontMono
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
}
