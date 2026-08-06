pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * LauncherState — whether the Antiquity app launcher is open, and on which
 * monitor.
 *
 *
 * WHY THIS EXISTS AT ALL
 *
 * synui owns the Super tap. `handle_keybinding` runs before the focused surface
 * sees anything, so the tap works regardless of what has focus — but synui does
 * not draw the result any more, it asks the bar to. Its own IPC (synctl) is
 * request/response with no event stream, so the compositor cannot push to a
 * client; quickshell's IPC runs the other way, and that is the whole trick.
 * input.c's `synui_start_menu_open` runs:
 *
 *     synui-bar ipc call menu toggle <focused-output>
 *
 * Upstream had no equivalent, because linux-antiquity ran under Hyprland, which
 * could be told things directly — its one IpcHandler was a per-screen
 * `appLauncher_<name> toggleAppLauncher`, named by a Hyprland bind that knew
 * the monitor it fired on. Nothing in SYNAPSE calls that, so the Super tap
 * failed against this shell and there was no way to open the launcher from the
 * keyboard at all. It is replaced by the target above rather than kept
 * alongside it: two handlers over one piece of state is the drift this file
 * exists to prevent, and the SYNAPSE bar already answers to `menu`, so a shell
 * swap is now invisible to the compositor.
 *
 * The name is `menu` and not `launcher` for that reason — it is the contract
 * with input.c, which must not have to know which shell is running.
 *
 *
 * WHY IT IS A SINGLETON AND THE WINDOW IS NOT
 *
 * Same split as the SYNAPSE bar's MenuState: the launcher is ONE logical thing
 * drawn on whichever monitor summoned it, while Bar.qml is instantiated per
 * screen off `Quickshell.screens`. Three monitors must not mean three launchers
 * with three independent searches. Bar.qml's `currentPopup` is now derived from
 * here rather than assigned to, so the taskbar button, the click-outside
 * catcher and the Super tap all move the same variable.
 *
 * THE OUTPUT IS PASSED IN, not probed. Both callers already know it: a click
 * knows which bar was clicked, and the Super tap comes from synui, the only
 * process that knows what has focus. The probe below is a fallback for a caller
 * that names nothing — relying on it normally would flash the launcher onto the
 * previously-focused monitor for the length of a `synctl` round trip, which on
 * the thing you have just summoned is exactly where you are looking.
 */
Singleton {
    id: root

    property bool open: false
    property string output: ""

    function show(outputName) {
        if (outputName)
            root.output = outputName;
        else if (!root.output)
            outputProbe.running = true;
        root.open = true;
    }

    function close() {
        root.open = false;
    }

    function toggle(outputName) {
        // Re-summoning on a DIFFERENT monitor moves the launcher there rather
        // than closing it. The alternative is a launcher that vanishes when you
        // press Super on the screen you are actually looking at.
        if (root.open && (!outputName || outputName === root.output))
            root.close();
        else
            root.show(outputName);
    }

    // Fallback only — see the header. There is no Wayland protocol that tells a
    // layer-shell client which output has focus.
    property Process outputProbe: Process {
        command: ["synctl", "outputs"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    for (const o of JSON.parse(this.text))
                        if (o.focused) {
                            root.output = o.name;
                            return;
                        }
                } catch (e) {
                    // Keep whatever we had. A launcher on the wrong monitor
                    // beats a launcher on none.
                }
            }
        }
    }
}
