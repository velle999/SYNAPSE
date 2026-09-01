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

    // The device behind the equalizer when it is on — the chain is pinned at
    // unity, so the default sink would read a flat 100% that never moves. See
    // EqState.targetNode. Unchanged whenever the equalizer is off.
    readonly property var sink: EqState.targetNode || Pipewire.defaultAudioSink
    readonly property var audio: sink ? sink.audio : null
    readonly property bool muted: audio ? audio.muted : false
    readonly property int  volume: audio ? Math.round(audio.volume * 100) : 0

    // defaultAudioSink stays in the list even when it is not what is shown:
    // this is the tracker that opens the PipeWire connection for the whole bar
    // process, and EqState's own node lookup depends on it having done so.
    PwObjectTracker { objects: [Pipewire.defaultAudioSink, EqState.targetNode].filter(n => n) }

    icon: muted ? Icons.volMuted
                : (volume >= 66 ? Icons.volHigh
                : (volume >= 33 ? Icons.volMed : Icons.volLow))
    iconColor: muted ? root.pal.dim : root.pal.glyph
    text: muted ? "muted" : (volume + "%")
    textColor: muted ? root.pal.dim : root.pal.fg
    // The equalizer line only appears when there is something to explain. With
    // it on, the device named above is "SynapseOS Equalizer" — a sink nobody
    // plugged in — and the tooltip is where that stops being a mystery. It is
    // also where a chain that says it is on but is not gets noticed, since the
    // mixer has to be opened to see the row itself.
    tooltipText: {
        // ⛔ sink.description is PipeWire's name for the device. Not translated:
        // it has to match what the mixer and pavucontrol call the same sink.
        let s = (sink && sink.description ? sink.description : I18n.tr("Audio"))
        s += "\n" + (muted ? I18n.tr("muted") : volume + "%")
        if (EqState.enabled)
            s += "\n" + I18n.tr("Equalizer: %1").arg(EqState.status)
                 + (EqState.warning ? "" : "  ·  " + EqState.preset)
        return s + "\n" + I18n.tr("Click to mute · scroll to adjust · right-click for mixer")
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

    /*
     * …and the other way in: the menu entry, and anything else that asks synui
     * for `mixer`.
     *
     * The right-click above is the discoverable route and stays the primary
     * one. This exists because the mixer was reachable ONLY that way, which is
     * how a "Volume Mixer" menu entry came to open synui's Event sounds panel
     * instead — nothing could ask the bar for this one.
     *
     * ⚠ SCREEN-SCOPED. This module is instantiated per monitor, so every
     * instance sees the same MixerState. Without the output test, one request
     * opens a mixer on all three. MenuState/StartMenu solve it the same way.
     *
     * The anchor is the module's own position, exactly as the right-click
     * computes it — so a mixer summoned from a menu lands under the volume
     * module it belongs to rather than in the middle of the bar.
     */
    Connections {
        target: MixerState
        function onOpenChanged() {
            const mine = !MixerState.output
                      || MixerState.output === root.QsWindow.window.screen.name
            if (MixerState.open && mine) {
                if (!mixer.visible)
                    mixer.toggleAt(root.mapToItem(null, 0, 0).x + root.width / 2)
            } else if (!MixerState.open && mixer.visible) {
                mixer.visible = false
            }
        }
    }

    // Closing it any other way — click-off, Escape, the right-click again —
    // has to put the state back, or the next request is read as "already open"
    // and toggles to closed instead of opening.
    onMixerOpenChanged: if (!root.mixerOpen && MixerState.open) MixerState.close()
}
