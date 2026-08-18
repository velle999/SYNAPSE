import QtQuick
import "tuxart.js" as TuxArt

/*
 * TuxScreen — the LCD. Everything the pet does, and nothing about the desktop.
 *
 * A separate file from Tuxagotchi.qml for one reason, and it is not tidiness:
 * THIS one can be rendered without a compositor. It imports QtQuick and its
 * sibling TuxPixels, takes its four colours and its font as properties rather
 * than reading Theme, and takes the pet as a property rather than reaching for
 * the TuxState singleton — so tests/tux_screen.qml can hand it a plain QtObject
 * with `stage: "sick"` and grab the result to a PNG with the `qml` tool. A
 * screen that could only be seen by running the whole desktop is a screen
 * nobody checks before shipping, and this widget is almost entirely a picture.
 *
 * The contract is small on purpose: `pet` needs the properties TuxState
 * publishes and none of its functions, except that a click on a grave is a
 * SIGNAL rather than a call, so the screen never changes anything.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
Rectangle {
    id: lcd

    // The pet: TuxState in the widget, a stub in the test rig.
    property var pet: null

    // The desktop, passed in rather than looked up. See the header.
    property color ink:     "#d6e6da"
    property color accent:  "#7fd4c1"
    property color warn:    "#f38ba8"
    property bool  isLight: false
    property string fontFamily: "sans-serif"

    // The animation clock, owned by whoever put this on screen — one timer for
    // the whole widget, so nothing here can drift out of step with the icons.
    property int frame: 0
    property bool showStatus: false

    readonly property bool blinkOn: frame % 2 === 0
    // Every seven seconds, for one frame. Cheaper than a second timer, and it
    // lands on the same grid as the walk.
    readonly property bool blinking: (frame % 14) === 0

    signal hatchRequested()

    radius: 6
    border.width: 1
    border.color: Qt.rgba(0, 0, 0, 0.35)
    clip: true

    // ── Which picture the pet is ─────────────────────────────
    readonly property bool small: pet && (pet.stage === "baby" || pet.stage === "child")

    // Growing up is the same two sprite sets at four sizes. A separate drawing
    // per stage would be four chances for one of them to be off by a pixel, and
    // the toy's own growth was a size change too.
    readonly property int petZoom: {
        if (!pet) return 5
        switch (pet.stage) {
        case "baby":  return 4
        case "child": return 5
        case "teen":  return 4
        default:      return 5
        }
    }

    readonly property var petMood: {
        if (!pet) return null
        if (pet.sick > 0)           return TuxArt.mood("sick")
        if (pet.asleep)             return TuxArt.mood("asleep")
        if (pet.stage === "senior") return TuxArt.mood("senior")
        return null
    }

    readonly property var petRows: {
        if (!pet) return TuxArt.egg
        if (pet.stage === "egg")
            // Cracks in the last half-minute, so hatching is something you can
            // catch rather than something that has already happened.
            return ((pet.stageAt.baby - pet.ageMin) * 60 < 30 && lcd.blinkOn)
                   ? TuxArt.eggCrack : TuxArt.egg
        if (pet.stage === "gone") return TuxArt.grave

        const set = lcd.small
            ? { idle: TuxArt.chickIdle, step: TuxArt.chickStep,
                blink: TuxArt.chickBlink, open: TuxArt.chickOpen,
                cheer: TuxArt.chickStep }
            : { idle: TuxArt.tuxIdle, step: TuxArt.tuxStep,
                blink: TuxArt.tuxBlink, open: TuxArt.tuxOpen,
                cheer: TuxArt.tuxCheer }

        if (pet.asleep)            return set.blink
        if (pet.doing === "eat")   return lcd.blinkOn ? set.open : set.idle
        if (pet.doing === "cheer") return set.cheer
        if (pet.doing === "sad" || pet.doing === "refuse") return set.blink
        if (pet.sick > 0)          return set.blink
        if (lcd.blinking)          return set.blink
        return lcd.blinkOn ? set.idle : set.step
    }

    // Facing. The guessing game is the only thing that turns him round.
    readonly property bool petFlip: pet && pet.playing && pet.gameFace > 0

    readonly property bool isEggOrGrave:
        pet && (pet.stage === "egg" || pet.stage === "gone")

    // ── The two meters ───────────────────────────────────────
    Row {
        id: hungerRow
        anchors { left: parent.left; top: parent.top; leftMargin: 8; topMargin: 7 }
        spacing: 2
        Repeater {
            model: 4
            delegate: TuxPixels {
                required property int index
                readonly property bool full: lcd.pet && index < lcd.pet.hungerHearts
                rows: full ? TuxArt.heart : TuxArt.heartEmpty
                zoom: 2
                tint: full ? null : ({ "k": lcd.ink })
            }
        }
    }
    Text {
        anchors { left: hungerRow.left; top: hungerRow.bottom; topMargin: 1 }
        text: "FED"
        color: lcd.ink
        opacity: 0.55
        font.family: lcd.fontFamily
        font.pixelSize: 8
        font.letterSpacing: 1.0
    }

    Row {
        id: happyRow
        anchors { right: parent.right; top: parent.top; rightMargin: 8; topMargin: 7 }
        spacing: 2
        Repeater {
            model: 4
            delegate: TuxPixels {
                required property int index
                readonly property bool full: lcd.pet && index < lcd.pet.happyHearts
                rows: full ? TuxArt.heart : TuxArt.heartEmpty
                zoom: 2
                tint: full ? null : ({ "k": lcd.ink })
            }
        }
    }
    Text {
        anchors { right: happyRow.right; top: happyRow.bottom; topMargin: 1 }
        text: "FUN"
        color: lcd.ink
        opacity: 0.55
        font.family: lcd.fontFamily
        font.pixelSize: 8
        font.letterSpacing: 1.0
    }

    // The call. Blinks in the middle of the top edge, where nothing else is,
    // and it is the same "!" the discipline button carries — because the two
    // are the same conversation.
    TuxPixels {
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 6 }
        rows: TuxArt.iconScold
        zoom: 2
        tint: ({ "k": lcd.accent })
        visible: lcd.pet && lcd.pet.attention && lcd.blinkOn
    }

    // ── The pet ──────────────────────────────────────────────
    TuxPixels {
        id: sprite
        rows: lcd.petRows
        zoom: lcd.isEggOrGrave ? 5 : lcd.petZoom
        tint: lcd.petMood
        flip: lcd.petFlip

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.horizontalCenterOffset:
            (lcd.pet && lcd.pet.doing === "refuse") ? (lcd.blinkOn ? 3 : -3) : 0

        // Stands on a floor 12px up from the bottom of the screen at any size,
        // so growing up does not make it hover.
        y: parent.height - 12 - implicitHeight
           - ((lcd.pet && lcd.pet.doing === "cheer" && lcd.blinkOn) ? 6 : 0)
           - ((lcd.pet && !lcd.isEggOrGrave && !lcd.pet.asleep && lcd.blinkOn) ? 1 : 0)

        Behavior on y { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

        // The egg rocks. One property animation, no second picture.
        transform: Rotation {
            origin.x: sprite.implicitWidth / 2
            origin.y: sprite.implicitHeight
            angle: (lcd.pet && lcd.pet.stage === "egg") ? (lcd.blinkOn ? 7 : -7) : 0
            Behavior on angle {
                NumberAnimation { duration: 420; easing.type: Easing.InOutSine }
            }
        }
    }

    // What it is eating, held in front of it for as long as the mouthful lasts.
    TuxPixels {
        rows: TuxArt.fish
        zoom: 2
        visible: lcd.pet && lcd.pet.doing === "eat"
        anchors {
            right: sprite.left; rightMargin: -4
            bottom: sprite.bottom; bottomMargin: 20
        }
    }

    // Asleep. Three z's, rising.
    Row {
        spacing: 2
        visible: lcd.pet && lcd.pet.asleep && lcd.pet.stage !== "gone"
        anchors {
            left: sprite.right; leftMargin: -8
            bottom: sprite.top; bottomMargin: -18
        }
        Repeater {
            model: 3
            delegate: Text {
                required property int index
                text: "z"
                color: lcd.ink
                font.family: lcd.fontFamily
                font.pixelSize: 8 + index * 3
                opacity: ((lcd.frame + index) % 3) === 0 ? 0.35 : 0.9
                y: -index * 4
            }
        }
    }

    // The mess. Along the floor on the left, where the pet is not.
    Row {
        spacing: 4
        anchors { left: parent.left; leftMargin: 8; bottom: parent.bottom; bottomMargin: 10 }
        Repeater {
            model: lcd.pet ? lcd.pet.poops : 0
            delegate: TuxPixels { rows: TuxArt.poop; zoom: 2 }
        }
    }

    // Illness: the pill's own icon, flashing over the pet, so the sick pet and
    // the button that fixes it carry the same picture.
    TuxPixels {
        rows: TuxArt.pill
        zoom: 2
        visible: lcd.pet && lcd.pet.sick > 0 && lcd.blinkOn && !lcd.pet.asleep
        anchors {
            horizontalCenter: sprite.horizontalCenter
            bottom: sprite.top; bottomMargin: -8
        }
    }

    // ── The game ─────────────────────────────────────────────
    /*
     * Best of three: he turns left or right and you say which. The round pips
     * are here; the arrows take over the widget's bottom icon row, because they
     * ARE the buttons for as long as the game is on, and a third row would make
     * the card taller for something that is there for ten seconds.
     */
    Row {
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 30 }
        spacing: 5
        visible: lcd.pet && lcd.pet.playing
        Repeater {
            model: 3
            delegate: Rectangle {
                required property int index
                width: 6; height: 6; radius: 3
                color: !lcd.pet ? "transparent"
                     : index < lcd.pet.gameWins ? lcd.accent
                     : index < (lcd.pet.gameRound - 1) ? lcd.warn
                     : "transparent"
                border.width: 1
                border.color: lcd.ink
                opacity: 0.9
            }
        }
    }

    // ── After it has gone ────────────────────────────────────
    Text {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 8 }
        visible: lcd.pet && lcd.pet.stage === "gone"
        text: "press for a new egg"
        color: lcd.ink
        opacity: 0.75
        font.family: lcd.fontFamily
        font.pixelSize: 9
        font.letterSpacing: 0.6
    }
    MouseArea {
        anchors.fill: parent
        // Only ever live over a grave. A screen that swallowed clicks the rest
        // of the time would be a screen the widget cannot be dragged from and
        // the desktop cannot be reached through.
        enabled: lcd.pet && lcd.pet.stage === "gone"
        cursorShape: Qt.PointingHandCursor
        onClicked: lcd.hatchRequested()
    }

    /*
     * The light switch is a real switch: off is a dark room and the pet is a
     * shape in it. Not black — a screen you cannot see at all reads as a broken
     * widget rather than a sleeping pet. Above everything the pet does and
     * below the status card, because the numbers are the widget talking rather
     * than the pet.
     */
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: Qt.rgba(0, 0, 0, 0.62)
        visible: lcd.pet && !lcd.pet.light
    }

    // ── The status card ──────────────────────────────────────
    /*
     * The numbers the animation cannot say: how old it is, what it weighs, how
     * well it has been brought up. Over the screen rather than beside it — the
     * shell has no room, and a pet hidden behind its own statistics was a joke
     * the real toy made too.
     */
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        // The whole screen, not a card inset into it: at five pixels of margin
        // the meters peeked out over the top row and the numbers read as a
        // dialog somebody had dropped on the pet.
        visible: lcd.showStatus && lcd.pet
        // OPAQUE. This is a mode the screen is in, not an overlay on top of
        // one: at 0.78 the two heart meters read straight through it and the
        // top row of numbers had four hearts sitting in it, and 0.94 still
        // left a ghost of them.
        color: lcd.isLight ? "#e8eee2" : "#0b100f"
        border.width: 1
        border.color: lcd.ink

        Column {
            anchors { fill: parent; margins: 8 }
            spacing: 1

            Repeater {
                model: !lcd.pet ? [] : [
                    { k: "STAGE",      v: lcd.pet.stage },
                    { k: "AGE",        v: lcd.pet.ageDays + "d " + lcd.pet.ageHours + "h" },
                    { k: "WEIGHT",     v: lcd.pet.weight + " oz" },
                    { k: "DISCIPLINE", v: lcd.pet.discipline + "%" },
                    { k: "MESS",       v: lcd.pet.poops + " / 4" },
                    { k: "HEALTH",     v: lcd.pet.sick === 0 ? "well"
                                        : lcd.pet.sick === 1 ? "ill" : "very ill" },
                    { k: "MISTAKES",   v: String(lcd.pet.careMisses) }
                ]
                delegate: Item {
                    id: statRow
                    required property var modelData
                    width: parent.width
                    // Seven rows have to fit a 130px screen with room for the
                    // border: at 15 the last one (MISTAKES, the one that says
                    // how you are doing) was cut off by the bottom edge.
                    height: 14

                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        text: statRow.modelData.k
                        color: lcd.ink
                        opacity: 0.6
                        font.family: lcd.fontFamily
                        font.pixelSize: 9
                        font.letterSpacing: 1.0
                    }
                    Text {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: statRow.modelData.v
                        color: lcd.ink
                        font.family: lcd.fontFamily
                        font.pixelSize: 9
                    }
                }
            }
        }
    }
}
