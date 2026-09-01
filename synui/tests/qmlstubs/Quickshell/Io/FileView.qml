import QtQuick
QtObject {
    property string path: ""
    property bool blockLoading: false
    property bool printErrors: true
    // XMLHttpRequest reads a local file:// URL synchronously when async=false,
    // which is the same contract FileView.text() offers under blockLoading.
    function text() {
        if (path === "") return ""
        try {
            const x = new XMLHttpRequest()
            x.open("GET", "file://" + path, false)
            x.send(null)
            return x.responseText || ""
        } catch (e) { return "" }
    }
}
