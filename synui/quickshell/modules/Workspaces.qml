import QtQuick
import Quickshell
import Quickshell.Io
import ".."

/*
 * Virtual desktops.
 *
 * synui's workspaces span the WHOLE DESK — the same nine on every monitor — so
 * this is one row of pills rather than a per-output set. `synctl workspaces`
 * gives the whole picture in a single call (id, name, window count, which
 * monitors are showing it), so there is no second poll for the active one.
 *
 * ⚠ WHICH PILL IS LIT IS PER-MONITOR, and `visible` is NOT that question.
 * Under `workspace_mode = per-monitor` each screen chooses its own desktop, so
 * more than one workspace is `visible` at a time and every bar would light both
 * of them. The row's `outputs` array is the per-screen answer — the monitors
 * currently showing that desktop, by name — and under `shared` it is every
 * monitor for exactly one desktop, so this reads identically in both modes.
 * The `visible` fallback is for a compositor older than that field.
 *
 * Clicking dispatches through synctl rather than reimplementing anything:
 * `synctl dispatch ws N` runs the exact keybind action Super+N does. It moves
 * the monitor the POINTER is on, which under per-monitor desktops is the one
 * whose bar was just clicked — so a click on this bar moves this screen, with
 * nothing here needing to say so.
 */
Item {
    id: root


    /* The screen this module's bar is on, handed down by Bar.qml. Not derived
     * from QsWindow.window: see Theme.barPaletteSpanOn for the race that costs. */
    property var barScreen: null
    // The palette for the strip this module covers — a clear bar's ink comes off
    // whatever is behind it, which differs per monitor and, where a window sits
    // under the bar, along one monitor too. See Theme.barStrips.
    readonly property var pal:
        Theme.barPaletteSpanOn(root.barScreen, root.QsWindow.window,
                               root, root.x, root.width)

    // This used to reserve the launcher's width at its left edge, because the
    // compositor drew the "◢ SYNAPSE" button over the bar's top-left corner and
    // hit-tested it there — anything placed under it was invisible AND
    // unclickable, so this mirrored launcher.c's own width formula off the same
    // state file. The button is a bar module now (modules/Launcher.qml) and sits
    // in the same Row as this, so the corner is ordinary bar again and the whole
    // mirror is gone.

    property var workspaces: []

    /* The name synui knows this monitor by, which is what `outputs` carries. */
    readonly property string outName: root.barScreen ? (root.barScreen.name || "") : ""

    /* Is `w` the desktop THIS screen is showing? */
    function shownHere(w) {
        if (w && Array.isArray(w.outputs))
            return root.outName !== "" ? w.outputs.indexOf(root.outName) >= 0
                                       : w.outputs.length > 0
        return w && w.visible === true
    }

    /*
     * The desktop this screen is on, resolved ONCE per poll rather than per
     * pill.
     *
     * ⚠ AND THIS IS WHY IT IS A PROPERTY AND NOT A CALL IN THE DELEGATE.
     * `active: root.shownHere(modelData)` looks equivalent and is a STALE
     * BINDING: modelData is a plain JS object, so nothing in that expression is
     * a property QML can watch, and the pill keeps whatever answer it was first
     * given. On a two-monitor desk that showed the second screen lighting both
     * its old desktop and its new one at once. Reading root.workspaces here —
     * which the poll reassigns — is what makes the dependency real.
     */
    readonly property int litId: {
        for (const w of root.workspaces)
            if (root.shownHere(w))
                return w.id
        return -1
    }

    implicitWidth: row.implicitWidth
    implicitHeight: Theme.barHeight

    Row {
        id: row
        height: parent.height
        spacing: 3

        Repeater {
            model: root.workspaces

            delegate: Rectangle {
                id: pill
                required property var modelData

                readonly property bool active: pill.modelData.id === root.litId
                readonly property bool occupied: (modelData.windows || 0) > 0

                width: 22
                height: 20
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.radius

                // Three states worth telling apart at a glance: the one you are
                // on, ones holding windows you can go back to, and empty ones.
                color: pill.active ? root.pal.activeBg
                                   : (mouse.containsMouse ? root.pal.hoverBg : "transparent")
                border.width: pill.active ? 1 : 0
                border.color: root.pal.accent
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                Text {
                    anchors.centerIn: parent
                    text: pill.modelData.id
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    color: pill.active ? root.pal.fg
                                       : (pill.occupied ? root.pal.glyph : root.pal.dim)
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        switcher.command = ["synctl", "dispatch", "ws",
                                            String(pill.modelData.id)]
                        switcher.running = true
                    }
                }
            }
        }
    }

    Process { id: switcher }

    Process {
        id: poll
        command: ["synctl", "workspaces"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const ws = JSON.parse(this.text)
                    if (Array.isArray(ws)) root.workspaces = ws
                } catch (e) {
                    // Leave the last good list up. Blanking the row because one
                    // poll raced a compositor restart would look like the
                    // desktops vanished.
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
