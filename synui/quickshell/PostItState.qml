pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * PostItState — the text on the desktop note, and whether it is being edited.
 *
 * A singleton for the same reason MenuState is one: the note is drawn by one
 * window and typed into by another, and there is exactly ONE note however many
 * monitors are plugged in. Putting the FileView in the widget would give a
 * three-monitor desktop three watchers of the same file and three writers of it.
 *
 * The file is plain text at ~/.config/synui/postit.txt — not a state file with a
 * format, because the whole content is the user's and anything that reads or
 * edits text should be able to work with it.
 */
QtObject {
    id: root

    // What is saved. The editor deliberately does NOT bind to this; see
    // PostItEditor.qml.
    property string note: ""

    // Whether the editor surface is mapped. Only the editor and the note's
    // click handler touch it.
    property bool editing: false

    /*
     * Watched and written atomically, the same arrangement bar.json has and for
     * the same two reasons: the watch means editing the file in a terminal
     * shows up on the desktop with no restart, and atomic writes mean the watch
     * can never catch a half-written note and paint it.
     *
     * Nothing here creates ~/.config/synui, and nothing needs to: the widget is
     * off by default and the only way to switch it on is `synui-widgets postit
     * on`, which mkdir -p's that directory to write widgets.state. So by the
     * time there is a note to click, the directory the note lands in exists.
     */
    property FileView noteFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/postit.txt"
        watchChanges: true
        atomicWrites: true
        // An empty note is not merely a possible state, it is the state every
        // post-it starts in — the file does not exist until the first save. Same
        // call LauncherStyle.qml makes about a missing synuirc: a warning per
        // session for the documented default is noise.
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.note = this.text()
        // No file at all is the normal case: nobody has written anything yet.
        onLoadFailed: root.note = ""
    }

    function save(text) {
        if (text === root.note) return   // nothing to write, and no reload to cause
        root.note = text
        noteFile.setText(text)
    }
}
