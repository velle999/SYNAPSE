//@ pragma UseQApplication
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io

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
    readonly property color cAccentRaw: themed("accent", 78, 201, 176, 1.0)
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

    readonly property var sections: [
        { id: "updates",   label: "Updates",      kind: "list" },
        { id: "suggested", label: "Suggested",    kind: "list" },
        { id: "repo",      label: "Repositories", kind: "source" },
        { id: "aur",       label: "AUR",          kind: "source" },
        { id: "flathub",   label: "Flathub",      kind: "source" },
        { id: "arsenal",   label: "Arsenal",      kind: "list" },
        { id: "system",    label: "SynapseOS",    kind: "list" },
        { id: "about",     label: "About",        kind: "about" }
    ]

    function sectionKind(id) {
        for (const s of root.sections)
            if (s.id === id) return s.kind
        return "list"
    }
    readonly property bool isSourceTab: root.sectionKind(root.section) === "source"

    // What each tab is, in one line, under the title. Half of these are only
    // obvious if you already know the packaging landscape.
    function sectionHint(id) {
        if (id === "updates")   return "everything with a newer version — repositories, the AUR, Flathub and SynapseOS's own components"
        if (id === "suggested") return "the curated SynapseOS software list"
        if (id === "repo")      return "official Arch repositories and SynapseOS's own — signed, binary, managed by pacman"
        if (id === "aur")       return "the Arch User Repository — recipes built from source on this machine"
        if (id === "flathub")   return "sandboxed applications from Flathub, with their own runtimes"
        if (id === "arsenal")   return "BlackArch security tooling, by category"
        if (id === "system")    return "this system's own components, rebuilt from git by syn-update"
        return ""
    }

    // ── State ───────────────────────────────────────────────────────────────
    // `synpkg gui flathub` opens straight on that tab, so the start menu can
    // point separate entries at separate sources. An unrecognised name falls
    // back to Updates rather than opening a blank window on a section that
    // does not exist.
    property string section: {
        const want = Quickshell.env("SYNPKG_SECTION") || ""
        for (const s of root.sections)
            if (s.id === want) return want
        return "updates"
    }
    property string mode: "browse"    // source tabs: browse | search | installed
    property string busy: ""          // package currently being (un)installed
    property string statusLine: ""
    property bool   loading: false

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
    function makeRow(name, title, installed, version, repo, size, desc, extra, source) {
        return { name: name, title: title || "", installed: installed,
                 version: version || "", repo: repo || "", size: size || 0,
                 desc: desc || "", extra: extra || "", source: source }
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
        return table.map(r => root.makeRow(
            r.name, "", true, r.new_version, r.repo, parseInt(r.size || "0"),
            (r.installed_version || "?") + "  →  " + (r.new_version || "update available"),
            "update", tab))
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
        onExited: {
            root.busy = ""
            root.statusLine = ""
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

    // Hand a command to a terminal. kitty first: it is SynapseOS's default and
    // what syn-install writes into limine-snapper-sync.conf. Both kitty and
    // foot take the command positionally and both have --hold; the others need
    // -e and a pause of their own.
    //
    // The command is passed as sh's $1 rather than pasted into the script, so
    // nothing here has to be quoted. Joining argv on spaces is safe for what
    // reaches it: ALPM package names and Flatpak application ids cannot
    // contain whitespace.
    function inTerminal(argv, note) {
        root.statusLine = note || "opened a terminal"
        termProc.command = ["sh", "-c",
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
                            "rebuilding " + row.name + " in a terminal")
            return
        }

        if (row.source === "aur" && verb !== "remove") {
            root.inTerminal([root.bin, "aur", "install", row.name],
                            "building " + row.name + " in a terminal")
            return
        }

        root.busy = row.name
        root.statusLine = (verb === "remove" ? "removing " : "installing ") + row.name + "…"

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
        if (row.extra === "update") return "Update"
        return row.installed ? "Remove" : "Install"
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
        if (tab === "suggested") return "Everything"
        if (tab === "arsenal")   return "Installed tools"
        if (tab === "flathub")   return "Installed apps"
        return "Installed packages"
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
                { kind: "updates", tab: "repo",    args: ["updates"],           note: "checking repositories…" },
                { kind: "updates", tab: "aur",     args: ["aur", "updates"],    note: "checking the AUR…" },
                { kind: "updates", tab: "flathub", args: ["flatpak", "updates"], note: "checking Flathub…" },
                // SynapseOS's own components belong on the page called Updates.
                // They were only ever on their own tab, so the one page a
                // person opens to answer "is anything out of date?" answered it
                // wrongly — and the components are the half of this system that
                // no other updater can see.
                //
                // Last in the chain deliberately: it shells out to syn-update,
                // which fetches from git, so it is the slowest step and the
                // three fast ones should already be on screen.
                { kind: "system",  tab: "system",  args: ["system", "check"],   note: "checking SynapseOS components…" }
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
    Component.onCompleted: showSection(root.section)

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
                    text: "SYNAPSE Software"
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
                color: root.busy !== "" || root.loading ? root.cAccent : root.cDim
                font { family: root.uiFont; pixelSize: root.ui(12) }
                horizontalAlignment: Text.AlignRight
                text: root.loading ? (root.statusLine !== "" ? root.statusLine : "loading…")
                                   : (root.statusLine !== "" ? root.statusLine
                                      : (root.section === "about" ? ""
                                         : root.shownRows.length + " items"))
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
                        text: "Upgrade all"
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
                                                   "upgrading in a terminal")
                    }
                }
                Text {
                    width: parent.width
                    text: root.upgradeSystem ? "repos, AUR and components — in a terminal"
                                             : "repos and AUR — in a terminal"
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
                            text: "include SynapseOS"
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
                        model: root.hasCategories(root.section)
                               ? [{ id: "browse",    label: "Browse" },
                                  { id: "search",    label: "Search" },
                                  { id: "installed", label: "Installed" }]
                               : [{ id: "search",    label: "Search" },
                                  { id: "installed", label: "Installed" }]
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
                            if (!searchBar.searching)       return "filter this list…"
                            if (root.section === "repo")    return "search the repositories — press Enter"
                            if (root.section === "aur")     return "search the AUR — press Enter"
                            if (root.section === "flathub") return "search Flathub — press Enter"
                            return "filter this list…"
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
                visible: root.section === "about"
                contentHeight: aboutCol.implicitHeight
                clip: true

                Column {
                    id: aboutCol
                    width: parent.width
                    spacing: 14

                    Text {
                        text: "SYNAPSE Software"
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(22); bold: true }
                    }
                    Text {
                        width: aboutCol.width
                        text: "One package manager over the repositories, the AUR, Flathub, "
                              + "BlackArch and SynapseOS's own components. The graphical "
                              + "browser, the terminal browser and the command line are the "
                              + "same binary reading the same code paths, so they cannot "
                              + "disagree about what is installed."
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
                                    text: aboutRow.openable ? "Open" : "Run"
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
                                            root.statusLine = "opened in your browser"
                                            return
                                        }
                                        // Every one of these downloads something —
                                        // a bootstrap, an appstream index — and
                                        // prints as it goes.
                                        root.inTerminal(
                                            aboutRow.modelData.detail.split(" "),
                                            "running in a terminal")
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
                        if (root.section === "updates")   return "Everything is up to date."
                        if (root.section === "system")    return "SynapseOS components are current."
                        if (root.section === "suggested") return root.currentGroup === ""
                                                                 ? "Pick a category."
                                                                 : "You already have everything suggested."
                        if (root.section === "arsenal")   return root.categories.length === 0
                                                                 ? "The BlackArch repository is not enabled."
                                                                 : "Pick a category."
                        if (root.mode === "installed") {
                            if (root.section === "aur")     return "Nothing installed from outside a repository."
                            if (root.section === "flathub") return "No Flatpak applications installed."
                            return "Nothing installed from the repositories."
                        }
                        // Browse with nothing picked yet is the normal opening
                        // state of these tabs, not a failure — say what to do,
                        // and only claim something is wrong when it is.
                        if (root.mode === "browse" && root.currentGroup === "") {
                            if (root.section === "flathub" && !root.flathubEnabled)
                                return "Flathub is not enabled on this machine."
                            if (root.categories.length > 0) return "Pick a category."
                            if (root.section === "flathub")
                                return "Flathub has no application index yet."
                            return "Nothing to browse."
                        }
                        if (root.section === "aur")     return "Search the AUR."
                        if (root.section === "flathub") return "Search Flathub."
                        return "Search the repositories."
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
                            return "This lists every installed package no repository offers — "
                                 + "AUR builds and anything else built on this machine, "
                                 + "including SynapseOS's own packages."
                        if (root.section === "aur")
                            return "Packages here are recipes, not binaries. Installing one "
                                 + "opens a terminal, shows you its PKGBUILD, and builds it."
                        if (root.section === "flathub")
                            return "Sandboxed applications with their own runtimes, installed "
                                 + "alongside your system packages rather than into them."
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
                        text: "Enable BlackArch"
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    MouseArea {
                        id: baMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.inTerminal([root.bin, "arsenal", "enable-repo"],
                                                   "enabling BlackArch in a terminal")
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
                        text: "Enable Flathub"
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    MouseArea {
                        id: fhMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.inTerminal([root.bin, "flatpak", "enable-flathub"],
                                                   "enabling Flathub in a terminal")
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
                        text: "Fetch the app index"
                        color: root.cAccent
                        font { family: root.uiFont; pixelSize: root.ui(12) }
                    }
                    MouseArea {
                        id: fiMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.inTerminal([root.bin, "flatpak", "enable-flathub"],
                                                   "fetching Flathub's index in a terminal")
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "runs in a terminal — takes a minute"
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
                visible: root.section !== "about"
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
                            // When the button is hidden the text takes its
                            // place; anchoring to a hidden item would keep
                            // reserving the 84 px that is the whole problem.
                            right: actionBtn.visible ? actionBtn.left : parent.right
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
                                  ? "working…" : root.rowVerb(pkgRow.modelData)
                        }
                        MouseArea {
                            id: btnMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (pkgRow.modelData.extra === "update")
                                    root.act(pkgRow.modelData, "install")
                                else
                                    root.act(pkgRow.modelData,
                                             pkgRow.modelData.installed ? "remove" : "install")
                            }
                        }
                    }
                }
            }
        }
    }
}
