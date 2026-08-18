.pragma library

/*
 * tuxart.js — the pixels Tuxagotchi is drawn from.
 *
 * Every sprite is an array of equal-length strings, one character per pixel,
 * and the character names a colour in `pal`. `.` is transparent. That is the
 * whole format: it is diffable, a frame can be read in a terminal, and adding
 * an animation frame is adding a picture rather than writing code.
 *
 * DATA, NOT DRAWING. The widget scales these onto a Canvas at whatever the
 * screen wants; nothing here knows how big the pet is.
 *
 * A `.pragma library` JS file rather than a QML singleton, which is what this
 * was first, and the difference is not style. A singleton has to be declared in
 * quickshell/qmldir — and importing that module INSTANTIATES EVERY SINGLETON IN
 * IT, so the sprite table would drag Theme, WidgetState and the rest of the bar
 * in behind it, every one of which imports Quickshell. That is exactly what
 * broke tests/tux_screen.qml: `Type TuxArt unavailable` → `Type TuxState
 * unavailable` → `plugin "quickshell-coreplugin" not found`, four levels down
 * from a file that wanted a picture of a penguin. A library is loaded once,
 * shared between every file that imports it, and owes nothing to any module.
 *
 * WHY NOT A PNG, like the pizza slice. Because this one MOVES and changes
 * colour: the pet blinks, chews, sleeps and gets old, and every mood is the
 * same pixels through a different palette (see `mood`). A sheet of PNGs would
 * be a dozen files that cannot be tinted and cannot be read.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */


/*
 * The pen. Tux is BLACK, WHITE AND ORANGE and stays that way whatever the
 * desktop theme is doing — he is a logo, not a widget accent, and a
 * magenta penguin is a different animal. The theme gets the screen he
 * stands on (see Tuxagotchi.qml), which is where a tint belongs.
 */
var pal = {
    "k": "#16181d",   // body, outline
    "w": "#f7f7f2",   // belly, face
    "o": "#f6a01e",   // beak, feet
    "p": "#8a6a3a",   // what comes out the other end
    "f": "#9fc3d8",   // fish
    "n": "#d9a45b",   // biscuit
    "b": "#6b4423",   // chocolate chips
    "r": "#e05263",   // hearts
    "m": "#e8eef2",   // tablet
    "g": "#9aa0a6",   // headstone
    "y": "#e0ac1f",   // the light
    "j": "#4aa697",   // the ball
    "q": "#4f9bd0"    // water
}

/*
 * ⚠ The three icon colours above are deliberately the darker end of their hue.
 * These sprites are printed on the WIDGET CARD, not on the LCD, and that card
 * follows the theme — a pale yellow bulb and a mint ball are perfectly readable
 * on the dark HUD and nearly gone on macOS 26. Everything on the screen itself
 * can be brighter, because the screen is a colour this file chooses.
 */

/*
 * Moods are PALETTE SWAPS of the same frames, which is why there is no
 * "sick Tux" drawing. A sick penguin is the same penguin gone grey-green,
 * an old one is the same penguin faded — and doing it this way means every
 * frame gets every mood for free, including frames added later. It is also
 * the one thing here that costs nothing at all to animate.
 */
function mood(name) {
    if (name === "sick")
        return { "k": "#2b3129", "w": "#d8e0cf", "o": "#b9922f" }
    if (name === "senior")
        return { "k": "#3b3f47", "w": "#e6e6e2", "o": "#c9a05c" }
    if (name === "asleep")
        return { "k": "#2a2d35", "w": "#dcdcd8", "o": "#c08a2a" }
    return null
}

// ── The penguin, 16 × 18 ─────────────────────────────────
/*
 * Feet together and feet apart. The bob between them is the view's (a one
 * pixel lift), so this pair is a WALK rather than a redraw of the whole
 * bird — the difference between the two frames is six pixels.
 */
var tuxIdle = [
    ".....kkkkkk.....",
    "...kkkkkkkkkk...",
    "..kkkkkkkkkkkk..",
    "..kkkkkkkkkkkk..",
    "..kkkwwwwwwkkk..",
    "..kkwwkwwkwwkk..",
    "..kkwwkwwkwwkk..",
    "..kkwwwwwwwwkk..",
    "..kkwwoooowwkk..",
    "..kkwwwoowwwkk..",
    "..kkkwwwwwwkkk..",
    ".kkkwwwwwwwwkkk.",
    "kkkkwwwwwwwwkkkk",
    "kkkkwwwwwwwwkkkk",
    "kkkkwwwwwwwwkkkk",
    ".kkkwwwwwwwwkkk.",
    "..kkwwwwwwwwkk..",
    "..ooo......ooo.."
]

var tuxStep = [
    ".....kkkkkk.....",
    "...kkkkkkkkkk...",
    "..kkkkkkkkkkkk..",
    "..kkkkkkkkkkkk..",
    "..kkkwwwwwwkkk..",
    "..kkwwkwwkwwkk..",
    "..kkwwkwwkwwkk..",
    "..kkwwwwwwwwkk..",
    "..kkwwoooowwkk..",
    "..kkwwwoowwwkk..",
    "..kkkwwwwwwkkk..",
    ".kkkwwwwwwwwkkk.",
    "kkkkwwwwwwwwkkkk",
    "kkkkwwwwwwwwkkkk",
    "kkkkwwwwwwwwkkkk",
    ".kkkwwwwwwwwkkk.",
    "..kkwwwwwwwwkk..",
    "...ooo....ooo..."
]

