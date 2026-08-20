pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * MixerState — whether the volume mixer is open, and which monitor it is on.
 *
 * The mixer itself is a PopupWindow anchored to the bar's volume module
 * (components/Mixer.qml, opened by right-clicking it). It was reachable ONLY
 * that way, which is how the menu came to carry a "Volume Mixer" entry that
 * dispatched `sounds` and opened Event sounds instead — a different panel
 * entirely, in a different process. The entry described this mixer word for
 * word ("outputs, inputs and per-application volume") and opened something
 * else.
 *
 * Same split as MenuState and OsdState: the STATE is a singleton and the
 * WINDOW is per-screen, because the mixer is one logical thing drawn on
 * whichever monitor it was summoned from. Three monitors must not mean three
 * mixers with three independent scroll positions.
 *
 * ⚠ THE OUTPUT IS PASSED IN, not probed, for the same reason MenuState says:
 * synui is the only process that knows which monitor has focus — no Wayland
 * protocol tells a layer-shell client — and it is answering a request it just
 * handled, so it already knows. The probe is a fallback for a caller that
 * names nothing.
 */
QtObject {
    id: root

    property bool   open:   false
    property string output: ""

    function show(outputName) {
        if (outputName) root.output = outputName
        else if (!root.output) outputProbe.running = true
        root.open = true
    }

    function close() { root.open = false }

    function toggle(outputName) {
        // Re-summoning on a DIFFERENT monitor moves it there rather than
        // closing it — the same rule the start menu follows, and for the same
        // reason: a panel that vanishes when you ask for it on the screen you
        // are looking at is a panel that feels broken.
        if (root.open && (!outputName || outputName === root.output)) root.close()
        else root.show(outputName)
    }

    // Fallback only. See the note above.
    property Process outputProbe: Process {
        command: ["synctl", "outputs"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    for (const o of JSON.parse(this.text))
                        if (o.focused) { root.output = o.name; break }
                } catch (e) {
                    // A mixer on the wrong monitor is recoverable; a shell that
                    // threw on a malformed reply is not.
                }
            }
        }
    }
}
