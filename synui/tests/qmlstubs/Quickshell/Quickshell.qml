pragma Singleton
import QtQuick
QtObject {
    // The real Quickshell.env() reads the process environment. Qt exposes no
    // direct accessor, so a probe sets this map before it first touches I18n —
    // which is when the singleton is built and its language binding evaluates.
    property var _env: ({})
    function env(name) {
        const v = _env[name]
        return (v === undefined || v === null) ? "" : v
    }

    /*
     * path -> file contents, for the stub FileView beside this.
     *
     * ⛔ THE CONTENT IS INJECTED, NOT READ OFF DISK, AND THAT IS DELIBERATE.
     * The first version of the stub read the path with a synchronous
     * XMLHttpRequest, which returns EMPTY for a file:// URL in this
     * configuration — so every assertion that needed a catalog silently got no
     * catalog, fell back to English, and was compared against English. Seven of
     * them passed for that reason and proved nothing.
     *
     * ⚠ AND READING THE DISK WAS NEVER THIS SUITE'S JOB. Fetching bytes from a
     * path is quickshell's FileView, which is quickshell's to test; what is
     * ours is what I18n.qml does with those bytes, and which path it asks for —
     * and the path is asserted separately, against the string. Injecting the
     * content tests exactly our half and cannot pass by accident.
     */
    property var _files: ({})
}
