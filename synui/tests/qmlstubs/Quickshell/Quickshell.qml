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
}
