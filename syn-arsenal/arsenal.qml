//@ pragma UseQApplication
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * SYNAPSE Arsenal — a category browser for the BlackArch repository.
 *
 * Two panes: the ~50 blackarch-* groups on the left, that group's packages on
 * the right. Every fact on screen comes from arsenal-query, the same backend
 * `syn arsenal` uses, so the GUI and the TUI can never disagree about what is
 * installed.
 *
 * TSV, not JSON, across that boundary — see arsenal-query for why. Parsing it
 * here is a split on tab and a split on newline.
 *
 * Colours follow ~/.config/synui/theme.json (the file synui-apply-theme writes),
 * SURFACES included — see the palette block below for why taking only the ink
 * from it is what made this window unreadable.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
FloatingWindow {
    id: root

    title: "SYNAPSE Arsenal"
    implicitWidth: 1040
    implicitHeight: 680

    // Closing the window must END the process. quickshell's root object outlives
    // its window: destroying the FloatingWindow leaves `quickshell` alive owning
    // nothing — invisible, unreachable, and still holding whatever the QML had
    // open. Measured on a live session: an Arsenal closed eight minutes earlier
    // was still running with no window anywhere in `synctl clients`, and every
    // open-and-close leaves another one.
    //
    // synpkg, synfiles and syn-update all carry this line; this window was the
    // one that did not.
    onClosed: Qt.quit()

    readonly property string query: Quickshell.env("SYN_ARSENAL_QUERY")
                                    || "/usr/lib/syn-arsenal/arsenal-query"

    // ── Palette ─────────────────────────────────────────────────────────────
    // Themed, but the SURFACES are themed too, and that is the whole point.
    //
    // This file used to take only its ink from theme.json while drawing on its
    // own hardcoded dark surfaces. theme.json carries the DESKTOP's scheme, so
    // under a light theme it handed over fg "#000000" — black text on a
    // near-black background — and the window looked permanently empty. Ink and
    // surface have to come from the same place or they can disagree.
    //
    // theme.json does not write a bg/panel key at all: Theme.qml builds its
    // surfaces from "bar" and "popup". So do we, and the second surface is
    // derived from the first rather than invented, which keeps the two panes
    // distinguishable under any theme instead of only under the dark default.
    property var p: ({})
    readonly property bool isLight: p.scheme === "light"

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/theme.json"
        watchChanges: true
        // synui-apply-theme writes a temp file and renames it, so this fires
        // once, on a complete palette — never on a half-written one.
        onFileChanged: reload()
        onLoaded: { try { root.p = JSON.parse(this.text()) } catch (e) { root.p = ({}) } }
        onLoadFailed: root.p = ({})
    }

    // theme.json writes accent/glyph/bar/popup as [r,g,b] ARRAYS and only
    // fg/clockFg as hex strings. Reading an array key as a string was the
    // second bug here: a QML `color` handed a JS array fails to convert, so the
    // selected-category highlight, the installed dot, and the Install button
    // with its border all painted as nothing — a click landed, the pane
    // repainted, and none of it was visible. Same converter as Theme.qml.
    function themed(key, r, g, b, a) {
        const c = root.p[key]
        return (c && c.length === 3) ? Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, a)
                                     : Qt.rgba(r / 255, g / 255, b / 255, a)
    }
    function pick(dark, light) { return root.isLight ? light : dark }

    // WCAG relative luminance. QML colour channels are already 0..1.
    function lum(c) {
        function ch(v) { return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b)
    }
    function contrast(a, b) {
        const la = lum(a), lb = lum(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }

    // Push a colour away from the surface it sits on until it carries, keeping
    // its hue — the hue is the part the user chose. Iterative rather than one
    // fixed multiplier because Qt.lighter scales the HSV VALUE: a near-black
    // accent times any single constant is still near-black. Qt drops saturation
    // once value saturates, so this converges toward white on a dark surface,
    // and the loop bound catches pure black, where scaling can never move.
    function readable(c, on, want) {
        if (contrast(c, on) >= want) return c
        const up = lum(on) <= 0.18
        let out = c
        for (let i = 0; i < 16; i++) {
            out = up ? Qt.lighter(out, 1.25) : Qt.darker(out, 1.25)
            if (contrast(out, on) >= want) return out
        }
        return up ? "#ffffff" : "#000000"
    }

    // ── Surfaces ────────────────────────────────────────────────────────────
    // "bar" goes to the CHROME (header, category pane, search bar), because
    // that is what the key means on the desktop — it is the colour of the strip
    // synui draws. The content area then steps off it in whichever direction
    // the scheme leaves room: lighter on light, darker on dark. Taking "bar"
    // for the window background instead would paint a full-window slab of
    // win95 silver and leave the chrome nowhere to go.
    readonly property color cPanel: themed("bar", 11, 11, 20, 1.0)
    readonly property color cBg: isLight ? Qt.lighter(cPanel, 1.15) : Qt.darker(cPanel, 1.4)

    // ── Ink ─────────────────────────────────────────────────────────────────
    readonly property color cInk: p.fg ? Qt.color(p.fg) : pick("#e6e9ef", "#12141a")
    // The guard that makes the original bug unrepresentable. A theme whose fg
    // does not carry against our background gets a computed ink instead, so
    // this window can never again render text it cannot show. 4.5:1 is the WCAG
    // AA threshold for body text.
    readonly property color cText: contrast(cInk, cBg) >= 4.5
                                   ? cInk
                                   : (lum(cBg) > 0.18 ? "#12141a" : "#e6e9ef")

    // Deliberately below body contrast — descriptions and counts are secondary
    // and should read as such — but still above the 3:1 floor on both schemes.
    readonly property color cDim: pick("#8b93a7", "#4a5568")

    readonly property color cAccentRaw: themed("accent", 78, 201, 176, 1.0)
    // The accent is drawn as TEXT — the title, the Install label — and not just
    // as a wash, so it has to carry. Measured against cPanel because that is
    // the harder of the two surfaces either way: on a light scheme cBg is the
    // lighter one, on a dark scheme it is the darker one, so whichever
    // direction the accent needs to go, the chrome is what it has to clear.
    //
    // 4.5 and not 3.0: the title is 18px bold, which is just under the 18.66px
    // WCAG counts as large text, so it is held to the body-text threshold. The
    // washes derived from this colour are alpha-blended and do not care that it
    // may have been pushed a shade further than they needed.
    readonly property color cAccent: readable(cAccentRaw, cPanel, 4.5)

    // Not themed, because this carries MEANING rather than style: a keyring
    // warning rendered in the accent the user picked is not a warning. Only the
    // scheme switches it, since a pastel amber on a silver panel lands around
    // 2:1 and the warning is the one line here you cannot afford to lose — the
    // same reason Theme.qml keeps its green/red status colours out of the
    // themed set. The light value is burnt rather than bright for that reason.
    readonly property color cWarn: pick("#e0af68", "#5c3a00")

    // ── State ───────────────────────────────────────────────────────────────
    property string repoState: "loading"   // loading|enabled|unsynced|disabled
    property string keyring: ""
    property int    totalPkgs: 0
    property string currentGroup: ""
    property string filter: ""
    property string busy: ""               // package name currently (un)installing

    property var categories: []            // [{name, count}]
    property var packages: []              // [{name, installed, version, desc}]

    readonly property var shownPackages: {
        if (filter === "") return packages
        const f = filter.toLowerCase()
        return packages.filter(p => p.name.toLowerCase().includes(f)
                                 || p.desc.toLowerCase().includes(f))
    }

    // ── Backend ─────────────────────────────────────────────────────────────
    Process {
        id: statusProc
        running: true
        command: [root.query, "status"]
        stdout: StdioCollector {
            onStreamFinished: {
                const f = this.text.trim().split("\t")
                root.repoState = f[0] || "disabled"
                root.totalPkgs = parseInt(f[1] || "0")
                root.keyring   = f[2] || ""
                if (root.repoState === "enabled") catProc.running = true
            }
        }
    }

    Process {
        id: catProc
        command: [root.query, "categories"]
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                for (const line of this.text.split("\n")) {
                    if (!line) continue
                    const f = line.split("\t")
                    out.push({ name: f[0], count: parseInt(f[1] || "0") })
                }
                root.categories = out
            }
        }
    }

    Process {
        id: pkgProc
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                for (const line of this.text.split("\n")) {
                    if (!line) continue
                    const f = line.split("\t")
                    out.push({ name: f[0], installed: f[1] === "1",
                               version: f[2] || "", desc: f[3] || "" })
                }
                root.packages = out
            }
        }
    }

    // Mutations re-read the group rather than patching the row in place: pacman
    // may have pulled dependencies in, and a list that disagrees with the disk
    // is worse than a slightly slower refresh.
    Process {
        id: actProc
        onExited: { root.busy = ""; root.openGroup(root.currentGroup) }
    }

    function openGroup(g) {
        if (!g) return
        currentGroup = g
        packages = []
        pkgProc.command = [query, "packages", g]
        pkgProc.running = true
    }

    function act(verb, pkg) {
        if (busy !== "") return
        busy = pkg
        actProc.command = [query, verb, pkg]
        actProc.running = true
    }

    Rectangle {
        anchors.fill: parent
        color: root.cBg

        // ── Header ──────────────────────────────────────────────────────────
        Rectangle {
            id: header
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 56
            color: root.cPanel

            Text {
                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                text: "SYNAPSE Arsenal"
                color: root.cAccent
                font { pixelSize: 18; bold: true }
            }
            Text {
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                color: root.keyring === "missing" ? root.cWarn : root.cDim
                font.pixelSize: 12
                text: {
                    if (root.repoState === "loading")  return "checking repository…"
                    if (root.repoState === "disabled") return "BlackArch not enabled"
                    if (root.repoState === "unsynced") return "configured, never synced — run pacman -Sy"
                    return root.totalPkgs + " packages"
                         + (root.keyring === "missing" ? "  ·  blackarch-keyring missing" : "")
                }
            }
        }

        // ── Repo absent: say what to do, not just that the list is empty ────
        Item {
            anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            visible: root.repoState === "disabled" || root.repoState === "unsynced"

            Column {
                anchors.centerIn: parent
                spacing: 10
                Text {
                    text: root.repoState === "disabled"
                          ? "The BlackArch repository is not enabled."
                          : "BlackArch is configured but has never been synced."
                    color: root.cText; font.pixelSize: 15
                }
                Text {
                    text: root.repoState === "disabled"
                          ? "sudo syn arsenal --enable-repo"
                          : "sudo pacman -Sy"
                    color: root.cAccent; font { pixelSize: 13; family: "monospace" }
                }
            }
        }

        // ── Categories ──────────────────────────────────────────────────────
        Rectangle {
            id: catPane
            visible: root.repoState === "enabled"
            anchors { top: header.bottom; left: parent.left; bottom: parent.bottom }
            width: 250
            color: root.cPanel

            ListView {
                anchors.fill: parent
                anchors.topMargin: 6
                clip: true
                model: root.categories
                spacing: 1

                delegate: Rectangle {
                    id: catRow
                    required property var modelData
                    readonly property bool current: catRow.modelData.name === root.currentGroup

                    width: ListView.view.width
                    height: 30
                    color: catRow.current
                           ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.16)
                           // Hover tints with the accent rather than plain white:
                           // a grey wash on a phosphor palette reads as dust.
                           : (ma.containsMouse
                              ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.08)
                              : "transparent")

                    Text {
                        anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                        // Every group is "blackarch-<thing>"; the prefix is noise
                        // repeated 50 times down the pane.
                        text: catRow.modelData.name.replace("blackarch-", "")
                        color: catRow.current ? root.cAccent : root.cText
                        font.pixelSize: 13
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        text: catRow.modelData.count
                        color: root.cDim
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: ma
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root.filter = ""; root.openGroup(catRow.modelData.name) }
                    }
                }
            }
        }

        // ── Packages ────────────────────────────────────────────────────────
        Item {
            visible: root.repoState === "enabled"
            anchors {
                top: header.bottom; left: catPane.right
                right: parent.right; bottom: parent.bottom
            }

            Rectangle {
                id: searchBar
                anchors { top: parent.top; left: parent.left; right: parent.right }
                anchors.margins: 10
                height: 30
                radius: 4
                color: root.cPanel
                visible: root.currentGroup !== ""

                TextInput {
                    id: searchInput
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.cText
                    font.pixelSize: 13
                    clip: true
                    onTextChanged: root.filter = text
                }
                Text {
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    text: "filter packages…"
                    color: root.cDim
                    font.pixelSize: 13
                    visible: searchInput.text === ""
                }
            }

            Text {
                anchors.centerIn: parent
                visible: root.currentGroup === ""
                text: "Pick a category"
                color: root.cDim
                font.pixelSize: 15
            }

            ListView {
                anchors {
                    top: searchBar.bottom; topMargin: 6
                    left: parent.left; right: parent.right; bottom: parent.bottom
                }
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                clip: true
                visible: root.currentGroup !== ""
                model: root.shownPackages
                spacing: 2

                delegate: Rectangle {
                    id: pkgRow
                    required property var modelData
                    width: ListView.view.width
                    height: 46
                    radius: 4
                    color: rowMa.containsMouse
                           ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.07)
                           : "transparent"

                    MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true }

                    Rectangle {
                        id: dot
                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 7; height: 7; radius: 4
                        color: pkgRow.modelData.installed ? root.cAccent : "transparent"
                        border { width: pkgRow.modelData.installed ? 0 : 1; color: root.cDim }
                    }

                    Column {
                        anchors {
                            left: dot.right; leftMargin: 12
                            right: actionBtn.left; rightMargin: 12
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: 2
                        Text {
                            text: pkgRow.modelData.name
                            color: root.cText
                            font { pixelSize: 13; bold: true }
                        }
                        Text {
                            width: parent.width
                            text: pkgRow.modelData.desc
                            color: root.cDim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }

                    Rectangle {
                        id: actionBtn
                        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 78; height: 26; radius: 4
                        color: btnMa.containsMouse
                               ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.25)
                               : Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.12)
                        border { width: 1; color: root.cAccent }
                        opacity: root.busy === "" || root.busy === pkgRow.modelData.name ? 1 : 0.4

                        Text {
                            anchors.centerIn: parent
                            color: root.cAccent
                            font.pixelSize: 11
                            text: root.busy === pkgRow.modelData.name
                                  ? "working…"
                                  : (pkgRow.modelData.installed ? "Remove" : "Install")
                        }
                        MouseArea {
                            id: btnMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.act(pkgRow.modelData.installed ? "remove" : "install",
                                                pkgRow.modelData.name)
                        }
                    }
                }
            }
        }
    }
}
