import QtQuick
import Quickshell.Io
import "../components"
import ".."

/*
 * The assistant — a chat window, opened from the bar.
 *
 * ⚠ THE BUTTON OWNS THE WINDOW'S PROCESS, and that is what makes it a toggle
 * rather than a launcher. `vibe gui` execs quickshell on vibe's own QML; a
 * click that only spawned would open a second chat window every time somebody
 * pressed it looking for the one already on screen, and quickshell gives a
 * client no way to raise another client's window.
 *
 * ⚠ SO THE WINDOW GOES WHEN THE BAR GOES. A bar reload closes the assistant,
 * which is the cost of the toggle. It is the right trade for a chat box —
 * losing a conversation across a bar restart is a nuisance, and having four
 * identical windows on screen with no way to tell them apart is worse.
 *
 * ⛔ `running = true` ON AN ALREADY-RUNNING PROCESS IS A SILENT NO-OP, so the
 * click asks which way it is going rather than assuming.
 */
BarModule {
    id: root

    icon: Icons.assistant
    iconColor: root.active ? root.pal.accent : root.pal.glyph
    active: chat.running
    tooltipText: chat.running ? "Assistant — click to close"
                              : "Assistant — ask, or say what you want done"

    // Hidden where vibe is not installed, rather than drawn as a button that
    // reports a missing package when pressed. An optdepend that is absent is
    // not an error state, it is a desktop that did not want this.
    moduleVisible: root.haveVibe

    property bool haveVibe: false

    onClicked: chat.running = !chat.running

    Process {
        id: chat
        command: ["vibe", "gui"]
    }

    Process {
        id: probe
        running: true
        command: ["sh", "-c", "command -v vibe >/dev/null 2>&1"]
        onExited: (code) => root.haveVibe = (code === 0)
    }
}
