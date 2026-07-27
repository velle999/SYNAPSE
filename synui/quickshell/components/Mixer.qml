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

    function toggleAt(x) {
        if (mixer.visible) { mixer.visible = false; return }
        mixer.anchorX = x
        mixer.visible = true
        closeTimer.stop()
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
        rect.y: Theme.barHeight + 2
    }

    // Dismissal is pointer-leave plus an explicit Done, not a click-anywhere
    // grab — same trade as BarMenu. Longer than the menu's 1200ms because this
    // panel is aimed at with the pointer far more than it is read.
    Timer {
        id: closeTimer
        interval: 1600
        onTriggered: {
            if (mixer.holds > 0) restart()      // still dragging
            else                 mixer.visible = false
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.popupBg
        border.color: Theme.magenta
        border.width: 1

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
