import QtQuick
import Quickshell
import Quickshell.Wayland
import ".."

/*
 * The SynapseOS welcome guide.
 *
 * Replaces the compositor-drawn welcome menu (render.c's synui_render_welcome,
 * synui 0.1.0-1…-495), which was one 513px column of nineteen labels and their
 * chords. Everything wrong with it was structural rather than cosmetic:
 *
 *   · it was a LIST, not a guide. Nineteen doors in one scroll, alphabetically
 *     unrelated, with no room to say what any of them was for — so "Neural
 *     Overlay" and "Cat Mode" arrived side by side with no explanation and the
 *     only way to find out was to press them.
 *   · its key column was a hardcoded rgba(0.45, 0.45, 0.55). No theme moved it,
 *     and on the dark panel this desktop actually draws it measured under 3:1 —
 *     present, and unreadable. That is the "secondary colour is too dark" this
 *     rewrite starts from; the chip reads Theme.fgDim now, which is measured
 *     against the surface it lands on.
 *   · it was cairo in the compositor, so every change to it was a change to the
 *     thing drawing every other window.
 *
 * ⚠ IT IS A SEPARATE QUICKSHELL PROCESS, NOT PART OF THE BAR. `synui-welcome`
 * starts `quickshell -p …/welcome.qml` on this same QML tree. Putting the guide
 * in the bar would have been less code and it would have been wrong: the bar is
 * swappable (`bar_shell = synapse|antiquity`), and a guide inside it would
 * simply not exist for anyone running the other shell — a regression against
 * the compositor-drawn menu, which every configuration had. As its own entry
 * point it gets Theme, the fonts and the glass namespace for free while owing
 * the bar nothing.
 */
