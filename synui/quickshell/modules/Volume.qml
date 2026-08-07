import QtQuick
import Quickshell
import Quickshell.Services.Pipewire
import "../components"
import ".."

/*
 * Volume — bound to PipeWire directly rather than shelling out to pactl.
 *
 * The waybar module polled `pulseaudio` and ran pactl on click/scroll. Binding
 * the node means the reading is live (a change from pavucontrol, a media key,
 * or synui's own volume knob shows immediately) instead of up to a poll
 * interval stale.
 *
 * PwObjectTracker is not optional: pipewire objects are unbound by default and
 * their properties read as null until something tracks them. Without it this
 * module silently shows nothing.
 */
BarModule {
    id: root

    acceptsRight: true

    // Read by the bar: an auto-hiding bar must not slide up out from under its
    // own mixer while the pointer is over the popup rather than the bar.
    readonly property bool mixerOpen: mixer.visible

    readonly property var sink: Pipewire.defaultAudioSink
    readonly property var audio: sink ? sink.audio : null
    readonly property bool muted: audio ? audio.muted : false
    readonly property int  volume: audio ? Math.round(audio.volume * 100) : 0

    PwObjectTracker { objects: [Pipewire.defaultAudioSink] }

    icon: muted ? Icons.volMuted
                : (volume >= 66 ? Icons.volHigh
                : (volume >= 33 ? Icons.volMed : Icons.volLow))
    iconColor: muted ? Theme.fgDim : Theme.cyan
    text: muted ? "muted" : (volume + "%")
    textColor: muted ? Theme.fgDim : Theme.fg
    // The equalizer line only appears when there is something to explain. With
    // it on, the device named above is "SynapseOS Equalizer" — a sink nobody
    // plugged in — and the tooltip is where that stops being a mystery. It is
    // also where a chain that says it is on but is not gets noticed, since the
    // mixer has to be opened to see the row itself.
    tooltipText: {
        let s = (sink && sink.description ? sink.description : "Audio")
        s += "\n" + (muted ? "muted" : volume + "%")
        if (EqState.enabled)
            s += "\nEqualizer: " + EqState.status
                 + (EqState.warning ? "" : "  ·  " + EqState.preset)
        return s + "\nClick to mute · scroll to adjust · right-click for mixer"
    }

    onClicked: if (audio) audio.muted = !audio.muted

    // Opened centred under this module. The x is mapped to the BAR's coordinate
    // space (mapToItem(null, ...)) because that is the space the popup's anchor
    // rect is in — the same mapping BarModule's tooltip does.
    onRightClicked: mixer.toggleAt(root.mapToItem(null, 0, 0).x + root.width / 2)

    // Scroll adjusts in 5% steps, clamped to 1.0. PipeWire will happily accept
    // volumes above 1.0 (software boost) and that is a good way to blow out a
    // pair of speakers from a stray scroll on the bar.
    onScrolled: (delta) => {
        if (!audio) return
        audio.volume = Math.max(0, Math.min(1, audio.volume + delta * 0.05))
    }

    // The mixer the tooltip promises. This used to spawn `pavucontrol`, which is
    // not installed and is not a dependency of anything SYNAPSE ships — so the
    // tooltip advertised a mixer and the right-click did nothing whatsoever, with
    // not even a message to say why.
    Mixer {
        id: mixer
        // A PopupWindow cannot find its own window; BarModule's root is an Item,
        // so the attached property resolves here and not inside Mixer.qml.
        barWindow: root.QsWindow.window
    }
}
