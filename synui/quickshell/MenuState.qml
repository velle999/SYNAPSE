pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * MenuState — whether the start menu is open, and which monitor it is on.
 *
 * Same split as OsdState: the state is a singleton and the WINDOW
 * (StartMenu.qml) is per-screen, because the menu is one logical thing drawn on
 * whichever monitor it was summoned from. Three monitors must not mean three
 * menus with three independent selections.
 *
 * THE OUTPUT IS PASSED IN, not probed. Both things that open this menu already
 * know the answer: a click knows which bar was clicked, and the Super tap comes
 * from synui, which is the only process that knows what has focus — so its IPC
 * call names the output (see input.c's start_menu action). The probe below is a
 * fallback for a caller that names nothing; relying on it for the normal path
 * would flash the menu onto the previously-focused monitor for however long the
 * round trip takes, which on a menu (unlike the OSD) is the thing you are
 * looking straight at.
 */
QtObject {
    id: root

    property bool   open:   false
    property string output: ""

    // The search text and the page live here rather than in the window so that
    // opening on a different monitor does not resurrect the last search.
    property string search: ""
    property string page:   ""      // "" = root, else a page id

    function show(outputName) {
        if (outputName) root.output = outputName
        else if (!root.output) outputProbe.running = true
        root.search = ""
        root.page   = ""
        root.open   = true
    }

    function close() { root.open = false }

    function toggle(outputName) {
        // Re-summoning on a DIFFERENT monitor moves the menu there rather than
        // closing it — the alternative is a menu that vanishes when you press
        // Super on the screen you are actually looking at.
        if (root.open && (!outputName || outputName === root.output)) root.close()
        else root.show(outputName)
    }

    // Fallback only. synui is the only thing that knows which output has focus;
    // there is no Wayland protocol that tells a layer-shell client.
    property Process outputProbe: Process {
        command: ["synctl", "outputs"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    for (const o of JSON.parse(this.text))
                        if (o.focused) { root.output = o.name; return }
                } catch (e) { /* keep whatever we had */ }
            }
        }
    }

    // Is synpkg installed? The start menu's "Software Manager" and "Update
    // System" rows prefer it and fall back to shelly when it is absent.
    //
    // WHY THIS EXISTS AT ALL. synui pkgrel 317 pointed both rows at synpkg the
    // release synpkg landed, but a component that is not yet installed could
    // never arrive through `syn-update apply` — so an updated system got a
    // "Software Manager" that silently did nothing (a failed exec on the argv
    // path is not reported anywhere) and an "Update System" that opened a
    // terminal reading `synpkg: command not found`. syn-update installs new
    // components now, but a menu row must not be a dead click even when
    // delivery goes wrong again, and shelly is still installed on purpose for
    // exactly this reason — the replacement is phased.
    //
    // `pacman -Qq`, with the answer read off STDOUT rather than the exit status:
    // Process's exited(int, QProcess::ExitStatus) cannot be given a typed
    // handler from QML, so the exit code is not reachable here — the same wall
    // synpkg.qml's actProc hit. Empty stdout means not installed.
    //
    // Unlike outputProbe this runs unconditionally at startup, because the
    // answer is wanted before the first click rather than in response to one.
    property bool synpkgPresent: false
    property Process synpkgProbe: Process {
        running: true
        command: ["pacman", "-Qq", "synpkg"]
        stdout: StdioCollector {
            onStreamFinished: root.synpkgPresent = this.text.trim() === "synpkg"
        }
    }
}
