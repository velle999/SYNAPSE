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
 * Hyprland scopes a workspace to a monitor, so upstream filtered the list by
 * `w.monitor.name == screen.name` and each bar showed only its own screen's
 * desktops. synui's workspaces span ALL monitors at once — switching moves
 * every screen together, and `synctl workspaces` has no monitor field to
 * filter on because the concept does not exist. So the filter is GONE rather
 * than reimplemented: every bar shows the same nine desktops, which is what
 * they actually are. Reintroducing a per-screen filter here would mean
 * inventing a partition the compositor does not have.
 */
Singleton {
    id: root

    // Last good list. Deliberately not cleared on a failed parse — see below.
    property var list: []

    // The visible desktop. synui marks exactly one `visible`, so this is a
    // derived fact rather than a second query. -1 while the first poll is
    // still in flight, which no delegate should ever match.
    readonly property int focusedId: {
        for (const w of root.list)
            if (w.visible === true)
                return w.id
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
