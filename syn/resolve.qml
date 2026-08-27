//@ pragma UseQApplication

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * DaVinci Doctor — a window around `syn resolve`.
 *
 * Getting DaVinci Resolve running on Arch is four jobs in a fixed order, and
 * the order is the part that catches people out:
 *
 *   1. download the Linux .zip from Blackmagic (a registration form — the one
 *      step nothing here can do for you),
 *   2. build the AUR package from it, which routinely fails on a version
 *      mismatch because the AUR lags Blackmagic by days,
 *   3. install an OpenCL runtime FOR THE GPU YOU HAVE, because
 *      /opt/resolve/bin/resolve has libOpenCL.so.1 in its NEEDED and the
 *      package asks only for the virtual `opencl-driver` — under --noconfirm
 *      pacman takes the first provider, so a box can end up holding an ICD for
 *      a card it does not have. That fails LATE and blames the GPU.
 *   4. shadow the packaged .desktop so Resolve gets a clean Qt environment.
 *
 * `syn resolve` has done all of that for a while. What it did not have was
 * anywhere to start from if you did not already know it existed. This is that:
 * drop the installer on the window, or point it at one, and it walks the rest.
 *
 * WHY THE PRIVILEGED HALF OPENS A TERMINAL
 *
 * Steps 2 and 3 run makepkg and pacman. sudo with no controlling terminal
 * cannot prompt, so running them inside this window would fail at the install
 * step every time — minutes into a four-gigabyte build. The alternative, a
 * NOPASSWD rule for pacman, is a privilege-escalation hole rather than a
 * convenience. Same split, and the same reasoning, as SynapseOS Updates: this
 * window owns the read-only half and hands the rest to syntty, where sudo can
 * do its job and the build is visible while it runs.
 *
 * WHAT IT READS
 *
 * `syn resolve doctor --porcelain`, which is tab-separated <key>\t<state>\t<text>
 * from the SAME emitter the terminal report uses. Deliberately not the pretty
 * output: that carries ANSI, box drawing and wrapped prose, all of which a
 * parser can trip on. The key is the contract; the text is free to be reworded.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
