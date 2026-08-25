import QtQuick
import Quickshell
import Quickshell.Services.Pipewire
import ".."

/*
 * The mixer the volume module's tooltip has been promising.
 *
 * Right-clicking the volume module used to run `pavucontrol`, which is not
 * installed and is not a dependency — so the tooltip advertised a mixer and the
 * click did nothing at all, silently. This is that mixer, drawn by the shell:
 * output and input devices with a radio to pick the default, and a live slider
 * per application stream.
 *
 * Bound to PipeWire rather than shelling out, for the same reason Volume.qml is:
 * a change made here, from a media key, or from synui's own volume knob shows up
 * in all three immediately instead of at the next poll.
 *
 * PwObjectTracker is LOAD-BEARING, and more so than in Volume.qml. Nothing
 * about the graph exists until something tracks an object: with no tracker
 * anywhere, `Pipewire.nodes` is EMPTY and `Pipewire.ready` is false, because the
 * service does not open its connection until a first consumer asks. Even once
 * connected, an untracked node reports its name, description and type but an
 * EMPTY property map and a placeholder volume — which is why this tracks its
 * whole node set continuously rather than only while visible. Tracking on open
 * would draw every slider at 100% for a frame and then snap.
 */
PopupWindow {
    id: mixer

    // A PopupWindow is not an Item: it has no `parent` and the QsWindow
    // attached property does not resolve on it, so the anchor window has to be
    // handed in. See BarMenu.barWindow — a defensive `parent ? ... : null` there
    // took the null branch every time and left a popup that never mapped.
    required property var barWindow
    property int anchorX: 0

    // How many sliders are mid-drag. The dismissal timer refuses to fire while
    // this is above zero: a drag that wanders off the popup still holds the
    // implicit pointer grab, so "exited" arrives during a live gesture.
    property int holds: 0

    // ── The graph ────────────────────────────────────────
    // Non-audio nodes (the dummy driver, MIDI bridges) have no audio interface
    // and are filtered out here rather than checked per section.
    readonly property var audioNodes: Pipewire.nodes.values.filter(n => n.audio)

    // isSink on a STREAM means the stream feeds a sink, i.e. it is playback —
    // verified against the live graph, where Firefox and the wallpaper engine
    // report isStream && isSink and cava's capture reports isStream && !isSink.
    readonly property var sinks:    audioNodes.filter(n => !n.isStream &&  n.isSink)
    readonly property var sources:  audioNodes.filter(n => !n.isStream && !n.isSink)
    readonly property var playback: audioNodes.filter(n =>  n.isStream &&  n.isSink)
    readonly property var capture:  audioNodes.filter(n =>  n.isStream && !n.isSink)

    PwObjectTracker { objects: mixer.audioNodes }

    // A device's nickname is the short human one ("ALC1220 Analog",
    // "Pixio PX248P"); the description is the full card string, which elides to
    // nothing useful in a 320px popup.
    function deviceLabel(node) {
        if (!node) return ""
        return node.nickname || node.description || node.name || "?"
    }

    // A stream's description is routinely EMPTY (Firefox sets none), so the
    // application name from the property map leads and the node name — always
    // present, tracked or not — is the floor.
    function streamLabel(node) {
        if (!node) return ""
        const p = node.properties || ({})
        return p["application.name"] || node.name || node.description || "?"
    }

    // media.name is the track or window title. Suppressed when it merely
    // repeats the app name, which is what a stream with nothing playing sets.
    function streamSub(node) {
        if (!node) return ""
        const p = node.properties || ({})
        const m = p["media.name"] || ""
        return m === streamLabel(node) ? "" : m
    }

    // ── Window ───────────────────────────────────────────
    implicitWidth: 320
    // Capped and scrolled rather than grown: a machine with several cards and a
    // browser full of tabs can list a dozen rows, and an unbounded popup would
    // run off the bottom of a 1080-tall screen with the Done row on it.
    //
    // 40 is the chrome the rows do NOT get: 8 above the header + 18 of header +
    // 6 below it + 8 at the foot. Counting it as 38 left the Flickable two
    // pixels shorter than its content, so every mixer was permanently — and
    // pointlessly — scrollable, clipping the last row's slider by 2px.
    implicitHeight: Math.min(col.implicitHeight + 40, 560)

    color: "transparent"
    visible: false

    // See BarMenu: an xdg_popup grab is what makes a click anywhere else close
    // this, instead of the pointer-leave timer having to be the whole answer.
    grabFocus: true

    // The compositor broke the grab and the window is already down; put the QML
    // property back in step, or the next toggleAt() reads `visible` as true and
    // toggles a window that is not there into a window that is still not there.
    onClosed: mixer.visible = false

    function toggleAt(x) {
        if (mixer.visible) { mixer.visible = false; return }
        mixer.anchorX = x
        mixer.visible = true
        closeTimer.stop()
        // The equalizer's state file may not have existed when the bar started —
        // it is created by the first `synui-eq on` — and a watch cannot have
        // been placed on a path that was not there. Ask on open, so a mixer
        // never shows an equalizer that was switched on some other way as off.
        EqState.refresh()
    }

    anchor {
        window: mixer.barWindow
        // Clamped to BOTH edges, unlike BarMenu's. That menu opens wherever the
        // bar was clicked; this one always opens under the volume module, which
        // lives a few pixels from the right edge — centred on it, a 320px popup
        // hangs off the screen every time.
        //
        // barWindow is NULL on the first evaluation: the module hands in
        // `root.QsWindow.window`, and that attached property does not resolve
        // until the item has been given to a window. Reading `.width` through it
        // unguarded threw a TypeError on every startup (caught in the nested
        // rig) — harmless-looking, but it aborts the binding, and a binding that
        // aborts is one that can be left holding a stale x.
        rect.x: mixer.barWindow
                ? Math.max(4, Math.min(mixer.anchorX - mixer.implicitWidth / 2,
                                       mixer.barWindow.width - mixer.implicitWidth - 4))
                : 4
        rect.y: BarConfig.popupY(mixer.implicitHeight)
    }

    // A BACKSTOP for the grab above, not the mechanism — same as BarMenu. It is
    // what closes the mixer if the grab is ever refused, and it refuses to fire
    // mid-drag: a slider drag that wanders off the popup still holds the
    // implicit pointer grab, so "exited" arrives during a live gesture.
    //
    // ⚠ SAME FIX AS BarMenu (see its header): a short interval here makes the
    // timer the real mechanism instead of a backstop, since a held grab does
    // not care where the pointer rests — only a click outside dismisses it —
    // and 1600ms closed the mixer on an ordinary glance away mid-adjustment.
    Timer {
        id: closeTimer
        interval: 8000
        onTriggered: {
            if (mixer.holds > 0) restart()      // still dragging
            else                 mixer.visible = false
        }
    }

    /* What the wallpaper is doing under the mixer. Same reasoning as BarMenu's:
     * the anchor rect is in the bar window's coordinates and the bar spans a
     * screen edge, so those are the screen's coordinates too. */
    readonly property var backdrop: mixer.barWindow
        ? Theme.backdropFor(mixer.barWindow.screen,
                            mixer.anchor.rect.x, mixer.anchor.rect.y,
                            mixer.implicitWidth, mixer.implicitHeight)
        : null

    Rectangle {
        anchors.fill: parent
        radius: Theme.panelRadius
        color: Theme.popupBgOn(mixer.backdrop)
        border.color: Theme.magenta
        border.width: 1

        // Escape closes, now that the grab brings keyboard focus with it.
        focus: true
        Keys.onEscapePressed: mixer.visible = false
        onVisibleChanged: if (visible) forceActiveFocus()

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton     // so the sliders above still get clicks
            onEntered: closeTimer.stop()
            onExited: closeTimer.restart()
        }

        // ── Header ───────────────────────────────────────
        // Outside the scroller on purpose: Done has to stay reachable on a
        // machine with enough streams to push it past the bottom.
        Item {
            id: head
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
            height: 18

            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: "MIXER"
                color: Theme.magenta
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.letterSpacing: 1
            }

            Rectangle {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                width: 46
                height: 18
                radius: Theme.radius
                color: doneMouse.containsMouse ? Theme.activeBg : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "Done"
                    color: Theme.fg
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }

                MouseArea {
                    id: doneMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: closeTimer.stop()
                    onClicked: mixer.visible = false
                }
            }
        }

        Flickable {
            id: body
            anchors {
                left: parent.left; right: parent.right
                top: head.bottom; bottom: parent.bottom
                leftMargin: 8; rightMargin: 8; topMargin: 6; bottomMargin: 8
            }
            contentWidth: width
            contentHeight: col.implicitHeight
            clip: true
            // Only scrolls when it has to; otherwise a wheel over the panel
            // would drag a fully-visible list around under the pointer.
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: col
                width: body.width
                spacing: 4

                // ── Output devices ───────────────────────
                MixerHeading { width: col.width; text: "Output" }

                Repeater {
                    model: mixer.sinks

                    delegate: MixerSlider {
                        required property var modelData

                        width: col.width
                        audio: modelData.audio
                        label: mixer.deviceLabel(modelData)
                        selectable: true
                        // Marked from the ACTUAL default, not from what was last
                        // asked for: setting the preferred sink is a request to
                        // PipeWire, and a device that has gone away will not take
                        // it. The radio should say what is true.
                        selected: Pipewire.defaultAudioSink === modelData
                        onSelectRequested: Pipewire.preferredDefaultAudioSink = modelData

                        onHeldChanged: mixer.holds += held ? 1 : -1
                        // A node destroyed mid-drag would otherwise leave the
                        // count stuck above zero and the panel unable to close.
                        Component.onDestruction: if (held) mixer.holds -= 1
                    }
                }

                // ── Equalizer ────────────────────────────
                // Under Output because that is what it is: switching it on
                // inserts a filter-chain sink and makes it the default, so the
                // device list above grows a "SynapseOS Equalizer" row and the
                // radio moves to it. Putting the switch anywhere else would
                // leave that as an unexplained device appearing on its own.
                //
                // The row is a LINK, not an editor — the ten bands live in
                // synui's own panel, and the button opens that rather than this
                // popup growing a second copy of synui-eq's preset table.
                MixerHeading { width: col.width; text: "Equalizer" }

                MixerEqRow {
                    width: col.width

                    on: EqState.enabled
                    status: EqState.status
                    preset: EqState.preset
                    preamp: EqState.preamp
                    warning: EqState.warning

                    onToggleRequested: EqState.toggle()
                    // Closed first. The panel it opens is compositor-drawn and
                    // modal, and leaving an xdg_popup with a keyboard grab on
                    // screen underneath it means two things believe they own
                    // Escape.
                    onOpenRequested: {
                        mixer.visible = false
                        EqState.openPanel()
                    }

                    // The panel-wide MouseArea loses hover to this row's own
                    // hover-enabled children, which fires its `exited` and arms
                    // the dismissal timer. Same fix the Done button carries.
                    onHoveredChanged: if (hovered) closeTimer.stop()
                }

                // ── Input devices ────────────────────────
                MixerHeading {
                    width: col.width
                    text: "Input"
                    visible: mixer.sources.length > 0
                }

                Repeater {
                    model: mixer.sources

                    delegate: MixerSlider {
                        required property var modelData

                        width: col.width
                        audio: modelData.audio
                        label: mixer.deviceLabel(modelData)
                        accent: Theme.cyan
                        input: true
                        selectable: true
                        selected: Pipewire.defaultAudioSource === modelData
                        onSelectRequested: Pipewire.preferredDefaultAudioSource = modelData

                        onHeldChanged: mixer.holds += held ? 1 : -1
                        Component.onDestruction: if (held) mixer.holds -= 1
                    }
                }

                // ── Application streams ──────────────────
                MixerHeading { width: col.width; text: "Applications" }

                Text {
                    visible: mixer.playback.length === 0
                    width: col.width
                    leftPadding: 22
                    text: "Nothing is playing"
                    color: Theme.fgDim
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }

                Repeater {
                    model: mixer.playback

                    delegate: MixerSlider {
                        required property var modelData

                        width: col.width
                        audio: modelData.audio
                        label: mixer.streamLabel(modelData)
                        sublabel: mixer.streamSub(modelData)

                        onHeldChanged: mixer.holds += held ? 1 : -1
                        Component.onDestruction: if (held) mixer.holds -= 1
                    }
                }

                // ── Recording streams ────────────────────
                // Hidden entirely when empty: on a desktop with no microphone in
                // use this section is always empty, and a permanent "Recording"
                // heading over nothing invites the reader to wonder what is.
                MixerHeading {
                    width: col.width
                    text: "Recording"
                    visible: mixer.capture.length > 0
                }

                Repeater {
                    model: mixer.capture

                    delegate: MixerSlider {
                        required property var modelData

                        width: col.width
                        audio: modelData.audio
                        label: mixer.streamLabel(modelData)
                        sublabel: mixer.streamSub(modelData)
                        accent: Theme.cyan
                        input: true

                        onHeldChanged: mixer.holds += held ? 1 : -1
                        Component.onDestruction: if (held) mixer.holds -= 1
                    }
                }
            }
        }
    }
}
