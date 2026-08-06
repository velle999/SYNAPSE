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
 * Colours are fixed terminal green rather than themed — see the palette block
 * below for what following ~/.config/synui/theme.json actually did to this app.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
FloatingWindow {
    id: root

    title: "SYNAPSE Arsenal"
    implicitWidth: 1040
    implicitHeight: 680

    readonly property string query: Quickshell.env("SYN_ARSENAL_QUERY")
                                    || "/usr/lib/syn-arsenal/arsenal-query"

    // ── Palette ─────────────────────────────────────────────────────────────
    // Fixed, not themed. This used to take its ink from ~/.config/synui/theme.json,
    // but that file carries the DESKTOP's scheme, and Arsenal draws on its own
    // dark surfaces regardless. Under a light theme theme.json says fg
    // "#000000" — black text on this near-black background — while bg/panel,
    // which it does not write at all, kept falling back to the dark defaults.
    // The result was an app that looked permanently empty.
    //
    // The accent was worse, and was the reason clicking a category appeared to
    // do nothing. theme.json writes accent/glyph/bar/popup as [r,g,b] ARRAYS
    // (Theme.qml converts them in themed()) and only fg/clockFg as hex strings.
    // This file read accent as a string, so a QML `color` was handed a JS array,
    // failed to convert, and every accented thing — the selected-category
    // highlight, the installed dot, the Install button and its border — painted
    // as nothing. The click landed and the pane repainted; none of it was
    // visible.
    //
    // Phosphor green on black is what a BlackArch browser should have looked
    // like anyway, and it cannot be knocked out by a theme switch.
    readonly property color cBg:     "#0a0e0a"
    readonly property color cPanel:  "#111a11"
    readonly property color cText:   "#33ff33"   // shell green — body text
    readonly property color cDim:    "#3fae55"   // dimmed green — counts, descriptions
    readonly property color cAccent: "#7dffb0"   // brighter mint — selection, buttons
    // Amber, not green: the keyring warning carries MEANING, and a warning
    // rendered in the body colour is not a warning. Same reason Theme.qml keeps
    // its green/red status colours out of the themed set.
    readonly property color cWarn:   "#e0af68"

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