PanelWindow {
    id: root

    required property var modelData
    screen: modelData

    readonly property string outName: modelData.name

    // One window per screen, one guide. Empty output means the fallback probe
    // has not answered yet; treat that as "here" so the guide is never invisible
    // on every monitor at once.
    visible: GuideState.open
             && (GuideState.output === root.outName || GuideState.output === "")

    // All four edges: the surface is the whole screen and the guide is a card
    // in the middle of it. That is what makes a click on the desktop dismiss —
    // there is no Wayland protocol that tells a layer surface "the pointer went
    // down somewhere else", so the only way to hear that click is to be the
    // surface it lands on.
    anchors { top: true; left: true; right: true; bottom: true }

    // NEVER reserve space, and Ignore rather than a zone of 0 — a zone of 0
    // still respects the bar's, which would push a full-screen surface down by
    // the height of the strip and make `centerIn` off-centre by half of it.
    exclusionMode: ExclusionMode.Ignore

    // Ask synui to frost what is behind the card on a glass theme. Safe on a
    // full-screen surface: the blur is masked to where the client actually
    // paints, so the card frosts and the transparent catcher stays clear.
    WlrLayershell.namespace: "synui-glass"

    // The guide is arrow-driven, so it needs the keyboard. `focusable: true` is
    // the portable spelling; `WlrLayershell.keyboardFocus` alone did not apply
    // it.
    //
    // ⚠ IT IS ON-DEMAND, NOT EXCLUSIVE. WlrKeyboardFocus is
    // None=0/Exclusive=1/OnDemand=2 and this reads back 2. The guide gets the
    // keyboard anyway because layer.c:layer_surface_map() grants it to any
    // interactivity that is not NONE — at MAP and nowhere else. Which also means
    // it LOSES the keyboard the moment a toplevel maps and focus_view() notifies
    // it; see the ToplevelManager watch above, which is the other half of that
    // fact rather than a workaround for it.
    focusable: true

    color: "transparent"

    onVisibleChanged: if (visible) keys.forceActiveFocus()
    Component.onCompleted: if (visible) keys.forceActiveFocus()

    /*
     * ── Something else opened: get out of the way ────────────────────────────
     *
     * The old menu hid itself when the first window mapped (synui_main.c), and
     * the rule is kept because this surface is the WHOLE SCREEN. A guide left up
     * over the window that just opened covers it completely — and goes deaf
     * while it does, because synui hands the keyboard to the new toplevel
     * (focus_view() notifies it unconditionally) and grants it to a layer
     * surface only at map. So it is a full-screen panel you cannot type into,
     * sitting on top of the application you just asked for.
     *
     * ── …and why there is a guard in front of it ─────────────────────────────
     *
     * ⚠ THE WINDOWS THAT WERE ALREADY OPEN ARRIVE AS INSERTIONS, TOO. This is
     * not a guess and it is not what it looks like: `ToplevelManager.toplevels`
     * is EMPTY at Component.onCompleted, and the existing toplevels are then
     * inserted one event-loop turn later. Measured on a headless rig with one
     * window open three seconds ahead of the guide:
     *
     *     PROBE completed t=0 count=0
     *     PROBE active    t=1 -> syntty
     *     PROBE insert    t=1 appId=syntty
     *
     * So an unguarded watch closes the guide instantly on any desktop that is
     * not empty — which is most of them, and is exactly what happened when this
     * guard was briefly removed.
     *
     * The interval is SLACK over that 1ms, not a measurement of anything. It is
     * short on purpose: every millisecond of it is also a window in which a
     * genuinely new window is ignored, and the only thing it has to outlast is
     * one turn of the event loop.
     *
     * ⛔ IT IS NOT ABOUT AUTOSTART. 497 held this for 1.5s "so the login burst
     * passes", on the belief that `autostart` defaults to a terminal. It does
     * not: config.c's compiled-in `syntty` is the fallback for finding NO config
     * file, and opening any synuirc zeroes the list before parsing ("Config file
     * found — reset autostart"). Every install ships /etc/synui/synuirc, so on a
     * real desktop nothing autostarts unless somebody asked for it. The
     * fallback applies only in a hermetic test rig — the case
     * tests/bar_radius.sh documents.
     */
    Timer {
        id: arm
        interval: 400
        running: root.visible
    }

    Connections {
        target: ToplevelManager.toplevels
        function onObjectInsertedPost() { if (!arm.running) GuideState.close() }
    }

    // Everywhere that is not the card. Press rather than click, and every
    // button, so a right-click on the desktop dismisses exactly as a left one
    // does and a press-drag-release never leaves the guide hanging around.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: GuideState.close()
    }

    // ── The card ─────────────────────────────────────────
    Rectangle {
        id: card

        anchors.centerIn: parent

        // Sized to the content, capped to what the monitor can show. A page of
        // six rows and a page of three must not leave the nav rail jumping
        // around, so the height is the tallest page's, resolved once.
        width:  Math.min(root.screen.width  - 64, 880)
        height: Math.min(root.screen.height - 64, 620)

        readonly property var backdrop:
            Theme.backdropFor(root.screen, card.x, card.y, card.width, card.height)

        color: Theme.popupBgOn(card.backdrop)
        border.color: Theme.magenta
        border.width: 1
        radius: Theme.panelRadius

        // Swallows presses that land on the card but not on a row: a Rectangle
        // accepts no buttons, so without this a click on the padding falls
        // through to the dismiss catcher behind and closes the guide.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        // ── The rail ─────────────────────────────────────
        Item {
            id: rail
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: 216

            Column {
                anchors { left: parent.left; right: parent.right; top: parent.top
                          leftMargin: 22; rightMargin: 16; topMargin: 24 }
                spacing: 14

                Row {
                    spacing: 12

                    /* The thin mark, not logo-bold.svg: the bold cut exists for
                     * the bar's 20px strip, where the thin one's strokes close
                     * up. At 40px the thin mark is the brand as drawn. The ink
                     * cut is the same mark for a pale surface — a purple logo on
                     * a light theme is a smudge. */
                    Image {
                        width: 40; height: 40
                        anchors.verticalCenter: parent.verticalCenter
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: 80
                        sourceSize.height: 80
                        source: Theme.isLight
                                ? "file:///usr/share/synui/logo-ink.svg"
                                : "file:///usr/share/synui/logo.svg"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "SYNAPSEOS"
                        color: Theme.cyan
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize + 6
                        font.letterSpacing: 2
                    }
                }

                Rectangle {
                    width: rail.width - 38; height: 1
                    color: Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.12)
                }

                // The page list IS the progress indicator. Dots would have been
                // smaller and would have said nothing; a guide whose contents
                // page is visible the whole way through is one you can leave in
                // the middle of and come back to.
                Column {
                    spacing: 2
                    Repeater {
                        model: GuideState.pages
                        delegate: Rectangle {
                            required property var modelData
                            required property int index

                            width: rail.width - 38
                            height: 30
                            radius: Math.min(6, Theme.panelRadius)
                            color: index === GuideState.page
                                   ? Qt.rgba(Theme.cyan.r, Theme.cyan.g,
                                             Theme.cyan.b, 0.13)
                                   : nav.hovered
                                   ? Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.05)
                                   : "transparent"

                            HoverHandler { id: nav }
                            TapHandler { onTapped: GuideState.goTo(index) }

                            Text {
                                anchors { left: parent.left; leftMargin: 10
                                          verticalCenter: parent.verticalCenter }
                                text: (index + 1) + ".  " + modelData.nav
                                elide: Text.ElideRight
                                width: parent.width - 20
                                color: index === GuideState.page ? Theme.fg
                                                                 : Theme.fgDim
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                }
            }

            Text {
                anchors { left: parent.left; bottom: parent.bottom
                          leftMargin: 22; bottomMargin: 18 }
                text: GuideState.version
                color: Theme.fgDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize - 1
            }
        }

        Rectangle {
            anchors { left: rail.right; top: parent.top; bottom: parent.bottom
                      topMargin: 14; bottomMargin: 14 }
            width: 1
            color: Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.12)
        }

        // ── The page ─────────────────────────────────────
        Item {
            id: page
            anchors { left: rail.right; right: parent.right; top: parent.top
                      bottom: footer.top
                      leftMargin: 26; rightMargin: 22; topMargin: 26 }

            Column {
                id: head
                anchors { left: parent.left; right: parent.right; top: parent.top }
                spacing: 8

                Text {
                    width: parent.width
                    text: GuideState.current ? GuideState.current.title : ""
                    color: Theme.fg
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize + 10
                }
                Text {
                    width: parent.width
                    text: GuideState.current ? GuideState.current.blurb : ""
                    color: Theme.fgDim
                    wrapMode: Text.WordWrap
                    lineHeight: 1.25
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }

            // The rows scroll only when a page needs more room than the card
            // has. A ListView rather than a Column so a long page on a small
            // monitor is reachable rather than clipped — the failure the start
            // menu's fixed height used to have, where Shut Down sat off the
            // bottom edge and could only be arrowed into blind.
            ListView {
                id: list
                anchors { left: parent.left; right: parent.right
                          top: head.bottom; bottom: parent.bottom
                          topMargin: 18; bottomMargin: 6 }
                clip: true
                interactive: contentHeight > height
                spacing: 2
                currentIndex: GuideState.selected

                model: GuideState.current ? GuideState.current.rows : []

                delegate: GuideRow {
                    required property var modelData
                    required property int index

                    width: ListView.view.width
                    row: modelData
                    selected: index === GuideState.selected
                    chord: modelData.live !== undefined
                           ? GuideState.aiBackend
                           : GuideState.keyFor(modelData)
                    onActivated: GuideState.activate(index)
                }
            }
        }

        // ── The footer ───────────────────────────────────
        Item {
            id: footer
            anchors { left: rail.right; right: parent.right; bottom: parent.bottom }
            height: 62

            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top
                          leftMargin: 26; rightMargin: 22 }
                height: 1
                color: Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.12)
            }

            /*
             * "Don't show again", phrased as the opt-OUT so the box you tick is
             * the thing you came here to do. The config field it writes is the
             * opposite sense (welcome_at_startup) and this is the only place
             * that inverts it — exactly as the old menu's corner checkbox was.
             *
             * It is on EVERY page rather than only the last one: it is the one
             * preference in the guide, and hiding it behind five pages means
             * anyone who dismisses the guide early never finds the switch that
             * stops it coming back.
             *
             * The box is a Rectangle and the tick is two lines, not a "☑". A
             * typed checkbox glyph is at the mercy of whatever family the font
             * picker last applied, and a checkbox that draws as a tofu box is
             * worse than no checkbox.
             */
            Item {
                id: check
                anchors { left: parent.left; leftMargin: 26
                          verticalCenter: parent.verticalCenter }
                width: checkBox.width + checkLabel.width + 10
                height: 22

                readonly property bool selected:
                    GuideState.selected === GuideState.rowCount

                HoverHandler { id: checkHover }
                TapHandler { onTapped: GuideState.toggleStartup() }

                Rectangle {
                    id: checkBox
                    width: 14; height: 14
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 2
                    color: "transparent"
                    border.width: 1
                    border.color: check.selected || checkHover.hovered
                                  ? Theme.cyan : Theme.fgDim

                    Canvas {
                        anchors.fill: parent
                        visible: !GuideState.atStartup
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = Theme.cyan
                            ctx.lineWidth = 2
                            ctx.lineCap = "round"
                            ctx.beginPath()
                            ctx.moveTo(3, 7); ctx.lineTo(6, 10); ctx.lineTo(11, 4)
                            ctx.stroke()
                        }
                        // The tick is painted, so it has to be repainted when
                        // the palette moves under it — a Canvas does not
                        // re-run onPaint because a colour binding changed.
                        Connections {
                            target: Theme
                            function onCyanChanged() { parent.requestPaint() }
                        }
                    }
                }
                Text {
                    id: checkLabel
                    anchors { left: checkBox.right; leftMargin: 10
                              verticalCenter: parent.verticalCenter }
                    text: "Don't show this guide at startup"
                    color: check.selected ? Theme.fg : Theme.fgDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }

            Row {
                anchors { right: parent.right; rightMargin: 22
                          verticalCenter: parent.verticalCenter }
                spacing: 8

                // Back is drawn disabled rather than hidden on page one, so the
                // pair does not shuffle sideways as you move through the guide.
                Rectangle {
                    id: backBtn
                    width: 96; height: 30
                    radius: Math.min(6, Theme.panelRadius)
                    color: GuideState.onFirst ? "transparent"
                         : backHover.hovered
                           ? Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.10)
                           : Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.05)
                    border.width: 1
                    border.color: Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b,
                                          GuideState.onFirst ? 0.07 : 0.16)

                    HoverHandler { id: backHover; enabled: !GuideState.onFirst }
                    TapHandler {
                        enabled: !GuideState.onFirst
                        onTapped: GuideState.back()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "Back"
                        color: GuideState.onFirst
                               ? Qt.rgba(Theme.fgDim.r, Theme.fgDim.g,
                                         Theme.fgDim.b, 0.5)
                               : Theme.fg
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }

                // …and the last page's Next says Done, because it does something
                // different. A button is its own label: "Next" on a button that
                // closes the guide is the button lying about where it goes.
                Rectangle {
                    width: 96; height: 30
                    radius: Math.min(6, Theme.panelRadius)
                    color: nextHover.hovered
                           ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.28)
                           : Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.18)
                    border.width: 1
                    border.color: Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.5)

                    HoverHandler { id: nextHover }
                    TapHandler { onTapped: GuideState.next() }

                    Text {
                        anchors.centerIn: parent
                        text: GuideState.onLast ? "Done" : "Next"
                        color: Theme.fg
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }

        // ── The way out that is not a keystroke ──────────
        //
        // Kept from the old menu, and for the reason it was added there: Escape
        // closes, and "press Escape and hope you knew that" is not a way out for
        // somebody meeting the desktop for the first time — which is precisely
        // who is looking at this window.
        Item {
            width: 22; height: 22
            anchors { right: parent.right; top: parent.top
                      rightMargin: 10; topMargin: 10 }

            HoverHandler { id: closeHover }
            TapHandler { onTapped: GuideState.close() }

            Rectangle {
                anchors.fill: parent
                radius: Math.min(4, Theme.panelRadius)
                color: closeHover.hovered
                       ? Qt.rgba(Theme.fg.r, Theme.fg.g, Theme.fg.b, 0.12)
                       : "transparent"
            }
            Canvas {
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = Theme.fg
                    ctx.lineWidth = 1.6
                    ctx.lineCap = "round"
                    ctx.beginPath()
                    ctx.moveTo(7, 7);  ctx.lineTo(15, 15)
                    ctx.moveTo(15, 7); ctx.lineTo(7, 15)
                    ctx.stroke()
                }
                Connections {
                    target: Theme
                    function onFgChanged() { parent.requestPaint() }
                }
            }
        }
    }

    // ── Keyboard ─────────────────────────────────────────
    /*
     * Up/Down walk the rows, Left/Right turn the page, Enter opens the selected
     * row, Space ticks the checkbox, Escape leaves.
     *
     * The checkbox is one past the last row, which is where the old menu put it
     * too (WELCOME_CHECK == synui_welcome_menu_len). It stopped being a row when
     * it became a corner control and stayed reachable by keyboard, and moving it
     * out of the arrow ring now would take the one preference in this window
     * away from anyone driving it without a mouse.
     */
    Item {
        id: keys
        anchors.fill: parent
        focus: true

        Keys.onPressed: function (event) {
            switch (event.key) {
            case Qt.Key_Escape:
                GuideState.close(); event.accepted = true; break
            case Qt.Key_Down:
            case Qt.Key_J:
                GuideState.move(1); event.accepted = true; break
            case Qt.Key_Up:
            case Qt.Key_K:
                GuideState.move(-1); event.accepted = true; break
            case Qt.Key_Right:
            case Qt.Key_L:
            case Qt.Key_PageDown:
                GuideState.next(); event.accepted = true; break
            case Qt.Key_Left:
            case Qt.Key_H:
            case Qt.Key_PageUp:
                GuideState.back(); event.accepted = true; break
            case Qt.Key_Home:
                GuideState.goTo(0); event.accepted = true; break
            case Qt.Key_End:
                GuideState.goTo(GuideState.pages.length - 1)
                event.accepted = true; break
            case Qt.Key_Return:
            case Qt.Key_Enter:
                GuideState.activate(GuideState.selected)
                event.accepted = true; break
            case Qt.Key_Space:
                // Space is the checkbox's key wherever the selection is: it is
                // the only toggle in the window, and a page with no selectable
                // row would otherwise have nothing Space could mean.
                GuideState.toggleStartup(); event.accepted = true; break
            }
        }
    }
}
