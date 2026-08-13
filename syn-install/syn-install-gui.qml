// syn-install-gui — the graphical face of syn-install.
//
// A FORM, and nothing more. Every answer collected here is written to an
// install profile and handed to `syn-install --config`, which is the same
// script the text installer runs and the only thing in SynapseOS that knows
// how to partition a disk. Nothing in this file runs parted, pacstrap or
// mkfs, and nothing in it decides whether a disk is safe to erase — that
// answer arrives as records from `syn-install --list-disks`.
//
// The rule is the one synfiles, synpkg and syn-disks are built on: the tool
// does the work and prints records, the window only renders them. It matters
// more here than anywhere else in the system, because the alternative is two
// implementations of the partition rules, and the second one would be the
// copy with no test suite behind it.
//
// ── The one rule for the profile ───────────────────────────────────────────
//
// A key this form omits is a question syn-install ASKS — on a terminal nobody
// is looking at, because the window is in front of it. So every key a run can
// reach has to be written, and the keys that answer nothing must NOT be
// (syn-install reports unused keys at the end, and that report is how a
// typo in a hand-written profile gets caught; filling it with noise from here
// would blunt it). See buildConfig(), where each conditional says which
// question it is answering.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

// No `pragma ComponentBehavior: Bound` here, though qmllint suggests it for
// every `root.` reached from inside a component. It also stops a delegate
// seeing `index` and `modelData` unless each is redeclared as a required
// property — so the pragma trades a class of lint warning for a class of
// runtime breakage, in the delegates. syn-disks carries the same warnings for
// the same reason.
//
// The two remaining warnings are Process.exited's QProcess::ExitStatus
// parameter, which QML cannot resolve. synpkg.qml has exactly the same pair.
// They are why the handlers below take no parameters — see the note there.

import QtQuick
import Quickshell
import Quickshell.Io

