pragma Singleton

import QtQml
import QtQuick
import Quickshell
import Qt.labs.folderlistmodel

/*
 * PostItState — the notes on the desktop, and which one is being edited.
 *
 * A singleton for the same reason MenuState is one: a note is drawn by one
 * window and typed into by another, and there is exactly ONE set of notes
 * however many monitors are plugged in. Putting the FileView in the widget
 * would give a three-monitor desktop three watchers of the same file and three
 * writers of it.
 *
 *
 * ONE FILE PER NOTE, AND THE DIRECTORY IS THE LIST
 *
 * Each note is plain text at ~/.config/synui/<id>.txt — not a state file with a
 * format, because the whole content is the user's and anything that reads or
 * edits text should be able to work with it. The first note is `postit.txt`,
 * the name it has always had, so nobody's note moves when they upgrade into a
 * version that can hold more than one.
 *
 * WHICH notes exist is the set of those files, read back out of the directory
 * rather than kept in an index of our own. An index would be a second thing to
 * keep in step with the notes themselves, and it would be wrong the moment
 * somebody deleted a note with rm — which is a perfectly reasonable thing to do
 * to a text file in your own config directory, and here it just works.
 *
 * There is always at least one note whether or not any file exists: `postit` is
 * unconditional. A desktop with zero notes would have nowhere to put the +.
 */
