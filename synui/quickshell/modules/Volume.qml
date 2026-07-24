import QtQuick
import Quickshell.Io
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
    tooltipText: (sink && sink.description ? sink.description : "Audio")
                 + "\n" + (muted ? "muted" : volume + "%")
                 + "\nClick to mute · scroll to adjust · right-click for mixer"

    onClicked: if (audio) audio.muted = !audio.muted

    onRightClicked: mixer.running = true

    // Scroll adjusts in 5% steps, clamped to 1.0. PipeWire will happily accept
    // volumes above 1.0 (software boost) and that is a good way to blow out a
    // pair of speakers from a stray scroll on the bar.
    onScrolled: (delta) => {
        if (!audio) return
        audio.volume = Math.max(0, Math.min(1, audio.volume + delta * 0.05))
    }

    Process {
        id: mixer
        command: ["pavucontrol"]
    }
}
