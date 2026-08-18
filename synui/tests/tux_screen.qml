import QtQuick
import QtQuick.Window
import "../quickshell/widgets"

/*
 * tux_screen.qml — every mood the pet has, rendered to a PNG with no
 * compositor, no bar and no pet.
 *
 *     QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
 *         qml tests/tux_screen.qml -- --out /tmp/tux.png
 *
 * Run by tests/tux_screen.sh. What it is FOR: the widget is almost entirely a
 * picture, and the only other way to look at that picture is to switch it on
 * inside a running synui — which on this machine means the live seat, and which
 * shows one mood at a time, the one the pet happens to be in. A sick pet is
 * three hours of not feeding it away. This puts all of them side by side in
 * half a second.
 *
 * It works because TuxScreen takes its pet and its colours as PROPERTIES and
 * imports nothing but QtQuick and tuxart.js. Keep it that way: the first Theme
 * reference inside TuxScreen turns this file into a rig that cannot start.
 *
 * ⚠ THE OUTPUT PATH IS NAMED, AND CHECKED. The first version of this took it
 * from the last of Qt.application.arguments, which under a runner that passes
 * no path at all is THE QML FILE ITSELF — and grabToImage cheerfully wrote a
 * PNG over this source and truncated it to nothing. `--out`, and it must end
 * in .png, or nothing is written.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
Window {
    id: win

    // A pet, exactly as far as the screen is concerned: the properties
    // TuxState publishes, with nothing behind them.
    component Pet: QtObject {
        property string stage: "adult"
        property real   ageMin: 3000
        property int    ageDays: 2
        property int    ageHours: 2
        property var    stageAt: ({ baby: 3, child: 60, teen: 480, adult: 1440, senior: 10080 })
        property int    hungerHearts: 3
        property int    happyHearts: 2
        property int    weight: 14
        property int    discipline: 40
        property int    poops: 0
        property int    sick: 0
        property int    careMisses: 0
        property bool   light: true
        property bool   asleep: false
        property bool   attention: false
        property bool   hatched: true
        property bool   playing: false
        property int    gameFace: 0
        property int    gameWins: 0
        property int    gameRound: 0
        property string doing: ""
        property string need: ""
        property bool   muted: false

        // The buttons. A stub that only had the properties would take the rig
        // down the first time a rendered ShellIcon resolved its onPressed.
        function feed(kind) {}
        function toggleLight() {}
        function medicine() {}
        function clean() {}
        function scold() {}
        function startGame() {}
        function stopGame() {}
        function guess(dir) {}
        function newEgg() {}
        function toggleMute() {}
        function beep(name) {}
    }

    property Pet egg:      Pet { stage: "egg"; ageMin: 2.9; hungerHearts: 4; happyHearts: 4; hatched: false }
    property Pet baby:     Pet { stage: "baby"; ageDays: 0; ageHours: 0; hungerHearts: 4; happyHearts: 4 }
    property Pet child:    Pet { stage: "child"; ageDays: 0; ageHours: 3 }
    property Pet teen:     Pet { stage: "teen"; ageDays: 0; ageHours: 11 }
    property Pet adult:    Pet { stage: "adult" }
    property Pet senior:   Pet { stage: "senior"; ageDays: 8; weight: 31 }
    property Pet eating:   Pet { doing: "eat" }
    property Pet sleeping: Pet { asleep: true }
    property Pet ill:      Pet { sick: 2; attention: true; need: "sick" }
    property Pet filthy:   Pet { poops: 4 }
    // Empty hearts and NOT calling: the only case that differs from `adult` in
    // the meters alone, which is what makes the meters testable at all.
    property Pet peckish:  Pet { hungerHearts: 0; happyHearts: 0 }
    property Pet calling:  Pet { hungerHearts: 0; attention: true; need: "hungry" }
    property Pet dark:     Pet { light: false }
    property Pet gaming:   Pet { playing: true; gameRound: 2; gameWins: 1; gameFace: 1 }
    property Pet gone:     Pet { stage: "gone"; ageDays: 4; ageHours: 6; hungerHearts: 0; happyHearts: 0; hatched: false }

    /*
     * EVERY CASE IS ON FRAME 0, and that is an assertion rather than a
     * convenience: tux_screen.sh proves a mood is drawn by finding two pets
     * that must differ and checking that they do, so a pair caught on
     * different frames of the walk cycle differs for a reason that has nothing
     * to do with what is being tested. `senior` on frame 1 made the
     * adult-versus-senior comparison pass with the mood palette deleted.
     */
    readonly property var cases: [
        { name: "egg",      pet: win.egg,      frame: 0 },
        { name: "baby",     pet: win.baby,     frame: 0 },
        { name: "child",    pet: win.child,    frame: 0 },
        { name: "teen",     pet: win.teen,     frame: 0 },
        { name: "adult",    pet: win.adult,    frame: 0 },
        { name: "senior",   pet: win.senior,   frame: 0 },
        { name: "eating",   pet: win.eating,   frame: 0 },
        { name: "asleep",   pet: win.sleeping, frame: 0 },
        { name: "ill",      pet: win.ill,      frame: 0 },
        { name: "filthy",   pet: win.filthy,   frame: 0 },
        { name: "hungry",   pet: win.peckish,  frame: 0 },
        { name: "calling",  pet: win.calling,  frame: 0 },
        { name: "lights out", pet: win.dark,   frame: 0 },
        { name: "playing",  pet: win.gaming,   frame: 0 },
        { name: "gone",     pet: win.gone,     frame: 0 },
        { name: "status",   pet: win.adult,    frame: 0, status: true },
        { name: "light theme", pet: win.adult, frame: 0, pale: true }
    ]

    readonly property int cols: 4
    readonly property int cellW: 210
    // The toy's own height: two icon rows, the screen, the gaps and the label.
    // Asked of a TuxShell rather than written down, so a change to the layout
    // cannot leave this rig cropping it.
    readonly property int cellH: probe.implicitHeight
    readonly property int gap: 14
    readonly property int labelH: 16

    width: cols * (cellW + gap) + gap
    height: Math.ceil(cases.length / cols) * (cellH + gap + labelH) + gap
    visible: true
    color: "#22252b"

    // Never drawn: it is asked how tall a toy is, and that is all.
    TuxShell { id: probe; visible: false; width: win.cellW; animate: false }

    Grid {
        id: grid
        anchors { fill: parent; margins: win.gap }
        columns: win.cols
        spacing: win.gap

        Repeater {
            id: repeater
            model: win.cases
            delegate: Column {
                id: cell
                required property var modelData
                spacing: 2

                // What the per-cell grab takes. NOT this Column: it carries the
                // case's NAME underneath, so grabbing it made every pair differ
                // by its label and the comparison in tux_screen.sh proved
                // nothing at all — it passed with the mess deleted.
                readonly property Item toy: toyItem

                TuxShell {
                    id: toyItem
                    width: win.cellW
                    height: win.cellH
                    pet: cell.modelData.pet
                    frame: cell.modelData.frame
                    showStatus: cell.modelData.status === true
                    isLight: cell.modelData.pale === true
                    // Frozen: a grab has to be of a KNOWN frame, or the sheet
                    // is of whenever the timer happened to be and two runs
                    // disagree about which foot the pet has forward.
                    animate: false
                    lcdBg: cell.modelData.pale === true
                        ? Qt.rgba(0.79, 0.84, 0.75, 1.0)
                        : Qt.rgba(0.09, 0.13, 0.12, 1.0)
                    lcdInk: cell.modelData.pale === true ? "#2b3a2f" : "#d6e6da"
                    accent: "#7fd4c1"
                    warn: "#f38ba8"
                    label: "#8a929c"
                    labelBright: "#c8e3ee"
                    fontFamily: "sans-serif"
                    soundGlyph: "♪"
                    muteGlyph: "×"
                }

                Text {
                    text: cell.modelData.name
                    color: "#8a929c"
                    font.pixelSize: 10
                    font.letterSpacing: 1.0
                }
            }
        }
    }

    function argAfter(name) {
        const a = Qt.application.arguments
        for (let i = 0; i < a.length - 1; i++)
            if (a[i] === name) return a[i + 1]
        return ""
    }

    // A file name per case, for the shell to compare. Spaces would be one
    // quoting bug away from a test that passes because it compared nothing.
    function slug(name) { return String(name).replace(/[^a-z0-9]+/gi, "-") }

    /*
     * Every cell to its own PNG, so the shell can assert that two pets which
     * must look different DO. That is the check the sheet cannot make: an
     * undefined sprite name is `undefined`, TuxPixels draws nothing for it, and
     * the render succeeds with one thing missing and NOT ONE WORD anywhere —
     * which is exactly how the first version of this test passed with the mess
     * deleted out of the filthy pet.
     *
     * The renders are deterministic (software renderer, animation off, a fixed
     * frame per case), so identical bytes really do mean identical pictures.
     */
    property int pending: 0

    function grab(item, path) {
        return item.grabToImage(function (r) {
            if (!r.saveToFile(path)) console.error("tux_screen: cannot write " + path)
            win.pending--
            if (win.pending === 0) Qt.exit(0)
        })
    }

    function grabCells(dir) {
        const rep = repeater
        win.pending = rep.count * 2
        for (let i = 0; i < rep.count; i++) {
            const toy = rep.itemAt(i).toy
            const base = dir + "/" + win.slug(win.cases[i].name)
            // Two grabs per case: the whole toy, and the LCD on its own. The
            // shell compares SCREENS for anything the pet does, because the
            // icon row lights up in sympathy and a whole-toy comparison passes
            // on that alone.
            if (!win.grab(toy, base + ".png") || !win.grab(toy.screenItem, base + "-lcd.png")) {
                console.error("tux_screen: cannot grab cell " + i)
                Qt.exit(1)
                return
            }
        }
    }

    // One shot, after the scene has had a frame to settle: a grab taken in
    // Component.onCompleted catches a window that has not laid out yet, and the
    // PNG comes back empty with no error anywhere.
    Timer {
        interval: 400
        running: true
        onTriggered: {
            const out = win.argAfter("--out")
            const cells = win.argAfter("--cells")
            if (out === "" || !out.endsWith(".png")) {
                console.error("tux_screen: need --out <file>.png")
                Qt.exit(2)
                return
            }
            grid.grabToImage(function (result) {
                if (!result.saveToFile(out)) {
                    console.error("tux_screen: cannot write " + out)
                    Qt.exit(1)
                    return
                }
                console.warn("tux_screen: wrote " + out)
                if (cells === "") Qt.exit(0)
                else win.grabCells(cells)
            })
        }
    }
}