FloatingWindow {
    id: root

    title: "Install SynapseOS"
    implicitWidth: 900
    implicitHeight: 640
    // Below this the two-column summary and the disk rows stop fitting, and a
    // layout with no floor does not degrade — it paints over itself.
    minimumSize: Qt.size(760, 560)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYN_INSTALL_BIN") || "syn-install"
    // Where the profile is written. /run is a tmpfs, which is the point: it
    // holds a password until the install reads it and never reaches a disk.
    readonly property string confPath: Quickshell.env("SYN_INSTALL_CONF") || "/run/synapseos/install.conf"

    // ── Theme ───────────────────────────────────────────────────────────────
    //
    // Same source and shape as syn-disks, synfiles and the bar, so a theme
    // switch moves all of them together. The live session may have no
    // theme.json at all, so every colour below has a literal fallback.
    property var p: ({})
    readonly property bool isLight: p.scheme === "light"

    FileView {
        path: (Quickshell.env("HOME") || "/root") + "/.config/synui/theme.json"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: { try { root.p = JSON.parse(this.text()) } catch (e) { root.p = ({}) } }
        onLoadFailed: root.p = ({})
    }

    function themed(key, fallback) {
        const c = root.p[key]
        if (c === undefined || c === null) return fallback
        if (typeof c === "string") return c
        // synui writes {r,g,b} floats in 0..1.
        if (c.r !== undefined) return Qt.rgba(c.r, c.g, c.b, c.a === undefined ? 1 : c.a)
        return fallback
    }

    readonly property color cBg:     themed("bg",     isLight ? "#f2f4f7" : "#12151a")
    readonly property color cPanel:  themed("panel",  isLight ? "#ffffff" : "#1a1f27")
    readonly property color cText:   themed("fg",     isLight ? "#12151a" : "#e6ecf3")
    readonly property color cDim:    themed("dim",    isLight ? "#5a6472" : "#8b97a8")
    readonly property color cAccent: themed("accent", "#33ccff")
    readonly property color cWarn:   themed("warn",   "#ffb454")
    readonly property color cErr:    themed("error",  "#ff5c66")
    readonly property color cLine:   isLight ? "#d3d9e0" : "#2a323d"

    color: cBg

    // ── The answers ─────────────────────────────────────────────────────────
    //
    // Defaults match the text installer's defaults exactly. Where they drift
    // the two installers become two products, and the one people compare
    // against is whichever they used first.
    property string aDisk: ""
    property string aMode: "erase"          // erase | alongside
    property string aFs: "ext4"
    property string aBoot: "grub"
    property bool   aSnapshots: false
    property bool   aEncrypt: false
    property string aLuks: ""
    property string aPreset: "standard"
    property string aModel: "mistral-7b"
    // ── Custom, and why every one of these has a default ────────────────────
    //
    // Custom is the text installer's fourth preset, and it asks nineteen y/n
    // questions. Every one of them has to be written by this window when the
    // preset is custom, because a key left out is a question asked at a terminal
    // nobody is looking at — so these ARE the answers, and their defaults are
    // Standard's, exactly as `ask_opt`'s defaults are.
    property bool aChibi: true
    property bool aVibe: true
    property bool aNexus: false
    property bool aTepris: false
    property bool aArsenal: true
    property bool aBluetooth: true
    property bool aPrinting: true
    property bool aWine: true
    property bool aPhone: true
    property bool aSteam: false
    property bool aBlackarch: true
    property bool aNix: false
    // The core daemons sit behind their own gate, the way the text installer
    // gates them: dropping one is allowed because it was asked for, but it is
    // not offered in the same list as a block game.
    property bool aCustomCore: false
    property bool aCoreSynapd: true
    property bool aCoreSynui: true
    property bool aCoreSynsh: true
    property bool aCoreSynnet: true
    property bool aCoreGuard: true
    property bool aCoreUpdate: true
    property string aUser: "syn"
    property string aFullname: ""
    property string aPass: ""
    property string aPass2: ""
    property string aDesktop: "synui"
    property string aLocale: "en_US.UTF-8"
    property string aKeymap: "us"
    property string aXkb: "us"
    property string aTz: "UTC"
    property bool   aGpuInference: true

    // The label of the language row these came from, and the font pack it named.
    // aLangKnown is what says the pair is trustworthy: a language picked off the
    // list carries its own font pack (an empty one is a real answer — Latin
    // needs nothing beyond noto-fonts), while a locale typed by hand carries no
    // idea of what script it is, so the key is left out and syn-install falls
    // back to the widest cover it has.
    property string aLangLabel: "English (US)"
    property string aFonts: ""
    property bool   aLangKnown: true

    // Facts discovered about the machine, not chosen.
    property bool   hasNvidia: false
    property string release: ""

    property int page: 0
    readonly property var pageNames: ["Welcome", "Disk", "Software", "Account", "Region", "Summary", "Install"]

    // ── Disk records ────────────────────────────────────────────────────────
    //
    // dev, bytes, size, model, live, usable, reason — straight from
    // `syn-install --list-disks`. `live` marks the installer's own media and
    // `usable` is that plus the minimum size; both are decided there, not here.
    property var disks: []

    Process {
        id: diskProc
        command: [root.bin, "--list-disks"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                for (const line of this.text.split("\n")) {
                    if (!line.trim()) continue
                    const f = line.split("\t")
                    if (f.length < 6) continue
                    out.push({
                        dev: f[0], bytes: parseInt(f[1]) || 0, size: f[2],
                        model: f[3], live: f[4] === "1", usable: f[5] === "1",
                        reason: f.length > 6 ? f[6] : ""
                    })
                }
                root.disks = out
                // Preselect the first disk that can actually be installed to,
                // never merely the first one: on a live USB the first row is
                // very often the stick itself.
                for (const d of out) { if (d.usable) { root.aDisk = d.dev; break } }
            }
        }
    }

    // ── Region records ──────────────────────────────────────────────────────
    //
    // Four lists, all of them printed by syn-install for the same reason the
    // disks are: the answers are a locale table with a font column, and two
    // keyboard namespaces that disagree, and a second copy of any of that here
    // would be the copy no test suite reads. `--list-keymaps` and
    // `--list-xkb-layouts` report what THIS image can actually resolve, which is
    // the check the install makes on the answer anyway.
    //
    // Each list may come back empty — an image without xkeyboard-config, say.
    // That is not an error: the picker still opens, and the filter box is a text
    // field, so a name can always be typed. Which is the whole point of the
    // sheet: it is a helper for a question that never stops accepting a
    // hand-typed answer.
    property var locales: []
    property var timezones: []
    property var keymaps: []
    property var xkbLayouts: []

    // A tab-separated record list, parsed into {value, label} plus whatever else
    // the record carries. `mk` turns the fields of one line into an option.
    function parseRecords(text, mk, minFields) {
        const out = []
        for (const line of text.split("\n")) {
            if (!line.trim()) continue
            const f = line.split("\t")
            if (f.length < minFields) continue
            out.push(mk(f))
        }
        return out
    }

    Process {
        command: [root.bin, "--list-locales"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: root.locales = root.parseRecords(this.text, f => ({
                // value is the locale: it is what gets written, and what the
                // filter box should match when someone types "en_GB".
                value: f[1], label: f[0],
                keymap: f[2], xkb: f[3], fonts: f.length > 4 ? f[4] : ""
            }), 4)
        }
    }
    Process {
        command: [root.bin, "--list-timezones"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: root.timezones = root.parseRecords(this.text, f => ({
                value: f[0], label: f[1]
            }), 1)
        }
    }
    Process {
        command: [root.bin, "--list-keymaps"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: root.keymaps = root.parseRecords(this.text, f => ({
                value: f[0], label: f.length > 1 ? f[1] : ""
            }), 1)
        }
    }
    Process {
        command: [root.bin, "--list-xkb-layouts"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: root.xkbLayouts = root.parseRecords(this.text, f => ({
                value: f[0], label: f.length > 1 ? f[1] : ""
            }), 1)
        }
    }

    // The description a list gives for a value, for the closed selector's
    // caption. A binding, so it fills in when the list finishes loading rather
    // than staying blank because the window drew first.
    function describe(list, value) {
        for (const o of list) if (o.value === value) return o.label
        return ""
    }
    function withDescription(list, value) {
        const d = describe(list, value)
        if (!d) return value
        // UTC's label in the table is "UTC — no local time", which appended to
        // its own name reads "UTC — UTC — no local time".
        if (d.indexOf(value) === 0) return d
        return value + "  —  " + d
    }

    // The release the ISO says it is. Same source as the text installer's
    // header: /etc/os-release, stamped by archiso/build.sh from iso_version.
    Process {
        id: relProc
        command: ["sh", "-c", ". /etc/os-release 2>/dev/null; [ \"$ID\" = synapseos ] && printf %s \"$VERSION_ID\""]
        running: true
        stdout: StdioCollector { onStreamFinished: root.release = this.text.trim() }
    }

    // gpu_inference is asked ONLY on NVIDIA, so it may only be written on
    // NVIDIA — see the note at the top about not blunting the unused-key
    // report.
    Process {
        id: gpuProc
        command: ["sh", "-c", "lspci 2>/dev/null | grep -qiE 'vga|3d|display' && lspci 2>/dev/null | grep -qi nvidia && echo yes || echo no"]
        running: true
        stdout: StdioCollector { onStreamFinished: root.hasNvidia = this.text.trim() === "yes" }
    }

    // ── Validation ──────────────────────────────────────────────────────────
    //
    // Each page reports why Next is unavailable rather than greying the button
    // out silently. A disabled control with no reason is a bug report.
    function pageProblem(n) {
        if (n === 1) {
            if (!aDisk) return "Choose a disk to install to."
            if (aEncrypt && aLuks.length < 8)
                return "The encryption passphrase needs at least 8 characters."
        }
        if (n === 3) {
            if (!/^[a-z_][a-z0-9_-]*$/.test(aUser))
                return "A username is lower-case letters, digits, - and _, and cannot start with a digit."
            if (aPass.length < 1) return "Set a password for the account."
            if (aPass !== aPass2) return "The two passwords do not match."
        }
        if (n === 4) {
            if (!aLocale.trim()) return "A locale is needed, e.g. en_US.UTF-8."
            if (!aTz.trim()) return "A timezone is needed, e.g. Europe/Lisbon."
        }
        return ""
    }

    function selectedDisk() {
        for (const d of disks) if (d.dev === aDisk) return d
        return null
    }

    // ── The picker sheet ────────────────────────────────────────────────────
    //
    // One sheet, driven by pickKey, rather than a dropdown per field. Two
    // reasons, and neither is taste:
    //
    // - The lists are 599 timezones and 252 keymaps. A dropdown that drops is a
    //   list you scroll looking for a name you already know; a sheet with a
    //   filter box is one you type three letters into. The filter is the helper.
    // - A popup anchored under its field has to be positioned, and the body
    //   Rectangle it would live in is `clip: true`. Positioning it means
    //   mapToItem, whose result must not be BOUND (it does not re-evaluate when
    //   an ancestor moves — the panel ends up where the field used to be). A
    //   centred sheet has no geometry to get wrong.
    //
    // pickKey is also the open/closed state: "" is closed. One property, so
    // there is no way for it to be open and pointing at nothing.
    property string pickKey: ""
    property string pickTitle: ""
    property string pickNote: ""
    property string pickCurrent: ""
    property string pickFilter: ""

    // Derived from pickKey rather than handed to openPicker, so the sheet shows
    // the list as it IS and not as it was when the sheet opened. Each list
    // arrives from its own subprocess: a sheet opened with a copy would be empty
    // for whichever query had not answered yet, and would stay empty for as long
    // as it was open.
    readonly property var pickOptions:
          pickKey === "language" ? locales
        : pickKey === "timezone" ? timezones
        : pickKey === "keymap"   ? keymaps
        : pickKey === "xkb"      ? xkbLayouts
        : []

    readonly property var pickFiltered: {
        const f = pickFilter.trim().toLowerCase()
        if (!f) return pickOptions
        const out = []
        for (const o of pickOptions) {
            if (o.value.toLowerCase().indexOf(f) >= 0) { out.push(o); continue }
            if (o.label && o.label.toLowerCase().indexOf(f) >= 0) out.push(o)
        }
        return out
    }

    // Whether the filter text is itself an answer nobody offered. This is the
    // escape hatch the text installer's "Other" is, kept in the same place a
    // search box already is: glibc has 500 locales and this list has fifteen.
    readonly property string pickTyped: {
        const t = pickFilter.trim()
        if (!t) return ""
        for (const o of pickOptions) if (o.value === t) return ""
        return t
    }

    function openPicker(key, title, note, current) {
        pickKey = key; pickTitle = title; pickNote = note
        pickCurrent = current
        pickFilter = ""
    }
    function closePicker() { pickKey = ""; pickFilter = "" }

    // opt is the whole record when a row was picked, and null when the value was
    // typed into the filter box — which is exactly the difference between
    // "picked a language" and "typed a locale", and the reason lang_fonts is
    // written in one case and not the other.
    function applyPick(value, opt) {
        switch (pickKey) {
        case "language":
            aLocale = value
            if (opt) {
                // A language row answers all four questions at once. That is the
                // point of it: the keymap/layout pair is where a hand-typed
                // answer goes wrong, and the four rows where the two namespaces
                // disagree are already right in this table.
                aLangLabel = opt.label; aKeymap = opt.keymap
                aXkb = opt.xkb; aFonts = opt.fonts; aLangKnown = true
            } else {
                aLangLabel = ""; aFonts = ""; aLangKnown = false
            }
            break
        case "timezone": aTz = value; break
        case "keymap":   aKeymap = value; break
        case "xkb":      aXkb = value; break
        }
        closePicker()
    }

    // ── The profile ─────────────────────────────────────────────────────────
    //
    // key=value lines, the format syn-install's config_load reads directly.
    // Read the header note before adding a key: every one here has to be a
    // question this run will actually reach.
    // The installer's y/n vocabulary. `answer` maps yes->y and no->n, so these
    // are the words a hand-written profile uses too.
    function yn(b) { return b ? "yes" : "no" }

    // ── What Custom chose, in words ─────────────────────────────────────────
    //
    // For the summary. The word "custom" is not a read-back of nineteen answers,
    // and the summary is the only page between them and a partitioned disk.
    function joinPicked(pairs) {
        const out = []
        for (const p of pairs) if (p[0]) out.push(p[1])
        return out.join(", ")
    }
    function customApps() {
        return joinPicked([[aChibi, "Chibi"], [aVibe, "Vibe"], [aNexus, "Nexus Chat"],
                           [aTepris, "TEPRIS"], [aArsenal, "Arsenal"]])
    }
    function customOpts() {
        return joinPicked([[aBluetooth, "Bluetooth"], [aPrinting, "printing"], [aWine, "Wine"],
                           [aPhone, "KDE Connect"], [aSteam, "Steam"],
                           [aBlackarch, "BlackArch repo"], [aNix, "Nix"]])
    }
    // The daemons that were turned OFF, because that is the short list and the
    // one worth reading twice.
    function customCoreDropped() {
        return joinPicked([[!aCoreSynapd, "synapd"], [!aCoreSynui, "synui"],
                           [!aCoreSynsh, "synsh"], [!aCoreSynnet, "synnet"],
                           [!aCoreGuard, "synguard"], [!aCoreUpdate, "syn-update"]])
    }

    // The two-column grid. Custom's three lines are NOT here: they are sentences
    // ("Bluetooth, printing, Wine, KDE Connect, Steam, BlackArch repo, Nix") and
    // a half-width cell elides them, on the one page whose whole job is to be
    // read. They get full-width rows of their own below the grid.
    function summaryRows() {
        const R = []
        R.push(["Disk", aDisk + "  (" + (selectedDisk() ? selectedDisk().size : "?") + ")"])
        R.push(["Mode", aMode])
        R.push(["Filesystem", aFs + (aEncrypt ? " on LUKS2" : "")])
        R.push(["Bootloader", aBoot
                + (aFs === "btrfs" && aBoot === "limine" && aSnapshots ? " + snapshots" : "")])
        R.push(["Install", aPreset])
        R.push(["AI model", aPreset === "minimal" ? "none" : aModel])
        R.push(["Account", aUser + (aFullname ? "  (" + aFullname + ")" : "")])
        R.push(["Desktop", aDesktop])
        R.push(["Locale", aLocale + "   keys " + aKeymap + " / " + aXkb])
        R.push(["Timezone", aTz])
        return R
    }

    function customRows() {
        if (aPreset !== "custom") return []
        const R = [["Apps", customApps() || "none"], ["Options", customOpts() || "none"]]
        if (aCustomCore) {
            const off = customCoreDropped()
            R.push(["Core", off ? "WITHOUT " + off : "all of it"])
        }
        return R
    }

    function buildConfig() {
        const L = []
        L.push("# Written by syn-install-gui. Answers a graphical run; not meant to be kept.")
        L.push("disk=" + aDisk)
        L.push("install_mode=" + aMode)
        // The destructive confirmation is per-mode and each one is a separate
        // question in the script. Pressing Install on the summary page IS this
        // answer — the summary is the read-back the text installer prints
        // before asking, in the same words.
        L.push(aMode === "erase" ? "confirm_erase=yes" : "confirm_alongside=yes")
        L.push("filesystem=" + aFs)
        L.push("bootloader=" + aBoot)
        // snapshots is only ASKED for btrfs + limine. Writing it anywhere else
        // would land in the unused-key report.
        if (aFs === "btrfs" && aBoot === "limine") L.push("snapshots=" + (aSnapshots ? "yes" : "no"))
        L.push("disk_plan_ok=yes")
        L.push("encrypt=" + (aEncrypt ? "yes" : "no"))
        // luks_passphrase is read only when encrypt=yes, and the form already
        // refuses one under 8 characters, so short_passphrase_ok is never asked.
        if (aEncrypt) L.push("luks_passphrase=" + aLuks)
        L.push("preset=" + aPreset)
        // Custom is nineteen further questions, and they are asked ONLY under
        // custom — writing them under any other preset would put them in the
        // unused-key report. The core six are asked only when the gate is open,
        // for the same reason.
        if (aPreset === "custom") {
            L.push("want_chibi=" + yn(aChibi))
            L.push("want_vibe=" + yn(aVibe))
            L.push("want_nexus=" + yn(aNexus))
            L.push("want_tepris=" + yn(aTepris))
            L.push("want_arsenal=" + yn(aArsenal))
            L.push("want_bluetooth=" + yn(aBluetooth))
            L.push("want_printing=" + yn(aPrinting))
            L.push("want_wine=" + yn(aWine))
            L.push("want_phone=" + yn(aPhone))
            L.push("want_steam=" + yn(aSteam))
            L.push("want_blackarch=" + yn(aBlackarch))
            L.push("want_nix=" + yn(aNix))
            L.push("customise_core=" + yn(aCustomCore))
            if (aCustomCore) {
                L.push("core_synapd=" + yn(aCoreSynapd))
                L.push("core_synui=" + yn(aCoreSynui))
                L.push("core_synsh=" + yn(aCoreSynsh))
                L.push("core_synnet=" + yn(aCoreSynnet))
                L.push("core_guard=" + yn(aCoreGuard))
                L.push("core_update=" + yn(aCoreUpdate))
            }
        }
        // The model question is asked on every preset except minimal.
        if (aPreset !== "minimal") L.push("ai_model=" + aModel)
        L.push("selection_ok=yes")
        L.push("username=" + aUser)
        L.push("fullname=" + aFullname)
        L.push("password=" + aPass)
        L.push("desktop=" + aDesktop)
        // "other" plus the three explicit keys, never a menu number: a number
        // means whatever that row is on the day it runs.
        L.push("language=other")
        L.push("locale=" + aLocale)
        // The font pack that goes with the language, and ONLY when the language
        // came off the table — see aLangKnown. An empty value is a real answer
        // (Latin needs nothing past noto-fonts); leaving the key out is the
        // different answer "no idea what script this is", which syn-install
        // covers with noto-fonts-extra.
        if (aLangKnown) L.push("lang_fonts=" + aFonts)
        L.push("keymap=" + aKeymap)
        L.push("xkb_layout=" + aXkb)
        L.push("timezone=" + aTz)
        // Only asked when there is no connection, and the install cannot start
        // without one — answering it keeps a flaky link from stopping on a
        // picker this window cannot draw.
        L.push("wifi_picker=no")
        if (hasNvidia) L.push("gpu_inference=" + (aGpuInference ? "yes" : "no"))
        return L.join("\n") + "\n"
    }

    // ── Running the install ─────────────────────────────────────────────────
    property bool running: false
    property bool finished: false
    property int exitCode: -1
    property string lastLine: ""

    // A ListModel, not a string the view splits. An install prints thousands of
    // lines, and `model: text.split("\n")` rebuilds and re-diffs the entire
    // list on every one of them — the window gets slower the longer the install
    // runs, which is precisely backwards.
    ListModel { id: logModel }

    function appendLog(s) {
        for (const l of String(s).split("\n")) logModel.append({ line: l })
        const t = String(s).trim()
        if (t) lastLine = t
        logView.positionViewAtEnd()
    }

    FileView {
        id: confWriter
        path: root.confPath
        // The file does not exist yet, and FileView will not write a path it
        // has never loaded unless it is allowed to create one.
        preload: false
        atomicWrites: true
    }

    // ── Why the exit status travels in the OUTPUT ───────────────────────────
    //
    // Quickshell's signal is exited(int, QProcess::ExitStatus), and QML cannot
    // resolve that second type — so a handler that declares parameters fails to
    // compile and then simply NEVER RUNS. Silently: no error at load, the
    // window just sits there. Both handlers here were written that way at
    // first, which meant the install never started and the finished state never
    // arrived.
    //
    // So the handlers below take no parameters, and each command reports its
    // own status as a line on stdout that the parser picks out. That also
    // survives the thing an exit code cannot describe — a process killed
    // before it ever reported.
    property bool confOk: false

    function startInstall() {
        if (running) return
        page = 6
        running = true; finished = false; exitCode = -1; confOk = false
        logModel.clear(); lastLine = "Starting…"
        writeProc.running = true
    }

    Process {
        id: writeProc
        // umask BEFORE the redirect, or the file is created with the session's
        // umask and only then narrowed — a window in which a profile holding a
        // password is world-readable.
        command: ["sh", "-c",
                  "umask 077; mkdir -p \"$(dirname \"$1\")\" && cat > \"$1\" && echo __syn_conf_ok",
                  "sh", root.confPath]
        running: false
        stdinEnabled: true
        onStarted: {
            write(root.buildConfig())
            stdinEnabled = false     // EOF, or `cat` never returns
        }
        stdout: SplitParser {
            onRead: (data) => { if (data.indexOf("__syn_conf_ok") >= 0) root.confOk = true }
        }
        stderr: SplitParser { onRead: (data) => root.appendLog(data) }
        onExited: {
            if (!root.confOk) {
                root.appendLog("Could not write " + root.confPath)
                root.running = false; root.finished = true; root.exitCode = 1
                root.lastLine = "Could not write the install profile."
                return
            }
            root.appendLog("Profile written to " + root.confPath)
            installProc.running = true
        }
    }

    Process {
        id: installProc
        command: ["sh", "-c", "\"$@\"; echo \"__syn_install_exit=$?\"",
                  "sh", root.bin, "--config", root.confPath]
        running: false
        // SplitParser, not StdioCollector: a collector fires once, when the
        // stream ENDS, which for a twenty-minute install means a blank window
        // for twenty minutes and then everything at once. This is the half of
        // live progress that lives on the reading side.
        stdout: SplitParser {
            onRead: (data) => {
                const m = /__syn_install_exit=(\d+)/.exec(data)
                if (m) { root.exitCode = parseInt(m[1]); return }
                root.appendLog(data)
            }
        }
        stderr: SplitParser { onRead: (data) => root.appendLog(data) }
        onExited: {
            root.running = false; root.finished = true
            root.lastLine = root.exitCode === 0
                ? "Installation complete."
                : "Installation failed — see the log."
            // The profile holds the account password and the LUKS passphrase.
            // /run is a tmpfs so it never reached a disk, but it should not sit
            // readable in the live session either.
            shredProc.running = true
        }
    }

    Process {
        id: shredProc
        command: ["sh", "-c", "rm -f \"$1\"", "sh", root.confPath]
        running: false
    }

    // ── Widgets ─────────────────────────────────────────────────────────────
    component Btn: Rectangle {
        id: btn
        property string text: ""
        property bool primary: false
        // NOT `enabled`: that is QQuickItem's own property, and shadowing it
        // means the base class's and this one's disagree about the same word.
        property bool canPress: true
        signal clicked()
        implicitWidth: label.implicitWidth + 34
        implicitHeight: 34
        radius: 6
        color: !btn.canPress ? Qt.rgba(0.5, 0.5, 0.5, 0.12)
             : btn.primary ? (ma.containsMouse ? Qt.lighter(root.cAccent, 1.15) : root.cAccent)
             : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05))
        border.width: btn.primary ? 0 : 1
        border.color: root.cLine
        Text {
            id: label
            anchors.centerIn: parent
            text: btn.text
            font.pixelSize: 14
            font.bold: btn.primary
            color: !btn.canPress ? root.cDim
                 : btn.primary ? (root.isLight ? "#ffffff" : "#0b0f14") : root.cText
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: btn.canPress ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (btn.canPress) btn.clicked()
        }
    }

    component Choice: Rectangle {
        id: ch
        property string text: ""
        property string subtext: ""
        property bool checked: false
        // See Btn: `enabled` belongs to QQuickItem.
        property bool selectable: true
        signal picked()
        implicitHeight: 44
        radius: 6
        color: ch.checked ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.14)
             : cma.containsMouse && ch.selectable ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
        border.width: 1
        border.color: ch.checked ? root.cAccent : root.cLine
        opacity: ch.selectable ? 1 : 0.45
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Rectangle {
                width: 14; height: 14; radius: 7
                anchors.verticalCenter: parent.verticalCenter
                color: "transparent"
                border.width: 2
                border.color: ch.checked ? root.cAccent : root.cDim
                Rectangle {
                    anchors.centerIn: parent
                    width: 6; height: 6; radius: 3
                    color: root.cAccent
                    visible: ch.checked
                }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text { text: ch.text; color: root.cText; font.pixelSize: 14 }
                Text {
                    text: ch.subtext; color: root.cDim; font.pixelSize: 11
                    visible: ch.subtext !== ""
                }
            }
        }
        MouseArea {
            id: cma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: ch.selectable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (ch.selectable) ch.picked()
        }
    }

    // A checkbox row. Choice is the 44px card the pages are built from and is
    // right for four options; nineteen of them is 840 pixels of card, so the
    // Custom lists get this instead — a square box, one line, 26px.
    component Check: Rectangle {
        id: ck
        property string text: ""
        property bool checked: false
        signal toggled()
        implicitWidth: ckRow.implicitWidth + 6
        implicitHeight: 26
        color: ckma.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
        radius: 4
        Row {
            id: ckRow
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Rectangle {
                width: 14; height: 14; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: ck.checked ? root.cAccent : "transparent"
                border.width: ck.checked ? 0 : 1
                border.color: root.cDim
                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    visible: ck.checked
                    font.pixelSize: 11
                    font.bold: true
                    color: root.isLight ? "#ffffff" : "#0b0f14"
                }
            }
            Text {
                text: ck.text
                color: root.cText
                font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        MouseArea {
            id: ckma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ck.toggled()
        }
    }

    // A closed selector: what is chosen, and a click that opens the sheet. It
    // holds no list of its own — the caller passes one to openPicker, so there
    // is one place a list is turned into a choice.
    component Selector: Column {
        id: selr
        property string label: ""
        property string caption: ""
        property string hint: ""
        signal opened()
        spacing: 4
        width: 300
        Text { text: selr.label; color: root.cDim; font.pixelSize: 12 }
        Rectangle {
            width: parent.width
            height: 32
            radius: 5
            color: selma.containsMouse ? Qt.rgba(1, 1, 1, 0.10)
                 : root.isLight ? "#ffffff" : Qt.rgba(1, 1, 1, 0.05)
            border.width: 1
            border.color: selma.containsMouse ? root.cAccent : root.cLine
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.right: arrow.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: selr.caption
                color: root.cText
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            Text {
                id: arrow
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "▾"
                color: root.cDim
                font.pixelSize: 12
            }
            MouseArea {
                id: selma
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: selr.opened()
            }
        }
        Text {
            text: selr.hint; color: root.cDim; font.pixelSize: 11
            visible: selr.hint !== ""
            width: parent.width
            elide: Text.ElideRight
        }
    }

    component Field: Column {
        id: fld
        property string label: ""
        property string hint: ""
        property alias text: input.text
        property bool secret: false
        // The starting value, assigned ONCE. Binding `text` to the answer it
        // also writes back is a loop with nothing to break it — it settles
        // today only because the values happen to be equal by the time it
        // re-evaluates.
        property string initial: ""
        Component.onCompleted: if (initial !== "") text = initial
        spacing: 4
        width: 300
        Text { text: fld.label; color: root.cDim; font.pixelSize: 12 }
        Rectangle {
            width: parent.width
            height: 32
            radius: 5
            color: root.isLight ? "#ffffff" : Qt.rgba(1, 1, 1, 0.05)
            border.width: 1
            border.color: input.activeFocus ? root.cAccent : root.cLine
            TextInput {
                id: input
                anchors.fill: parent
                anchors.margins: 8
                verticalAlignment: TextInput.AlignVCenter
                color: root.cText
                font.pixelSize: 14
                selectByMouse: true
                echoMode: fld.secret ? TextInput.Password : TextInput.Normal
                clip: true
            }
        }
        Text {
            text: fld.hint; color: root.cDim; font.pixelSize: 11
            visible: fld.hint !== ""
        }
    }

    // A bar that is honest about what it knows — the same one syn-settings and
    // SYNAPSE Software draw, so the three windows show a wait the same way.
    //
    // There is no percentage here and there is not going to be a fake one: an
    // install is pacstrap, then a package set, then a multi-gigabyte model
    // download, and the only honest split between those is "still going". The
    // shuttle says that. What it adds over the log is the quiet stretches — a
    // long download prints nothing for minutes, and a window with a frozen
    // last line is where someone decides the installer has hung.
    component ProgressTrack: Rectangle {
        id: track
        property bool active: false
        height: 3
        radius: height / 2
        color: "transparent"
        clip: true

        Rectangle {
            id: shuttle
            visible: track.active
            width: Math.max(48, track.width * 0.22)
            height: parent.height
            radius: parent.radius
            color: root.cAccent
            opacity: 0.8
            x: -width
        }

        // from/to are read at (re)start, not bound, so a resize must restart it
        // or the shuttle keeps sweeping the width the window used to have.
        NumberAnimation {
            id: shuttleAnim
            target: shuttle
            property: "x"
            from: -shuttle.width
            to: track.width
            duration: 1400
            loops: Animation.Infinite
            running: shuttle.visible
        }
        onWidthChanged: if (shuttle.visible) shuttleAnim.restart()
    }

    component Head: Column {
        id: hd
        property string title: ""
        property string blurb: ""
        spacing: 4
        // Addressed through the id, never `parent`: inside a Column, `parent`
        // happens to be this same object, so the two read alike right up until
        // something is nested one level deeper and it silently is not.
        Text { text: hd.title; color: root.cText; font.pixelSize: 20; font.bold: true }
        Text {
            text: hd.blurb; color: root.cDim; font.pixelSize: 13
            visible: hd.blurb !== ""
            width: 640; wrapMode: Text.WordWrap
        }
    }

    // ── Layout ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.cBg

        // Step rail.
        Row {
            id: rail
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 16
            spacing: 6
            Repeater {
                model: root.pageNames
                Row {
                    spacing: 6
                    Rectangle {
                        width: 22; height: 22; radius: 11
                        anchors.verticalCenter: parent.verticalCenter
                        color: index === root.page ? root.cAccent
                             : index < root.page ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.35)
                             : "transparent"
                        border.width: 1
                        border.color: index <= root.page ? root.cAccent : root.cLine
                        Text {
                            anchors.centerIn: parent
                            text: index < root.page ? "✓" : String(index + 1)
                            font.pixelSize: 11
                            color: index === root.page ? (root.isLight ? "#ffffff" : "#0b0f14") : root.cText
                        }
                    }
                    Text {
                        text: modelData
                        anchors.verticalCenter: parent.verticalCenter
                        color: index === root.page ? root.cText : root.cDim
                        font.pixelSize: 12
                        font.bold: index === root.page
                    }
                    Rectangle {
                        width: 18; height: 1
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.cLine
                        visible: index < root.pageNames.length - 1
                    }
                }
            }
        }

        Rectangle {
            id: body
            anchors.top: rail.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: foot.top
            anchors.margins: 16
            radius: 8
            color: root.cPanel
            border.width: 1
            border.color: root.cLine
            clip: true

            // ── 0: Welcome ──────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 16
                visible: root.page === 0
                Head {
                    title: "Install SynapseOS" + (root.release ? " " + root.release : "")
                    blurb: "This asks for a disk, an account and a few preferences, then hands "
                         + "the answers to the same installer the text version runs. Nothing "
                         + "is written to any disk until the last page, and that page says "
                         + "exactly what it is about to do."
                }
                Column {
                    spacing: 8
                    Repeater {
                        model: [
                            "A disk is partitioned and formatted",
                            "The base system and the SynapseOS packages are installed",
                            "An account and a desktop are set up",
                            "A bootloader is written"
                        ]
                        Row {
                            spacing: 8
                            Text { text: "•"; color: root.cAccent; font.pixelSize: 14 }
                            Text { text: modelData; color: root.cText; font.pixelSize: 14 }
                        }
                    }
                }
                Rectangle {
                    width: parent.width; height: 1; color: root.cLine
                }
                Text {
                    text: "Partitioning an existing layout by hand is the text installer's "
                        + "ADVANCED mode — quit this and run `syn-install` in a terminal for that."
                    color: root.cDim; font.pixelSize: 12
                    width: parent.width; wrapMode: Text.WordWrap
                }
            }

            // ── 1: Disk ─────────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14
                visible: root.page === 1
                Head {
                    title: "Where should SynapseOS go?"
                    blurb: "The installer's own media is listed and cannot be chosen."
                }
                Column {
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: root.disks
                        Choice {
                            width: parent.width
                            text: modelData.dev + "   " + modelData.size
                            subtext: modelData.usable ? modelData.model
                                                      : modelData.model + " — " + modelData.reason
                            selectable: modelData.usable
                            checked: root.aDisk === modelData.dev
                            onPicked: root.aDisk = modelData.dev
                        }
                    }
                    Text {
                        text: "No disks found."
                        color: root.cErr; font.pixelSize: 13
                        visible: root.disks.length === 0
                    }
                }
                Row {
                    spacing: 22
                    Column {
                        spacing: 6
                        Text { text: "Mode"; color: root.cDim; font.pixelSize: 12 }
                        Row {
                            spacing: 6
                            Choice {
                                width: 190; text: "Erase the disk"
                                subtext: "every partition and all data"
                                checked: root.aMode === "erase"
                                onPicked: root.aMode = "erase"
                            }
                            Choice {
                                width: 190; text: "Install alongside"
                                subtext: "use free space, UEFI only"
                                checked: root.aMode === "alongside"
                                onPicked: root.aMode = "alongside"
                            }
                        }
                    }
                }
                Row {
                    spacing: 22
                    Column {
                        spacing: 6
                        Text { text: "Filesystem"; color: root.cDim; font.pixelSize: 12 }
                        Row {
                            spacing: 6
                            Repeater {
                                model: ["ext4", "btrfs", "xfs", "f2fs"]
                                Choice {
                                    width: 92; text: modelData
                                    checked: root.aFs === modelData
                                    onPicked: root.aFs = modelData
                                }
                            }
                        }
                    }
                    Column {
                        spacing: 6
                        Text { text: "Bootloader"; color: root.cDim; font.pixelSize: 12 }
                        Row {
                            spacing: 6
                            Repeater {
                                model: ["grub", "systemd-boot", "limine"]
                                Choice {
                                    width: 118; text: modelData
                                    checked: root.aBoot === modelData
                                    onPicked: root.aBoot = modelData
                                }
                            }
                        }
                    }
                }
                Row {
                    spacing: 18
                    Choice {
                        width: 240
                        text: "Snapshots"
                        subtext: "btrfs + limine only"
                        selectable: root.aFs === "btrfs" && root.aBoot === "limine"
                        checked: root.aSnapshots && selectable
                        onPicked: root.aSnapshots = !root.aSnapshots
                    }
                    Choice {
                        width: 240
                        text: "Encrypt the disk"
                        subtext: "LUKS2"
                        checked: root.aEncrypt
                        onPicked: root.aEncrypt = !root.aEncrypt
                    }
                    Field {
                        width: 240
                        label: "Passphrase"
                        secret: true
                        visible: root.aEncrypt
                        hint: "8 characters or more"
                        onTextChanged: root.aLuks = text
                    }
                }
            }

            // ── 2: Software ─────────────────────────────────────────────────
            //
            // The one page that can outgrow the window: Custom opens nineteen
            // checkboxes below the four presets. Hence the Flickable — and it
            // costs nothing on the other three presets, where the content is
            // shorter than the view and the scroll indicator stays hidden.
            //
            // (A Flickable takes the pointer grab off its delegates once the
            // pointer moves far enough — which kills drag-and-drop, and there is
            // none here. A click that does not travel is still a click.)
            Flickable {
                id: swFlick
                anchors.fill: parent
                anchors.margins: 24
                visible: root.page === 2
                contentWidth: width
                contentHeight: swCol.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Column {
                    id: swCol
                    // swFlick, not `parent`: a Flickable's children are parented
                    // to its contentItem, whose width is not the view's.
                    width: swFlick.width
                    spacing: 14

                    Head {
                        title: "What should be installed?"
                        blurb: "The SynapseOS core — the compositor, the daemons and the "
                             + "applications it is built on — is installed by every choice here."
                    }
                    Column {
                        width: parent.width
                        spacing: 6
                        Choice {
                            width: parent.width; text: "Full"
                            subtext: "Standard + Steam + Nix + Nexus Chat + TEPRIS"
                            checked: root.aPreset === "full"
                            onPicked: root.aPreset = "full"
                        }
                        Choice {
                            width: parent.width; text: "Standard"
                            subtext: "AI model, Bluetooth, printing, Wine, phone, Chibi + Vibe + Arsenal"
                            checked: root.aPreset === "standard"
                            onPicked: root.aPreset = "standard"
                        }
                        Choice {
                            width: parent.width; text: "Minimal"
                            subtext: "core daemons only — no apps, no model, none of the above"
                            checked: root.aPreset === "minimal"
                            onPicked: root.aPreset = "minimal"
                        }
                        Choice {
                            width: parent.width; text: "Custom"
                            subtext: "choose each app and each option below"
                            checked: root.aPreset === "custom"
                            onPicked: root.aPreset = "custom"
                        }
                    }

                    // ── Custom ──────────────────────────────────────────────
                    //
                    // The same questions the text installer asks under Custom,
                    // in the same order, with the same defaults. Two columns
                    // because they are two lists there too: what to install, and
                    // what to turn on.
                    Column {
                        width: parent.width
                        spacing: 10
                        visible: root.aPreset === "custom"

                        Rectangle { width: parent.width; height: 1; color: root.cLine }

                        Row {
                            spacing: 40
                            Column {
                                spacing: 2
                                Text {
                                    text: "Applications"; color: root.cDim; font.pixelSize: 12
                                    bottomPadding: 4
                                }
                                Check {
                                    text: "Chibi — voice companion + security sentinel"
                                    checked: root.aChibi
                                    onToggled: root.aChibi = !root.aChibi
                                }
                                Check {
                                    text: "Vibe — local AI coding assistant"
                                    checked: root.aVibe
                                    onToggled: root.aVibe = !root.aVibe
                                }
                                Check {
                                    text: "Nexus Chat — peer-to-peer chat"
                                    checked: root.aNexus
                                    onToggled: root.aNexus = !root.aNexus
                                }
                                Check {
                                    text: "TEPRIS — block game"
                                    checked: root.aTepris
                                    onToggled: root.aTepris = !root.aTepris
                                }
                                Check {
                                    text: "SYNAPSE Arsenal — browse BlackArch tooling"
                                    checked: root.aArsenal
                                    onToggled: root.aArsenal = !root.aArsenal
                                }
                            }
                            Column {
                                spacing: 2
                                Text {
                                    text: "Options"; color: root.cDim; font.pixelSize: 12
                                    bottomPadding: 4
                                }
                                Check {
                                    text: "Bluetooth"
                                    checked: root.aBluetooth
                                    onToggled: root.aBluetooth = !root.aBluetooth
                                }
                                Check {
                                    text: "Printing (CUPS)"
                                    checked: root.aPrinting
                                    onToggled: root.aPrinting = !root.aPrinting
                                }
                                Check {
                                    text: "Wine — run Windows .exe/.msi"
                                    checked: root.aWine
                                    onToggled: root.aWine = !root.aWine
                                }
                                Check {
                                    text: "KDE Connect — pair a phone"
                                    checked: root.aPhone
                                    onToggled: root.aPhone = !root.aPhone
                                }
                                Check {
                                    text: "Steam + game stack + Proton (~3.1 GB)"
                                    checked: root.aSteam
                                    onToggled: root.aSteam = !root.aSteam
                                }
                                Check {
                                    text: "BlackArch repo — ~5000 tools, none installed"
                                    checked: root.aBlackarch
                                    onToggled: root.aBlackarch = !root.aBlackarch
                                }
                                Check {
                                    text: "Nix + Home Manager"
                                    checked: root.aNix
                                    onToggled: root.aNix = !root.aNix
                                }
                            }
                        }

                        Rectangle { width: parent.width; height: 1; color: root.cLine }

                        // Behind its own gate, exactly as the text installer
                        // gates it. Dropping a core daemon is allowed — it was
                        // asked for — but it stops being SynapseOS, so it is not
                        // in the same list as a block game.
                        Check {
                            text: "Customise the core daemons too"
                            checked: root.aCustomCore
                            onToggled: root.aCustomCore = !root.aCustomCore
                        }
                        Column {
                            width: parent.width
                            spacing: 6
                            visible: root.aCustomCore
                            Text {
                                text: "The core daemons are what SynapseOS is. Turning one off "
                                    + "produces an Arch system with some SynapseOS parts, and the "
                                    + "AI, security and desktop features will not work as documented. "
                                    + "The package manager and the file manager stay either way."
                                color: root.cWarn
                                font.pixelSize: 11
                                width: parent.width
                                wrapMode: Text.WordWrap
                            }
                            Row {
                                spacing: 40
                                Column {
                                    spacing: 2
                                    Check {
                                        text: "synapd — the LLM daemon"
                                        checked: root.aCoreSynapd
                                        onToggled: root.aCoreSynapd = !root.aCoreSynapd
                                    }
                                    Check {
                                        text: "synui — the compositor / desktop"
                                        checked: root.aCoreSynui
                                        onToggled: root.aCoreSynui = !root.aCoreSynui
                                    }
                                    Check {
                                        text: "synsh — the AI-native shell"
                                        checked: root.aCoreSynsh
                                        onToggled: root.aCoreSynsh = !root.aCoreSynsh
                                    }
                                }
                                Column {
                                    spacing: 2
                                    Check {
                                        text: "synnet — network policy daemon"
                                        checked: root.aCoreSynnet
                                        onToggled: root.aCoreSynnet = !root.aCoreSynnet
                                    }
                                    Check {
                                        text: "synguard + kernel module"
                                        checked: root.aCoreGuard
                                        onToggled: root.aCoreGuard = !root.aCoreGuard
                                    }
                                    Check {
                                        text: "syn-update"
                                        checked: root.aCoreUpdate
                                        onToggled: root.aCoreUpdate = !root.aCoreUpdate
                                    }
                                }
                            }
                            // The one deselection that cannot be undone later,
                            // said at the moment it is made rather than in a
                            // summary line: without syn-update the machine has no
                            // way to receive another SynapseOS package, and
                            // fixing that means reinstalling.
                            Text {
                                text: "syn-update is off: this machine will have no way to receive "
                                    + "another SynapseOS package. Fixing that later means reinstalling."
                                color: root.cErr
                                font.pixelSize: 11
                                visible: !root.aCoreUpdate
                                width: parent.width
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Column {
                        spacing: 6
                        visible: root.aPreset !== "minimal"
                        Text { text: "AI model — downloaded during the install"; color: root.cDim; font.pixelSize: 12 }
                        Row {
                            spacing: 6
                            Choice {
                                width: 190; text: "Mistral 7B"; subtext: "~4.1 GB — recommended"
                                checked: root.aModel === "mistral-7b"
                                onPicked: root.aModel = "mistral-7b"
                            }
                            Choice {
                                width: 170; text: "Phi-3 Mini"; subtext: "~2.2 GB — weaker"
                                checked: root.aModel === "phi3"
                                onPicked: root.aModel = "phi3"
                            }
                            Choice {
                                width: 170; text: "Qwen2 0.5B"; subtext: "~0.4 GB — much weaker"
                                checked: root.aModel === "tiny"
                                onPicked: root.aModel = "tiny"
                            }
                            Choice {
                                width: 150; text: "None"; subtext: "AI stays inert"
                                checked: root.aModel === "none"
                                onPicked: root.aModel = "none"
                            }
                        }
                    }
                    Choice {
                        width: 340
                        visible: root.hasNvidia
                        text: "NVIDIA GPU inference"
                        subtext: "the CUDA runtime, ~4.7 GiB"
                        checked: root.aGpuInference
                        onPicked: root.aGpuInference = !root.aGpuInference
                    }
                }

                // There is more page than window under Custom, and a page that
                // scrolls with nothing saying so is a page whose bottom half does
                // not exist. Hidden when everything fits.
                Rectangle {
                    anchors.right: parent.right
                    width: 3
                    radius: 1.5
                    color: root.cDim
                    opacity: 0.5
                    visible: swFlick.contentHeight > swFlick.height
                    height: Math.max(24, swFlick.height * (swFlick.height / swFlick.contentHeight))
                    y: swFlick.contentY
                       + (swFlick.height - height) * (swFlick.contentHeight > swFlick.height
                          ? swFlick.contentY / (swFlick.contentHeight - swFlick.height) : 0)
                }
            }

            // ── 3: Account ──────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14
                visible: root.page === 3
                Head { title: "Who is this machine for?" }
                Row {
                    spacing: 18
                    Field {
                        label: "Username"; initial: root.aUser
                        hint: "lower-case, no spaces"
                        onTextChanged: root.aUser = text
                    }
                    Field {
                        label: "Full name (optional)"; initial: root.aFullname
                        onTextChanged: root.aFullname = text
                    }
                }
                Row {
                    spacing: 18
                    Field {
                        label: "Password"; secret: true
                        onTextChanged: root.aPass = text
                    }
                    Field {
                        label: "Password again"; secret: true
                        hint: root.aPass2.length > 0 && root.aPass !== root.aPass2 ? "They do not match" : ""
                        onTextChanged: root.aPass2 = text
                    }
                }
                Column {
                    spacing: 6
                    Text { text: "Desktop"; color: root.cDim; font.pixelSize: 12 }
                    Row {
                        spacing: 6
                        Choice {
                            width: 200; text: "SynapseUI"; subtext: "the native compositor"
                            checked: root.aDesktop === "synui"
                            onPicked: root.aDesktop = "synui"
                        }
                        Choice {
                            width: 150; text: "KDE Plasma"
                            checked: root.aDesktop === "kde"
                            onPicked: root.aDesktop = "kde"
                        }
                        Choice {
                            width: 130; text: "GNOME"
                            checked: root.aDesktop === "gnome"
                            onPicked: root.aDesktop = "gnome"
                        }
                        Choice {
                            width: 130; text: "None"; subtext: "headless"
                            checked: root.aDesktop === "tty"
                            onPicked: root.aDesktop = "tty"
                        }
                    }
                }
            }

            // ── 4: Region ───────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 14
                visible: root.page === 4
                Head {
                    title: "Language, keyboard and time"
                    blurb: "Pick a language and the other three follow it. The console keymap "
                         + "and the desktop layout are separate on purpose — Swedish is "
                         + "'sv-latin1' to the console and 'se' to the desktop — so they can be "
                         + "changed on their own afterwards."
                }
                Row {
                    spacing: 18
                    Selector {
                        label: "Language"
                        caption: root.aLangKnown && root.aLangLabel
                                 ? root.aLangLabel + "  —  " + root.aLocale
                                 : root.aLocale
                        hint: root.aLangKnown ? "sets the keyboard and the fonts too"
                                              : "typed by hand — fonts cover as much as possible"
                        onOpened: root.openPicker(
                            "language", "Language",
                            "Sets the locale, both keyboard names and the font pack. "
                            + "Any locale glibc has can be typed instead.",
                            root.aLocale)
                    }
                    Selector {
                        label: "Timezone"
                        caption: root.withDescription(root.timezones, root.aTz)
                        onOpened: root.openPicker(
                            "timezone", "Timezone",
                            "The common zones first, then every name tzdata ships.",
                            root.aTz)
                    }
                }
                Row {
                    spacing: 18
                    Selector {
                        label: "Console keymap"
                        caption: root.aKeymap
                        hint: "loadkeys — the text console and the greeter"
                        onOpened: root.openPicker(
                            "keymap", "Console keymap",
                            "Every keymap this image can load. This one names a file "
                            + "loadkeys has to find, which is why it is not the same list "
                            + "as the desktop layout.",
                            root.aKeymap)
                    }
                    Selector {
                        label: "Desktop layout"
                        caption: root.withDescription(root.xkbLayouts, root.aXkb)
                        hint: "XKB — the compositor"
                        onOpened: root.openPicker(
                            "xkb", "Desktop keyboard layout",
                            "The layouts xkbcommon can compile. 'uk' is a console keymap "
                            + "and not a layout here — the layout is 'gb'.",
                            root.aXkb)
                    }
                }
            }

            // ── 5: Summary ──────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12
                visible: root.page === 5
                Head {
                    title: "Read this back"
                    blurb: "Nothing has been written yet. The next button is the one that starts."
                }
                Rectangle {
                    width: parent.width
                    height: 44
                    radius: 6
                    color: Qt.rgba(root.cErr.r, root.cErr.g, root.cErr.b, 0.14)
                    border.width: 1
                    border.color: root.cErr
                    Text {
                        anchors.centerIn: parent
                        text: root.aMode === "erase"
                              ? "EVERY PARTITION ON " + root.aDisk + " WILL BE DELETED"
                              : "SynapseOS will be installed into the free space on " + root.aDisk
                        color: root.cText
                        font.pixelSize: 14
                        font.bold: true
                    }
                }
                Grid {
                    id: sumGrid
                    // Explicit, so the cells can be sized from it: a cell width
                    // derived from a Grid whose own width is derived from its
                    // cells is a loop.
                    width: parent.width
                    columns: 2
                    columnSpacing: 24
                    rowSpacing: 6
                    Repeater {
                        // A function, not a literal, so the row set can depend on
                        // the answers. The binding still re-evaluates: QML records
                        // every property the call reads, the same way the
                        // selectedDisk() call already relied on.
                        model: root.summaryRows()
                        Row {
                            width: (sumGrid.width - sumGrid.columnSpacing) / 2
                            spacing: 10
                            Text {
                                text: modelData[0]; color: root.cDim
                                font.pixelSize: 13; width: 96
                            }
                            Text {
                                text: modelData[1]; color: root.cText; font.pixelSize: 13
                                // A summary that runs off the edge of the panel is
                                // not a read-back. Half a cell is enough for every
                                // value here; the ones that need a whole line are
                                // below.
                                width: parent.width - 106
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                // Custom's answers, one full-width line each and wrapped rather
                // than elided: they are the part of this page that is not
                // recoverable from a glance at the disk.
                Column {
                    width: parent.width
                    spacing: 6
                    visible: root.aPreset === "custom"
                    Rectangle { width: parent.width; height: 1; color: root.cLine }
                    Repeater {
                        model: root.customRows()
                        Row {
                            width: sumGrid.width
                            spacing: 10
                            Text {
                                text: modelData[0]; color: root.cDim
                                font.pixelSize: 13; width: 96
                            }
                            Text {
                                text: modelData[1]
                                color: modelData[1].indexOf("WITHOUT") === 0 ? root.cWarn : root.cText
                                font.pixelSize: 13
                                width: parent.width - 106
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            // ── 6: Install ──────────────────────────────────────────────────
            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12
                visible: root.page === 6
                Head {
                    title: root.finished ? (root.exitCode === 0 ? "SynapseOS is installed" : "The install stopped")
                                         : "Installing SynapseOS"
                    blurb: root.finished && root.exitCode === 0
                           ? "Reboot and remove the installation media."
                           : root.finished ? "The log below is the whole story — the last lines say why."
                           : "This takes a while: the base system and the packages are downloaded, "
                           + "and an AI model is gigabytes on its own."
                }
                Rectangle {
                    width: parent.width
                    height: 28
                    radius: 4
                    color: Qt.rgba(1, 1, 1, 0.05)
                    clip: true

                    // Along the bottom of the status strip, running for as
                    // long as the install is.
                    ProgressTrack {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        active: root.running
                        visible: active
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.lastLine
                        color: root.finished && root.exitCode !== 0 ? root.cErr : root.cText
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        width: parent.width - 20
                    }
                }
                Rectangle {
                    width: parent.width
                    height: parent.height - y
                    radius: 6
                    color: root.isLight ? "#ffffff" : Qt.rgba(0, 0, 0, 0.25)
                    border.width: 1
                    border.color: root.cLine
                    clip: true
                    ListView {
                        id: logView
                        anchors.fill: parent
                        anchors.margins: 8
                        model: logModel
                        clip: true
                        delegate: Text {
                            required property string line
                            text: line
                            color: root.cDim
                            font.family: "monospace"
                            font.pixelSize: 11
                            width: logView.width
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // ── Footer ──────────────────────────────────────────────────────────
        Item {
            id: foot
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 16
            height: 40

            Text {
                id: problem
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: root.pageProblem(root.page)
                color: root.cWarn
                font.pixelSize: 12
                width: parent.width - 320
                elide: Text.ElideRight
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Btn {
                    text: "Back"
                    visible: root.page > 0 && root.page < 6
                    onClicked: root.page--
                }
                Btn {
                    text: "Next"
                    primary: true
                    visible: root.page < 5
                    canPress: root.pageProblem(root.page) === ""
                    onClicked: root.page++
                }
                Btn {
                    text: "Install"
                    primary: true
                    visible: root.page === 5
                    onClicked: root.startInstall()
                }
                Btn {
                    text: "Reboot"
                    primary: true
                    visible: root.page === 6 && root.finished && root.exitCode === 0
                    onClicked: rebootProc.running = true
                }
                Btn {
                    text: "Close"
                    visible: root.page === 6 && root.finished
                    onClicked: Qt.quit()
                }
            }
        }

        // ── The picker sheet ────────────────────────────────────────────────
        //
        // Last child of the frame, so it is drawn over the body AND the footer:
        // a sheet the Next button can be clicked through is a sheet that changes
        // the page underneath it while it is open.
        Rectangle {
            id: scrim
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.45)
            visible: root.pickKey !== ""

            // Closes on a click outside the sheet, and swallows every click that
            // would otherwise reach the form.
            MouseArea {
                anchors.fill: parent
                onClicked: root.closePicker()
            }

            Rectangle {
                id: sheet
                anchors.centerIn: parent
                width: Math.min(600, parent.width - 48)
                height: Math.min(460, parent.height - 48)
                radius: 8
                color: root.cPanel
                border.width: 1
                border.color: root.cLine

                // Declared BEFORE the content, so it sits under it: this one only
                // stops a click on the sheet's own background from reaching the
                // scrim's close handler. Declared after, it would swallow the
                // rows' clicks instead and nothing would ever be selectable.
                MouseArea { anchors.fill: parent }

                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    Text {
                        text: root.pickTitle
                        color: root.cText
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Text {
                        text: root.pickNote
                        color: root.cDim
                        font.pixelSize: 11
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: root.pickNote !== ""
                    }

                    // The filter box IS the free-text field. Whatever is typed
                    // here either narrows the list or becomes the answer through
                    // the row at the bottom — so the sheet never takes away what
                    // the text installer's "Other" offers.
                    Rectangle {
                        width: parent.width
                        height: 32
                        radius: 5
                        color: root.isLight ? "#ffffff" : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1
                        border.color: filterInput.activeFocus ? root.cAccent : root.cLine
                        TextInput {
                            id: filterInput
                            anchors.fill: parent
                            anchors.margins: 8
                            verticalAlignment: TextInput.AlignVCenter
                            color: root.cText
                            font.pixelSize: 14
                            selectByMouse: true
                            clip: true
                            // One direction only. `text: root.pickFilter` as well
                            // would look tidier and be a lie: assigning text
                            // below (to clear it) BREAKS that binding, and from
                            // then on the box and the filter it drives disagree
                            // whenever anything but typing changes pickFilter.
                            // The field owns the text; pickFilter follows it.
                            onTextChanged: root.pickFilter = text
                            // Opening the sheet is asking for the keyboard: the
                            // list is 599 rows and the answer is three letters.
                            Connections {
                                target: root
                                function onPickKeyChanged() {
                                    if (root.pickKey !== "") {
                                        filterInput.text = ""
                                        filterInput.forceActiveFocus()
                                    }
                                }
                            }
                            Keys.onEscapePressed: root.closePicker()
                            // Enter takes the first row, or the typed value when
                            // nothing matches — the two things Enter can mean
                            // here, in the order they are on screen.
                            Keys.onReturnPressed: {
                                if (root.pickFiltered.length > 0) {
                                    const o = root.pickFiltered[0]
                                    root.applyPick(o.value, o)
                                } else if (root.pickTyped !== "") {
                                    root.applyPick(root.pickTyped, null)
                                }
                            }
                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                text: "type to filter, or type a name that is not listed"
                                color: root.cDim
                                font.pixelSize: 13
                                visible: filterInput.text === ""
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        // Fills what the title, the note, the filter box and the
                        // typed-value row leave: `parent.height - y` is the
                        // remaining space in a Column, and the row below it is
                        // subtracted only when it is on screen.
                        height: parent.height - y - (typedRow.visible ? typedRow.height + parent.spacing : 0)
                        radius: 6
                        color: root.isLight ? "#ffffff" : Qt.rgba(0, 0, 0, 0.2)
                        border.width: 1
                        border.color: root.cLine
                        clip: true

                        ListView {
                            id: pickList
                            anchors.fill: parent
                            anchors.margins: 4
                            model: root.pickFiltered
                            clip: true
                            // The current answer, in view the moment the sheet
                            // opens — a picker that opens at the top of 599 rows
                            // does not show what is already chosen.
                            Connections {
                                target: root
                                function onPickOptionsChanged() {
                                    for (let i = 0; i < root.pickFiltered.length; i++) {
                                        if (root.pickFiltered[i].value === root.pickCurrent) {
                                            pickList.positionViewAtIndex(i, ListView.Center)
                                            return
                                        }
                                    }
                                    pickList.positionViewAtBeginning()
                                }
                            }
                            delegate: Rectangle {
                                id: prow
                                required property var modelData
                                width: pickList.width
                                height: 30
                                radius: 4
                                color: prow.modelData.value === root.pickCurrent
                                       ? Qt.rgba(root.cAccent.r, root.cAccent.g, root.cAccent.b, 0.16)
                                       : pma.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                                Row {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 10
                                    Text {
                                        text: prow.modelData.value
                                        color: root.cText
                                        font.pixelSize: 13
                                        font.family: "monospace"
                                    }
                                    Text {
                                        text: prow.modelData.label ? prow.modelData.label : ""
                                        color: root.cDim
                                        font.pixelSize: 12
                                    }
                                }
                                MouseArea {
                                    id: pma
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.applyPick(prow.modelData.value, prow.modelData)
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 24
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: root.pickOptions.length === 0
                                  ? "Nothing to list on this image — type the name instead."
                                  : "Nothing matches — the row below uses what you typed."
                            color: root.cDim
                            font.pixelSize: 12
                            visible: root.pickFiltered.length === 0
                        }
                    }

                    Rectangle {
                        id: typedRow
                        width: parent.width
                        height: 32
                        radius: 6
                        visible: root.pickTyped !== ""
                        color: tma.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
                        border.width: 1
                        border.color: root.cAccent
                        Text {
                            anchors.centerIn: parent
                            text: "Use “" + root.pickTyped + "” as typed"
                            color: root.cText
                            font.pixelSize: 13
                        }
                        MouseArea {
                            id: tma
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.applyPick(root.pickTyped, null)
                        }
                    }
                }
            }
        }
    }

    Process {
        id: rebootProc
        command: ["systemctl", "reboot"]
        running: false
    }
}
