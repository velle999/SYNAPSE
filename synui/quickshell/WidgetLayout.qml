pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * WidgetLayout — where the desktop widgets have been dragged to.
 *
 * DELIBERATELY NOT widgets.state. That file has exactly one writer, the
 * synui-widgets shell script, and the whole point of that arrangement is that
 * however a toggle is reached there is one format and one place it can be wrong
 * (see WidgetState.qml). Positions come from the opposite direction — the
 * pointer, in this process, many times a second — so a second file is what keeps
 * that rule intact. widgets.state stays the script's; widgets.pos is ours.
 *
 * A widget with no entry here is at its designed home corner, which is what
 * every widget is until someone drags one. That is not merely a default: it is
 * the only reason an upgrade cannot move anybody's desktop around.
 *
 * ~/.config/synui is never created here, and never needs to be: the only way to
 * see a widget at all is `synui-widgets <w> on`, which mkdir -p's that directory
 * to write widgets.state. By the time there is something to drag, there is
 * somewhere to save it. Same call PostItState.qml makes about postit.txt.
 */
QtObject {
    id: root

    /*
     * name -> { edgeH, edgeV, x, y }
     *
     * x and y are the distance from the edges named by edgeH/edgeV to the
     * matching corner of the widget, in the layer-shell usable area — the same
     * space the widget's own home margins are written in, so a stored position
     * and a designed one are the same kind of number and the code never has to
     * know which it is holding.
     *
     * Positions are stored against an EDGE rather than as absolute coordinates
     * so that a resolution change, or unplugging a monitor onto a smaller one,
     * moves a widget with its corner instead of stranding it off-screen. Which
     * edges a widget is anchored to is re-derived from where it was dropped, so
     * dragging the clock to the top-left makes it a top-left widget.
     */
    property var pos: ({})

    // Set for the length of a drag. A widget being dragged is authoritative
    // over the file: without this, the reload our own save triggers would land
    // mid-gesture and yank the card back to the last saved position.
    property bool dragging: false

    property FileView posFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/widgets.pos"
        watchChanges: true
        // The bar is watching this path, so a half-written file is a desktop
        // full of widgets at position NaN. Rename is atomic; nothing can read a
        // partial one.
        atomicWrites: true
        // No file is the normal case — it means nobody has dragged anything —
        // and a warning per session for the documented default is noise. Same
        // call LauncherStyle.qml makes about a missing synuirc.
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.adopt(this.text())
        onLoadFailed: root.pos = ({})
    }

    function adopt(text) {
        if (root.dragging) return
        try {
            const j = JSON.parse(text)
            // A malformed file must not take the desktop down and must not
            // leave half the widgets moved either: fall back wholesale, exactly
            // as Theme.qml does with a bad palette.
            root.pos = (j && typeof j === "object" && !Array.isArray(j)) ? j : ({})
        } catch (e) {
            root.pos = ({})
        }
    }

    function has(name)   { return root.pos[name] !== undefined }
    function edgeH(name, fallback) { const e = root.pos[name]; return e && e.edgeH ? e.edgeH : fallback }
    function edgeV(name, fallback) { const e = root.pos[name]; return e && e.edgeV ? e.edgeV : fallback }
    function x(name, fallback) { const e = root.pos[name]; return e && typeof e.x === "number" ? e.x : fallback }
    function y(name, fallback) { const e = root.pos[name]; return e && typeof e.y === "number" ? e.y : fallback }
    /* How big the user dragged it, for the widgets that can be resized. A
     * widget that has never been resized has no `size` and takes its designed
     * one — the same rule the position takes, and for the same reason: a later
     * change to a widget's default size has to reach everyone who never touched
     * it. */
    function size(name, fallback) { const e = root.pos[name]; return e && typeof e.size === "number" ? e.size : fallback }

    /*
     * Writes go through here rather than to pos[name] directly because `pos` is
     * a var: assigning to a MEMBER of it changes the object without emitting
     * posChanged, so every binding reading it would keep the old number and the
     * widget would not move until something else happened to invalidate it.
     * Replacing the whole object is what makes the position a live binding.
     */
    function place(name, edgeH, edgeV, x, y) {
        /* ⚠ MERGED OVER THE OLD ENTRY, NOT REPLACING IT. A resized widget keeps
         * its size in the same record, and this used to build the entry from
         * scratch — so dragging a clock you had resized threw the size away and
         * the card snapped back to its designed one mid-gesture. Anything else
         * that ever joins this record is preserved for free. */
        const next = {}
        for (const k in root.pos) next[k] = root.pos[k]
        const was = root.pos[name] || {}
        const now = {}
        for (const k in was) now[k] = was[k]
        now.edgeH = edgeH; now.edgeV = edgeV
        now.x = Math.round(x); now.y = Math.round(y)
        next[name] = now
        root.pos = next
    }

    /* The other half: how big it was dragged to. Same merge rule — a widget
     * that is resized must not lose where it was put. */
    function resize(name, size) {
        const next = {}
        for (const k in root.pos) next[k] = root.pos[k]
        const was = root.pos[name] || {}
        const now = {}
        for (const k in was) now[k] = was[k]
        now.size = Math.round(size)
        next[name] = now
        root.pos = next
    }

    // Back to the designed corner. The entry is removed rather than set to the
    // home numbers so that a later change to a widget's default home reaches
    // everyone who never moved it.
    function clear(name) {
        if (!has(name)) return
        const next = {}
        for (const k in root.pos) if (k !== name) next[k] = root.pos[k]
        root.pos = next
        save()
    }

    function save() {
        posFile.setText(JSON.stringify(root.pos, null, 2) + "\n")
    }
}
