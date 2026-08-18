import QtQuick
import "tuxart.js" as TuxArt

/*
 * TuxShell — the toy: a screen with eight printed icons round it.
 *
 * The device, without the desktop. Same rule TuxScreen keeps and for the same
 * reason: QtQuick and the sprite table, nothing else — no Theme, no Quickshell,
 * no singleton — so tests/tux_screen.qml can render the whole thing, buttons
 * and all, with the `qml` tool and no compositor. Everything it needs from the
 * desktop arrives as a property: five colours, two font families, and the pet.
 * Tuxagotchi.qml is what fills those in, and it is thirty lines because of it.
 *
 *
 * THE ICONS ARE ON THE SHELL, THE ANIMATION IS ON THE LCD
 *
 * That line is the whole design and it is the toy's own: printed icons round a
 * screen. Keeping it means the pet never has to share its 210 pixels with a
 * button, and it is why the icon row swaps for the game's arrows rather than
 * the arrows appearing over the pet.
 *
 * Clicking an icon does the thing directly. The original's three-button
 * select-and-confirm shuffle exists because a 1996 keychain had three buttons,
 * and reproducing it with a mouse in hand would be a costume rather than a
 * feature.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
Item {
    id: shell

    // TuxState in the widget, a stub in the test rig. Everything below reads
    // its properties and calls its functions; nothing here keeps a copy.
    property var pet: null

    // The desktop, passed in. See the header.
    property color lcdBg:  "#17211e"
    property color lcdInk: "#d6e6da"
    property color accent: "#7fd4c1"
    property color warn:   "#f38ba8"
    property color label:  "#8a929c"
    property color labelBright: "#c8e3ee"
    property bool  isLight: false
    property string fontFamily: "sans-serif"
    property string iconFamily: "sans-serif"
    // The speaker is the one glyph here that is not a sprite, because it is the
    // bar's own mute icon and should look like it. A rig with no Nerd Font
    // shows a box; that is a missing font, not a broken widget.
    property string muteGlyph: ""
    property string soundGlyph: ""

    // Off in the test rig, so a grab is of a known frame rather than of
    // whenever the timer happened to be.
    property bool animate: true

    readonly property int screenH: 130
    readonly property int iconRowH: 28
    readonly property int footerH: 14

    implicitHeight: iconRowH + 6 + screenH + 6 + iconRowH + 4 + footerH

    // The LCD on its own. Exposed so tests/tux_screen.qml can grab the SCREEN
    // rather than the whole toy: comparing whole toys proved nothing, because
    // the icon over whatever the pet needs lights up too, and a pair that was
    // meant to isolate "is the mess drawn" differed by the flush button.
    readonly property Item screenItem: screen

    // Whether the status card is up. State about this WINDOW rather than about
    // the pet, which is why it lives here and not in TuxState: two monitors
    // disagreeing about whether a panel is open is correct.
    property bool showStatus: false

    // ── The animation clock ──────────────────────────────────
    /*
     * ONE timer for the whole toy. Every moving thing is a function of `frame`,
     * so nothing can drift out of step with anything else and there is exactly
     * one thing to slow down when the pet is asleep.
     *
     * Two frames a second. Pizza.qml is deliberately still at rest because a
     * widget that animates recomposites its output for as long as it is on;
     * this one cannot be still — a pet that never moves is a picture of a pet —
     * so it spends the least it can: the rate the real toy ran at, thirty times
     * less than the visualiser, over a 210×130 rectangle rather than a screen.
     * Asleep it drops to one frame every two seconds.
     */
    property int frame: 0
    // The only thing THIS file does with the clock is flash the icon over
    // whatever the pet is calling for. The walk, the blink and the rocking egg
    // are TuxScreen's, off the same number.
    readonly property bool blinkOn: frame % 2 === 0

    Timer {
        interval: (shell.pet && shell.pet.asleep) ? 2000 : 500
        running: shell.animate
        repeat: true
        onTriggered: shell.frame++
    }

    // A printed icon on the shell: a sprite, a hit area, and a blink for the
    // one that would fix whatever the pet is calling about.
    component ShellIcon: Item {
        id: btn

        property var rows: []
        property bool hint: false        // blinks: this is the one it wants
        property bool dimmed: false
        property bool flip: false
        signal pressed()

        width: shell.width / 4
        height: shell.iconRowH

        TuxPixels {
            id: glyph
            anchors.centerIn: parent
            rows: btn.rows
            zoom: 2
            flip: btn.flip
            opacity: btn.dimmed ? 0.45 : (mouse.containsMouse ? 1.0 : 0.85)
            scale: mouse.pressed ? 0.86 : 1.0
            Behavior on opacity { NumberAnimation { duration: 120 } }
            Behavior on scale   { NumberAnimation { duration: 120 } }
        }

        /*
         * The blink is the toy's own alert: the icon over the thing it wants
         * flashes, so "what does it need" is answered on the shell instead of
         * in a status screen somebody has to think to open.
         */
        Rectangle {
            anchors.centerIn: glyph
            width: glyph.implicitWidth + 12
            height: glyph.implicitHeight + 10
            radius: 4
            color: "transparent"
            border.width: 1
            border.color: shell.accent
            visible: btn.hint && shell.blinkOn
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.pressed()
        }
    }

    Column {
        anchors.fill: parent
        spacing: 6

        // Top row: the two foods, the light, and the game.
        Row {
            width: parent.width
            height: shell.iconRowH

            ShellIcon {
                rows: TuxArt.fish
                hint: shell.pet && shell.pet.need === "hungry"
                dimmed: !shell.pet || !shell.pet.hatched || shell.pet.asleep
                onPressed: shell.pet.feed("meal")
            }
            ShellIcon {
                rows: TuxArt.biscuit
                dimmed: !shell.pet || !shell.pet.hatched || shell.pet.asleep
                onPressed: shell.pet.feed("snack")
            }
            ShellIcon {
                rows: TuxArt.iconLight
                dimmed: !shell.pet || !shell.pet.light
                onPressed: shell.pet.toggleLight()
            }
            ShellIcon {
                rows: TuxArt.iconPlay
                hint: shell.pet && shell.pet.need === "sad"
                dimmed: !shell.pet || !shell.pet.hatched || shell.pet.asleep
                         || shell.pet.sick > 0
                onPressed: shell.pet.playing ? shell.pet.stopGame() : shell.pet.startGame()
            }
        }

        TuxScreen {
            id: screen
            width: parent.width
            height: shell.screenH

            pet: shell.pet
            frame: shell.frame
            showStatus: shell.showStatus

            color: shell.lcdBg
            ink: shell.lcdInk
            accent: shell.accent
            warn: shell.warn
            isLight: shell.isLight
            fontFamily: shell.fontFamily

            onHatchRequested: shell.pet.newEgg()
        }

        // Bottom row: medicine, the flush, the numbers, and telling it off —
        // replaced by the game's arrows while a game is on.
        Row {
            width: parent.width
            height: shell.iconRowH
            visible: !(shell.pet && shell.pet.playing)

            ShellIcon {
                rows: TuxArt.pill
                hint: shell.pet && shell.pet.need === "sick"
                dimmed: !shell.pet || shell.pet.sick === 0
                onPressed: shell.pet.medicine()
            }
            ShellIcon {
                rows: TuxArt.iconClean
                hint: shell.pet && shell.pet.poops >= 2
                dimmed: !shell.pet || shell.pet.poops === 0
                onPressed: shell.pet.clean()
            }
            ShellIcon {
                rows: TuxArt.iconStatus
                onPressed: {
                    shell.showStatus = !shell.showStatus
                    shell.pet.beep("click")
                }
            }
            ShellIcon {
                rows: TuxArt.iconScold
                hint: shell.pet && shell.pet.need === "spoilt"
                dimmed: !shell.pet || !shell.pet.hatched || shell.pet.asleep
                onPressed: shell.pet.scold()
            }
        }

        // One arrow sprite, mirrored for the other way. Two drawings would be
        // two chances to get one of them slightly wrong.
        Row {
            width: parent.width
            height: shell.iconRowH
            visible: shell.pet && shell.pet.playing

            ShellIcon {
                width: shell.width / 2
                rows: TuxArt.iconArrow
                dimmed: shell.pet && shell.pet.gameFace !== 0
                onPressed: shell.pet.guess(-1)
            }
            ShellIcon {
                width: shell.width / 2
                rows: TuxArt.iconArrow
                flip: true
                dimmed: shell.pet && shell.pet.gameFace !== 0
                onPressed: shell.pet.guess(1)
            }
        }

        // ── The label under the toy ──────────────────
        Item {
            width: parent.width
            height: shell.footerH

            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: !shell.pet ? ""
                      : shell.pet.stage === "gone"
                        ? "lived " + shell.pet.ageDays + "d " + shell.pet.ageHours + "h"
                      : shell.pet.stage === "egg"
                        ? "an egg"
                        : shell.pet.ageDays + "d " + shell.pet.ageHours + "h  ·  "
                          + shell.pet.weight + "oz"
                color: shell.label
                font.family: shell.fontFamily
                font.pixelSize: 9
                font.letterSpacing: 1.0
            }

            /*
             * The speaker. The beeps are the point of the thing, so this is the
             * one control here that is about the widget rather than about the
             * pet — and it is the answer to a toy that chirps while you are on
             * a call. Persisted with everything else, because a mute that came
             * back on at every login would be a mute nobody trusted.
             */
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: (shell.pet && shell.pet.muted) ? shell.muteGlyph : shell.soundGlyph
                color: (shell.pet && shell.pet.muted) ? shell.label : shell.labelBright
                opacity: muteArea.containsMouse ? 1.0 : 0.75
                font.family: shell.iconFamily
                font.pixelSize: 11

                MouseArea {
                    id: muteArea
                    anchors { fill: parent; margins: -6 }
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: shell.pet.toggleMute()
                }
            }
        }
    }
}
