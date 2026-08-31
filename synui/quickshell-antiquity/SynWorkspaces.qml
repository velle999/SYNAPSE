pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

/*
 * The compositor's virtual desktops, for the Antiquity shell.
 *
 * Upstream talked to Hyprland directly (`Quickshell.Hyprland`), which gives a
 * live model with change signals. synui has no such thing: `synctl` is
 * request/response with NO event stream, so the compositor cannot push
 * anything at a client and the only way to notice a desktop change is to ask
 * again. Hence the timer. `synctl workspaces` answers the whole question in one
 * call — id, name, window count and which one is visible — so there is no
 * second poll for the active desktop.
 *
 * ONE poller, not three. RadialTaskbar, Sidebar and Workspaces all want this
 * data; upstream each held its own `Hyprland.workspaces` binding, which was
 * free there because the model was shared and event-driven. Here that shape
 * would mean three `synctl` forks every 400ms, three times per monitor.
 *
 * THE SEMANTIC DIFFERENCE, and why the port is not a rename:
 * Hyprland scopes a workspace to a monitor, so upstream filtered the LIST by
 * `w.monitor.name == screen.name` and each bar showed only its own screen's
 * desktops. synui's desktops span the whole desk — the same nine exist on every
 * monitor — so that filter is GONE rather than reimplemented: every bar shows
 * all nine, which is what they actually are.
 *
 * ⚠ WHICH ONE IS LIT is a different question, and it IS per-screen. Under
 * `workspace_mode = per-monitor` each monitor chooses the desktop it shows, so
 * more than one row comes back `visible` and a bar reading that alone would
 * light every screen's desktop on every screen. Each row now carries `outputs`
 * — the monitors showing that desktop, by name — and `focusedIdOn(name)` is the
 * question a bar should ask. Under `shared` it answers exactly what `focusedId`
 * always did, because there `outputs` is every monitor for one desktop.
 */
Singleton {
    id: root

    // Last good list. Deliberately not cleared on a failed parse — see below.
    property var list: []

    // The desk-wide answer: the first desktop that is up anywhere. Correct on
    // its own only while every monitor shows the same one — kept for callers
    // with no screen in hand, and as focusedIdOn's fallback. -1 while the first
    // poll is still in flight, which no delegate should ever match.
    readonly property int focusedId: {
        for (const w of root.list)
            if (w.visible === true)
                return w.id
        return -1
    }

    /*
     * The desktop MONITOR `name` is showing. This is what a bar wants: under
     * per-monitor desktops `focusedId` would light the same pill on every screen
     * regardless of which one that desktop is actually on.
     *
     * Falls back to focusedId for an empty name, and for a compositor too old to
     * send `outputs` at all — in which case there is only ever one visible
     * desktop and the two answers are the same.
     */
    function focusedIdOn(name: string): int {
        if (!name)
            return root.focusedId
        for (const w of root.list) {
            if (!Array.isArray(w.outputs))
                return root.focusedId
            if (w.outputs.indexOf(name) >= 0)
                return w.id
        }
        return -1
    }

    /*
     * Switch desktops. This dispatches the SAME action the Super+N keybind
     * runs rather than reimplementing it, so a click and a keypress can never
     * drift apart. Upstream's `Hyprland.dispatch(...)` is the exact analogue.
     */
    function switchTo(id: int): void {
        switcher.command = ["synctl", "dispatch", "ws", String(id)]
        switcher.running = true
    }

    Process { id: switcher }

    Process {
        id: poll
        command: ["synctl", "workspaces"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const ws = JSON.parse(this.text)
                    if (Array.isArray(ws))
                        root.list = ws
                } catch (e) {
                    // Keep the last good list. Blanking the row because one
                    // poll raced a compositor restart would read as "the
                    // desktops vanished", which is worse than being 400ms
                    // stale.
                }
            }
        }
    }

    Timer {
        interval: 400
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: poll.running = true
    }
}
