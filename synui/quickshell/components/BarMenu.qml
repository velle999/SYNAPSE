import QtQuick
import Quickshell
import ".."

/*
 * The bar's right-click menu — per-monitor feature toggles.
 *
 * DISMISSAL IS A POPUP GRAB. `grabFocus` makes this an xdg_popup with a real
 * grab, which is the mechanism every menu on every desktop uses and the one the
 * tray menus here (QsMenuAnchor) have always had: the COMPOSITOR notices a click
 * outside and dismisses, so there is no screen-covering input region to get
 * wrong, and the click that dismissed it does not also land on whatever was
 * underneath. It brings keyboard focus with it, which is how Escape can close
 * the menu — and it is given back the moment the menu goes away, so the "don't
 * steal keys from the focused window" objection this file used to carry does
 * not apply: the menu only holds them while it is up and you opened it.
 *
 * The pointer-leave timer is kept as a BACKSTOP, not as the mechanism. It is
 * what closes the menu if the grab is ever refused, which would otherwise leave
 * a menu with no way out but the Done row — the complaint that got this fixed.
 * Toggles still leave the menu open on purpose: fitting a narrow monitor
 * usually means turning off two or three things in one visit.
 *
 * ⚠ IT USED TO BE 1200ms, WHICH MADE IT THE MECHANISM ANYWAY. A grab that is
 * actually held does not care where the pointer rests — only a click outside
 * dismisses it — but the timer does not know whether the grab succeeded, so it
 * closed the menu itself the moment a hand came off the mouse to read it: open,
 * glance away, gone in a bit over a second. Reported as "closes itself before I
 * can read it" / "closes after about ten seconds" (the second being how long
 * velle usually waited before checking back, not a real interval — confirmed
 * live: two open/close cycles in well under 18 seconds, and nothing in the
 * compositor log at the moment of either close). Long enough now that no
 * ordinary pause trips it, short enough to still recover if the grab really is
 * ever refused.
 */
