import QtQuick
import Quickshell
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

    /* ⛔ AND IT SHOWS WHEN THE MICROPHONE IS OPEN. The assistant can be armed
     * to answer to its name, and that leaves a microphone listening for as long
     * as it is on — with the chat window closed, minimised or on another
     * workspace. A disclosure that lives only in a window nobody is looking at
     * is not one, so the engine writes the state and the bar wears it: the
     * glyph turns to a microphone and takes the warning colour.
     *
     * ⚠ The file is the interface, not an IPC call — the engine is a child of
     * this button on this monitor, and on a second monitor there is no child at
     * all. A file every bar watches says the same thing on all of them. */
    readonly property bool listening: assistantState.wake === "on"

    icon: root.listening ? Icons.mic : Icons.assistant
    iconColor: root.listening ? root.pal.red
                              : (root.active ? root.pal.accent : root.pal.glyph)
    active: chat.running || root.listening
    tooltipText: root.listening
        ? I18n.tr("Assistant is LISTENING for its name — click to open it and stop")
        : (chat.running ? I18n.tr("Assistant — click to close")
                        : I18n.tr("Assistant — ask, or say what you want done"))

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

    QtObject {
        id: assistantState
        property string wake: "off"
    }

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/assistant.state"
        watchChanges: true
        printErrors: false      // absent until the assistant has run once
        onFileChanged: reload()
        onLoaded: {
            const m = this.text().match(/^\s*wake\s*=\s*(\S+)\s*$/m)
            assistantState.wake = m ? m[1] : "off"
        }
        onLoadFailed: assistantState.wake = "off"
    }

    Process {
        id: probe
        running: true
        command: ["sh", "-c", "command -v vibe >/dev/null 2>&1"]
        onExited: (code) => root.haveVibe = (code === 0)
    }
}
