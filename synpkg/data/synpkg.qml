//@ pragma UseQApplication
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io
// The translation singleton, in qml/ beside this file. See qml/qmldir.
import "qml"

/*
 * SYNAPSE Software — the graphical front-end for synpkg.
 *
 * Every fact on screen comes from `synpkg --tsv <command>`, the same binary
 * the CLI and the terminal browser are, so the three can never disagree about
 * what is installed. Nothing here knows what a package IS; it renders rows.
 *
 * TSV rather than JSON across that boundary: the C side would have to escape
 * JSON by hand, and a package description containing a quote is not a hypo-
 * thetical. Parsing here is a split on newline and a split on tab.
 *
 * ── One tab per SOURCE ──────────────────────────────────────────────────────
 * The first version had a single "Search" pane fed by the repositories alone,
 * with the AUR and Flatpak reachable only from the command line. That made the
 * source of a row a thing you inferred from a small grey word. Now each source
 * owns a tab — Repositories, AUR, Flathub, Arsenal, SynapseOS — every row
 * carries a coloured badge naming where it came from, and every row remembers
 * its own source so the install button runs the RIGHT tool for it. The three
 * do genuinely different things: pacman transacts, makepkg builds from source
 * and needs a terminal, flatpak runs its own permission prompts.
 *
 * Rows are read by COLUMN NAME out of each command's header row, not by
 * position. Flatpak rows carry a seventh `title` column that no other source
 * emits, and a positional parser would have needed a special case per command
 * to cope with that — the exact shape of bug that renders as silently blank
 * cells with no error anywhere.
 *
 * The palette block is lifted from syn-arsenal's, deliberately unchanged — it
 * encodes two shipped bugs (reading theme.json's [r,g,b] ARRAYS as strings,
 * and taking ink from the theme while drawing on hardcoded surfaces) and this
 * window would reproduce both.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
FloatingWindow {
    id: root

    title: "SYNAPSE Software"
    implicitWidth: 1180
    implicitHeight: 760

    // Same failure as synfiles, same cause: fixed furniture either side of a
    // flexible middle, with no floor. nav is 176, catPane is 230, and the
    // toolbar's mode toggle is three hard-coded 84px buttons — 252 before the
    // search box gets a pixel. Narrower than the sum and the search box has
    // negative width, so its placeholder runs off the edge and gets sliced
    // mid-word ("filte" in the 2026-08-09 22:49 screenshot, taken mid-resize).
    //
    // 700 = 176 + 230 + 252 + margins, i.e. the width at which every fixed
    // element still fits and the search box is merely small rather than absent.
    minimumSize: Qt.size(700, 420)

    // ShellRoot outlives its window: without this, quickshell stays alive with
    // nothing on screen and every later launch exits 0 having drawn nothing.
    onClosed: Qt.quit()

    readonly property string bin: Quickshell.env("SYNPKG_BIN") || "synpkg"

    // ── Palette ─────────────────────────────────────────────────────────────
    property var p: ({})
    readonly property bool isLight: p.scheme === "light"

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/theme.json"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: { try { root.p = JSON.parse(this.text()) } catch (e) { root.p = ({}) } }
        onLoadFailed: root.p = ({})
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

    function themed(key, r, g, b, a) {
        const c = root.p[key]
        return (c && c.length === 3) ? Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, a)
                                     : Qt.rgba(r / 255, g / 255, b / 255, a)
    }
    function pick(dark, light) { return root.isLight ? light : dark }

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

    readonly property color cPanel: themed("bar", 11, 11, 20, 1.0)
    readonly property color cBg: isLight ? Qt.lighter(cPanel, 1.15) : Qt.darker(cPanel, 1.4)
    readonly property color cInk: p.fg ? Qt.color(p.fg) : pick("#e6e9ef", "#12141a")
    readonly property color cText: contrast(cInk, cBg) >= 4.5
                                   ? cInk
                                   : (lum(cBg) > 0.18 ? "#12141a" : "#e6e9ef")
    readonly property color cDim: pick("#8b93a7", "#4a5568")
    readonly property color cAccentRaw: root.wpAccent !== ""
                                        ? Qt.color(root.wpAccent)
                                        : themed("accent", 78, 201, 176, 1.0)
    readonly property color cAccent: readable(cAccentRaw, cPanel, 4.5)
    readonly property color cWarn: pick("#e0af68", "#5c3a00")
    readonly property color cOk: readable(Qt.color(pick("#7ee787", "#0b6b2f")), cBg, 4.5)

    function wash(a) { return Qt.rgba(cAccent.r, cAccent.g, cAccent.b, a) }

    // ── The UI font ─────────────────────────────────────────────────────────
    // Lifted from syn-settings for the same reason the palette block above is
    // lifted from syn-arsenal's. font.state is written by synui-apply-font(1)
    // and is deliberately NOT a key in theme.json — the font outlives a theme
    // switch. It carries the desktop's family AND its text scale, and an app
    // that reads one without the other still looks wrong beside its siblings.
    //
    // Qt resolves an application's default font ONCE at startup, so both the
    // family and the size have to be BINDINGS on every Text — which is why the
    // control panel's font picker moved Settings and Files and left this window
    // and Arsenal on the old face until they were reopened.
    property string uiFont: ""
    property int textScale: 100
    function ui(px) { return Math.max(6, Math.round(px * root.textScale / 100)) }

    /*
     * ⛔ A LIST OF EVERY PACKAGE ON THE SYSTEM WITH NO SCROLLBAR. Thousands of
     * rows, and nothing on screen saying how far down they went or any way to
     * get there but a wheel — which is the complaint this exists to answer.
     *
     * Same component, same reasoning, as the assistant's window: visible at
     * rest, because `active` is true in every state except the one where
     * somebody is deciding whether there is more to see. AsNeeded, so a short
     * list draws no furniture. Pinned by preflight's `scrollbar` gate.
     */
    component SynScrollBar: ScrollBar {
        id: sb
        policy: ScrollBar.AsNeeded
        /*
         * ⛔ NOTHING TO SCROLL MEANS NO SCROLLBAR AT ALL. AsNeeded hides the
         * handle by fading its OPACITY, and a custom contentItem replaces the
         * binding that does it — so a bar styled to be visible at rest became
         * visible at rest everywhere, a full-length handle that cannot move
         * sitting on every short list on the desktop. velle, 2026-08-28:
         * "if there's nothing to scroll the scrollbar should autohide. i don't
         * need the fucking scrollbars literally everywhere when they can't even
         * do anything."
         *
         * `size` is the fraction of the content the view can show: 1.0 means it
         * all fits. Visible at rest is for the case where there IS more — that
         * is the whole point of it — and is clutter in every other case.
         */
        readonly property bool needed:
            sb.policy === ScrollBar.AlwaysOn ||
            (sb.policy === ScrollBar.AsNeeded && sb.size < 1.0)
        visible: sb.needed
        implicitWidth: root.ui(11)
        padding: root.ui(2)

        contentItem: Rectangle {
            implicitWidth: root.ui(7)
            radius: width / 2
            color: sb.pressed ? root.cAccent : sb.hovered ? root.cText : root.cDim
            opacity: sb.pressed || sb.hovered ? 1.0 : 0.5
            Behavior on color   { ColorAnimation  { duration: 90 } }
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }

        background: Rectangle {
            radius: width / 2
            color: Qt.rgba(root.cText.r, root.cText.g, root.cText.b, 0.08)
            opacity: sb.hovered || sb.pressed ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/font.state"
        watchChanges: true
        // No font.state is the normal case on a box where nobody has picked
        // one; a warning per start for an expected miss is how a log becomes
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

    // ── Sources ─────────────────────────────────────────────────────────────
    // Which tool owns a row. Every list sets this per row, because the badge
    // that tells the USER where a package came from and the code path that
    // installs it must never be able to disagree.
    function sourceOf(repo) {
        const r = (repo || "").toLowerCase()
        if (r === "aur") return "aur"
        if (r === "flathub" || r === "flatpak") return "flathub"
        if (r === "synapseos") return "repo"
        return "repo"
    }

    // Badge hues are fixed rather than themed: the point of a source badge is
    // that "purple means built from source" survives a theme change. They are
    // still run through readable() against the surface they land on.
    function sourceHue(repo) {
        const r = (repo || "").toLowerCase()
        if (r === "aur") return "#a78bfa"
        if (r === "flathub" || r === "flatpak") return "#60a5fa"
        if (r.indexOf("blackarch") === 0) return "#f87171"
        if (r === "synapseos") return root.cAccentRaw
        if (r === "local") return "#94a3b8"
        return "#34d399"
    }
    function sourceColor(repo) {
        return root.readable(Qt.color(root.sourceHue(repo)), root.cBg, 4.5)
    }

    // ── A bar that is honest about what it knows ────────────────────────────
    //
    // The same component syn-settings draws, on purpose: the two windows show
    // the same kind of wait and looking alike is most of what makes a desktop
    // feel like one system rather than four applications.
    //
    // With a percentage it fills to it; without one it runs a shuttle back and
    // forth, which says "working" without claiming to know how much is left.
    // The alternative — a bar that creeps to 90% and waits — is a lie the user
    // finds out about. synpkg has no percentage to give yet: an install is one
    // libalpm transaction whose output this window collects at the end, so the
    // shuttle is the truthful state, not a placeholder for a real one.
    component ProgressTrack: Rectangle {
        id: track
        property int pct: -1
        property bool active: false
        height: 3
        radius: height / 2
        color: "transparent"
        clip: true

        Rectangle {
            visible: track.pct >= 0
            height: parent.height
            radius: parent.radius
            width: parent.width * Math.max(0, Math.min(100, track.pct)) / 100
            color: root.cAccent
            Behavior on width { NumberAnimation { duration: 200 } }
        }

        Rectangle {
            id: shuttle
            visible: track.active && track.pct < 0
            width: Math.max(48, track.width * 0.22)
            height: parent.height
            radius: parent.radius
            color: root.cAccent
            opacity: 0.8
            x: -width
        }

        // from/to are read at (re)start, NOT bound — so a window resize has to
        // restart it, or the shuttle keeps sweeping the width the window used
        // to have and stops short of the new edge.
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

    readonly property var sections: [
        { id: "updates",   label: I18n.tr("Updates"),      kind: "list" },
        { id: "held",      label: I18n.tr("Held back"),    kind: "list" },
        { id: "suggested", label: I18n.tr("Suggested"),    kind: "list" },
        // Above the three single-source tabs because it is the one to reach for
        // when you do not already know WHICH source has the thing. The three
        // stay: "search only the AUR" is a real question, and answering it from
        // a list of four hundred rows from everywhere is not the same as asking
        // it.
        { id: "all",       label: I18n.tr("All sources"),  kind: "source" },
        { id: "repo",      label: I18n.tr("Repositories"), kind: "source" },
        { id: "aur",       label: I18n.tr("AUR"),          kind: "source" },
        { id: "flathub",   label: I18n.tr("Flathub"),      kind: "source" },
        { id: "arsenal",   label: I18n.tr("Arsenal"),      kind: "list" },
        { id: "system",    label: I18n.tr("SynapseOS"),    kind: "list" },
        { id: "about",     label: I18n.tr("About"),        kind: "about" }
    ]

    function sectionKind(id) {
        for (const s of root.sections)
            if (s.id === id) return s.kind
        return "list"
    }
    readonly property bool isSourceTab: root.sectionKind(root.section) === "source"

    // What each tab is, in one line, under the title. Half of these are only
    // obvious if you already know the packaging landscape.
    // ⛔ `id` IS THE SECTION KEY, matched here and written to the state file;
    // only the sentences it returns are words.
    function sectionHint(id) {
        if (id === "updates")   return I18n.tr("everything with a newer version — repositories, the AUR, Flathub and SynapseOS's own components")
        if (id === "held")      return I18n.tr("updates you are deliberately not taking. Release one and it comes back on the next check")
        if (id === "suggested") return I18n.tr("the curated SynapseOS software list")
        if (id === "all")       return I18n.tr("every source at once — the repositories, BlackArch, the AUR and Flathub. Each result says where it came from")
        if (id === "repo")      return I18n.tr("official Arch repositories and SynapseOS's own — signed, binary, managed by pacman")
        if (id === "aur")       return I18n.tr("the Arch User Repository — recipes built from source on this machine")
        if (id === "flathub")   return I18n.tr("sandboxed applications from Flathub, with their own runtimes")
        if (id === "arsenal")   return I18n.tr("BlackArch security tooling, by category")
        if (id === "system")    return I18n.tr("this system's own components, rebuilt from git by syn-update")
        return ""
    }

    // ── State ───────────────────────────────────────────────────────────────
    // `synpkg gui flathub` opens straight on that tab, so the start menu can
    // point separate entries at separate sources. An unrecognised name falls
    // back to Updates rather than opening a blank window on a section that
    // does not exist.
    property string section: {
        // A term to open on (SYNPKG_QUERY, below) has to arrive somewhere it
        // can be searched. It comes from a launcher that ran out of local
        // answers, so All sources is the tab it means — and naming a section
        // as well is how you ask for that term on THAT source.
        const want = Quickshell.env("SYNPKG_SECTION") || ""
        for (const s of root.sections)
            if (s.id === want) return want
        return root.openQuery !== "" ? "all" : "updates"
    }

    /*
     * `SYNPKG_QUERY=<term> synpkg gui` opens already searching for it.
     *
     * This is the far end of the escape hatch synui's start menu and command
     * bar offer: both look a typed name up with `synpkg provides`, which asks
     * the local repositories only because it runs on a keystroke's budget, and
     * both end their list with a row that means "now ask EVERYWHERE" — the
     * AUR and Flathub included, which is a network round trip and a 48MB
     * appstream scan and has no business happening while somebody types.
     *
     * In the environment rather than as an argument for the same reason
     * SYNPKG_SECTION is: `synpkg gui` execs quickshell, which takes no
     * arguments of its own.
     */
    readonly property string openQuery: Quickshell.env("SYNPKG_QUERY") || ""
    property string mode: "browse"    // source tabs: browse | search | installed
    property string busy: ""          // package currently being (un)installed
    property string statusLine: ""
    property bool   loading: false

    // ── What a FAILED transaction leaves on screen ──────────────────────────
    //
    // `statusLine` cannot carry a failure, and that is not a style choice: every
    // finished action calls reload(), and reload() clears it. Anything written
    // there at exit lives for milliseconds.
    //
    // So `remove kitty` authenticated through polkit, was refused by libalpm —
    // synui depended on kitty, so the prepare failed with "synui: requires
    // kitty" — and the window cleared the status line, re-read the list, and
    // showed kitty still installed with nothing anywhere saying why. Every
    // other failed transaction was equally silent: actProc read neither the
    // exit code nor stderr.
    //
    // `outcome` survives the reload, is ranked above the item count in the
    // header, and is cleared when the next action starts. Same fix, same
    // reasoning and same name as syn-settings' — a message with the lifetime
    // of the thing that failed is a message nobody can read.
    property string outcome: ""

    // Assumed true until `flatpak remotes` says otherwise. The banner this
    // gates is an instruction to go and fix something, and showing it for the
    // half-second before the probe answers would put a "your Flathub is
    // broken" message in front of somebody whose Flathub is fine.
    property bool flathubEnabled: true

    property var rows: []             // canonical row objects, see pkgRows()
    property var aboutRows: []
    property var categories: []
    property string currentGroup: ""
    property string filter: ""

    readonly property var shownRows: {
        if (filter === "") return rows
        const f = filter.toLowerCase()
        return rows.filter(r => r.name.toLowerCase().includes(f)
                             || (r.title || "").toLowerCase().includes(f)
                             || (r.desc || "").toLowerCase().includes(f))
    }

    // ── Parsing ─────────────────────────────────────────────────────────────
    // Read the header row and key every field by NAME. Every --tsv command
    // emits its header first, and this is what lets one reader serve six
    // commands with different column counts.
    function parseTable(text) {
        const lines = text.split("\n").filter(l => l !== "")
        if (lines.length === 0) return []
        const cols = lines[0].split("\t")
        const out = []
        for (let i = 1; i < lines.length; i++) {
            const f = lines[i].split("\t")
            const o = ({})
            for (let c = 0; c < cols.length; c++)
                o[cols[c]] = f[c] !== undefined ? f[c] : ""
            out.push(o)
        }
        return out
    }

    // The canonical row. `source` is the tool that owns it; `repo` is the
    // human label shown on the badge; they are not the same thing (a row from
    // core and a row from blackarch are both `repo`).
    function makeRow(name, title, installed, version, repo, size, desc, extra, source, ignored) {
        return { name: name, title: title || "", installed: installed,
                 version: version || "", repo: repo || "", size: size || 0,
                 desc: desc || "", extra: extra || "", source: source,
                 ignored: ignored === true }
    }

    /*
     * ── The icon for a row ──────────────────────────────────────────────────
     *
     * Resolved here rather than in C for the reason synfiles resolves its own
     * the same way: quickshell already has the icon theme loaded, and the C
     * side would otherwise have to walk every theme directory to answer a
     * question the renderer is holding the answer to.
     *
     * ⚠ A PACKAGE IS NOT AN APPLICATION and most of them have no icon at all —
     * a library, a font, a kernel module. So this is a CHAIN that is allowed to
     * come up empty, and the delegate draws a monogram when it does, exactly as
     * the dock does for an app whose .desktop names an icon nothing ships. A
     * blank column where two thirds of the rows should be would read as icons
     * being broken rather than as most packages not having one.
     *
     * Two tries, and `name` carries both cases because it is the same field for
     * both kinds of row — a package's name IS `htop`, and a Flatpak's name IS
     * `org.mozilla.firefox`, which by freedesktop convention is also its icon:
     *
     *   1. The name verbatim. Resolves `htop`, `firefox`, `kitty`, `vlc` — what
     *      an Arch package installs its icon as — and an installed Flatpak on
     *      its ref.
     *   2. The last dot-segment, lower-cased, so `org.mozilla.firefox` still
     *      finds `firefox` when the Flatpak is not installed but the native
     *      package's icon is on disk. Lower-cased because icon theme names are
     *      and `Boxes` would miss `boxes`.
     *
     * Quickshell.iconPath(name, true) returns "" rather than throwing when the
     * theme has not got it, which is what makes a chain like this cheap.
     */
    function iconFor(row) {
        const id = row.name || ""
        if (!id) return ""

        let p = Quickshell.iconPath(id, true)
        if (p) return p

        const tail = id.split(".").pop()
        if (tail && tail !== id) {
            p = Quickshell.iconPath(tail.toLowerCase(), true)
            if (p) return p
        }

        /*
         * ── Last: what the package's own .desktop calls its icon ────────────
         *
         * ⚠ AN APPLICATION'S ICON IS OFTEN NOT NAMED AFTER ITS PACKAGE, and the
         * two tries above only ever ask that. `retroarch` installs
         * `com.libretro.RetroArch`, `openrgb` installs `org.openrgb.OpenRGB`,
         * `calibre` installs `calibre-gui` — so the rows a person actually
         * recognises in a package list were the ones coming up blank, which is
         * what "very few icons" looks like from outside.
         *
         * ⚠ IT IS A SMALL RESCUE AND THAT IS EXPECTED. Measured on this box, 34
         * of 187 explicitly-installed packages resolve by name and this finds 6
         * more. The other 147 have no icon under any name because they are not
         * applications — libraries, fonts, kernel modules, CLI tools — and the
         * monogram is the right answer for them, not a bug. The point of this
         * step is the handful it rescues, all of which are things with a face.
         */
        const named = root.desktopIcons[id.toLowerCase()]
        if (named) {
            /* An absolute path is a real file, which is what some third-party
             * launchers write; a bare name goes through the theme. */
            if (named.indexOf("/") === 0) return "file://" + named
            p = Quickshell.iconPath(named, true)
            if (p) return p
        }

        /*
         * ── Last: the icon a package HAS NOT INSTALLED YET ──────────────────
         *
         * ⛔ EVERY STEP ABOVE ASKS THE LOCAL DISK, so every one of them can only
         * answer for software that is already here. That is why the suggested
         * list — 105 applications, most of them by definition NOT installed —
         * came up as a column of monograms with a face on the handful you
         * happened to own: the lookups were not failing, there was simply
         * nothing on disk to find. Measured on this box, 15 of 104 curated ids
         * resolved, and every one of the 15 was installed.
         *
         * A software centre solves this with AppStream: the distribution ships
         * a catalogue of every application in its repositories, icons included,
         * so a list can draw something for a package you have never had. That
         * is what archlinux-appstream-data is, it is what GNOME Software and
         * Discover read, and synpkg is that kind of program. It adds 52 of the
         * remaining 89 curated rows. The rest are `git`, `ripgrep`, `tmux` —
         * command-line tools with no icon anywhere because they have no face,
         * and the monogram is the correct answer for them.
         */
        const key = root.appstreamAliases[id.toLowerCase()] || id.toLowerCase()
        const cached = root.appstreamIcons[key]
        if (cached) return cached

        return ""
    }

    /*
     * package/binary name -> the Icon= of a .desktop that claims it.
     *
     * Built ONCE per window from one scan, not per row: /usr/share/applications
     * is a few hundred files, and asking it per row would be that scan times the
     * length of a search result.
     *
     * Keyed on BOTH the .desktop basename and its Exec binary, lower-cased,
     * because neither alone matches a package reliably — `openrgb` matches by
     * Exec (the file is org.openrgb.OpenRGB.desktop) and `calibre` by neither
     * exactly, which is why the value is what gets looked up rather than the key.
     * First writer wins: a desktop file that mentions a name is a better claim
     * than a later one that happens to share an Exec.
     */
    property var desktopIcons: ({})

    Process {
        id: iconMapProc
        running: true
        /* One awk over the .desktop files, emitting `key\ticon`. In a Process
         * rather than QML because QML cannot list a directory, and going through
         * the shell keeps the whole scan to one fork instead of one per file.
         *
         * ⚠ ENDFILE IS GNU awk's, not POSIX. Arch's `base` pulls in gawk so it
         * is there on every SynapseOS install, and the failure mode if it ever
         * is not is the honest one: awk errors, the map stays empty, and every
         * row falls back to the two name tries above — which is where this
         * started. Nothing breaks; six rows lose an icon. */
        command: ["sh", "-c",
            "awk -F= '" +
            "FNR==1 { base=FILENAME; sub(/.*\\//,\"\",base); sub(/\\.desktop$/,\"\",base); icon=\"\"; exe=\"\" } " +
            "/^Icon=/ && icon==\"\" { icon=$2 } " +
            "/^Exec=/ && exe==\"\"  { split($2,a,\" \"); exe=a[1]; sub(/.*\\//,\"\",exe) } " +
            "ENDFILE { if (icon != \"\") { print tolower(base) \"\\t\" icon; " +
            "          if (exe != \"\") print tolower(exe) \"\\t\" icon } }' " +
            "/usr/share/applications/*.desktop 2>/dev/null"]
        stdout: StdioCollector {
            onStreamFinished: {
                const m = {}
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    /* First writer wins — see the note above. */
                    if (f.length === 2 && f[0] && f[1] && m[f[0]] === undefined)
                        m[f[0]] = f[1]
                }
                root.desktopIcons = m
            }
        }
    }

    /*
     * package name -> the icon file AppStream cached for it.
     *
     * Built the same way and for the same reason as desktopIcons above: one
     * scan per window, not one per row.
     *
     * ⚠ THE PACKAGE NAME IS IN THE FILE NAME, so this needs no XML and no
     * parser. A cached AppStream icon is `<pkgname>_<iconname>.png` under
     * `<origin>/<size>/`, which is exactly the two facts a lookup needs. The
     * split is at the FIRST underscore — that is the convention, and of the
     * 1,205 icons Arch ships today one file (`jack_mixer_jack_mixer.png`) has a
     * package name containing one, so `jack` is the single key here that could
     * name the wrong picture. Nothing in the curated list is called that, and a
     * mis-split can only ever produce a key no package matches.
     *
     * ⚠ AND IT IS ALLOWED TO FIND NOTHING. archlinux-appstream-data is a
     * dependency, but a container, a chroot or a half-finished install may not
     * have it — `find` then prints nothing, the map stays empty and every row
     * falls back to the monogram, which is where this started.
     */
    property var appstreamIcons: ({})

    /*
     * The handful of curated packages whose AppStream icon is filed under
     * another package's name.
     *
     * ⚠ A TABLE, NOT A HEURISTIC. Two package names sharing a prefix is not
     * evidence they are the same program — `wine` and `winetricks` are filed
     * next to each other and are different things, and a prefix match would put
     * winetricks' picture on Wine. Each line here is a judgement that the two
     * names are one application, and today there is exactly one: Arch's
     * AppStream data carries the LibreOffice suite icons under
     * libreoffice-still, while the curated list recommends libreoffice-fresh.
     */
    readonly property var appstreamAliases: ({ "libreoffice-fresh": "libreoffice-still" })

    Process {
        id: appstreamProc
        running: true
        /* Both roots: the package installs under /usr/share, and appstreamcli's
         * own refresh writes to /var/cache. Whichever exists is scanned. */
        command: ["sh", "-c",
            "find /usr/share/swcatalog/icons /var/cache/swcatalog/icons " +
            "-mindepth 3 -maxdepth 3 -type f -name '*_*.png' " +
            "-printf '%h\t%f\t%p\n' 2>/dev/null"]
        stdout: StdioCollector {
            onStreamFinished: {
                /* Biggest wins. The tile is 32px and the Image decodes to
                 * sourceSize, so a 128px source costs nothing extra and is the
                 * one that still looks right on a HiDPI screen. */
                const rank = { "128x128": 3, "64x64": 2, "48x48": 1 }
                const best = {}
                const lines = this.text.split("\n")
                for (let i = 0; i < lines.length; i++) {
                    const f = lines[i].split("\t")
                    if (f.length !== 3 || !f[1]) continue
                    const cut = f[1].indexOf("_")
                    if (cut <= 0) continue
                    const pkg = f[1].slice(0, cut).toLowerCase()
                    const r = rank[f[0].slice(f[0].lastIndexOf("/") + 1)] || 0
                    if (best[pkg] === undefined || r > best[pkg].r)
                        best[pkg] = { r: r, path: "file://" + f[2] }
                }
                const m = {}
                for (const k in best) m[k] = best[k].path
                root.appstreamIcons = m
            }
        }
    }

    /* The letter drawn when there is no icon, and the colour behind it.
     *
     * The SOURCE's hue, not a random one: a row with no icon still says where
     * it came from, and tinting the placeholder by source makes the badge and
     * the tile agree instead of introducing a second colour language. */
    function iconLetter(row) {
        const t = (row.title && row.title !== "") ? row.title : (row.name || "?")
        /* The first letter of the last dot-segment, so org.mozilla.firefox is F
         * and not O — the reverse-DNS prefix is the vendor, not the app. */
        const seg = t.indexOf(".") > 0 ? t.split(".").pop() : t
        return (seg || "?").charAt(0).toUpperCase()
    }

    function pkgRows(table, tab) {
        return table.map(r => root.makeRow(r.name, r.title, r.installed === "1",
                                           r.version, r.repo, parseInt(r.size || "0"),
                                           r.description, r.flag,
                                           tab || root.sourceOf(r.repo)))
    }

    function updateRows(table, tab) {
        // An empty new_version is not missing data: Flatpak genuinely cannot
        // report one without a round trip per app. Say so rather than showing
        // "1.2.3 →  " with a blank after the arrow.
        //
        // A held row is MARKED, never dropped. Filtering it here would make
        // the one page somebody opens to ask "is anything out of date?" answer
        // "no" while an update sits deliberately unapplied — which is how a
        // package stays pinned for a year. The `ignored` column is carried by
        // all three sources for exactly this.
        return table.map(r => root.makeRow(
            r.name, "", true, r.new_version, r.repo, parseInt(r.size || "0"),
            // ⚠ THE ARROW IS PART OF THE SENTENCE. Built by concatenation it
            // cannot be moved, and a right-to-left language needs it mirrored.
            I18n.tr("%1  →  %2").arg(r.installed_version || "?")
                                .arg(r.new_version || I18n.tr("update available")),
            "update", tab, r.ignored === "1"))
    }

    // ── Held back ───────────────────────────────────────────────────────────
    //
    // `synpkg ignore` is the repositories, the AUR and Flathub in one listing;
    // `syn-update ignored` is the SynapseOS components, which are held in a
    // different file and released by a different command. Two steps, one page,
    // because "what am I holding back" is one question.
    function heldRows(table) {
        return table.map(r => root.makeRow(
            r.name, "", r.installed === "1", r.new_version,
            r.source === "flatpak" ? "flathub" : r.source, 0,
            r.new_version
                ? I18n.tr("%1  →  %2   held back").arg(r.installed_version || "?")
                                                  .arg(r.new_version)
                : (r.installed_version
                   ? I18n.tr("%1   held back, no update waiting").arg(r.installed_version)
                   : I18n.tr("held back")),
            "held",
            r.source === "flatpak" ? "flathub" : "repo", true))
    }

    function heldComponentRows(table) {
        return table.map(r => root.makeRow(
            r.name, "", true, "", "synapseos", 0,
            I18n.tr("%1   held back").arg(r.installed || "?"),
            "held", "system", true))
    }

    function suggestRows(table) {
        return table.map(r => root.makeRow(
            r.id, r.label, r.installed === "1", "",
            r.source === "flatpak" ? "flathub" : r.source, 0,
            r.description, r.category,
            r.source === "flatpak" ? "flathub" : (r.source === "aur" ? "aur" : "repo")))
    }

    // `update`, not `component`. The tag is what decides whether a row gets an
    // action button at all, and tagging these "component" is what left the
    // SynapseOS tab listing updates with no way to take them: the pane
    // reported work to do and offered nothing that did it.
    //
    // The source stays "system", which is what routes the click to syn-update
    // instead of into an ALPM transaction. See act().
    // ── The one stored preference this window touches ───────────────────────
    //
    // Read from and written to the binary, never a file this QML owns. The
    // optimistic local update is so the checkbox does not lag a subprocess by
    // a frame; confProc re-reads afterwards, so the binary still has the last
    // word if the write failed.
    property bool upgradeSystem: true

    function setUpgradeSystem(on) {
        root.upgradeSystem = on
        setConfProc.command = [root.bin, "config", "upgrade_system", on ? "yes" : "no"]
        setConfProc.running = true
    }

    Process {
        id: confProc
        command: [root.bin, "--tsv", "config"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                for (const r of root.parseTable(this.text))
                    if (r.key === "upgrade_system")
                        root.upgradeSystem = r.value !== "no"
            }
        }
    }

    Process {
        id: setConfProc
        onExited: confProc.running = true
    }

    function systemRows(table) {
        return table.map(r => root.makeRow(
            r.component, "", true, r.available, "synapseos", 0,
            (r.installed || "") + "  →  " + (r.available || ""),
            "update", "system"))
    }

    // ── Backend ─────────────────────────────────────────────────────────────
    // A step is {kind, tab, args}. Steps run in sequence and APPEND, which is
    // what lets the Updates tab be the union of pacman, the AUR and Flatpak
    // without any of the three knowing about the others.
    property var chain: []

    Process {
        id: listProc
        property string kind: ""
        property string tab: ""

        stdout: StdioCollector {
            onStreamFinished: {
                const table = root.parseTable(this.text)
                let out = []

                if (listProc.kind === "categories") {
                    // `name` is what goes back to the CLI, `label` is what the
                    // pane draws. Four sources emit this shape and none of
                    // them agrees on the two being the same string:
                    // blackarch-webapp displays as "webapp", Flathub's "Game"
                    // displays as "Games".
                    root.categories = table.map(r => ({
                        name: r.category, label: r.label || r.category,
                        total: parseInt(r.total || "0"),
                        installed: parseInt(r.installed || "0")
                    }))
                } else if (listProc.kind === "remotes") {
                    // The one fact the Flathub tab cannot infer from its own
                    // rows: an empty list means "no remote" and "remote with
                    // nothing matching" equally, and those need opposite
                    // advice.
                    root.flathubEnabled = table.some(r => r.remote === "flathub")
                } else if (listProc.kind === "about") {
                    root.aboutRows = table
                } else if (listProc.kind === "updates") {
                    out = root.updateRows(table, listProc.tab)
                } else if (listProc.kind === "held") {
                    out = root.heldRows(table)
                } else if (listProc.kind === "heldcomp") {
                    out = root.heldComponentRows(table)
                } else if (listProc.kind === "suggest") {
                    out = root.suggestRows(table)
                } else if (listProc.kind === "system") {
                    out = root.systemRows(table)
                } else {
                    out = root.pkgRows(table, listProc.tab)
                }

                if (out.length > 0)
                    root.rows = root.rows.concat(out)

                // Starting the next process from inside this handler would
                // re-enter a Process that has not finished tearing down; let
                // the event loop turn over first.
                Qt.callLater(root.runNext)
            }
        }
    }

    function runChain(steps) {
        root.rows = []
        root.chain = steps
        root.loading = true
        root.runNext()
    }

    function runNext() {
        if (root.chain.length === 0) {
            root.loading = false
            root.statusLine = ""
            return
        }
        const step = root.chain[0]
        root.chain = root.chain.slice(1)
        root.statusLine = step.note || ""
        listProc.kind = step.kind
        listProc.tab = step.tab || ""
        listProc.command = [root.bin, "--tsv"].concat(step.args)
        listProc.running = true
    }

    // Mutations re-read the pane rather than patching the row in place: an
    // install pulls dependencies, a remove takes unneeded ones with it, and a
    // list that disagrees with the disk is worse than a slower refresh.
    // No parameters on this handler, deliberately. Quickshell's `exited` signal
    // is exited(int, QProcess::ExitStatus), and a typed arrow-function handler
    // fails to compile because that second type is not resolvable from QML —
    // the linter flags it, and the handler would simply never run.
    Process {
        id: actProc
        property string errLine: ""

        // StdioCollector, not SplitParser: this text is only wanted once the
        // transaction is over, and what is wanted is the FIRST warning. alpm
        // reports the specific reason first ("synui: requires kitty") and the
        // generic one last ("transaction failed: could not satisfy
        // dependencies"), so the last line is the one that says least.
        stderr: StdioCollector {
            onStreamFinished: {
                if (!this.text) return
                const first = this.text.split("\n").find(l => l.trim() !== "") || ""
                // synpkg prefixes every warning; the window has its own way of
                // showing that something went wrong and does not need the word.
                actProc.errLine = first.replace(/^warning:\s*/, "")
                // Whether this arrives before or after onExited is not ours to
                // decide, so handle both: if the exit already settled for a
                // weaker message, replace it with the real one.
                if (root.busy === "" && root.outcome !== "")
                    root.outcome = actProc.errLine
            }
        }

        // One parameter, not none. The signal is exited(int, QProcess::
        // ExitStatus) and qmllint warns that the second type is unresolvable —
        // but declaring FEWER parameters than a signal has is legal QML, and
        // the same spelling is what carries syn-settings' outcome. Reading no
        // parameters at all is what made this window unable to tell a refusal
        // from a success in the first place.
        onExited: (code) => {
            root.busy = ""
            root.statusLine = ""
            if (code !== 0)
                root.outcome = actProc.errLine !== "" ? actProc.errLine
                    : I18n.tr("refused (exit %1) — polkit may have declined").arg(code)
            root.reload()
        }
    }

    // Everything handed to a terminal can change what the pane should show —
    // enabling Flathub, bootstrapping BlackArch, building from the AUR, a full
    // upgrade. Re-reading when the terminal closes is what stops "Enable
    // Flathub" from still being on screen after you enabled Flathub.
    //
    // No parameters on this handler, for the same reason as actProc's below.
    Process {
        id: termProc
        onExited: root.reload()
    }

    // Hand a command to a terminal. syntty first: it is SynapseOS's default,
    // it is the one terminal every install profile has, and as of 0.1.0-27 it
    // has --hold — which is the only reason this chain used to start at kitty.
    //
    // ⚠ THREE FORMS, not one, and they are not interchangeable. syntty takes
    // the command after `-e`; kitty and foot take it positionally after
    // --hold; alacritty, konsole and xterm have no --hold at all and need a
    // pause written into the command itself. Handing syntty the positional
    // form makes it read `sh` as a subcommand and then die on `-c` with
    // "unknown option" — a window that never opens, from a button that gave
    // no sign of having done anything.
    //
    // The command is passed as sh's $1 rather than pasted into the script, so
    // nothing here has to be quoted. Joining argv on spaces is safe for what
    // reaches it: ALPM package names and Flatpak application ids cannot
    // contain whitespace.
    function inTerminal(argv, note) {
        root.statusLine = note || I18n.tr("opened a terminal")
        termProc.command = ["sh", "-c",
            'command -v syntty >/dev/null 2>&1 && exec syntty --hold -e sh -c "$1"; ' +
            'for t in kitty foot; do command -v "$t" >/dev/null 2>&1 && exec "$t" --hold sh -c "$1"; done; ' +
            'for t in alacritty konsole xterm; do command -v "$t" >/dev/null 2>&1 && exec "$t" -e sh -c "$1; printf \'\\n[press enter] \'; read _"; done; ' +
            'echo "no terminal emulator found" >&2; exit 127',
            "sh", argv.join(" ")]
        termProc.running = true
    }

    // --noconfirm because the GUI has no terminal to answer a prompt in; the C
    // side deliberately refuses to assume yes without it. Escalation to root
    // happens inside synpkg via pkexec, so nothing here runs privileged.
    //
    // The AUR is the exception and always will be: `aur install` clones a git
    // repository, shows you a PKGBUILD, and runs makepkg for minutes. makepkg
    // refuses to run as root, needs a tty for its own prompts, and prints as
    // it goes. A spinner in a window is exactly where someone force-quits a
    // half-finished build.
    // Hold and release. Separate from act() because they are not transactions:
    // nothing is downloaded, installed or removed, and routing them through the
    // ALPM path would take the database lock for a one-line config edit.
    function hold(row, on) {
        if (root.busy !== "") return
        root.busy = row.name
        root.outcome = ""
        actProc.errLine = ""
        // ⛔ NOT (on ? "holding back " : "releasing ") + name. Two verbs sharing
        // one object is English grammar; a language that inflects the object by
        // the verb, or puts the verb last, cannot be built that way.
        root.statusLine = on ? I18n.tr("holding back %1…").arg(row.name)
                             : I18n.tr("releasing %1…").arg(row.name)
        actProc.command = on ? root.holdCommand(row) : root.releaseCommand(row)
        actProc.running = true
    }

    function act(row, verb) {
        if (root.busy !== "") return

        // A SynapseOS component is not an ALPM package and must never reach
        // the transaction below: `synpkg remove synui` would happily uninstall
        // the compositor. syn-update owns these, and it needs a terminal —
        // it drives build-all.sh, which calls sudo mid-build, and sudo with no
        // controlling terminal cannot prompt.
        //
        // ONE component, named. `syn-update apply <name>` filters the build to
        // what was asked for — the names are a filter and never a sequence, so
        // build-all.sh still walks its own fixed order and a subset comes out
        // in the right order anyway. That property is the whole reason a
        // per-row button can be honest here.
        //
        // syn-update warns in the terminal when the named component depends on
        // another that is ALSO out of date and was not named (synui on
        // scenefx0.5, synnet and vibe on synapd) — it reads the edges out of
        // the PKGBUILDs, so this window does not need to know them.
        if (row.source === "system") {
            root.inTerminal([root.bin, "system", "apply", row.name],
                            I18n.tr("rebuilding %1 in a terminal").arg(row.name))
            return
        }

        if (row.source === "aur" && verb !== "remove") {
            root.inTerminal([root.bin, "aur", "install", row.name],
                            I18n.tr("building %1 in a terminal").arg(row.name))
            return
        }

        root.busy = row.name
        root.outcome = ""
        actProc.errLine = ""
        root.statusLine = verb === "remove" ? I18n.tr("removing %1…").arg(row.name)
                                            : I18n.tr("installing %1…").arg(row.name)

        if (row.source === "flathub") {
            const fpVerb = verb === "remove" ? "remove"
                                             : (row.extra === "update" ? "update" : "install")
            actProc.command = [root.bin, "--tsv", "-y", "flatpak", fpVerb, row.name]
        } else {
            actProc.command = [root.bin, "--tsv", "-y", verb, row.name]
        }
        actProc.running = true
    }

    function rowVerb(row) {
        // A held row offers RELEASE, not Update. Offering "Update" on a row the
        // user deliberately pinned would be a button that either disobeys the
        // hold or does nothing, and there is no third possibility worth
        // shipping. Taking one update without releasing the hold is a real
        // thing to want, but it is a command (`syn-update apply <name>`), not
        // the obvious button on a row that says "held back".
        if (row.ignored) return I18n.tr("Release")
        if (row.extra === "update") return I18n.tr("Update")
        return row.installed ? I18n.tr("Remove") : I18n.tr("Install")
    }

    // Which command releases this row. Three sources, three mechanisms:
    // pacman.conf's IgnorePkg, `flatpak mask`, and syn-update's manifest.
    //
    // --tsv on every one of them, and not only for tidiness: synpkg's info()
    // goes to STDERR, and this window reads the first stderr line as the
    // reason something failed. Without --tsv a perfectly successful hold
    // reports ":: held back 1 package" as its error. TSV mode suppresses
    // info(), so silence means success, which is what the handler assumes.
    //
    // Components go through `synpkg system`, not straight to syn-update: that
    // path probes whether the installed syn-update knows the verb at all, and
    // says what to do about it when it does not.
    function releaseCommand(row) {
        if (row.source === "flathub") return [root.bin, "--tsv", "-y", "flatpak", "unignore", row.name]
        if (row.source === "system")  return [root.bin, "--tsv", "-y", "system", "unignore", row.name]
        return [root.bin, "--tsv", "-y", "unignore", row.name]
    }
    function holdCommand(row) {
        if (row.source === "flathub") return [root.bin, "--tsv", "-y", "flatpak", "ignore", row.name]
        if (row.source === "system")  return [root.bin, "--tsv", "-y", "system", "ignore", row.name]
        return [root.bin, "--tsv", "-y", "ignore", row.name]
    }

    // ── Navigation ──────────────────────────────────────────────────────────
    //
    // Four of the seven tabs can be browsed by category, and each one gets its
    // categories from a different place: BlackArch's pacman groups, the
    // curated catalogue's own column, Flathub's AppStream index, and a
    // cherry-picked set of pacman groups. They all emit the same four columns,
    // so one pane and one pair of functions serve all four.
    //
    // The AUR is the exception and cannot be fixed here: its RPC exposes no
    // categories at all, so that tab stays search-only.
    function hasCategories(tab) {
        return tab === "arsenal" || tab === "suggested"
            || tab === "flathub" || tab === "repo"
    }

    function categoryStep(tab) {
        if (tab === "arsenal")   return { kind: "categories", args: ["arsenal", "categories"] }
        if (tab === "suggested") return { kind: "categories", args: ["suggest", "categories"] }
        if (tab === "flathub")   return { kind: "categories", args: ["flatpak", "categories"] }
        return { kind: "categories", args: ["groups"] }
    }

    // What one category's rows come from. The tab a row is tagged with is not
    // always the tab it was browsed from: an Arsenal category yields ordinary
    // repository packages, and tagging them "arsenal" would send Install down
    // a path that does not exist.
    function groupStep(tab, group) {
        if (tab === "arsenal")   return { kind: "pkgs", tab: "repo",    args: ["arsenal", "packages", group] }
        if (tab === "suggested") return { kind: "suggest",              args: ["suggest", group] }
        if (tab === "flathub")   return { kind: "pkgs", tab: "flathub", args: ["flatpak", "category", group] }
        return { kind: "pkgs", tab: "repo", args: ["groups", group] }
    }

    // The pane's header row: everything, with no category filter. Each tab's
    // "everything" is a different command, and on the two big source tabs it
    // is deliberately the INSTALLED set rather than the whole repository —
    // "show me all 90000 packages" is not a question worth answering.
    function allStep(tab) {
        if (tab === "arsenal")   return { kind: "pkgs", tab: "repo", args: ["arsenal", "installed"] }
        if (tab === "suggested") return { kind: "suggest",           args: ["suggest"] }
        return installedStep(tab)
    }

    function allLabel(tab) {
        if (tab === "suggested") return I18n.tr("Everything")
        if (tab === "arsenal")   return I18n.tr("Installed tools")
        if (tab === "flathub")   return I18n.tr("Installed apps")
        return I18n.tr("Installed packages")
    }

    // "" is NOTHING selected, "*" is the header row. They are different states
    // and look different: nothing selected means the pane is waiting for a
    // click, and the header row being active means it is showing everything.
    // No pacman group or freedesktop category is named "*", so the sentinel
    // cannot collide with a real one.
    readonly property string allGroup: "*"

    function showGroup(group) {
        root.currentGroup = group
        root.mode = "browse"
        root.runChain([group === root.allGroup
                       ? root.allStep(root.section)
                       : root.groupStep(root.section, group)])
    }

    function installedStep(tab) {
        if (tab === "aur")     return { kind: "pkgs", tab: "aur",     args: ["aur", "installed"] }
        if (tab === "flathub") return { kind: "pkgs", tab: "flathub", args: ["flatpak", "list"] }
        // --native, so packages no repository offers do not show up under
        // Repositories wearing a "local" badge. Those are the AUR tab's.
        return { kind: "pkgs", tab: "repo", args: ["installed", "--explicit", "--native"] }
    }

    function searchStep(tab, term) {
        /*
         * ⚠ `tab` IS DELIBERATELY EMPTY FOR THE SUPER SEARCH, and it is the
         * whole reason one pane can carry four sources.
         *
         * pkgRows() reads it as `tab || sourceOf(r.repo)`: a single-source pane
         * names its tab, so every row it produces is tagged with it whatever
         * the repo column happens to say. `search --all` returns rows from the
         * repositories, BlackArch, the AUR and Flathub in one table, and each
         * one has to keep its OWN source — that is what the badge reads, and
         * more importantly it is what rowAction() dispatches on. Forcing a tab
         * here would send `flatpak install` at a pacman package.
         */
        if (tab === "all")     return { kind: "pkgs", tab: "",        args: ["search", "--all", term] }
        if (tab === "aur")     return { kind: "pkgs", tab: "aur",     args: ["aur", "search", term] }
        if (tab === "flathub") return { kind: "pkgs", tab: "flathub", args: ["flatpak", "search", term] }
        return { kind: "pkgs", tab: "repo", args: ["search", term] }
    }

    function reload() {
        statusLine = ""
        filter = ""
        searchInput.text = ""

        // Cleared unconditionally: a stale pane is worse than an empty one,
        // because clicking a leftover Arsenal category on the Flathub tab
        // would run `flatpak category blackarch-webapp`.
        categories = []

        if (section === "updates") {
            runChain([
                { kind: "updates", tab: "repo",    args: ["updates"],           note: I18n.tr("checking repositories…") },
                { kind: "updates", tab: "aur",     args: ["aur", "updates"],    note: I18n.tr("checking the AUR…") },
                { kind: "updates", tab: "flathub", args: ["flatpak", "updates"], note: I18n.tr("checking Flathub…") },
                // SynapseOS's own components belong on the page called Updates.
                // They were only ever on their own tab, so the one page a
                // person opens to answer "is anything out of date?" answered it
                // wrongly — and the components are the half of this system that
                // no other updater can see.
                //
                // Last in the chain deliberately: it shells out to syn-update,
                // which fetches from git, so it is the slowest step and the
                // three fast ones should already be on screen.
                { kind: "system",  tab: "system",  args: ["system", "check"],   note: I18n.tr("checking SynapseOS components…") }
            ])
        } else if (section === "held") {
            // `synpkg ignore` covers the repositories, the AUR and Flathub in
            // one listing; the components are held in syn-update's manifest
            // and released by syn-update, so they are a second step. Neither
            // fetches: `ignored` reads a file, and the pending versions come
            // from the local database.
            runChain([
                { kind: "held",     args: ["ignore"],             note: I18n.tr("reading held packages…") },
                { kind: "heldcomp", args: ["system", "ignored"],  note: I18n.tr("reading held components…") }
            ])
        } else if (section === "about") {
            runChain([{ kind: "about", args: ["about"] }])
        } else if (section === "system") {
            runChain([{ kind: "system", args: ["system", "check"] }])
        } else if (section === "suggested") {
            // The only category tab that opens on content: the catalogue is 100
            // rows, so showing all of it is a readable page rather than a dump.
            currentGroup = allGroup
            runChain([categoryStep(section), allStep(section)])
        } else if (section === "arsenal") {
            currentGroup = ""
            runChain([categoryStep(section)])
        } else if (isSourceTab) {
            currentGroup = ""
            const steps = []
            // Flathub first, so the "Enable Flathub" banner has an answer
            // before the pane it sits in has anything to draw.
            if (section === "flathub")
                steps.push({ kind: "remotes", args: ["flatpak", "remotes"] })
            if (hasCategories(section))
                steps.push(categoryStep(section))
            if (mode === "installed")
                steps.push(installedStep(section))
            runChain(steps)
        }
    }

    function showSection(id) {
        root.section = id
        // A refusal belongs to the pane it happened on. Carrying "synui:
        // requires kitty" across to Flathub would be a message about a package
        // that is not on screen any more.
        root.outcome = ""
        // Tabs that can be browsed open on their category pane; the AUR has no
        // categories and so has nothing to show until you type.
        root.mode = root.hasCategories(id) ? "browse" : "search"
        root.reload()
    }

    function doSearch(term) {
        if (!term) { rows = []; return }
        // A search answers across every category, so the pane's selection is
        // no longer describing what is on screen — drop it rather than leave a
        // category highlighted beside results it did not produce.
        root.currentGroup = ""
        runChain([searchStep(root.section, term)])
    }

    // showSection() rather than reload(), so the opening tab picks its mode by
    // the same rule every later tab does. `synpkg gui aur` starting in Browse —
    // a mode the AUR does not have — would show an empty pane with no
    // explanation until something else was clicked.
    Component.onCompleted: {
        showSection(root.section)
        if (root.openQuery !== "") {
            // AFTER showSection, which sets the mode and calls reload(): a
            // search issued first would have its rows replaced by the browse
            // load landing behind it, and the window would open on a category
            // pane with the term sitting unused in the box.
            searchInput.text = root.openQuery
            root.mode = "search"
            root.doSearch(root.openQuery)
        }
    }

    // ── Layout ──────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.cBg

        Rectangle {
            id: header
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 64
            color: root.cPanel

            // Anchored to the counter, NOT to a fixed reservation.
            //
            // This was `width: parent.width - 260`, and 260 is roughly five
            // times what "0 items" occupies — so the subtitle elided to
            // "everything with a newer v…" while ~180 px of empty header sat
            // between it and the counter. A constant cannot track a string
            // whose length changes with the status.
            Column {
                anchors {
                    left: parent.left; leftMargin: 18
                    right: headCount.left; rightMargin: 12
                    verticalCenter: parent.verticalCenter
                }
                spacing: 2

                Text {
                    width: parent.width
                    text: I18n.tr("SYNAPSE Software")
                    color: root.cAccent
                    font { family: root.uiFont; pixelSize: root.ui(17); bold: true }
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.sectionHint(root.section)
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(11) }
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    visible: text !== ""
                }
            }

            Text {
                id: headCount
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                // Sized to its own text, but never more than a share of the
                // header: this shows `statusLine` too, and a long status
                // ("opening …") would otherwise push the title out entirely
                // now that the title's width is derived from this one.
                width: Math.min(implicitWidth, header.width * 0.45)
                elide: Text.ElideRight
                color: root.outcome !== "" ? root.cWarn
                     : root.busy !== "" || root.loading ? root.cAccent : root.cDim
                font { family: root.uiFont; pixelSize: root.ui(12) }
                horizontalAlignment: Text.AlignRight
                // `outcome` outranks the item count and survives the reload
                // that follows a failed action — it is the only thing here
                // that outlives the action it describes. It still yields to a
                // live status line, because a NEW action in flight is more
                // current news than the last one's refusal.
                text: root.loading ? (root.statusLine !== "" ? root.statusLine : I18n.tr("loading…"))
                                   : root.statusLine !== "" ? root.statusLine
                                   : root.outcome !== "" ? root.outcome
                                   : (root.section === "about" ? ""
                                      : I18n.trn("%1 item", "%1 items", root.shownRows.length)
                              .arg(root.shownRows.length))
            }

            // Along the bottom edge of the header, so it reads as the whole
            // window being busy rather than one control being disabled.
            //
            // `busy` is a row action (install, remove, a component rebuild);
            // `loading` is a list being read. Both are waits the user cannot
            // do anything during, and until now the only sign of either was a
            // word in the corner that a person looking at the row they clicked
            // would never see move.
            //
            // NOT shown for the terminal paths — Upgrade all, an AUR build, a
            // component rebuild — because those hand the work to a terminal
            // that prints as it goes, and a second, vaguer indicator in a
            // window behind it would be claiming to track something it is not.
            ProgressTrack {
                id: headBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                active: root.loading || root.busy !== ""
                visible: active
            }
        }

        // ── Sections ────────────────────────────────────────────────────────
        Rectangle {
            id: nav
            anchors { top: header.bottom; left: parent.left; bottom: parent.bottom }
            width: 176
            color: root.cPanel

            Column {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                anchors.topMargin: 8

                Repeater {
                    model: root.sections
                    delegate: Rectangle {
                        id: navRow
                        required property var modelData
                        readonly property bool current: navRow.modelData.id === root.section
                        width: nav.width
                        height: 34
                        color: navRow.current ? root.wash(0.16)
                                              : (navMa.containsMouse ? root.wash(0.08) : "transparent")

                        // A source tab gets its badge colour as a left edge, so
                        // the nav and the row badges agree at a glance.
                        Rectangle {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                            width: 3
                            visible: navRow.modelData.kind === "source"
                            color: root.sourceColor(navRow.modelData.id)
                            opacity: navRow.current ? 1 : 0.5
                        }

                        Text {
                            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            text: navRow.modelData.label
                            color: navRow.current ? root.cAccent : root.cText
                            font { family: root.uiFont; pixelSize: root.ui(13); bold: navRow.current }
                        }
                        MouseArea {
                            id: navMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.showSection(navRow.modelData.id)
                        }
                    }
                }
            }

            // Whole-system actions live at the bottom of the nav, away from the
            // per-row buttons: "upgrade everything" and "remove this one thing"
            // being adjacent is how the wrong one gets clicked.
            Column {
                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                anchors.margins: 10
                spacing: 6

                Rectangle {
                    width: parent.width; height: 30; radius: 4
                    color: upMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                    border { width: 1; color: root.cAccent }
                    Text {
                        anchors.centerIn: parent
                        text: I18n.tr("Upgrade all")
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    MouseArea {
                        id: upMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // A full upgrade downloads for minutes and prints as it
                        // goes; a window with a spinner and no output is where
                        // people force-quit mid-transaction. Hand it a terminal.
                        onClicked: root.inTerminal([root.bin, "upgrade"],
                                                   I18n.tr("upgrading in a terminal"))
                    }
                }
                Text {
                    width: parent.width
                    text: root.upgradeSystem ? I18n.tr("repos, AUR and components — in a terminal")
                                             : I18n.tr("repos and AUR — in a terminal")
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                // ── Whether "Upgrade all" includes the components ───────────
                //
                // A component rebuild is minutes of compiling, and wanting the
                // repositories and the AUR current without it is a reasonable
                // way to run a machine — velle's case: still use the button,
                // just not for the SynapseOS half.
                //
                // The answer is stored by the BINARY (`synpkg config`), not
                // here, so `synpkg upgrade` typed into a terminal means the
                // same thing as the button that runs it. A preference this
                // window kept privately would make the two disagree, and the
                // one you would trust is whichever you used last.
                Rectangle {
                    width: parent.width; height: 26; radius: 4
                    color: sysMa.containsMouse ? root.wash(0.12) : "transparent"
                    border { width: 1; color: root.cDim }
                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle {
                            width: 12; height: 12; radius: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.upgradeSystem ? root.cAccent : "transparent"
                            border { width: 1; color: root.upgradeSystem ? root.cAccent : root.cDim }
                        }
                        Text {
                            text: I18n.tr("include SynapseOS")
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(10) }
                        }
                    }
                    MouseArea {
                        id: sysMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.setUpgradeSystem(!root.upgradeSystem)
                    }
                }
            }
        }

        // ── Content ─────────────────────────────────────────────────────────
        Item {
            id: content
            anchors {
                top: header.bottom; left: nav.right
                right: parent.right; bottom: parent.bottom
            }

            // The category column. Arsenal had it first; it now serves every
            // tab whose source can enumerate categories, because "browse by
            // category" and "type a name and hope" are different needs and
            // only one of them was catered for.
            //
            // It is driven entirely by root.categories, so nothing in here
            // knows which tab is open — the tab decides what to load, this
            // decides how to draw it.
            Rectangle {
                id: catPane
                visible: root.categories.length > 0
                anchors { top: parent.top; left: parent.left; bottom: parent.bottom }
                width: visible ? 230 : 0
                color: Qt.rgba(root.cPanel.r, root.cPanel.g, root.cPanel.b, 0.6)

                ListView {
                    anchors.fill: parent
                    anchors.topMargin: 6
                    clip: true
                    ScrollBar.vertical: SynScrollBar {}
                    model: root.categories
                    spacing: 1

                    header: Rectangle {
                        readonly property bool current: root.currentGroup === root.allGroup
                        width: ListView.view.width
                        height: 30
                        color: current ? root.wash(0.16)
                                       : (allMa.containsMouse ? root.wash(0.08) : "transparent")
                        Text {
                            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                            text: root.allLabel(root.section)
                            color: root.cAccent
                            font { family: root.uiFont; pixelSize: root.ui(12); bold: true }
                        }
                        MouseArea {
                            id: allMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.showGroup(root.allGroup)
                        }
                    }

                    delegate: Rectangle {
                        id: catRow
                        required property var modelData
                        readonly property bool current: catRow.modelData.name === root.currentGroup
                        width: ListView.view.width
                        height: 28
                        color: catRow.current ? root.wash(0.16)
                                              : (catMa.containsMouse ? root.wash(0.08) : "transparent")

                        Text {
                            anchors {
                                left: parent.left; leftMargin: 14
                                right: catCount.left; rightMargin: 8
                                verticalCenter: parent.verticalCenter
                            }
                            // The display name the source chose. Arsenal strips
                            // its "blackarch-" prefix and Flathub turns "Game"
                            // into "Games" — both on the C side, where the
                            // naming scheme is actually known.
                            text: catRow.modelData.label
                            elide: Text.ElideRight
                            color: catRow.current ? root.cAccent : root.cText
                            font { family: root.uiFont; pixelSize: root.ui(12) }
                        }
                        Text {
                            id: catCount
                            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            text: catRow.modelData.installed > 0
                                  ? catRow.modelData.installed + "/" + catRow.modelData.total
                                  : catRow.modelData.total
                            color: catRow.modelData.installed > 0 ? root.cAccent : root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(10) }
                        }
                        MouseArea {
                            id: catMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.showGroup(catRow.modelData.name)
                        }
                    }
                }
            }

            // ── Toolbar: mode toggle + search box ───────────────────────────
            Item {
                id: toolbar
                anchors { top: parent.top; left: catPane.right; right: parent.right }
                anchors.margins: 10
                height: 30
                visible: root.section !== "about"
                // Belt to minimumSize's braces: a pane narrower than the mode
                // toggle must not let the search box paint over it or off the
                // window. minimumSize keeps us out of that range; this makes
                // the failure tidy if some future layout gets there anyway.
                clip: true

                Row {
                    id: modeToggle
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    spacing: 0
                    visible: root.isSourceTab

                    Repeater {
                        // Browse only exists where there is something to browse.
                        // Offering it on the AUR tab would be a button that can
                        // only ever produce an empty pane.
                        // ⚠ ALL SOURCES IS SEARCH-ONLY. "Browse" would mean
                        // listing four repositories at once, and "Installed"
                        // already has a page of its own per source — offering
                        // either here would be a button whose pane cannot say
                        // which source it is describing.
                        model: root.section === "all"
                               ? [{ id: "search",    label: I18n.tr("Search") }]
                               : root.hasCategories(root.section)
                               ? [{ id: "browse",    label: I18n.tr("Browse") },
                                  { id: "search",    label: I18n.tr("Search") },
                                  { id: "installed", label: I18n.tr("Installed") }]
                               : [{ id: "search",    label: I18n.tr("Search") },
                                  { id: "installed", label: I18n.tr("Installed") }]
                        delegate: Rectangle {
                            id: modeBtn
                            required property var modelData
                            required property int index
                            readonly property bool current: modeBtn.modelData.id === root.mode
                            width: 84; height: 30
                            color: modeBtn.current ? root.wash(0.22)
                                                   : (modeMa.containsMouse ? root.wash(0.10)
                                                                           : root.cPanel)
                            border { width: 1; color: modeBtn.current ? root.cAccent : "transparent" }

                            Text {
                                anchors.centerIn: parent
                                text: modeBtn.modelData.label
                                color: modeBtn.current ? root.cAccent : root.cDim
                                font { family: root.uiFont; pixelSize: root.ui(12); bold: modeBtn.current }
                            }
                            MouseArea {
                                id: modeMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.mode = modeBtn.modelData.id
                                    root.reload()
                                }
                            }
                        }
                    }
                }

                // Search box: doubles as a filter for every other list, which is
                // why it is always present rather than only on a search pane.
                Rectangle {
                    id: searchBar
                    anchors {
                        left: modeToggle.visible ? modeToggle.right : parent.left
                        leftMargin: modeToggle.visible ? 10 : 0
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    height: 30
                    radius: 4
                    color: root.cPanel
                    // The box itself clips, so the placeholder is cut at ITS
                    // edge with the rounded border still drawn round it,
                    // instead of running past the window and being sliced by
                    // the screen.
                    clip: true
                    border {
                        width: 1
                        color: searchInput.activeFocus ? root.cAccent : "transparent"
                    }

                    readonly property bool searching: root.isSourceTab && root.mode === "search"

                    TextInput {
                        id: searchInput
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.cText
                        font { family: root.uiFont; pixelSize: root.ui(13) }
                        clip: true
                        onTextChanged: if (!searchBar.searching) root.filter = text
                        onAccepted: if (searchBar.searching) root.doSearch(text)
                        Keys.onEscapePressed: { text = ""; root.filter = "" }
                    }
                    Text {
                        anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                        text: {
                            // Keyed off the MODE, not the section: on a source
                            // tab in Browse the box filters the rows already
                            // loaded, and telling somebody to press Enter to
                            // search would be describing the other mode.
                            if (!searchBar.searching)       return I18n.tr("filter this list…")
                            if (root.section === "all")     return I18n.tr("search everything — repos, BlackArch, AUR and Flathub — press Enter")
                            if (root.section === "repo")    return I18n.tr("search the repositories — press Enter")
                            if (root.section === "aur")     return I18n.tr("search the AUR — press Enter")
                            if (root.section === "flathub") return I18n.tr("search Flathub — press Enter")
                            return I18n.tr("filter this list…")
                        }
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(13) }
                        visible: searchInput.text === ""
                    }
                }
            }

            // ── About ───────────────────────────────────────────────────────
            // Not a credits screen: every source synpkg can install from is
            // optional at runtime, and until this pane existed the only way to
            // find out one was switched off was to open its tab and find it
            // empty. "Nothing here" and "this source is disabled" are very
            // different claims.
            Flickable {
                anchors.fill: parent
                anchors.margins: 18
                ScrollBar.vertical: SynScrollBar {}
                visible: root.section === "about"
                contentHeight: aboutCol.implicitHeight
                clip: true

                Column {
                    id: aboutCol
                    width: parent.width
                    spacing: 14

                    Text {
                        text: I18n.tr("SYNAPSE Software")
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(22); bold: true }
                    }
                    Text {
                        width: aboutCol.width
                        // ⚠ ONE msgid. It is five source lines because it is long;
                        // marking only the first handed a translator a sentence
                        // that stops mid-clause with the rest glued on in English.
                        text: I18n.tr("One package manager over the repositories, the AUR, "
                                      + "Flathub, BlackArch and SynapseOS's own components. "
                                      + "The graphical browser, the terminal browser and the "
                                      + "command line are the same binary reading the same "
                                      + "code paths, so they cannot disagree about what is "
                                      + "installed.")
                        color: root.cDim
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: root.aboutRows
                        delegate: Rectangle {
                            id: aboutRow
                            required property var modelData
                            width: aboutCol.width
                            height: 54
                            radius: 4
                            color: root.wash(0.05)

                            readonly property color stateColor:
                                aboutRow.modelData.state === "ok"      ? root.cOk
                              : aboutRow.modelData.state === "off"     ? root.cWarn
                              : aboutRow.modelData.state === "missing" ? root.cDim
                                                                       : root.cAccent

                            // A detail that is a command is an OFFER, not a
                            // footnote: the pane that tells you Flathub is off
                            // is the natural place to switch it on.
                            readonly property bool runnable:
                                aboutRow.modelData.detail !== undefined
                                && aboutRow.modelData.detail.indexOf("synpkg ") === 0

                            // A detail that is a URL opens in a browser rather
                            // than a terminal. The two must stay separate:
                            // `runnable` splits argv on spaces and hands it to
                            // a shell, and doing that to a URL would be both
                            // broken and a way to run whatever a detail string
                            // happened to contain.
                            readonly property bool openable:
                                aboutRow.modelData.detail !== undefined
                                && aboutRow.modelData.detail.indexOf("https://") === 0

                            Rectangle {
                                id: stateDot
                                anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                                width: 8; height: 8; radius: 4
                                color: aboutRow.stateColor
                            }

                            Text {
                                id: aboutKey
                                anchors { left: stateDot.right; leftMargin: 12; verticalCenter: parent.verticalCenter }
                                width: 110
                                text: aboutRow.modelData.item
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(12); bold: true }
                                elide: Text.ElideRight
                            }

                            Column {
                                anchors {
                                    left: aboutKey.right; leftMargin: 12
                                    right: aboutBtn.left; rightMargin: 12
                                    verticalCenter: parent.verticalCenter
                                }
                                spacing: 2

                                Text {
                                    width: parent.width
                                    text: aboutRow.modelData.value
                                    color: aboutRow.stateColor
                                    font { family: root.uiFont; pixelSize: root.ui(12) }
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: aboutRow.modelData.detail
                                    color: root.cDim
                                    // Not `undefined` for the non-monospace
                                    // case: QString will not take it, and Qt
                                    // logs "Unable to assign [undefined]" once
                                    // per row while rendering the row anyway.
                                    // Qt.application.font.family was the wrong
                                    // fallback: Qt resolves the application
                                    // font once at startup, so this row kept
                                    // the face the window opened with while the
                                    // rest of the pane followed font.state.
                                    font { pixelSize: root.ui(11)
                                           family: aboutRow.runnable ? "monospace"
                                                                     : root.uiFont }
                                    elide: Text.ElideRight
                                    visible: text !== ""
                                }
                            }

                            Rectangle {
                                id: aboutBtn
                                anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                width: 70; height: 26; radius: 4
                                visible: aboutRow.runnable || aboutRow.openable
                                color: aboutBtnMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                                border { width: 1; color: root.cAccent }

                                Text {
                                    anchors.centerIn: parent
                                    text: aboutRow.openable ? I18n.tr("Open") : I18n.tr("Run")
                                    color: root.cAccent
                                    font { family: root.uiFont; pixelSize: root.ui(11) }
                                }
                                MouseArea {
                                    id: aboutBtnMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (aboutRow.openable) {
                                            // Qt.openUrlExternally rather than a
                                            // terminal: the whole point of the
                                            // openable/runnable split.
                                            Qt.openUrlExternally(aboutRow.modelData.detail)
                                            root.statusLine = I18n.tr("opened in your browser")
                                            return
                                        }
                                        // Every one of these downloads something —
                                        // a bootstrap, an appstream index — and
                                        // prints as it goes.
                                        root.inTerminal(
                                            aboutRow.modelData.detail.split(" "),
                                            I18n.tr("running in a terminal"))
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Empty states ────────────────────────────────────────────────
            // They say what to DO. "No results" with no next step is how the
            // old arsenal looked on a machine without BlackArch.
            Column {
                // Centred in the RESULTS area, not in the whole pane. It used
                // to be anchors.centerIn: parent with width = parent.width-80,
                // and parent includes catPane — so on a narrow window the block
                // reached back across the category list and painted on top of
                // it. Screenshot 2026-08-10 11:38 caught "Utilities 948" with
                // "sandboxed applications with their own runtimes, installed"
                // drawn straight through it.
                //
                // Anchored exactly like the ListView it stands in for, so the
                // empty state occupies the space the rows would have. catPane
                // is width 0 when hidden, so this is still the full pane when
                // there are no categories.
                anchors {
                    left: catPane.right; leftMargin: 40
                    right: parent.right; rightMargin: 40
                    verticalCenter: parent.verticalCenter
                }
                spacing: 10
                visible: root.section !== "about" && !root.loading
                         && root.shownRows.length === 0

                Text {
                    // width + wrap, or this sizes to implicitWidth and runs
                    // straight out of the window: "Everything is up to date."
                    // rendered as "Everything is up to da" with the rest past
                    // the right edge. Centring an item wider than its parent
                    // centres the OVERFLOW too, so it was clipped at both ends.
                    //
                    // Wrapped and NOT elided on purpose — a Text with both
                    // wrapMode and elide whose height is not yet resolved at
                    // first layout elides every line away and never recomputes,
                    // which is how synui's post-it shipped blank.
                    width: parent.width
                    wrapMode: Text.WordWrap
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: root.cText
                    font { family: root.uiFont; pixelSize: root.ui(15) }
                    horizontalAlignment: Text.AlignHCenter
                    text: {
                        if (root.section === "updates")   return I18n.tr("Everything is up to date.")
                        if (root.section === "held")      return I18n.tr("Nothing is being held back.\nHold an update from the Updates page to stop it arriving.")
                        if (root.section === "system")    return I18n.tr("SynapseOS components are current.")
                        if (root.section === "suggested") return root.currentGroup === ""
                                                                 ? I18n.tr("Pick a category.")
                                                                 : I18n.tr("You already have everything suggested.")
                        if (root.section === "arsenal")   return root.categories.length === 0
                                                                 ? I18n.tr("The BlackArch repository is not enabled.")
                                                                 : I18n.tr("Pick a category.")
                        if (root.mode === "installed") {
                            if (root.section === "aur")     return I18n.tr("Nothing installed from outside a repository.")
                            if (root.section === "flathub") return I18n.tr("No Flatpak applications installed.")
                            return I18n.tr("Nothing installed from the repositories.")
                        }
                        // Browse with nothing picked yet is the normal opening
                        // state of these tabs, not a failure — say what to do,
                        // and only claim something is wrong when it is.
                        if (root.mode === "browse" && root.currentGroup === "") {
                            if (root.section === "flathub" && !root.flathubEnabled)
                                return I18n.tr("Flathub is not enabled on this machine.")
                            if (root.categories.length > 0) return I18n.tr("Pick a category.")
                            if (root.section === "flathub")
                                return I18n.tr("Flathub has no application index yet.")
                            return I18n.tr("Nothing to browse.")
                        }
                        if (root.section === "all")     return I18n.tr("Search every source at once.")
                        if (root.section === "aur")     return I18n.tr("Search the AUR.")
                        if (root.section === "flathub") return I18n.tr("Search Flathub.")
                        return I18n.tr("Search the repositories.")
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(12) }
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: text !== ""
                    text: {
                        if (root.section === "aur" && root.mode === "installed")
                            return I18n.tr("This lists every installed package no repository "
                                           + "offers — AUR builds and anything else built on "
                                           + "this machine, including SynapseOS's own packages.")
                        if (root.section === "aur")
                            return I18n.tr("Packages here are recipes, not binaries. "
                                           + "Installing one opens a terminal, shows you its "
                                           + "PKGBUILD, and builds it.")
                        if (root.section === "flathub")
                            return I18n.tr("Sandboxed applications with their own runtimes, "
                                           + "installed alongside your system packages rather "
                                           + "than into them.")
                        return ""
                    }
                }

                // The one-click fix, where there is one.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 200; height: 32; radius: 4
                    visible: root.section === "arsenal" && root.categories.length === 0
                    color: baMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                    border { width: 1; color: root.cAccent }
                    Text {
                        anchors.centerIn: parent
                        text: I18n.tr("Enable BlackArch")
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    MouseArea {
                        id: baMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.inTerminal([root.bin, "arsenal", "enable-repo"],
                                                   I18n.tr("enabling BlackArch in a terminal"))
                    }
                }

                // Flathub's remote is not added by default, and `flatpak
                // search` against a box with no remotes returns nothing at all
                // rather than an error — the failure looks exactly like
                // "Flathub has no browsers".
                //
                // Gated on the ACTUAL remote state. It used to be shown
                // whenever the Flathub tab was open, so a machine with Flathub
                // already enabled got a permanent instruction to enable it,
                // and clicking it ran a command that correctly reported
                // "already enabled" and changed nothing — which reads exactly
                // like a dead button. An offer to fix something has to be able
                // to tell whether it is broken.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 200; height: 32; radius: 4
                    visible: root.section === "flathub" && !root.flathubEnabled
                    color: fhMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                    border { width: 1; color: root.cAccent }
                    Text {
                        anchors.centerIn: parent
                        text: I18n.tr("Enable Flathub")
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    MouseArea {
                        id: fhMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.inTerminal([root.bin, "flatpak", "enable-flathub"],
                                                   I18n.tr("enabling Flathub in a terminal"))
                    }
                }

                // The other half of the same problem: the remote is there but
                // its application index is not, which is the state a remote
                // added by anything other than enable-flathub is left in.
                // Searching and browsing both come up empty and neither says
                // why. Same command fixes it — it fetches the index whether or
                // not the remote already existed.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 200; height: 32; radius: 4
                    visible: root.section === "flathub" && root.flathubEnabled
                             && root.categories.length === 0
                    color: fiMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                    border { width: 1; color: root.cAccent }
                    Text {
                        anchors.centerIn: parent
                        text: I18n.tr("Fetch the app index")
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    MouseArea {
                        id: fiMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.inTerminal([root.bin, "flatpak", "enable-flathub"],
                                                   I18n.tr("fetching Flathub's index in a terminal"))
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: I18n.tr("runs in a terminal — takes a minute")
                    color: root.cDim
                    font { family: root.uiFont; pixelSize: root.ui(10) }
                    visible: root.section === "flathub"
                             && (!root.flathubEnabled || root.categories.length === 0)
                }
            }

            // ── Rows ────────────────────────────────────────────────────────
            ListView {
                anchors {
                    top: toolbar.visible ? toolbar.bottom : parent.top
                    topMargin: 6
                    left: catPane.right; right: parent.right; bottom: parent.bottom
                }
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.bottomMargin: 10
                ScrollBar.vertical: SynScrollBar {}
                // ⚠ AND ONLY WHEN IT HAS ROWS. This ListView is declared after
                // the empty-state block above and covers exactly the same area,
                // so it is the topmost item there — and a Flickable takes the
                // PRESS across its whole rect whether or not it drew anything.
                // Hover fell through (the list is not hoverEnabled, so the
                // buttons still lit up under the cursor) and the click did not,
                // which made "Enable Flathub" and "Enable BlackArch" look dead:
                // the one state those buttons exist for is the state with no
                // rows in it. Exactly the complement of the empty state's own
                // condition, so the two can never both take the pointer.
                visible: root.section !== "about" && root.shownRows.length > 0
                clip: true
                model: root.shownRows
                spacing: 2

                delegate: Rectangle {
                    id: pkgRow
                    required property var modelData
                    width: ListView.view.width
                    height: 52
                    radius: 4
                    color: rowMa.containsMouse ? root.wash(0.07) : "transparent"
                    // A held row is legible but visibly not part of the work
                    // ahead. Dimming the whole row rather than adding a badge:
                    // the badge column is already carrying the repo name, and a
                    // second one turns every row into a legend.
                    opacity: pkgRow.modelData.ignored ? 0.62 : 1.0

                    MouseArea { id: rowMa; anchors.fill: parent; hoverEnabled: true }

                    /*
                     * ── The icon, and the installed dot on top of it ────────
                     *
                     * The dot used to be the whole left column: 7px, accent
                     * when installed, an outline when not. It still is, but it
                     * now sits on the corner of the icon instead of standing
                     * alone — one column rather than two, because a row is 52px
                     * and a separate icon column would have cost the name and
                     * the description the width they need.
                     *
                     * ⚠ THE TILE IS ALWAYS DRAWN, WITH OR WITHOUT AN ICON. A
                     * package is not an application and most have no icon at
                     * all; a column that appeared for some rows and not others
                     * would make every list look ragged and would read as icons
                     * failing to load. The monogram is the same answer the dock
                     * gives for an app whose icon the theme has not got.
                     */
                    Rectangle {
                        id: iconTile
                        anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                        width: 32; height: 32; radius: 6
                        readonly property string src: root.iconFor(pkgRow.modelData)
                        // Only behind the monogram. An icon with its own shape
                        // and transparency sitting on a tinted square reads as
                        // a badge rather than as the application's icon.
                        color: iconTile.src !== "" ? "transparent"
                             : Qt.rgba(root.sourceColor(pkgRow.modelData.repo).r,
                                       root.sourceColor(pkgRow.modelData.repo).g,
                                       root.sourceColor(pkgRow.modelData.repo).b, 0.14)

                        Image {
                            anchors.fill: parent
                            source: iconTile.src
                            visible: iconTile.src !== ""
                            // sourceSize, or a 512px hicolor PNG is decoded at
                            // full size for a 32px tile — once per row, on a
                            // list that can be four hundred rows long.
                            sourceSize { width: 32; height: 32 }
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            smooth: true
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: iconTile.src === ""
                            text: root.iconLetter(pkgRow.modelData)
                            color: root.sourceColor(pkgRow.modelData.repo)
                            font { family: root.uiFont; pixelSize: root.ui(15); bold: true }
                        }

                        // The installed dot, on the tile's bottom-right corner
                        // with a ring of the row's own background so it reads
                        // as ON the icon rather than as part of it.
                        Rectangle {
                            id: dot
                            anchors { right: parent.right; bottom: parent.bottom
                                      rightMargin: -2; bottomMargin: -2 }
                            width: 11; height: 11; radius: 6
                            visible: pkgRow.modelData.installed
                            color: root.cAccent
                            border { width: 2; color: root.cBg }
                        }
                    }

                    Column {
                        anchors {
                            left: iconTile.right; leftMargin: 12
                            // When the button is hidden the text takes its
                            // place; anchoring to a hidden item would keep
                            // reserving the 84 px that is the whole problem.
                            right: holdBtn.visible ? holdBtn.left
                                 : actionBtn.visible ? actionBtn.left : parent.right
                            rightMargin: 12
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: 3

                        Row {
                            id: nameRow
                            spacing: 8
                            width: parent.width

                            // ── Why every child below is capped against its
                            //    OWN x ────────────────────────────────────
                            //
                            // A Row lays children out left to right and does
                            // NOT clip: a child wider than the space left just
                            // draws past the end, over whatever is there. What
                            // is there is the Install button, so the id and the
                            // category ran underneath it and the row read
                            // "widelands Games" with "Install" printed through
                            // the middle of it.
                            //
                            // `parent.width - x` is the space actually left at
                            // the point this item starts, and Row has already
                            // assigned x from the widths of the items BEFORE
                            // it — so there is no loop, and the cap adapts to a
                            // long name, a long badge, or a narrow window
                            // without any of them knowing about each other.
                            //
                            // Math.max(0, …) matters on its own: a negative
                            // width does not clamp, it defeats clip and paints
                            // the item mirrored across its own origin.

                            // A Flatpak's identity is org.mozilla.firefox and
                            // its name is Firefox. The name goes here; the id
                            // stays visible beside it, because the id is what
                            // the command line takes.
                            Text {
                                // Capped and elided, not free-running. With no
                                // width a Text renders at implicitWidth whatever
                                // its parent is, so at cascade width on the
                                // portrait monitor "binutils" drew straight
                                // over the badge and the Update button — the
                                // row read "binutits". The cap is what stops a
                                // name escaping its column at ANY width; the
                                // hidden button below is what gives it room.
                                // 0.6 of the row at most, so a long name
                                // cannot consume the space the badge and the id
                                // need and push them off the end. Below that it
                                // is capped by its own content as before.
                                width: Math.max(0, Math.min(implicitWidth, parent.width * 0.6))
                                elide: Text.ElideRight
                                text: pkgRow.modelData.title !== ""
                                      ? pkgRow.modelData.title : pkgRow.modelData.name
                                color: root.cText
                                font { family: root.uiFont; pixelSize: root.ui(13); bold: true }
                            }

                            // The source badge. This is the whole point of the
                            // rewrite: no row on this screen is ambiguous about
                            // where it came from or what will install it.
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                height: 15
                                width: Math.max(0, Math.min(badgeText.implicitWidth + 12,
                                                            nameRow.width - x))
                                radius: 3
                                clip: true
                                visible: pkgRow.modelData.repo !== ""
                                color: Qt.rgba(root.sourceColor(pkgRow.modelData.repo).r,
                                               root.sourceColor(pkgRow.modelData.repo).g,
                                               root.sourceColor(pkgRow.modelData.repo).b, 0.16)
                                Text {
                                    id: badgeText
                                    anchors.centerIn: parent
                                    text: pkgRow.modelData.repo
                                    color: root.sourceColor(pkgRow.modelData.repo)
                                    font { family: root.uiFont; pixelSize: root.ui(9); bold: true }
                                }
                            }

                            Text {
                                text: pkgRow.modelData.title !== "" ? pkgRow.modelData.name : ""
                                color: root.cDim
                                font { family: "monospace"; pixelSize: root.ui(10) }
                                anchors.verticalCenter: parent.verticalCenter
                                visible: text !== "" && width > 0
                                width: Math.max(0, Math.min(implicitWidth, nameRow.width - x))
                                elide: Text.ElideRight
                            }
                            Text {
                                text: pkgRow.modelData.extra
                                color: root.cWarn
                                font { family: root.uiFont; pixelSize: root.ui(10) }
                                anchors.verticalCenter: parent.verticalCenter
                                visible: text !== "" && text !== "update"
                                         && text !== "component" && width > 0
                                width: Math.max(0, Math.min(implicitWidth, nameRow.width - x))
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            width: parent.width
                            text: pkgRow.modelData.desc
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }

                    Rectangle {
                        id: actionBtn
                        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 84; height: 26; radius: 4
                        // Every row that has an action gets this, SynapseOS
                        // components included — they used to be excluded here
                        // (`extra !== "component"`), which is what made the
                        // SynapseOS tab a list of updates with no way to take
                        // them. The click routes through act(), which sends a
                        // component to syn-update in a terminal and never into
                        // an ALPM transaction.
                        //
                        // It goes away when the row is too narrow to hold
                        // it. Cascaded onto the portrait monitor (Super+Shift+Y)
                        // this list is about 165 px wide: the button plus its
                        // margins take 104 of that, leaving ~34 px for name,
                        // badge and version — so every row collapsed into
                        // itself and the versions read "2.…".
                        //
                        // That size is INTENTIONAL, not a window too small to
                        // use, so the row has to give something up, and a
                        // per-row button is the right thing to lose: "Upgrade
                        // all" is still there, and a wider window brings it
                        // back. 250 = the 104 it costs plus enough left over
                        // for a package name and a version.
                        visible: pkgRow.width >= 250
                        color: btnMa.containsMouse ? root.wash(0.25) : root.wash(0.12)
                        border { width: 1; color: root.cAccent }
                        opacity: root.busy === "" || root.busy === pkgRow.modelData.name ? 1 : 0.4

                        Text {
                            anchors.centerIn: parent
                            color: root.cAccent
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                            text: root.busy === pkgRow.modelData.name
                                  ? I18n.tr("working…") : root.rowVerb(pkgRow.modelData)
                        }
                        MouseArea {
                            id: btnMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (pkgRow.modelData.ignored)
                                    root.hold(pkgRow.modelData, false)
                                else if (pkgRow.modelData.extra === "update")
                                    root.act(pkgRow.modelData, "install")
                                else
                                    root.act(pkgRow.modelData,
                                             pkgRow.modelData.installed ? "remove" : "install")
                            }
                        }
                    }

                    // Hold. Only on a pending update that is not already held,
                    // because holding back anything else means nothing: a
                    // package with no update waiting is not being upgraded by
                    // anybody, and there would be no visible effect to explain.
                    //
                    // It sits LEFT of the main button and is deliberately
                    // quieter — no accent border, dim text. Taking the update
                    // is the ordinary act; refusing it is the exception, and
                    // the two should not look like equal offers.
                    Rectangle {
                        id: holdBtn
                        visible: pkgRow.width >= 340
                                 && pkgRow.modelData.extra === "update"
                                 && !pkgRow.modelData.ignored
                        anchors {
                            right: actionBtn.left; rightMargin: 6
                            verticalCenter: parent.verticalCenter
                        }
                        width: 56; height: 26; radius: 4
                        color: holdMa.containsMouse ? root.wash(0.14) : "transparent"
                        border { width: 1; color: root.cDim }
                        opacity: root.busy === "" || root.busy === pkgRow.modelData.name ? 1 : 0.4

                        Text {
                            anchors.centerIn: parent
                            color: root.cDim
                            font { family: root.uiFont; pixelSize: root.ui(11) }
                            text: I18n.tr("Hold")
                        }
                        MouseArea {
                            id: holdMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.hold(pkgRow.modelData, true)
                        }
                    }
                }
            }
        }
    }
}
