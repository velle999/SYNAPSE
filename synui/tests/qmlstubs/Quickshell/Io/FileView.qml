import QtQuick
import Quickshell

QtObject {
    property string path: ""
    property bool blockLoading: false
    property bool printErrors: true

    // The blocking read quickshell's FileView offers under blockLoading. The
    // content comes from Quickshell._files — see the comment there for why it
    // is injected rather than read off disk.
    function text() {
        const v = Quickshell._files[path]
        return (v === undefined || v === null) ? "" : v
    }
}
