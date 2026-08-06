import QtQuick
import ".."

/*
 * `Config.barText`, not `Config.colors.text` and not `textLight` — the taskbar
 * is 80% wallpaper, so its ink is chosen per palette against the wallpaper that
 * palette brings with it. The full reasoning, with the measured contrast
 * ratios, is on `barText` in Config.qml.
 *
 * For four of the five palettes barText IS `text`, which is what upstream had
 * here. 0.1.0-272 changed this line to `textLight` on the strength of one
 * palette (`eros`, whose glass tint is dark) and one wallpaper (SYNAPSE's dark
 * default, which is what the headless rig showed) — and that broke the two pale
 * botanical wallpapers this shell actually ships with, where light ink lands
 * between 1.0 and 2.2 contrast. `eros` now carries its own override instead.
 */
Text {
    text: Time.time
    color: Config.barText
    font.pixelSize: 11
    font.family: Config.fontMono
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
}