// Eyes shut. Used for the blink, and it IS the sleeping face — a penguin
// asleep is a penguin with its eyes closed, and the z's are the view's.
var tuxBlink = [
    ".....kkkkkk.....",
    "...kkkkkkkkkk...",
    "..kkkkkkkkkkkk..",
    "..kkkkkkkkkkkk..",
    "..kkkwwwwwwkkk..",
    "..kkwwwwwwwwkk..",
    "..kkwkkwwkkwkk..",
    "..kkwwwwwwwwkk..",
    "..kkwwoooowwkk..",
    "..kkwwwoowwwkk..",
    "..kkkwwwwwwkkk..",
    ".kkkwwwwwwwwkkk.",
    "kkkkwwwwwwwwkkkk",
    "kkkkwwwwwwwwkkkk",
    "kkkkwwwwwwwwkkkk",
    ".kkkwwwwwwwwkkk.",
    "..kkwwwwwwwwkk..",
    "..ooo......ooo.."
]

// Beak open. Alternated with tuxIdle it chews; on its own it is a shout.
var tuxOpen = [
    ".....kkkkkk.....",
    "...kkkkkkkkkk...",
    "..kkkkkkkkkkkk..",
    "..kkkkkkkkkkkk..",
    "..kkkwwwwwwkkk..",
    "..kkwwkwwkwwkk..",
    "..kkwwkwwkwwkk..",
    "..kkwwwwwwwwkk..",
    "..kkwoooooowkk..",
    "..kkwwoooowwkk..",
    "..kkkwwoowwkkk..",
    ".kkkwwwwwwwwkkk.",
    "kkkkwwwwwwwwkkkk",
    "kkkkwwwwwwwwkkkk",
    "kkkkwwwwwwwwkkkk",
    ".kkkwwwwwwwwkkk.",
    "..kkwwwwwwwwkk..",
    "..ooo......ooo.."
]

// Flippers up. The same bird with its shoulders four rows higher, which is
// as close to jumping for joy as sixteen pixels get.
var tuxCheer = [
    ".....kkkkkk.....",
    "...kkkkkkkkkk...",
    "..kkkkkkkkkkkk..",
    "..kkkkkkkkkkkk..",
    "..kkkwwwwwwkkk..",
    "..kkwwkwwkwwkk..",
    "..kkwwkwwkwwkk..",
    "..kkwwwwwwwwkk..",
    "..kkwwoooowwkk..",
    "..kkwwwoowwwkk..",
    "k.kkkwwwwwwkkk.k",
    "kkkkwwwwwwwwkkkk",
    "kkkkwwwwwwwwkkkk",
    ".kkkwwwwwwwwkkk.",
    ".kkwwwwwwwwwwkk.",
    ".kkwwwwwwwwwwkk.",
    "..kkwwwwwwwwkk..",
    "..oooo....oooo.."
]

// ── The chick, 12 × 12 ───────────────────────────────────
var chickIdle = [
    "...kkkkkk...",
    "..kkkkkkkk..",
    "..kkkkkkkk..",
    ".kkkwwwwkkk.",
    ".kkwkwwkwkk.",
    ".kkwwwwwwkk.",
    ".kkwwoowwkk.",
    ".kkwwwwwwkk.",
    "kkkwwwwwwkkk",
    "kkkwwwwwwkkk",
    ".kkwwwwwwkk.",
    "..oo....oo.."
]

var chickStep = [
    "...kkkkkk...",
    "..kkkkkkkk..",
    "..kkkkkkkk..",
    ".kkkwwwwkkk.",
    ".kkwkwwkwkk.",
    ".kkwwwwwwkk.",
    ".kkwwoowwkk.",
    ".kkwwwwwwkk.",
    "kkkwwwwwwkkk",
    "kkkwwwwwwkkk",
    ".kkwwwwwwkk.",
    "...oo..oo..."
]

var chickBlink = [
    "...kkkkkk...",
    "..kkkkkkkk..",
    "..kkkkkkkk..",
    ".kkkwwwwkkk.",
    ".kkwwwwwwkk.",
    ".kkwkwwkwkk.",
    ".kkwwoowwkk.",
    ".kkwwwwwwkk.",
    "kkkwwwwwwkkk",
    "kkkwwwwwwkkk",
    ".kkwwwwwwkk.",
    "..oo....oo.."
]

var chickOpen = [
    "...kkkkkk...",
    "..kkkkkkkk..",
    "..kkkkkkkk..",
    ".kkkwwwwkkk.",
    ".kkwkwwkwkk.",
    ".kkwwwwwwkk.",
    ".kkwoooowkk.",
    ".kkwwoowwkk.",
    "kkkwwwwwwkkk",
    "kkkwwwwwwkkk",
    ".kkwwwwwwkk.",
    "..oo....oo.."
]

