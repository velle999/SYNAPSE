import QtQml
import Quickshell.Io

/*
 * PostItFile — one note's text file, watched.
 *
 * There is one of these per note, built by PostItState's pool, and it exists
 * only because a FileView has to be a real object with a real `path` and there
 * are now N of them. Everything a note IS lives in PostItState; this is the
 * plumbing that keeps one of its entries and one file on disk agreeing.
 *
 * It does not own the text. The store does, so that a note being drawn on three
 * monitors is one string and not three.
 */
QtObject {
    id: file

    required property string noteId

    property FileView view: FileView {
        path: PostItState.path(file.noteId)

        /*
         * Watched and written atomically, the same arrangement bar.json has and
         * for the same two reasons: the watch means editing the note in a
         * terminal shows up on the desktop with no restart, and atomic writes
         * mean the watch can never catch a half-written note and paint it.
         */
        watchChanges: true
        atomicWrites: true

        // An empty note is not merely a possible state, it is the state every
        // new note starts in, and a note added this second has no file until
        // the write lands. Same call LauncherStyle.qml makes about a missing
        // synuirc: a warning per session for the documented default is noise.
        printErrors: false

        onFileChanged: reload()

        onLoaded: {
            PostItState.adopt(file.noteId, this.text())
            PostItState.noteFileSeen(file.noteId)
        }

        // No file is a state, not an error, and it is the one state a note
        // cannot be left in: a FileView will not write to a path that does not
        // exist — no `saved`, no `saveFailed`, the write simply never happens —
        // so a note whose file is missing is a note that silently refuses to
        // save. Making the file is what makes the note writable.
        onLoadFailed: {
            PostItState.adopt(file.noteId, "")
            PostItState.ensureFile(file.noteId)
        }
    }

    // The store writes through the view, and can only find it if it is told.
    Component.onCompleted:   PostItState.registerView(file.noteId, file.view)
    Component.onDestruction: PostItState.unregisterView(file.noteId, file.view)
}