QtObject {
    id: root

    readonly property string dir: Quickshell.env("HOME") + "/.config/synui"

    // Note ids, in the order they were created — "postit", "postit-2", …
    property var ids: ["postit"]

    // id -> text. Replaced wholesale rather than assigned into, for the reason
    // WidgetLayout.place() documents: assigning to a MEMBER of a var does not
    // emit the change signal, so every note on screen would keep painting the
    // text it had when it was created.
    property var notes: ({})

    // Which note the editor is open on; "" is nobody. A string rather than a
    // bool now that there is more than one note to be in.
    property string editing: ""

    function path(id)   { return root.dir + "/" + id + ".txt" }
    function textOf(id) { const t = root.notes[id]; return t === undefined ? "" : t }
    function indexOf(id) { return Math.max(0, root.ids.indexOf(id)) }

    /*
     * Where a note sits until somebody drags it: a grid filling rightwards
     * along the bottom-left corner, which is the one corner nothing else claims
     * (quick-launch is top-left, the system monitor top-right, the big clock
     * bottom-right).
     *
     * Laid out rather than stacked, which is what this was first: a cascade
     * puts every note but the newest under the newest, and the strip of an
     * older note left showing is the one strip with no writing on it. Three to
     * a row is what fits beside the start menu on the narrowest screen anybody
     * runs this on; the fourth note starts a row above, which is a direction
     * that never runs out.
     *
     * The numbers are the card's own — 264 wide and 168 of body under 43 of
     * chrome — plus a 20px gutter. Both the note and its editor resolve their
     * position through here, so the editor opens over the note it belongs to
     * without the note having to tell it anything.
     */
    readonly property int columns: 3

    function homeX(id) { return 20 + 284 * (indexOf(id) % columns) }
    function homeY(id) {
        // Clear of the visualiser when both are on, on the same terms and with
        // the same number BigClock uses at the other end of that strip.
        const base = WidgetState.visualizer ? 124 : 24
        return base + 227 * Math.floor(indexOf(id) / columns)
    }

    // ── The notes on disk ────────────────────────────────────
    /*
     * The directory listing IS the note list. Qt watches the folder, so a note
     * created or removed from a terminal shows up on the desktop with no
     * restart — the same live-file arrangement everything else here has.
     *
     * A missing ~/.config/synui is the normal state on a fresh install and is
     * not an error: nothing here creates it and nothing needs to, because the
     * only way to switch the widget on is `synui-widgets postit on`, which
     * mkdir -p's it to write widgets.state. By the time there is a note to
     * click, the directory the note lands in exists.
     */
    property FolderListModel scan: FolderListModel {
        folder: "file://" + root.dir
        nameFilters: ["postit.txt", "postit-*.txt"]
        showDirs: false
        showHidden: false
        onCountChanged: root.rescan()
        onStatusChanged: if (status === FolderListModel.Ready) root.rescan()
    }

    function rescan() {
        const found = []
        for (let i = 0; i < scan.count; i++) {
            const name = String(scan.get(i, "fileName"))
            const id = name.slice(0, -4)                       // drop ".txt"
            if (id === "postit" || /^postit-[0-9]+$/.test(id)) found.push(id)
        }
        // The first note is not a file, it is a fact. See the header.
        if (found.indexOf("postit") < 0) found.push("postit")
        found.sort(function (a, b) { return root.serial(a) - root.serial(b) })

        // Only replace the list if it actually changed. Handing out a new array
        // rebuilds every note's window and its FileView, and the folder watcher
        // fires for things that are none of our business — including our own
        // atomic writes landing.
        if (found.length === root.ids.length) {
            let same = true
            for (let j = 0; j < found.length; j++)
                if (found[j] !== root.ids[j]) { same = false; break }
            if (same) return
        }
        root.ids = found
    }

    // "postit" is note 1; every other id carries its number after the dash.
    function serial(id) { return id === "postit" ? 1 : parseInt(id.slice(7), 10) }

    /*
     * One FileView per note, kept here rather than in the widget for the reason
     * in the header. The pool follows `ids`, so a note added or removed brings
     * its watcher with it.
     */
    property Instantiator pool: Instantiator {
        model: root.ids
        delegate: PostItFile {
            // Declared, not inherited from the delegate's context. A delegate
            // that already has a required property of its own does not get
            // `modelData` handed to it implicitly — it reads as undefined, the
            // whole pool builds against a path of "undefined.txt", and every
            // note on the desktop shows as empty with no error but a warning.
            required property string modelData
            noteId: modelData
        }
    }

    /*
     * id -> FileView, filled in by the pool as it builds. Deliberately mutated
     * in place, which is the opposite of what `notes` does above and safe for
     * the opposite reason: nothing binds to this, it is only ever looked up
     * from a function, so there is no binding to leave stale.
     */
    property var views: ({})

    function registerView(id, view) { root.views[id] = view }

    // The OUTGOING view has to be named, not just its note. A change to `ids`
    // can rebuild the whole pool rather than the one entry that moved, and then
    // the survivor's replacement registers before the original is torn down — a
    // blind delete here would leave that note with no writer and saves that
    // silently did nothing.
    function unregisterView(id, view) {
        if (root.views[id] === view) delete root.views[id]
    }

    // What a note's file says, on load and on every outside change to it.
    function adopt(id, text) {
        if (root.notes[id] === text) return
        const next = {}
        for (const k in root.notes) next[k] = root.notes[k]
        next[id] = text
        root.notes = next
    }

    function save(id, text) {
        if (id === "") return
        if (text === root.notes[id]) return   // nothing to write, and no reload to cause
        adopt(id, text)
        const v = root.views[id]
        if (v) v.setText(text)
    }

    // ── Making the file ──────────────────────────────────────
    /*
     * A note IS a file, and it has to be one before it can be written to: a
     * FileView pointed at a path that does not exist drops the write on the
     * floor, with no `saved` and no `saveFailed` to notice (see PostItFile).
     * So an empty file is created up front rather than waiting for the first
     * save to make one — waiting is what silently loses the first thing anybody
     * ever types on a fresh install, where postit.txt does not exist yet.
     *
     * Once, per note. `born` is what stops a note deleted with rm from coming
     * straight back: its file going missing after we have seen it means it was
     * removed, not that it was never made. `postit` is exempt because it is the
     * unconditional note — it exists whether or not anybody wants it to, so a
     * missing file there is always something to fix rather than a deletion to
     * respect.
     */
    property var born: ({})

    function noteFileSeen(id) { root.born[id] = true }

    function ensureFile(id) {
        if (root.born[id] && id !== "postit") return
        root.born[id] = true
        if (root.ids.indexOf(id) < 0) return
        Quickshell.execDetached(["touch", root.path(id)])
    }

    // ── Adding and removing ──────────────────────────────────
    function add() {
        let n = 2
        while (root.ids.indexOf("postit-" + n) >= 0) n++
        const id = "postit-" + n

        adopt(id, "")
        root.ids = root.ids.concat([id])
        ensureFile(id)

        // Open on it. The point of asking for a note is having somewhere to
        // write, and an empty card you then have to click is a second step for
        // nothing.
        root.editing = id
    }

    /*
     * Removing takes the file with it — anything less would leave a note that
     * came back on the next rescan. The last note is not removable: it is the
     * only thing on the desktop carrying the + that makes another one.
     *
     * execDetached rather than a Process: there is nothing to read back, and a
     * shared Process object would have to queue two deletions in a row.
     */
    function remove(id) {
        if (root.ids.length <= 1) return
        if (root.editing === id) root.editing = ""

        root.ids = root.ids.filter(function (x) { return x !== id })

        const next = {}
        for (const k in root.notes) if (k !== id) next[k] = root.notes[k]
        root.notes = next

        // Don't leave a position behind for a note that is gone: the id is
        // reusable, and the next note-2 would open wherever the last one was
        // dropped rather than at its own corner. Its birth is forgotten for the
        // same reason — an id remembered as already made is an id whose file
        // would never be created when it came round again, and a note with no
        // file cannot be written to.
        WidgetLayout.clear(id)
        delete root.born[id]

        Quickshell.execDetached(["rm", "-f", root.path(id)])
    }
}