// ── The egg, 12 × 14 ─────────────────────────────────────
// One frame. The wobble is a rotation, which is a property animation and
// not a second picture — an egg that rocks needs no more pixels than an
// egg that sits.
var egg = [
    "....kkkk....",
    "..kkwwwwkk..",
    ".kkwwwwwwkk.",
    ".kwwwwwwwwk.",
    "kkwwwwwwwwkk",
    "kwwwwwwwwwwk",
    "kwwwwwwwwwwk",
    "kwwoowwoowwk",
    "kwwwwwwwwwwk",
    "kwoowwwwoowk",
    "kwwwwwwwwwwk",
    "kkwwwwwwwwkk",
    ".kkwwwwwwkk.",
    "..kkkkkkkk.."
]

var eggCrack = [
    "....kkkk....",
    "..kkwwwwkk..",
    ".kkwwwwwwkk.",
    ".kwwwwwwwwk.",
    "kkwwwwkwwwkk",
    "kwwwwkwwwwwk",
    "kwwwwwkwwwwk",
    "kwwoowkwoowk",
    "kwwwwwkwwwwk",
    "kwoowkwwoowk",
    "kwwwwwkwwwwk",
    "kkwwwwwwwwkk",
    ".kkwwwwwwkk.",
    "..kkkkkkkk.."
]

// ── Things that are not the pet ──────────────────────────
var poop = [
    "...p...",
    "..ppp..",
    ".ppppp.",
    "..ppp..",
    ".ppppp.",
    "ppppppp"
]

var fish = [
    "..ffff....",
    ".ffffff..f",
    "fffffff.ff",
    "fffkffffff",
    "fffffff.ff",
    ".ffffff..f",
    "..ffff...."
]

var biscuit = [
    "..nnnnn..",
    ".nnnbnnn.",
    "nnnnnnnbn",
    "nnbnnnnnn",
    "nnnnnbnnn",
    ".nnnnnnn.",
    "..nnnnn.."
]

var pill = [
    "..kkkkk..",
    ".kmmmrrk.",
    "kmmmmrrrk",
    "kmmmmrrrk",
    ".kmmmrrk.",
    "..kkkkk.."
]

var heart = [
    ".rr.rr.",
    "rrrrrrr",
    "rrrrrrr",
    ".rrrrr.",
    "..rrr..",
    "...r..."
]

var heartEmpty = [
    ".kk.kk.",
    "k..k..k",
    "k.....k",
    ".k...k.",
    "..k.k..",
    "...k..."
]

var grave = [
    "...kkkkkk...",
    "..kggggggk..",
    ".kkggggggkk.",
    "kggggggggggk",
    "kggggkkggggk",
    "kggggkkggggk",
    "kgkkkkkkkkgk",
    "kggggkkggggk",
    "kggggkkggggk",
    "kggggkkggggk",
    "kggggggggggk",
    "kkkkkkkkkkkk",
    "..kkkkkkkk..",
    ".kkkkkkkkkk."
]

// ── The eight controls ───────────────────────────────────
/*
 * Drawn rather than set in a font, and drawn HERE rather than taken from
 * Icons.qml, because the icon row is inside the toy: a Nerd Font glyph
 * beside a 16-pixel penguin is a glyph beside a penguin. These are the same
 * kind of picture the pet is, at the same pixel size, so the whole widget
 * reads as one object. It also means the row needs no font installed to be
 * legible, which the rest of the bar cannot say.
 *
 * The meal and the biscuit above double as the icons for feeding, so there
 * is nothing to keep in step: the button IS the thing it gives him.
 */
var iconLight = [
    "...yyy...",
    "..yyyyy..",
    ".yyyyyyy.",
    ".yyyyyyy.",
    "..yyyyy..",
    "...yyy...",
    "...kkk...",
    "...kkk..."
]

var iconPlay = [
    "..jjjjj..",
    ".jjjjjjj.",
    "jjwwjjjjj",
    "jwwjjjjjj",
    "jjjjjjjjj",
    "jjjjjjjjj",
    ".jjjjjjj.",
    "..jjjjj.."
]

var iconClean = [
    "....q....",
    "...qqq...",
    "..qqqqq..",
    ".qqqqqqq.",
    "qqqqqqqqq",
    "qqqqqqqqq",
    ".qqqqqqq.",
    "..qqqqq.."
]

var iconStatus = [
    ".......kk",
    ".......kk",
    "....kk.kk",
    "....kk.kk",
    ".kk.kk.kk",
    ".kk.kk.kk",
    ".kk.kk.kk"
]

var iconScold = [
    "...kkk...",
    "...kkk...",
    "...kkk...",
    "...kkk...",
    "...kkk...",
    ".........",
    "...kkk...",
    "...kkk..."
]

// The left/right the guessing game is played with, and the same arrows the
// pet turns to face. One sprite, mirrored by the view for the other way.
var iconArrow = [
    "....k....",
    "...kk....",
    "..kkk....",
    ".kkkkkkk.",
    "..kkk....",
    "...kk....",
    "....k...."
]
