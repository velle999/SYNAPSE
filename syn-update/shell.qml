//@ pragma UseQApplication

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io

/*
 * SynapseOS Updates — a window around `syn-update`.
 *
 * quickshell rather than GTK or a Qt C++ app because it is what the rest of
 * this desktop is already built on (the bar, the start menu, the OSD) and
 * because it costs NOTHING new on the ISO: quickshell and qt6-declarative are
 * both already there, the latter dragged in by Dolphin's Qt6/KF6 stack. A GTK4
 * app would have been a second toolkit for one window, and plain `qml` cannot
 * launch a process at all — Quickshell.Io's Process is the whole reason this
 * can be QML instead of C++.
 *
 * WHY "Install updates" OPENS A TERMINAL
 *
 * `syn-update apply` drives build-all.sh, which runs `sudo pacman -U` in the
 * middle of the build. sudo with no controlling terminal cannot prompt, so
 * running apply inside this window would fail at the install step every time,
 * several minutes into a build. The alternatives are worse: a NOPASSWD sudoers
 * rule for `pacman -U` is a rule that lets any package be installed as root,
 * which is a privilege-escalation hole, not a convenience. So the window owns
 * the read-only half — what is available, what changed, why something is not
 * updatable — and hands the privileged half to a terminal where sudo can do its
 * job and the build is visible while it runs.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
ShellRoot {
    id: root

    /* ── palette ────────────────────────────────────────────
     * From ~/.config/synui/theme.json, which synui-apply-theme rewrites on every
     * theme switch — the same file the bar reads. This window used to hard-code
     * synui's dark chrome, so the updater was a slab of navy in the middle of a
     * beige XP desktop or a Gruvbox one: the one window on the system that no
     * theme could touch.
     *
     * Read here rather than by importing the bar's Theme singleton, because that
     * singleton lives in synui's package and this is a different one — an import
     * path across packages breaks the moment either is installed alone. The
     * contract is the JSON, not the QML.
     *
     * Every colour falls back to the old hard-coded value, so a box that has
     * never applied a theme (a fresh install, the live ISO) looks exactly as it
     * did. NOTE the two shapes in that file: accent/bar/popup are [r,g,b]
     * ARRAYS, fg is a "#rrggbb" STRING. Handing an array to a QML colour paints
     * nothing at all, silently.
     */
    property var pal: ({})
    readonly property bool palLight: pal.scheme === "light"

    property FileView paletteFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/theme.json"
        watchChanges: true
        // Written as a temp file and renamed, so this never fires on a partial
        // palette — and the window restyles live, without a relaunch.
        onFileChanged: reload()
        onLoaded: {
            try { root.pal = JSON.parse(this.text()) }
            catch (e) { root.pal = ({}) }   // half a palette is worse than none
        }
        onLoadFailed: root.pal = ({})
    }

    // [r,g,b] 0..255 → colour, or `fb` when the key is missing or malformed.
    function rgbOf(key, fb) {
        const c = root.pal[key]
        return (c && c.length === 3) ? Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, 1)
                                     : fb
    }
    // t is a position from `a` toward `b`, and is allowed to go NEGATIVE — that
    // is how the sunken pane is expressed: a step AWAY from the ink, which is
    // darker than the surface on a dark theme and lighter on a light one, the
    // same way a KDE view sinks under a dark window and lifts under a pale one.
    function mix(a, b, t) {
        function ch(x, y) { return Math.max(0, Math.min(1, x + (y - x) * t)) }
        return Qt.rgba(ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), 1)
    }

    readonly property color cBg:      rgbOf("popup", "#11151c")
    // The panel and the rules are positions between the surface and the ink, so
    // they invert with the theme instead of staying a fixed dark slate.
    readonly property color cPanel:   mix(cBg, cText, 0.06)
    readonly property color cLine:    mix(cBg, cText, 0.18)
    readonly property color cText:    root.pal.fg ? root.pal.fg : "#dbe4ee"
    readonly property color cDim:     mix(cBg, cText, 0.58)
    readonly property color cAccent:  rgbOf("accent", "#38bdf8")
    // Success and warning carry meaning, so they are picked per scheme rather
    // than derived — the dark pair is unreadable on a light surface.
    readonly property color cOk:      palLight ? "#1a7f3d" : "#5ee68a"
    readonly property color cWarn:    palLight ? "#8a5a00" : "#f2b45c"
    // The log pane sits below the surface rather than at a fixed near-black.
    readonly property color cSunken:  mix(cBg, cText, -0.03)

    /* ── the UI font ────────────────────────────────────────
     * ~/.config/synui/font.state, written by synui-apply-font(1), is the
     * desktop-wide font setting — deliberately NOT a key in theme.json,
     * because the font outlives a theme switch. It carries the family AND
     * the text scale, and an app that honours one without the other still
     * looks wrong beside its siblings.
     *
     * Qt resolves an application's default font ONCE at startup, so BOTH
     * have to be bindings on every Text: a window that merely inherits the
     * app font keeps the face it launched with, and the control panel's
     * font picker appears to do nothing here while Settings and Files
     * follow it immediately. That is exactly what this window did — it read
     * font.state nowhere at all — and it is the same gap Arsenal and
     * Software had before 9ccefbd.
     *
     * Only pixelSize is scaled, never a width; the few heights that exist
     * solely to hold N lines of text are scaled too, or the card clips its
     * own contents at 150%.
     */
    property string uiFont: ""
    property int    textScale: 100
    function ui(px) { return Math.max(6, Math.round(px * root.textScale / 100)) }

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/font.state"
        watchChanges: true
        // No font.state is the normal case on a box where nobody has picked a
        // font; a warning per start for an expected miss is how a log becomes
        // something nobody reads.
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const t = this.text()
            const m = t.match(/^\s*family\s*=\s*(.+?)\s*$/m)
            root.uiFont = m ? m[1] : ""
            const s = t.match(/^\s*scale\s*=\s*(\d+)\s*$/m)
            root.textScale = s ? parseInt(s[1]) : 100
        }
        onLoadFailed: { root.uiFont = ""; root.textScale = 100 }
    }

    property bool   busy:     false
    property string statusLine: "Checking for updates…"
    property string revision: ""
    property string logText:  ""
    property bool   upToDate: false

    property var updates: []    // [{name, from, to}]
    // Components in the tree that are NOT installed here. A separate list from
    // updates because they are a different action — this ADDS software rather
    // than moving it forward — and because they arrive in a different shape:
    // there is no "from" version to show.
    property var fresh: []      // [{name, to}]
    property var commits: []    // ["hash subject"]
    property var blocked: []    // ["name  reason"]

    function appendLog(t) {
        if (!t) return
        root.logText += t.endsWith("\n") ? t : t + "\n"
    }

    /*
     * Parse `syn-update check`.
     *
     * This is coupled to syn-update's own output, which is acceptable only
     * because the same package ships both — they version together and cannot
     * drift apart in the field. It is deliberately TOLERANT: anything it fails
     * to recognise still lands in the log pane verbatim, so a format change
     * degrades to "the window shows the raw output" rather than to a window
     * that confidently says the wrong thing.
     *
     * No ANSI stripping is needed: syn-update only colours when stdout is a
     * tty, and under Process it is a pipe.
     */
    function parseCheck(text) {
        const ups = [], cms = [], blk = [], nws = []
        let section = ""
        root.upToDate = false

        for (const raw of String(text).split("\n")) {
            const line = raw.replace(/\s+$/, "")
            if (line === "") continue

            if (/everything build-all\.sh can update is already current/.test(line)) {
                root.upToDate = true; continue
            }
            let m = line.match(/source is already at (\S+)/) || line.match(/^==> ([0-9a-f]{7,}) -> ([0-9a-f]{7,})/)
            if (m) { root.revision = m[m.length - 1]; continue }

            if (/commits since your installed source revision/.test(line)) { section = "commits"; continue }
            if (/component\(s\) to rebuild/.test(line))                    { section = "updates"; continue }
            // "N NEW component(s) to install". This case existed in syn-update
            // from the day scan() learned to add components and was never
            // parsed here, so the first NEW component to actually appear —
            // syn-settings, 2026-08-10 — landed in no section, left `ups`
            // empty, failed the up-to-date test too, and the window reported
            // "Could not determine update status" about a report it had
            // understood perfectly well.
            if (/NEW component\(s\) to install/.test(line))                 { section = "new"; continue }
            if (/not updatable from source/.test(line))                    { section = "blocked"; continue }

            if (section === "updates") {
                // "  synui            0.1.0-203      -> 0.1.0-204"
                m = line.match(/^\s{2}(\S+)\s+(\S+)\s+->\s+(\S+)\s*$/)
                if (m && m[1] !== "COMPONENT") ups.push({ name: m[1], from: m[2], to: m[3] })
                continue
            }
            if (section === "new") {
                // "  syn-settings     0.1.0-3        (not installed here)"
                m = line.match(/^\s{2}(\S+)\s+(\S+)\s+\(not installed here\)\s*$/)
                if (m && m[1] !== "COMPONENT") nws.push({ name: m[1], to: m[2] })
                continue
            }
            if (section === "commits") {
                m = line.match(/^\s{2}([0-9a-f]{7,})\s+(.*)$/)
                if (m) cms.push({ hash: m[1], subject: m[2] })
                continue
            }
            if (section === "blocked") {
                m = line.match(/^\s{4}(\S+)\s{2,}(.*)$/)
                if (m) blk.push({ name: m[1], reason: m[2] })
                continue
            }
        }

        root.updates = ups
        root.commits = cms
        root.blocked = blk
        root.fresh = nws

        // Counted together, because "apply" does both in one build-all.sh
        // invocation and a window that offered to install 1 thing while saying
        // "up to date" would be describing a different machine.
        const total = ups.length + nws.length
        if (total > 0) {
            const parts = []
            if (ups.length > 0) parts.push(ups.length + (ups.length === 1 ? " update" : " updates"))
            if (nws.length > 0) parts.push(nws.length + " new")
            root.statusLine = parts.join(", ") + " available"
        } else if (root.upToDate) {
            root.statusLine = "SynapseOS is up to date"
        } else {
            root.statusLine = "Could not determine update status — see the log"
        }
    }

    function check() {
        if (root.busy) return
        root.busy = true
        root.statusLine = "Checking for updates…"
        root.logText = ""
        checkProc.running = true
    }

    Process {
        id: checkProc
        command: ["syn-update", "check"]
        running: false
        stdout: StdioCollector {
            onStreamFinished: { root.appendLog(this.text); root.parseCheck(this.text) }
        }
        // The "not updatable" block and any git error go to stderr; it is part
        // of the answer, not noise, so it is parsed and logged like stdout.
        stderr: StdioCollector {
            onStreamFinished: { root.appendLog(this.text); root.parseCheck(root.logText) }
        }
        onExited: root.busy = false
    }

    // --hold so the window survives the run and the build output stays readable,
    // matching how the start menu already launches `synpkg upgrade`.
    Process {
        id: applyProc
        command: ["kitty", "--hold", "syn-update", "apply"]
        running: false
    }

    FloatingWindow {
        title: "SynapseOS Updates"
        minimumSize: Qt.size(760, 560)
        color: root.cBg

        Component.onCompleted: root.check()

        /*
         * Closing the window must END the process.
         *
         * ShellRoot is built for a persistent shell — a bar or an OSD, which
         * outlives every window it draws. This is not that: it is one dialog,
         * and destroying the FloatingWindow leaves `qs` alive owning nothing,
         * invisible and unreachable. `syn-update-gui` then runs
         * `qs -n`, whose --no-duplicate sees that corpse and exits 0 without
         * drawing anything, so the menu entry reports success and does nothing.
         * That is the "closed it, now it will not reopen" bug — and exit 0 is
         * why it produced no error anywhere to notice.
         *
         * Quitting here means no instance can outlive its window, so
         * --no-duplicate only ever matches a window actually on screen.
         */
        onClosed: Qt.quit()

        Rectangle {
            anchors.fill: parent
            color: root.cBg

            Column {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                // ── header ─────────────────────────────────
                Item {
                    width: parent.width
                    height: root.ui(52)

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: root.statusLine
                            color: root.upToDate ? root.cOk
                                 : ((root.updates.length + root.fresh.length) > 0
                                    ? root.cAccent : root.cText)
                            font.family: root.uiFont
                            font.pixelSize: root.ui(21)
                            font.bold: true
                        }
                        Text {
                            text: root.revision ? "source revision " + root.revision : ""
                            color: root.cDim
                            font.pixelSize: root.ui(12)
                            // A git hash is not prose: monospace stays literal
                            // across the suite, but still takes the scale.
                            font.family: "monospace"
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        // Controls styles their own font from the application
                        // font, which is the startup one — so the buttons
                        // needed the binding as much as the labels did.
                        Button {
                            text: root.busy ? "Checking…" : "Check again"
                            font.family: root.uiFont
                            font.pixelSize: root.ui(13)
                            enabled: !root.busy
                            onClicked: root.check()
                        }
                        Button {
                            text: "Install updates…"
                            font.family: root.uiFont
                            font.pixelSize: root.ui(13)
                            // New components count. Gating Apply on updates
                            // alone left the button dead on a machine whose
                            // only pending work was an install.
                            enabled: !root.busy && (root.updates.length + root.fresh.length) > 0
                            onClicked: applyProc.running = true
                        }
                    }
                }

                // ── what will change ───────────────────────
                Rectangle {
                    width: parent.width
                    height: Math.min(root.ui(150), root.ui(34) + root.updates.length * root.ui(26))
                    visible: root.updates.length > 0
                    color: root.cPanel
                    radius: 8
                    border.color: root.cLine

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Text {
                            text: "Components to rebuild"
                            color: root.cDim; font.family: root.uiFont
                            font.pixelSize: root.ui(11); font.bold: true
                        }
                        Repeater {
                            model: root.updates
                            Row {
                                spacing: 10
                                Text {
                                    text: modelData.name; color: root.cText
                                    font.pixelSize: root.ui(13); font.family: "monospace"
                                    // A column, not a font size: widths are not
                                    // scaled anywhere in the suite.
                                    width: 170
                                }
                                Text {
                                    text: modelData.from + "  →  " + modelData.to
                                    color: root.cAccent
                                    font.pixelSize: root.ui(13); font.family: "monospace"
                                }
                            }
                        }
                    }
                }

                // ── components that are not installed here yet ──
                // Its own card rather than another row in "to rebuild": these
                // ADD software to the machine, which is a different decision
                // from moving a version forward, and it should read that way
                // before it is applied rather than after.
                Rectangle {
                    width: parent.width
                    height: Math.min(root.ui(150), root.ui(34) + root.fresh.length * root.ui(26))
                    visible: root.fresh.length > 0
                    color: root.cPanel
                    radius: 8
                    border.color: root.cLine

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Text {
                            text: "New components to install"
                            color: root.cDim; font.family: root.uiFont
                            font.pixelSize: root.ui(11); font.bold: true
                        }
                        Repeater {
                            model: root.fresh
                            Row {
                                spacing: 10
                                Text {
                                    text: modelData.name; color: root.cText
                                    font.pixelSize: root.ui(13); font.family: "monospace"
                                    width: 170
                                }
                                Text {
                                    text: modelData.to + "   (not installed here)"
                                    color: root.cAccent
                                    font.pixelSize: root.ui(13); font.family: "monospace"
                                }
                            }
                        }
                    }
                }

                // ── the commits being applied ──────────────
                Rectangle {
                    width: parent.width
                    height: 150
                    visible: root.commits.length > 0
                    color: root.cPanel
                    radius: 8
                    border.color: root.cLine

                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Text {
                            text: "Changes since your installed version"
                            color: root.cDim; font.family: root.uiFont
                            font.pixelSize: root.ui(11); font.bold: true
                        }
                        ScrollView {
                            width: parent.width
                            // The 22 is the heading line above it, so it moves
                            // with the scale or the list loses its last row.
                            height: parent.height - root.ui(22)
                            clip: true
                            Column {
                                spacing: 2
                                Repeater {
                                    model: root.commits
                                    Row {
                                        spacing: 10
                                        Text {
                                            text: modelData.hash; color: root.cDim
                                            font.pixelSize: root.ui(12); font.family: "monospace"
                                        }
                                        Text {
                                            text: modelData.subject; color: root.cText
                                            font.family: root.uiFont
                                            font.pixelSize: root.ui(12)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── what this tool will NOT update ─────────
                Rectangle {
                    width: parent.width
                    height: Math.min(root.ui(120), root.ui(30) + root.blocked.length * root.ui(20))
                    visible: root.blocked.length > 0
                    color: "transparent"
                    radius: 8
                    border.color: root.cLine

                    Column {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 3
                        Text {
                            text: "Not updated this way — these move with an ISO upgrade"
                            color: root.cWarn; font.family: root.uiFont
                            font.pixelSize: root.ui(11); font.bold: true
                        }
                        Repeater {
                            model: root.blocked
                            Text {
                                text: "• " + modelData.name
                                color: root.cDim; font.family: root.uiFont
                                font.pixelSize: root.ui(11)
                            }
                        }
                    }
                }

                // ── raw log, the honest fallback ───────────
                Rectangle {
                    width: parent.width
                    height: 120
                    color: root.cSunken
                    radius: 8
                    border.color: root.cLine

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 10
                        clip: true
                        TextArea {
                            text: root.logText
                            readOnly: true
                            color: root.cDim
                            background: null
                            font.pixelSize: root.ui(11)
                            font.family: "monospace"
                            wrapMode: TextArea.NoWrap
                        }
                    }
                }
            }
        }
    }
}