PopupWindow {
    id: menu

    // Which monitor this menu is editing. The bar passes its own screen; every
    // toggle here writes only that output's settings.
    property string output: ""
    property int    anchorX: 0

    // The plugin section below, unlike everything above it, is NOT filtered
    // by output — see that section's own note for why. Filtered to
    // `unsupported === ""` here rather than in the delegate: a plugin the bar
    // cannot host has no switch to offer, and the row's own index (used to
    // dim the first ▲ and the last ▾) has to be an index into the list that
    // is actually on screen, not into Plugins.all before this filter runs.
    readonly property var hostablePlugins:
        Plugins.all.filter(p => p.unsupported === "")

    // The window this menu hangs off, passed in by the bar.
    //
    // It CANNOT be discovered from here. A PopupWindow is not an Item, so it has
    // no `parent` — inside a PanelWindow `menu.parent` is *undefined*, not a
    // window — and the `QsWindow` attached property only resolves on an Item
    // (which is why BarModule and Tray can say `root.QsWindow.window` and this
    // cannot). The defensive `parent ? parent.QsWindow.window : null` this used
    // to read therefore took the null branch every single time, leaving the
    // popup with no anchor window, and a PopupWindow with no anchor window never
    // maps: right-clicking the bar did nothing at all, silently.
    //
    // `required` so a caller that forgets fails loudly at load instead of
    // quietly reintroducing a dead menu.
    required property var barWindow

    implicitWidth: 232
    implicitHeight: col.implicitHeight + 16
    color: "transparent"
    visible: false

    // See the header: this is what makes a click anywhere else close the menu.
    grabFocus: true

    // The compositor broke the grab (a click outside, a Super+key, a window
    // taking focus). The window is already down; this is what puts the QML
    // property back in step with it, or the next openAt() would set visible to
    // a value it already held and map nothing.
    onClosed: menu.visible = false

    function openAt(x) {
        menu.anchorX = x
        menu.visible = true
        closeTimer.stop()
    }

    anchor {
        window: menu.barWindow
        rect.x: Math.max(4, menu.anchorX - menu.implicitWidth / 2)
        // Below a top bar, above a bottom one. BarConfig owns the arithmetic
        // so the four popup sites cannot drift apart.
        rect.y: BarConfig.popupY(menu.implicitHeight)
    }

    // ⚠ The FIRING is guarded on where the pointer is NOW, not only on the
    // restarts. A restart is a guess made up to 8 seconds ago; one stale guess
    // is all it takes to shut the menu under a hand resting on it.
    Timer {
        id: closeTimer
        interval: 8000
        onTriggered: if (!menuHover.hovered) menu.visible = false
    }

    /* What the wallpaper is doing under this menu. The anchor rect is relative
     * to the BAR window, which layer-shell has pinned to a screen edge spanning
     * its full width — so the rect's own coordinates are the screen's, and no
     * translation is needed. The screen comes off the bar window for the same
     * reason the anchor does: a PopupWindow is not an Item and cannot find
     * either for itself (see barWindow above). */
    readonly property var backdrop: menu.barWindow
        ? Theme.backdropFor(menu.barWindow.screen,
                            menu.anchor.rect.x, menu.anchor.rect.y,
                            menu.implicitWidth, menu.implicitHeight)
        : null

    Rectangle {
        anchors.fill: parent
        radius: Theme.panelRadius
        color: Theme.popupBgOn(menu.backdrop)
        border.color: Theme.magenta
        border.width: 1

        // Escape closes, now that the grab brings keyboard focus with it. Focus
        // is taken when the menu maps rather than declared once: a hidden window
        // has no focus to hold, so an unconditional `focus: true` would be
        // claimed at load and never re-claimed on the second open.
        focus: true
        Keys.onEscapePressed: menu.visible = false
        onVisibleChanged: if (visible) forceActiveFocus()

        // ⚠ A HoverHandler, NOT a MouseArea filling the panel. Qt hands the
        // hover enter/exit pair to exactly ONE item — the topmost under the
        // pointer — so a panel-filling MouseArea is `exited` the moment the
        // pointer reaches a ROW inside this menu, and every row here has its
        // own. Worse, the enter of the next item arrives BEFORE the exit of
        // the previous one, so the rows stopping the timer themselves could
        // not save it: the sequence was row.stop() then panel.restart(), and
        // the dismissal timer ran on while the pointer sat still on a row.
        // A HoverHandler reports the whole SUBTREE, which is the question
        // being asked: is the pointer anywhere on this menu?
        //
        // Measured on Qt 6.11 — see synfiles/tests/ctx_flyout_hover.qml, which
        // drives a real pointer through the identical graph.
        HoverHandler { id: menuHover }

        Column {
            id: col
            anchors { fill: parent; margins: 8 }
            spacing: 1

            Text {
                text: "Bar · " + menu.output
                color: Theme.magenta
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.letterSpacing: 1
                bottomPadding: 4
            }

            Repeater {
                model: BarConfig.rows

                delegate: Rectangle {
                    id: row
                    required property var modelData

                    readonly property bool on: BarConfig.get(menu.output, modelData.key)

                    width: col.width
                    height: 22
                    radius: Theme.radius
                    color: rowMouse.containsMouse ? Theme.hoverBg : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    // A checkbox, not just colour: on a light theme a "dim vs
                    // bright" distinction is nearly invisible, and this row is
                    // the only thing telling you what state you just set.
                    Text {
                        id: box
                        anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
                        text: row.on ? "[x]" : "[ ]"
                        color: row.on ? Theme.cyan : Theme.fgDim
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                    }

                    Text {
                        anchors {
                            left: box.right; leftMargin: 8
                            right: parent.right; rightMargin: 6
                            verticalCenter: parent.verticalCenter
                        }
                        text: row.modelData.label
                        color: row.on ? Theme.popupFgOn(menu.backdrop)
                                      : Theme.popupFgDimOn(menu.backdrop)
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        // Stays open on purpose — fitting a narrow monitor
                        // usually means turning off two or three things.
                        onClicked: BarConfig.toggle(menu.output, row.modelData.key)
                    }
                }
            }

            /*
             * ── Plugin widgets ───────────────────────────────────────────────
             *
             * Third-party bar widgets (see Plugins.qml / synui-plugins),
             * checked and ordered from the same menu the built-in modules
             * above are — a plugin is a bar widget like Clock or Volume, and
             * finding its switch meant knowing to open a whole separate
             * Plugin Manager window for a toggle this menu already draws for
             * everything else.
             *
             * ⚠ ONE LIST FOR THE WHOLE DESK, NOT PER OUTPUT. Every toggle
             * above writes bar.json keyed on `menu.output` because a narrow
             * monitor and a wide one reasonably disagree about which built-in
             * modules fit; a plugin is somebody's own widget and "on for this
             * monitor, off for that one" is not a distinction anyone has
             * asked this menu for. `Plugins.setEnabled` writes plugins.state,
             * which has no notion of an output at all, and every bar reads
             * the same file — so the row here reads the SAME state and sets
             * it the SAME way regardless of which monitor's menu is open.
             *
             * Filtered to `unsupported === ""`: a plugin the bar cannot host
             * has no switch to offer here — `synui-plugins list` names the
             * reason, and a checkbox nothing will ever draw for is worse than
             * no row at all.
             *
             * Order matches the bar's own: Plugins.all IS what the bar draws
             * from, so this list is not a view of the row — it is the row, and
             * moving something here moves it there in the same frame. See
             * Plugins.move for why the script is told the answer rather than
             * asked for it.
             */
            Text {
                visible: menu.hostablePlugins.length > 0
                text: "Plugins"
                color: Theme.magenta
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.letterSpacing: 1
                topPadding: 4
                bottomPadding: 4
            }

            Repeater {
                id: pluginRows
                model: menu.hostablePlugins

                delegate: Rectangle {
                    id: prow
                    required property var modelData
                    required property int index

                    width: col.width
                    height: 22
                    radius: Theme.radius
                    color: prowMouse.containsMouse ? Theme.hoverBg : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                    Text {
                        id: pbox
                        anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
                        text: prow.modelData.enabled ? "[x]" : "[ ]"
                        color: prow.modelData.enabled ? Theme.cyan : Theme.fgDim
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                    }

                    Text {
                        anchors {
                            left: pbox.right; leftMargin: 8
                            right: pmoveUp.left; rightMargin: 6
                            verticalCenter: parent.verticalCenter
                        }
                        text: prow.modelData.name
                        color: prow.modelData.enabled ? Theme.popupFgOn(menu.backdrop)
                                                       : Theme.popupFgDimOn(menu.backdrop)
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    // ▲▼ rather than a drag handle: a menu that closes on a
                    // click outside it and re-anchors on every reopen is a bad
                    // home for a drag gesture, and a fixed target you click
                    // twice moves a plugin exactly as far a drag would with no
                    // chance of missing the drop.
                    Text {
                        id: pmoveUp
                        anchors { right: pmoveDown.left; rightMargin: 2
                                  verticalCenter: parent.verticalCenter }
                        text: "▴"
                        color: prow.index > 0 ? Theme.popupFgOn(menu.backdrop)
                                               : Theme.fgDim
                        font.pixelSize: 11
                        MouseArea {
                            anchors { fill: parent; margins: -4 }
                            enabled: prow.index > 0
                            hoverEnabled: true
                            onClicked: Plugins.moveUp(prow.modelData.id)
                        }
                    }
                    Text {
                        id: pmoveDown
                        anchors { right: parent.right; rightMargin: 6
                                  verticalCenter: parent.verticalCenter }
                        text: "▾"
                        color: prow.index < pluginRows.count - 1
                               ? Theme.popupFgOn(menu.backdrop) : Theme.fgDim
                        font.pixelSize: 11
                        MouseArea {
                            anchors { fill: parent; margins: -4 }
                            enabled: prow.index < pluginRows.count - 1
                            hoverEnabled: true
                            onClicked: Plugins.moveDown(prow.modelData.id)
                        }
                    }

                    MouseArea {
                        id: prowMouse
                        // Left of the arrows only — the arrows are their own
                        // MouseAreas on top of this one and would otherwise
                        // toggle the plugin AND move it on the same click.
                        anchors { left: parent.left; right: pmoveUp.left
                                  top: parent.top; bottom: parent.bottom }
                        hoverEnabled: true
                        onClicked: Plugins.setEnabled(prow.modelData.id,
                                                       !prow.modelData.enabled)
                    }
                }
            }

            Rectangle {
                width: col.width; height: 1
                color: Theme.hoverBg
            }

            Rectangle {
                width: col.width
                height: 20
                radius: Theme.radius
                color: doneMouse.containsMouse ? Theme.activeBg : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "Done"
                    color: Theme.popupFgOn(menu.backdrop)
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }

                MouseArea {
                    id: doneMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: menu.visible = false
                }
            }
        }
    }
}
