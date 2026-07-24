pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * Which dress the start button wears: the "◢ SYNAPSE" wordmark, or the ◢ caret
 * beside the dendrite emblem.
 *
 * The setting still lives in the COMPOSITOR — `launcher_style` is a bind action
 * (input.c), reachable from the control panel, the start menu's Settings page
 * and `synctl dispatch launcher_style`. All it does now is flip the value and
 * write launcher.state; the button itself moved to the bar, so this watches that
 * file instead of the compositor redrawing a scene buffer.
 *
 * PRECEDENCE MIRRORS config.c, deliberately. synuirc's `launcher_style` is the
 * fallback and launcher.state is laid over it, so deleting the state file hands
 * control back to synuirc — exactly what config.c documents. Reading only the
 * state file would look right on every box that has ever toggled the setting and
 * silently disagree with the compositor on one that has not.
 */
QtObject {
    id: root

    // The compiled default is TEXT (config.c). Nothing SYNAPSE ships sets
    // launcher_style, so on a stock install this is the answer.
    property bool logo: false

    property string rcStyle:    ""   // from synuirc, "" = unset
    property string stateStyle: ""   // from launcher.state, "" = no file

    function resolve() {
        const s = root.stateStyle !== "" ? root.stateStyle : root.rcStyle
        root.logo = (s === "logo")
    }

    onRcStyleChanged:    root.resolve()
    onStateStyleChanged: root.resolve()

    function parseRc(text) {
        // Last assignment wins, matching config.c's line-by-line parse.
        let found = ""
        for (const line of text.split("\n")) {
            const m = line.match(/^\s*launcher_style\s*=\s*(\S+)/)
            if (m) found = m[1]
        }
        return found
    }

    // SYNUI_CONFIG overrides everything (the test harness uses it), then the
    // user's file, then the system one — config.c's `paths[3]`, same order.
    readonly property string rcPath: {
        const env = Quickshell.env("SYNUI_CONFIG")
        if (env) return env
        const home = Quickshell.env("HOME")
        return home ? home + "/.config/synui/synuirc" : "/etc/synui/synuirc"
    }

    property FileView rcFile: FileView {
        path: root.rcPath
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded:     root.rcStyle = root.parseRc(this.text())
        // No user synuirc is the normal case, not an error — fall through to the
        // system one rather than leaving the fallback unread.
        onLoadFailed: sysRcFile.reload()
    }

    // printErrors off on both: "no synuirc here" is the normal case on a box
    // that never wrote one, and a WARN on every bar start for an expected miss
    // trains you to ignore the log.
    property FileView sysRcFile: FileView {
        path: "/etc/synui/synuirc"
        printErrors: false
        onLoaded:     root.rcStyle = root.parseRc(this.text())
        onLoadFailed: root.rcStyle = ""
    }

    property FileView stateFile: FileView {
        path: (Quickshell.env("HOME") || "") + "/.config/synui/launcher.state"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const m = this.text().match(/^\s*style\s*=\s*(\S+)/m)
            root.stateStyle = m ? m[1] : ""
        }
        onLoadFailed: root.stateStyle = ""
    }
}
