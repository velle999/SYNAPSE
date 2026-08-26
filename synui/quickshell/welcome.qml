//@ pragma UseQApplication

import Quickshell
import Quickshell.Io
import "welcome"

/*
 * welcome.qml — the SynapseOS welcome guide, as its own quickshell.
 *
 * ⚠ THIS IS A SECOND ENTRY POINT INTO THE BAR'S QML TREE, NOT PART OF THE BAR.
 * `synui-welcome` runs `quickshell -p …/quickshell/welcome.qml`, which makes
 * this directory the shell root — so `import ".."` inside welcome/ reaches the
 * same Theme.qml the bar uses, and the guide is themed, fonted and glassed by
 * the one palette without a line of it being copied.
 *
 * Why not a window in shell.qml, which would have been fewer moving parts:
 *
 *   · TWO BARS SHIP. `bar_shell = synapse|antiquity` picks one, and a guide
 *     living inside the SYNAPSE bar would not exist at all for anyone running
 *     Antiquity. The menu this replaces was drawn by the compositor and every
 *     configuration had it; losing it on one of two shipped shells would be a
 *     silent regression, discovered by whoever switched.
 *   · IT COSTS NOTHING WHEN CLOSED. Dismissing the guide ends the process. A
 *     window in the bar is a window in the bar for the whole session, holding a
 *     full-screen layer surface's worth of scene graph for something most people
 *     see once.
 *   · THE BAR IS RESTARTED. Game mode stops and starts it (`bar_stop_cmd`), and
 *     a guide that reappeared on every bar restart would be a bug with a very
 *     confusing report.
 *
 * UseQApplication for the same reason shell.qml carries it: it is the mode the
 * rest of this tree is written against, and a second quickshell on the same QML
 * running in the other mode is a difference waiting to be found the hard way.
 */
ShellRoot {
    /*
     * One window per screen, one guide — GuideState decides which monitor shows
     * it. Variants over the live screen model rather than a single window, so
     * unplugging the monitor the guide is on does not leave a surface behind on
     * a screen that no longer exists.
     */
    Variants {
        model: Quickshell.screens
        delegate: Guide {}
    }

    /*
     * How something outside asks for the guide.
     *
     * `synui-welcome` calls this first and starts a process only when nothing
     * answers, which is what makes Super+Escape a toggle across a process
     * boundary: closing the guide quits, so "not running" and "closed" are the
     * same state and there is no third one to get wrong.
     *
     * The output is passed IN because synui is the only process that knows which
     * monitor has focus — no Wayland protocol tells a layer-shell client — and
     * it is answering a keypress it just handled, so it already knows.
     */
    IpcHandler {
        target: "welcome"

        function show(output: string): void { GuideState.show(output) }
        function hide(): void               { GuideState.close() }
        function toggle(output: string): void {
            // Re-summoning on a DIFFERENT monitor moves the guide there rather
            // than closing it — the alternative is a guide that vanishes when
            // you press the key on the screen you are actually looking at.
            if (GuideState.open && (!output || output === GuideState.output))
                GuideState.close()
            else
                GuideState.show(output)
        }
        function page(n: int): void { GuideState.goTo(n) }
    }
}