ShellRoot {
    id: root

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

    // ── …and the colour the WALLPAPER offers ────────────────────────────────
    //
    // synui measures a small palette off the wallpaper and publishes it here;
    // the bar reads this same file, in synui's quickshell/Theme.qml. This
    // window read theme.json alone, so on a desktop with the wallpaper accent
    // switched on the bar went the colour of the picture and every app window
    // beside it stayed the preset's — the half-applied feature 387 fixed
    // inside the bar, one process further out.
    //
    // A file and not the bar's Theme singleton: that singleton lives in synui's
    // package and this is a different one, and an import across the two breaks
    // the moment either is installed alone. The contract is the file, exactly
    // as it already is for theme.json.
    //
    // ⚠ `ok` AND `use` BOTH HAVE TO HOLD. `ok` is the PICTURE's answer — a
    // greyscale wallpaper has no hue to offer. `use` is the SETTING (Control
    // panel ▸ Appearance ▸ Wallpaper accent, where auto means Prism and nothing
    // else) and synui writes the file whichever way it is set. Reading the
    // colour without checking it is how the bar came to wear the wallpaper on
    // themes that never asked for it (386).
    //
    // Missing, unreadable, or refused all come out as the empty string, which
    // falls through to the theme's own accent below — the same answer as a
    // desktop that has never measured one, or a synui too old to write the
    // file. None of those needs telling apart here.
    //
    // ⚠ NOT `paletteFile`. That name is theme.json's reader in some of these
    // windows, and QML answers a duplicate property by refusing to load the
    // TYPE, naming a line that is not this one.
    property string wpAccent: ""

    property FileView wpPaletteFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/palette.state"
        watchChanges: true
        printErrors: false          // absent until a wallpaper has been measured
        onFileChanged: reload()
        onLoaded: {
            const t   = this.text()
            const ok  = /^\s*ok\s*=\s*yes\s*$/m.test(t)
            const use = !/^\s*use\s*=\s*no\s*$/m.test(t)
            const m   = t.match(/^\s*accent\s*=\s*(#[0-9A-Fa-f]{6})\s*$/m)
            root.wpAccent = (ok && use && m) ? m[1] : ""
        }
        onLoadFailed: root.wpAccent = ""
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

    // ── Legibility ──────────────────────────────────────────────────────────
    //
    // The contrast corrector synfiles, synpkg, syn-disks and the bar all carry.
    // It is here for the WALLPAPER accent alone — see cAccent below for why the
    // theme's own accent is deliberately not put through it.
    function lum(c) {
        function ch(v) { return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b)
    }
    function contrast(a, b) {
        const la = lum(a), lb = lum(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
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

    readonly property color cBg:      rgbOf("popup", "#11151c")
    // The panel and the rules are positions between the surface and the ink, so
    // they invert with the theme instead of staying a fixed dark slate.
    readonly property color cPanel:   mix(cBg, cText, 0.06)
    readonly property color cLine:    mix(cBg, cText, 0.18)
    readonly property color cText:    root.pal.fg ? root.pal.fg : "#dbe4ee"
    readonly property color cDim:     mix(cBg, cText, 0.58)
    // ⚠ THE CORRECTOR RUNS ON THE MEASURED COLOUR ONLY, and the asymmetry is
    // the point: a theme's accent was chosen by a person against these exact
    // surfaces, and a hue lifted off a photograph was not. Putting the preset
    // through it as well would re-tint windows this change is not about.
    readonly property color cAccent:  root.wpAccent !== ""
                                      ? readable(Qt.color(root.wpAccent), cBg, 4.5)
                                      : rgbOf("accent", "#38bdf8")
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

    // ── State ───────────────────────────────────────────────────────────────
    //
    // `checks` is the doctor's answer, keyed: checks["opencl"] = {state, text}.
    // Keyed rather than a list because every question this window asks is
    // "what did it say about X", and a list would mean a scan per question.
    property var checks: ({})
    property var order: []            // the keys, in the order they arrived
    property bool busy: false
    property string statusLine: "Looking at this machine…"
    property string logText: ""

    // The installer archive, from a drop, a picker, or ~/Downloads.
    property string zipPath: ""
    property bool   dropActive: false

    function stateOf(key)  { const c = root.checks[key]; return c ? c.state : "" }
    function textOf(key)   { const c = root.checks[key]; return c ? c.text  : "" }
    function factOf(key)   { return root.textOf(key) }

    readonly property bool haveResolve: root.stateOf("resolve") === "ok"
    readonly property bool haveOpenCL:  root.stateOf("opencl")  === "ok"
    readonly property bool haveOverride: root.stateOf("override") === "ok"
    // Everything the doctor can fix is done. The override is only expected once
    // Resolve is there, so it does not gate the earlier steps.
    readonly property bool allDone: root.haveResolve && root.haveOpenCL
                                    && root.haveOverride

    function appendLog(t) {
        if (!t) return
        root.logText += t.endsWith("\n") ? t : t + "\n"
    }

    // ── The doctor ──────────────────────────────────────────────────────────
    function refresh() {
        root.busy = true
        root.statusLine = "Looking at this machine…"
        doctorProc.running = true
    }

    Process {
        id: doctorProc
        command: ["syn", "resolve", "doctor", "--porcelain"]
        running: false
        stdout: StdioCollector {
            onStreamFinished: root.parseDoctor(this.text)
        }
        stderr: StdioCollector { onStreamFinished: root.appendLog(this.text) }
        onExited: root.busy = false
    }

    /*
     * <key>\t<state>\t<text>, one per line.
     *
     * A line that does not have two tabs is passed to the log rather than
     * dropped: if the contract ever changes, the window shows the raw answer
     * instead of confidently describing a machine it did not understand.
     */
    function parseDoctor(text) {
        const seen = {}
        const ord = []
        for (const raw of String(text).split("\n")) {
            if (raw === "") continue
            const f = raw.split("\t")
            if (f.length < 3) { root.appendLog(raw); continue }
            const key = f[0], state = f[1], msg = f.slice(2).join("\t")
            // A key can repeat — the hint lines do. The FIRST wins, because
            // that is the check itself and the rest are its detail.
            if (seen[key] === undefined) {
                seen[key] = { state: state, text: msg }
                ord.push(key)
            }
        }
        root.checks = seen
        root.order = ord

        // The installer, if the doctor found one and nothing has been chosen.
        if (root.zipPath === "" && root.stateOf("zip") === "ok") {
            const m = root.textOf("zip").match(/^Installer found: (.*)$/)
            if (m) root.zipPath = m[1]
        }

        root.statusLine =
            root.allDone   ? "DaVinci Resolve is set up on this machine."
          : !root.haveResolve ? "Resolve is not installed yet."
          : !root.haveOpenCL  ? "Resolve is installed, but it has no OpenCL runtime."
          :                     "One step left: the launch environment."
    }

    // ── The privileged half ─────────────────────────────────────────────────
    //
    // ⚠ syntty --hold, and both halves matter. syntty is the terminal that
    // ships in every install profile — it is a hard dependency of synui, where
    // kitty and foot are optdepends and kitty is not installed at all — and
    // --hold keeps the window open after the run so the build
    // log survives the thing that produced it. A four-gigabyte build that fails
    // and then vanishes is a build nobody can report a bug against.
    //
    // ⚠ ONE Process PER BUTTON, never one shared and re-pointed. Assigning
    // `running = true` to a Process that is ALREADY running is a silent no-op
    // in quickshell — the second press of a shared launcher does nothing at all
    // and says nothing. Two processes, two lifetimes.
    Process {
        id: installProc
        command: ["syntty", "--hold", "-e", "syn", "resolve", "install", root.zipPath]
        running: false
        // The doctor is re-run when the terminal closes, so the window catches
        // up with whatever happened in it without anybody pressing Refresh.
        onExited: root.refresh()
    }

    Process {
        id: setupProc
        command: ["syntty", "--hold", "-e", "sudo", "syn", "resolve", "setup"]
        running: false
        onExited: root.refresh()
    }

    Process {
        id: launchProc
        command: ["syn", "resolve", "launch"]
        running: false
    }

    Process {
        id: pickProc
        // zenity is already a dependency of the graphical updater in this
        // suite, and its file chooser is one line. A QML FileDialog would mean
        // importing QtQuick.Dialogs, which quickshell does not ship.
        command: ["zenity", "--file-selection", "--title=Locate the DaVinci Resolve installer",
                  "--file-filter=DaVinci Resolve installer (zip) | *.zip",
                  "--file-filter=All files | *"]
        running: false
        stdout: StdioCollector {
            onStreamFinished: {
                const p = this.text.trim()
                if (p !== "") root.setZip(p)
            }
        }
    }

    // One place that decides whether a path is the thing we want, so the drop
    // and the picker cannot disagree about it.
    function setZip(path) {
        if (!path) return
        if (!/\.zip$/i.test(path)) {
            root.statusLine = "That is not a .zip — Blackmagic ship the Linux "
                            + "installer as DaVinci_Resolve_<version>_Linux.zip"
            return
        }
        root.zipPath = path
        root.statusLine = "Ready to build from " + path.split("/").pop()
    }

    function urlToPath(u) {
        const s = "" + u
        if (s.indexOf("file://") !== 0) return ""
        return decodeURIComponent(s.substring(7))
    }

    // ── The window ──────────────────────────────────────────────────────────
    //
    // FloatingWindow, not PanelWindow: a PanelWindow needs zwlr_layer_shell_v1,
    // which mutter does not implement, so under GNOME it maps nothing at all —
    // no window, no error, no log. This is an ordinary application window and
    // has to work on every desktop somebody might run Resolve from.
    FloatingWindow {
        title: "DaVinci Doctor"
        minimumSize: Qt.size(680, 620)
        color: root.cBg

        Component.onCompleted: root.refresh()

        // Closing the window ends the process. ShellRoot is built for a shell
        // that outlives its windows; this is one dialog, and a `qs` left alive
        // owning nothing makes the next launch's --no-duplicate exit 0 without
        // drawing anything — the "closed it, now it will not reopen" bug.
        onClosed: Qt.quit()

        // ── Drop the installer anywhere on the window ───────────────────────
        //
        // The whole window, not a small target: somebody dragging a 4 GB zip
        // out of a file manager should not have to aim. The visual answer is
        // the border below, which is the only thing that says a drop is going
        // to be accepted before the button is let go.
        DropArea {
            anchors.fill: parent
            onEntered: (drag) => {
                root.dropActive = drag.hasUrls
                if (!drag.hasUrls) drag.accepted = false
            }
            onExited: root.dropActive = false
            onDropped: (drop) => {
                root.dropActive = false
                if (!drop.hasUrls) return
                for (const u of drop.urls) {
                    const p = root.urlToPath(u)
                    if (p !== "") { root.setZip(p); break }
                }
                drop.accept(Qt.CopyAction)
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border { width: root.dropActive ? 3 : 0; color: root.cAccent }
            z: 90
        }

        Column {
            anchors { fill: parent; margins: 18 }
            spacing: 14

            // ── Header ──────────────────────────────────────────────────
            Row {
                width: parent.width
                spacing: 10

                Text {
                    text: "DaVinci Doctor"
                    color: root.cAccent
                    font { family: root.uiFont; pixelSize: root.ui(18); bold: true }
                }
                Item { width: 1; height: 1 }
            }

            Text {
                width: parent.width
                text: root.statusLine
                wrapMode: Text.WordWrap
                color: root.cText
                font { family: root.uiFont; pixelSize: root.ui(13) }
            }

            // ── The four steps ──────────────────────────────────────────
            //
            // In the order they have to happen, each showing what the doctor
            // said and offering the one action that moves it forward. A step
            // that is already done says so and offers nothing — a button that
            // reinstalls something already installed is a way to waste an hour.
            Column {
                width: parent.width
                spacing: 8

                StepCard {
                    width: parent.width
                    index: 1
                    title: "The installer"
                    done: root.haveResolve || root.zipPath !== ""
                    // Blackmagic's download is behind a registration form. This
                    // is the one step that cannot be automated, and saying so
                    // plainly is better than a button that opens a browser and
                    // leaves somebody wondering what to do next.
                    detail: root.haveResolve
                            ? "Resolve is installed — the installer is not needed."
                          : root.zipPath !== ""
                            ? root.zipPath
                          : "Drop the DaVinci Resolve .zip anywhere on this window, "
                            + "or use Locate. The download itself is behind "
                            + "Blackmagic's registration form and cannot be automated."
                    action: root.haveResolve ? "" : "Locate…"
                    onActivated: pickProc.running = true
                }

                StepCard {
                    width: parent.width
                    index: 2
                    title: "Build and install"
                    done: root.haveResolve
                    detail: root.haveResolve
                            ? root.textOf("resolve")
                          : root.zipPath === ""
                            ? "Waiting for the installer above."
                            : "Builds the AUR package from that archive. Opens a "
                              + "terminal — makepkg and pacman need somewhere to "
                              + "ask for your password, and the build takes a while."
                    action: (!root.haveResolve && root.zipPath !== "") ? "Build…" : ""
                    onActivated: installProc.running = true
                }

                StepCard {
                    width: parent.width
                    index: 3
                    title: "OpenCL runtime"
                    done: root.haveOpenCL
                    // The one that fails late and blames the GPU. Naming the
                    // concrete package is the whole point: the Resolve package
                    // asks for the virtual `opencl-driver`, and under
                    // --noconfirm pacman takes whichever provider comes first.
                    detail: root.haveOpenCL
                            ? root.textOf("opencl")
                            : root.textOf("opencl") + (root.factOf("opencl.pkg") !== ""
                              && root.factOf("opencl.pkg") !== "none"
                              ? "\nInstalls " + root.factOf("opencl.pkg")
                                + " for your " + root.factOf("gpu") + " card."
                              : "")
                    action: root.haveOpenCL ? "" : "Fix…"
                    onActivated: setupProc.running = true
                }

                StepCard {
                    width: parent.width
                    index: 4
                    title: "Launch environment"
                    done: root.haveOverride
                    // Shadowing, not editing: a copy in /usr/local/share wins
                    // by XDG_DATA_DIRS order, so a Resolve upgrade cannot
                    // revert it and deleting one file undoes all of it.
                    detail: root.haveOverride
                            ? root.textOf("override")
                          : !root.haveResolve
                            ? "Applied once Resolve is installed."
                            : root.textOf("override")
                    action: (root.haveResolve && !root.haveOverride) ? "Fix…" : ""
                    onActivated: setupProc.running = true
                }
            }

            // ── Codecs, which is not a step but is the next question ────
            //
            // Free Resolve on Linux has no H.264/H.265 decoder, so phone and
            // camera footage imports as media offline. That is the single most
            // reported "Resolve is broken", and it is not broken — so it says
            // so here rather than waiting to be asked.
            Rectangle {
                width: parent.width
                height: codecCol.implicitHeight + 20
                radius: 6
                color: root.cPanel
                border { width: 1; color: root.cLine }
                visible: root.textOf("codecs") !== ""

                Column {
                    id: codecCol
                    anchors { left: parent.left; right: parent.right
                              verticalCenter: parent.verticalCenter
                              leftMargin: 12; rightMargin: 12 }
                    spacing: 3
                    Text {
                        width: parent.width
                        text: root.textOf("codecs")
                        wrapMode: Text.WordWrap
                        color: root.stateOf("codecs") === "ok" ? root.cOk : root.cWarn
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    Text {
                        width: parent.width
                        visible: root.stateOf("codecs") !== "ok"
                        text: "syn resolve transcode <files> rewraps footage to "
                              + "DNxHR, which it does read."
                        wrapMode: Text.WordWrap
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(11) }
                    }
                }
            }

            // ── The end of post-setup ───────────────────────────────────
            Row {
                width: parent.width
                spacing: 10

                DocBtn {
                    text: "Re-check"
                    on: !root.busy
                    onClicked: root.refresh()
                }
                DocBtn {
                    text: "Launch Resolve"
                    on: root.allDone
                    primary: root.allDone
                    onClicked: launchProc.running = true
                }
            }
        }
    }

    // ── Small pieces ────────────────────────────────────────────────────────

    component DocBtn: Rectangle {
        id: btn
        property alias text: btnText.text
        property bool primary: false
        // ⚠ NOT `enabled`. Item already has one, and it does something: a false
        // `enabled` on this Rectangle disables the MouseArea inside it too, so
        // the two meanings would fight and the greyed-out look and the dead
        // click would stop agreeing about which is in charge.
        property bool on: true
        signal clicked()

        width: btnText.implicitWidth + 28
        height: 32
        radius: 5
        color: !btn.on ? root.cPanel
             : btnMa.containsMouse ? root.mix(root.cBg, root.cText, 0.20)
             : root.cPanel
        border { width: 1; color: btn.primary && btn.on ? root.cAccent : root.cLine }

        Text {
            id: btnText
            anchors.centerIn: parent
            color: btn.on ? (btn.primary ? root.cAccent : root.cText) : root.cDim
            font { family: root.uiFont; pixelSize: root.ui(12) }
        }
        MouseArea {
            id: btnMa
            anchors.fill: parent
            hoverEnabled: true
            enabled: btn.on
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()
        }
    }

    component StepCard: Rectangle {
        id: card
        property int index: 0
        property string title: ""
        property string detail: ""
        property string action: ""
        property bool done: false
        signal activated()

        height: cardCol.implicitHeight + 22
        radius: 6
        color: root.cPanel
        border { width: 1; color: card.done ? root.cOk : root.cLine }

        Row {
            anchors { fill: parent; margins: 11 }
            spacing: 11

            // The number, or a tick once the step is behind you. Drawn as text
            // because the step order is the thing this window is teaching.
            Rectangle {
                width: 24; height: 24; radius: 12
                anchors.verticalCenter: parent.verticalCenter
                color: card.done ? root.cOk : root.mix(root.cBg, root.cText, 0.16)
                Text {
                    anchors.centerIn: parent
                    text: card.done ? "✓" : String(card.index)
                    color: card.done ? root.cBg : root.cText
                    font { family: root.uiFont; pixelSize: root.ui(12); bold: true }
                }
            }

            Column {
                id: cardCol
                width: parent.width - 24 - 11
                       - (actionBtn.visible ? actionBtn.width + 11 : 0)
                spacing: 3

                Text {
                    text: card.title
                    color: root.cText
                    font { family: root.uiFont; pixelSize: root.ui(13); bold: true }
                }
                Text {
                    width: parent.width
                    text: card.detail
                    wrapMode: Text.WordWrap
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                }
            }

            DocBtn {
                id: actionBtn
                anchors.verticalCenter: parent.verticalCenter
                visible: card.action !== ""
                text: card.action
                primary: true
                onClicked: card.activated()
            }
        }
    }
}
