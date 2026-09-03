import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick.Controls

/*
 * The SYNAPSE start menu.
 *
 * Ported from src/menu.c (synui pkgrel 95-161), which existed for one reason:
 * waybar sets keyboard_interactivity NONE once at startup and never revises it,
 * so a GTK menu there could map, draw, and never receive a key. That was never a
 * limitation of layer-shell or of synui — a quickshell PanelWindow with
 * `focusable: true` is handed the keyboard and arrow-navigates fine
 * (⚠ `focusable: true` is ON-DEMAND, not Exclusive — WlrKeyboardFocus is
 * None=0/Exclusive=1/OnDemand=2 and it reads back 2. It works because
 * layer.c:layer_surface_map() grants the keyboard to ANY interactivity that is
 * not NONE, at map and nowhere else, which is precisely the waybar failure
 * below: NONE set once at startup and never revised)
 * (verified on the live session 2026-07-22 with a real keyboard; note that
 * `wtype` gives a FALSE NEGATIVE against Qt clients, so a headless keyboard test
 * proving "zero key events" here proves nothing). With the bar already QML,
 * keeping a second menu implementation in the compositor bought nothing.
 *
 * WHAT THIS DELETED, and why that is the good part: menu.c hand-rolled a
 * .desktop scanner — field codes, escapes, Terminal=, Path=, Wine's
 * uncategorised shortcuts, ids as relpath with '/'→'-' — roughly 400 lines that
 * were a C port of the retired synapse-menu-gen.py. `DesktopEntries` is that,
 * maintained upstream, including execute()'s field-code handling and the
 * recursive walk that Wine's applications/wine/Programs/… tree needs. The only
 * thing kept is the CATEGORIES precedence table, because the *grouping* is a
 * SYNAPSE decision, not a freedesktop one.
 *
 * The static rows dispatch through `synctl` rather than reimplementing anything,
 * for the same reason menu.c called bind actions directly: a row must take the
 * exact path its Super+key shortcut does, or there are two definitions of "open
 * the display settings" waiting to drift apart.
 */
PanelWindow {
    id: root

    required property var modelData
    screen: modelData

    readonly property string outName: modelData.name

    // One window per screen, but only the one the menu was summoned on shows.
    // MenuState.output is empty only before the fallback probe has ever
    // answered; treat that as "here" so the menu is never invisible everywhere.
    visible: MenuState.open
             && (MenuState.output === root.outName || MenuState.output === "")

    // ALL FOUR EDGES — the window is the whole screen, the menu is a rectangle
    // drawn in its top-left corner. The surface used to be exactly the size of
    // the panel, which meant a click anywhere else was delivered to whatever was
    // under the pointer and the menu just sat there open. There is no Wayland
    // protocol that tells a layer-shell client "the pointer went down somewhere
    // else" (that is what xdg_popup's grab is for, and a PanelWindow is not a
    // popup), so the only way to hear that click is to be the surface that
    // receives it. Hence a full-screen transparent catcher.
    anchors { top: true; left: true; right: true; bottom: true }

    // NEVER reserve space. This is a transient panel; an exclusive zone would
    // shove every window on the monitor down by 470px each time it opened —
    // the same trap the OSD hit (see Osd.qml).
    //
    // Ignore, not a zone of 0: this surface is placed from wherever layer-shell
    // decides its anchor edge is, and a zone of 0 still RESPECTS everyone
    // else's. So with a bar that reserves its 28px (auto-hide off), the menu
    // started 28px below the bar and then added its own 28px offset on top of
    // that — it hung off the bar with a strip of desktop showing through the
    // join. Ignore anchors it to the true screen edge, so `panel.y` below means
    // what it says on every configuration.
    //
    // It also matters more now the surface is full-screen: a zone of 0 would
    // have made it screen-height MINUS the bar, and the panel would have been
    // pushed a second bar's worth down inside it.
    exclusionMode: ExclusionMode.Ignore

    // Ask synui to frost what is behind the menu on a glass theme. The
    // compositor keys the backdrop blur off this namespace and nothing else —
    // the bar is a PanelWindow too and deliberately keeps the plain one, so the
    // name is the whole of how they are told apart. See layer.c.
    //
    // Safe on a surface that is the WHOLE SCREEN with a transparent catcher in
    // it: the blur is masked to where the client actually paints, so the panel
    // below frosts and the catcher stays clear.
    WlrLayershell.namespace: "synui-glass"

    // The whole point of the port. Without it the menu is deaf, which is exactly
    // the waybar failure it replaces.
    focusable: true

    color: "transparent"

    onVisibleChanged: if (visible) keys.forceActiveFocus()

    /*
     * ── "nothing installed matches" ──────────────────────────────────────
     *
     * The term this window wants packages looked up for, or "" for none. A
     * property with a change handler rather than a call buried in a binding:
     * the lookup forks a process, and a side effect inside a binding runs
     * again every time anything the binding touches changes, which for `rows`
     * is every keystroke AND every arriving answer.
     *
     * Guarded on `visible` because StartMenu is instantiated once per monitor
     * and MenuState.search is shared: without it, three screens would mean
     * three synpkg processes racing to answer the same question. Only one
     * window is ever visible (see the binding at the top of this file).
     *
     * Two characters is the floor. One matches most of the repositories, and
     * `provides` refuses it anyway — asking is just a fork to be told so.
     */
    readonly property string wantPkgTerm:
        (root.visible && MenuState.synpkgPresent
         && MenuState.search.length >= 2 && rowModel.localHits.length === 0)
            ? MenuState.search : ""

    onWantPkgTermChanged: {
        if (root.wantPkgTerm === "") MenuState.forgetPackages()
        else                         MenuState.wantPackages(root.wantPkgTerm)
    }

    // Whether this window draws the install section at all. Same condition
    // minus the length floor: the "search every source" row is offered for a
    // one-character search too, because it is a link and not a lookup.
    readonly property bool offerPackages:
        root.visible && MenuState.synpkgPresent && MenuState.search !== ""

    // Everywhere that is not the menu. Press to dismiss — press rather than
    // click, so the menu is gone by the time the button comes back up and a
    // press-drag-release never leaves it hanging around; and every button, since
    // a right-click on the desktop is just as much "not the menu" as a left one.
    //
    // The click is CONSUMED, not passed through to the window underneath. That
    // is not a limitation to apologise for — Wayland gives a client no way to
    // forward a press it has already been handed — and it is the behaviour every
    // other start menu has: the first click dismisses, the second one acts.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: MenuState.close()
    }

    Rectangle {
        id: panel

        // Hangs off the bar, on whichever edge that is. Deliberately NOT tied to
        // the bar's auto-hide slide: the menu is summoned by a keystroke at
        // least as often as by a click, and a menu that sits at a different
        // height depending on where the pointer happens to be reads as a bug.
        //
        // The surface is full-screen with ExclusionMode.Ignore, so `y` is
        // measured from the true screen edge and a bottom bar's menu is
        // "screen height, less the bar, less my own height".
        x: 0
        y: BarConfig.atBottom
           ? root.screen.height - Theme.barHeight - panel.height
           : Theme.barHeight

        width: 340

        // Sized to its rows, capped to what the monitor can show.
        //
        // A fixed height was wrong in both directions: the root page overflowed
        // it (Log Out and Shut Down were clipped off the bottom edge, reachable
        // only by scrolling into them blind), while a search matching two things
        // left most of the panel empty. contentHeight is driven by the model
        // rather than by the list's own height, so reading it here does not
        // close a binding loop.
        height: Math.max(120,
                Math.min(root.screen.height - Theme.barHeight - 16,
                         searchBox.height + list.contentHeight + 4))

        /*
         * ── What the wallpaper is doing under this menu ──────────────────────
         *
         * ⛔ THE COLUMN THE MENU CAN OCCUPY, AND NOT THE PAGE'S OWN HEIGHT.
         *
         * This is one surface that changes size as you walk it: the root page
         * is half the screen, `Games` fills it, `Graphics` is five rows. Asked
         * about the box it happens to occupy, the correction below answers a
         * different question on every page — and the answer is a STEP, because
         * the grid is a ninth of the screen deep. Measured on the desktop this
         * was reported from: `Accessories` ends at y=277 and reads clear;
         * `Internet`, four rows longer, ends at 325, crosses into the next grid
         * row, and reads frosted. Same menu, same place, same wallpaper, two
         * looks — and a page is not something anybody thinks of as changing
         * what the menu is made of.
         *
         * So the question is asked once, about the strip from the bar to the
         * far edge, which is where this panel lives whatever page is open. The
         * answer stops depending on the page and starts depending only on the
         * wallpaper and the screen, which is what a surface's material should
         * depend on.
         *
         * ⚠ IT CAN ONLY EVER MAKE THE MENU MORE OPAQUE, never less: the walk
         * takes the worst cell it is given, and a bigger region has the same
         * worst cell or a worse one. Nothing here can make a menu less readable
         * than the page-sized question made it.
         *
         * A property rather than a call inside the colour binding so both the
         * surface and the ink re-resolve from one evaluation, and so the
         * dependencies (the screen, the bar's edge, the published grid) are
         * declared in one place.
         */
        readonly property real spanHeight: root.screen.height - Theme.barHeight - 16
        readonly property real spanY: BarConfig.atBottom
            ? root.screen.height - Theme.barHeight - panel.spanHeight
            : Theme.barHeight
        readonly property var backdrop:
            Theme.backdropFor(root.screen, panel.x, panel.spanY,
                              panel.width, panel.spanHeight)

        color: Theme.popupBgOn(panel.backdrop)
        border.color: Theme.magenta
        border.width: 1
        radius: Theme.panelRadius

        // Swallows presses that land on the panel but not on a row. A Rectangle
        // accepts no buttons, and neither does the search box (an Item and a
        // Text), so without this the press falls THROUGH to the dismiss catcher
        // behind — clicking the search field, the 1px border, or the empty strip
        // under a short list would close the menu. Declared first so it sits
        // behind the list and the rows still get their clicks.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        // ── The row model ────────────────────────────────
        // Built in JS rather than as nested Repeaters because search flattens
        // across every page: the pages are one list viewed different ways, not a
        // tree of separate lists.
        //
        // Row shapes:
        //   { kind: "header", label }
        //   { kind: "back",   label }
        //   { kind: "page",   label, page }   -> descend
        //   { kind: "action", label, action, arg? } -> synctl dispatch <action> [arg]
        //   { kind: "exec",   label, argv }   -> run argv directly
        //   { kind: "app",    label, entry }  -> entry.execute()
        //   { kind: "install", label, pkg }   -> offer to install it (search only)
        QtObject {
            id: rowModel

            // XDG main categories → the page they file under, IN TEST ORDER. An
            // app usually lists several ("Game;Emulator;"), so precedence
            // matters: the catch-alls (Settings/System/Utility) must come last
            // or they swallow half the menu. Same table, same order, as menu.c's
            // CATEGORIES — verified to reproduce its grouping on this box.
            readonly property var catTable: [
                ["Game",        "Games"],
                ["Development", "Development"],
                ["Graphics",    "Graphics"],
                ["AudioVideo",  "Multimedia"],
                ["Audio",       "Multimedia"],
                ["Video",       "Multimedia"],
                ["Office",      "Office"],
                ["Science",     "Science"],
                ["Education",   "Education"],
                ["Network",     "Internet"],
                ["Settings",    "Settings"],
                ["System",      "System"],
                ["Utility",     "Accessories"]
            ]

            // The SECOND pass, tested only when nothing in catTable matched.
            // These are registered "additional" categories — the freedesktop
            // spec says an entry may carry them WITHOUT a main one, and plenty
            // do. Testing them is the difference between filing a thing and
            // shrugging it into "Other", which is what this menu used to do.
            //
            // Deliberately after the main table, never merged into it: an entry
            // that says `Game;Emulator` is a game, and an entry that says
            // `Utility;TextEditor` is the Utility the author chose to lead with.
            readonly property var catTable2: [
                ["WebBrowser",       "Internet"],
                ["InstantMessaging", "Internet"],
                ["P2P",              "Internet"],
                ["FileTransfer",     "Internet"],
                ["Player",           "Multimedia"],
                ["Recorder",         "Multimedia"],
                ["Music",            "Multimedia"],
                ["TV",               "Multimedia"],
                ["Photography",      "Graphics"],
                ["RasterGraphics",   "Graphics"],
                ["VectorGraphics",   "Graphics"],
                ["2DGraphics",       "Graphics"],
                ["3DGraphics",       "Graphics"],
                ["Scanning",         "Graphics"],
                ["TextEditor",       "Accessories"],
                ["Calculator",       "Accessories"],
                ["Archiving",        "Accessories"],
                ["Compression",      "Accessories"],
                ["FileManager",      "Accessories"],
                ["TerminalEmulator", "System"],
                ["Monitor",          "System"],
                ["Security",         "System"],
                ["PackageManager",   "System"],
                ["Filesystem",       "System"],
                ["WordProcessor",    "Office"],
                ["Spreadsheet",      "Office"],
                ["Presentation",     "Office"],
                ["Dictionary",       "Office"],
                ["Publishing",       "Office"],
                ["Calendar",         "Office"],
                ["IDE",              "Development"],
                ["Debugger",         "Development"],
                ["Building",         "Development"],
                ["GUIDesigner",      "Development"],
                ["Translation",      "Development"],
                ["HardwareSettings", "Settings"],
                ["Printing",         "Settings"],
                ["Emulator",         "Games"],
                ["ArcadeGame",       "Games"],
                ["BlockGame",        "Games"],
                ["SportsGame",       "Games"]
            ]

            // THE LAST RESORT, for an entry carrying no usable Categories at
            // all — which is commoner than it should be among proprietary
            // installers. DaVinci Resolve ships `Categories=` absent entirely,
            // as does rofi; both used to land in "Other" beside each other,
            // which is the "lazy" this replaces.
            //
            // Matched against the entry's own prose (name, generic name,
            // comment, keywords), so it is the vendor's description doing the
            // filing rather than a hardcoded list of applications. Resolve's
            // comment names "editing", "colour correction" and "audio post
            // production", and that is enough to put it in Multimedia.
            //
            // Ordered: the first hit wins, so the narrow words come before the
            // broad ones. Nothing here is a substitute for real Categories — an
            // entry SYNAPSE itself ships must carry them.
            readonly property var wordTable: [
                [/\b(browser|web)\b/i,                                  "Internet"],
                [/\b(chat|messeng|irc|e-?mail)\b/i,                      "Internet"],
                [/\b(video|audio|music|player|media|movie|editing|post production)\b/i,
                                                                         "Multimedia"],
                [/\b(photo|image|paint|draw|graphic|colou?r correction)\b/i,
                                                                         "Graphics"],
                [/\b(game|arcade|emulator)\b/i,                          "Games"],
                [/\b(terminal|console|shell)\b/i,                        "System"],
                [/\b(disk|partition|filesystem|monitor|task manager)\b/i, "System"],
                [/\b(editor|notepad)\b/i,                                "Accessories"],
                [/\b(theme|wallpaper|appearance)\b/i,                    "Settings"],
                [/\b(setup|installer|configur|settings|preferences)\b/i,  "Settings"]
            ]

            function categoryOf(entry) {
                const cats = entry.categories || []
                for (const pair of rowModel.catTable)
                    if (cats.indexOf(pair[0]) >= 0) return pair[1]
                for (const pair of rowModel.catTable2)
                    if (cats.indexOf(pair[0]) >= 0) return pair[1]
                // Wine writes a shortcut per imported program, most with no
                // category at all. Left alone they scatter into "Other" among
                // the genuinely uncategorised, which on a box with a few games
                // installed is most of the menu.
                if ((entry.id || "").indexOf("wine-") === 0) return "Wine"

                const prose = [entry.name, entry.genericName, entry.comment,
                               (entry.keywords || []).join(" ")]
                              .filter(x => x).join(" ")
                for (const pair of rowModel.wordTable)
                    if (pair[0].test(prose)) return pair[1]

                return "Other"
            }

            // ── Things on disk that are not applications ─────
            //
            // A .desktop file is not evidence that a human ever wants to launch
            // the thing. A Wine prefix in particular writes one shortcut per
            // Start Menu item the installer created, which means uninstallers,
            // readmes, "Visit our web site", registration forms and — for
            // anything that shipped an SDK — several dozen developer tools. On
            // this desk that was 50 of the 65 entries under Wine: the menu was
            // mostly noise with the games buried in it.
            //
            // Hidden, never launched-and-broken: the .desktop file is untouched
            // and every one of these still runs from a terminal or from
            // synfiles. See data/menu-hidden.conf for the other half of this
            // (specific ids) and for how to put one back.
            //
            // ⚠ These are hidden from SEARCH as well as from the pages. That is
            // deliberate and it is the one place this is not merely tidying:
            // typing "sims" and being offered "Uninstall The Sims" one row from
            // "The Sims" is a genuinely dangerous list to arrow through.

            // Runs a document or a URL, not a program. The only universal rule
            // here — everything below it is scoped to Wine, because outside a
            // Wine prefix these words appear in the names of real applications
            // ("Help" is a program on some systems; "Documentation" is a
            // category Qt Assistant legitimately claims).
            readonly property var reDocExec: /\.(url|htm|html|txt|chm|hlp|pdf|rtf|nfo)\b/i
            readonly property var reUninst:  /(uninst|unins0)/i
            // The DirectX SDK installs ~30 tools into the Start Menu. They are
            // real programs and they are developer tooling for a toolkit the
            // user installed to run a GAME.
            readonly property var reSdkPath: /DirectX (SDK|Utilities|Documentation)|Windows DirectX/i
            readonly property var reWineNoise: [
                /^uninstall\b/i, /\breadme\b/i, /\bdocumentation\b/i, /\bmanual\b/i,
                /\brelease notes\b/i, /\bhelp\b/i, /\bregistration\b/i, /^register\b/i,
                /\bcontact support\b/i, /\btechnical support\b/i, /^visit\b/i,
                /\bweb ?page\b/i, /\bweb ?site\b/i, /\bhomepage\b/i,
                // Multiplayer matchmaking services that shut down two decades
                // ago. The shortcut opens a dead URL.
                /\bon mplayer\.com$/i, /\bon heat$/i,
                /\bupdates?$/i
            ]

            function isNoise(entry) {
                const id   = entry.id || ""
                const name = entry.name || ""
                // execString is the raw Exec line; a missing one cannot be
                // tested and is not evidence of anything either way.
                const ex   = entry.command ? entry.command.join(" ")
                                           : (entry.execString || "")

                if (rowModel.reDocExec.test(ex))    return true
                if (/winebrowser/i.test(ex))        return true

                if (id.indexOf("wine-") !== 0) return false

                if (rowModel.reUninst.test(ex))     return true
                if (rowModel.reSdkPath.test(id))    return true
                for (const re of rowModel.reWineNoise)
                    if (re.test(name)) return true
                return false
            }

            // TRAP: `DesktopEntries.applications` is populated LAZILY, and only
            // for a reactive binding. An imperative read (in Component.onCompleted,
            // in a Timer, in a function) returns an EMPTY model and stays empty —
            // it looks exactly like a broken scan. Reading it in a binding, as
            // here, is what makes it fill; it then streams in one entry at a time
            // and settles within about a second of startup.
            //
            // noDisplay is already excluded by that model (checked: 0 of 155
            // entries have it set, while byId() can still reach hidden ones). The
            // guard stays because relying on that is relying on an undocumented
            // detail, and it costs nothing.
            readonly property var apps: {
                const out = {}
                const hidden = MenuState.hiddenIds
                for (const e of DesktopEntries.applications.values) {
                    if (e.noDisplay) continue
                    // The two filters, in cost order: a hash lookup of the id
                    // the user (or the shipped list) named, then the pattern
                    // rules. Both drop the entry from the pages AND from
                    // search — see isNoise() for why search is included.
                    if (hidden[e.id]) continue
                    if (rowModel.isNoise(e)) continue
                    const c = rowModel.categoryOf(e)
                    if (!out[c]) out[c] = []
                    out[c].push(e)
                }
                for (const k in out)
                    out[k].sort((a, b) => a.name.localeCompare(b.name))
                return out
            }

            /*
             * The DISPLAY name for a category bucket.
             *
             * ⛔ THE BUCKET NAME IS A KEY, WHICH IS WHY THIS FUNCTION EXISTS
             * RATHER THAN A TRANSLATED catTable. `apps` is keyed by it, a row's
             * `page:` addresses a page by it, and the sort below tests
             * `a === "Other"` — so a translated table would make a German menu
             * open a page that does not exist and file every app under Other.
             * The strings stay English everywhere except the last step before
             * drawing, which is here.
             *
             * ⚠ A CLOSED SET, SPELLED OUT. catTable, catTable2, the Wine rule
             * and the "Other" fallback are the only writers, so no other bucket
             * can arise — and listing them means a bucket added there without a
             * translation shows up as English rather than silently passing
             * through a lookup that returns its argument.
             */
            function catLabel(name) {
                switch (name) {
                case "Games":        return I18n.tr("Games")
                case "Development":  return I18n.tr("Development")
                case "Graphics":     return I18n.tr("Graphics")
                case "Multimedia":   return I18n.tr("Multimedia")
                case "Office":       return I18n.tr("Office")
                case "Science":      return I18n.tr("Science")
                case "Education":    return I18n.tr("Education")
                case "Internet":     return I18n.tr("Internet")
                case "Settings":     return I18n.tr("Settings")
                case "System":       return I18n.tr("System")
                case "Accessories":  return I18n.tr("Accessories")
                case "System Tools": return I18n.tr("System Tools")
                // ⛔ Wine is a product name and stays as it is in every language.
                case "Wine":         return "Wine"
                case "Other":        return I18n.tr("Other")
                }
                return name
            }

            // "Other" sinks to the bottom; the rest are alphabetical.
            // ⚠ SORTED ON THE KEY, NOT THE LABEL. Sorting a translated list
            // would be more correct for a reader and would also reorder the
            // menu the moment the language changed; the keys are what menu.c
            // ordered by and what the test compares against.
            readonly property var categoryNames: {
                const names = Object.keys(rowModel.apps)
                names.sort((a, b) => a === "Other" ?  1
                                   : b === "Other" ? -1
                                   : a.localeCompare(b))
                return names
            }

            // Every page, keyed by page id. Search walks this whole object; a
            // page view is one slice of it.
            readonly property var pages: {
                const p = {}

                p[""] = [
                    { kind: "header", label: I18n.tr("SYSTEM") },
                    { kind: "action", label: I18n.tr("Control Panel"), action: "control" },
                    { kind: "action", label: I18n.tr("Task Manager"),  action: "taskmgr" },
                    { kind: "exec",   label: I18n.tr("Terminal"),      argv: ["syntty"] }
                ]

                p["System Tools"] = [
                    { kind: "exec", label: I18n.tr("AI Shell (synsh)"), argv: ["syntty", "-e", "synsh"] },
                    // ⚠ --hold, because the whole value of this row is the
                    // output STAYING on screen after the command finishes. It
                    // was pinned to kitty for exactly that flag while syntty
                    // was the default terminal everywhere else; syntty grew it
                    // in 0.1.0-27 and these move with it.
                    //
                    // ⚠ AND `-e`, which kitty did not need. syntty takes the
                    // command after -e; handed it positionally it reads `syn`
                    // as a subcommand and dies on the next argument, so the
                    // window never opens and the row silently does nothing.
                    { kind: "exec", label: I18n.tr("System Status"),
                      argv: ["syntty", "--hold", "-e", "syn", "status"] },
                    { kind: "exec", label: I18n.tr("Network Setup"),    argv: ["syntty", "-e", "nmtui"] },
                    // synpkg, our own manager. A GUI (Terminal=false in its
                    // .desktop), so it runs bare, no terminal wrapper.
                    //
                    // This row used to fall back to shelly-ui. shelly is gone —
                    // the phased replacement finished — so there is no second
                    // graphical package manager to fall back TO, and the row is
                    // OMITTED rather than pointed at something absent.
                    //
                    // That is not a formality. pkgrel 317 shipped this row
                    // pointing at a synpkg that no upgrade path could install,
                    // and a failed exec here is silent: the row did nothing at
                    // all, with nothing anywhere saying why. A missing row is a
                    // fact the user can act on; a dead one is not. `needs` is
                    // filtered below, MenuState probes for the package, and
                    // `pages` is a binding, so the row appears on its own when
                    // synpkg lands mid-session, with no relog.
                    { kind: "exec", label: I18n.tr("Software Manager"),
                      needs: "synpkg", argv: ["synpkg", "gui"] },
                    // The full-system upgrade, same libalpm engine the Software
                    // Manager GUI drives, so CLI and GUI stay in agreement —
                    // not raw pacman, and never a partial-upgrade -Sy (synpkg
                    // upgrade always refreshes first unless told not to).
                    // --hold so the window survives the run.
                    //
                    // The fallback IS raw pacman, and -Syu (never -Sy): without
                    // synpkg there is nothing else on the box that does a full
                    // upgrade, and a bare -Sy leaves a system whose databases
                    // are newer than its packages — every later install then
                    // 404s on a filename the mirror has already rotated away.
                    // ⚠ --hold and -e, for the reasons on System Status above.
                    { kind: "exec", label: I18n.tr("Update System"),
                      argv: MenuState.synpkgPresent
                          ? ["syntty", "--hold", "-e", "synpkg", "upgrade"]
                          : ["syntty", "--hold", "-e", "sudo", "pacman", "-Syu"] },
                    // SynapseOS's OWN components. They come from the
                    // [synapseos] repo, which is a frozen copy of the
                    // installing ISO that nothing ever writes to, so no ALPM
                    // upgrade can ever see a newer synui — only syn-update,
                    // which rebuilds from git, can.
                    //
                    // Deliberately NOT folded into "Update System" above — that
                    // upgrades Arch, this upgrades the distro, and conflating
                    // them is how someone ends up believing they are current
                    // while synui sits 200 releases behind. synpkg surfaces the
                    // same check under `synpkg system check`, but applying it
                    // needs a terminal (build-all.sh runs sudo mid-build), and
                    // this GUI is the thing that owns that.
                    { kind: "exec", label: I18n.tr("SynapseOS Updates"), argv: ["syn-update-gui"] }
                ].filter(r => r.needs !== "synpkg" || MenuState.synpkgPresent)

                // The control panel's categories, not a second list of settings.
                //
                // This was thirteen hand-written rows naming individual panels,
                // and it had already drifted: no Theme, no Cursor theme, no
                // Printers, no Task manager, no Transparency, no Dock — nine
                // settings the compositor owns and this menu did not know about.
                // Nothing could have caught that, because the list was its own
                // source of truth. Now each row names a CATEGORY and the
                // compositor fills it from ctlpanel.c's item table, so a setting
                // added there appears here with no edit to this file.
                //
                // These are bind ACTIONS, not .desktop files, so none of them
                // appear under the scanned "Settings" category — which on a
                // stock install holds only cups' "Manage Printing". The page id
                // matches the CATEGORIES display name on purpose, so such an
                // entry folds onto this page instead of creating a second one.
                // ONE ROW PER CONTROL-PANEL CATEGORY, in the panel's own order.
                //
                // The arg is matched by ctlpanel_cat_from_name(), which
                // strcasecmp's it against ctlpanel_cat_name() — so these
                // strings are the category display names, lower-cased, and
                // nothing else is valid. An arg that matches nothing does NOT
                // fail: ctlpanel_show_cat falls back to the plain front door,
                // so a wrong or missing row looks like the panel opening on
                // whatever category it happened to be left on.
                //
                // Which is exactly how this list came to be missing Windows and
                // Input for as long as it was (fixed 2026-08-08): two whole
                // categories were unreachable from the menu and nothing
                // anywhere said so. tests/menu_cats.sh now compares this list
                // against ctlpanel.c and fails if a category is not here.
                p["Settings"] = [
                    { kind: "action", label: I18n.tr("Control Panel"),  action: "control" },
                    { kind: "action", label: I18n.tr("Appearance"),     action: "control", arg: "appearance" },
                    { kind: "action", label: I18n.tr("Windows"),        action: "control", arg: "windows" },
                    { kind: "action", label: I18n.tr("Desktop"),        action: "control", arg: "desktop" },
                    { kind: "action", label: I18n.tr("Input"),          action: "control", arg: "input" },
                    { kind: "action", label: I18n.tr("Display"),        action: "control", arg: "display" },
                    { kind: "action", label: I18n.tr("Sound"),          action: "control", arg: "sound" },
                    { kind: "action", label: I18n.tr("Network"),        action: "control", arg: "network" },
                    { kind: "action", label: I18n.tr("Power"),          action: "control", arg: "power" },
                    { kind: "action", label: I18n.tr("System"),         action: "control", arg: "system" },
                    { kind: "action", label: I18n.tr("Shortcuts"),      action: "control", arg: "shortcuts" },
                    // Lock stays a direct row. It is the one thing on this page
                    // nobody wants two clicks away, and it is an action rather
                    // than a setting — the control panel lists it under Power
                    // for findability, not because it belongs to a category.
                    { kind: "action", label: I18n.tr("Lock Screen"),    action: "lock" }
                ]

                for (const c of rowModel.categoryNames) {
                    if (!p[c]) p[c] = []
                    for (const e of rowModel.apps[c])
                        p[c].push({ kind: "app", label: e.name, entry: e })
                }

                return p
            }

            // What the search matches ON THIS MACHINE. Kept separate from
            // `rows` below because two other things key off "the search found
            // nothing installed" — whether to offer packages, and whether to
            // go and look for any — and neither may depend on `rows`, which
            // would then depend on them: a binding loop, and QML breaks those
            // by dropping one silently rather than by complaining.
            //
            // Empty whenever the box is empty, so nothing downstream has to
            // ask twice whether a search is happening.
            readonly property var localHits: {
                const q = MenuState.search.toLowerCase()
                if (q === "") return []

                // Search reaches EVERY page. Searching only the page you are
                // looking at would make the submenus a place things can hide,
                // which is the one thing they must not be. Page rows drop out:
                // they launch nothing, and "Games" matching "gam" ahead of the
                // games is not an answer to the question.
                const hits = []
                for (const key in rowModel.pages)
                    for (const r of rowModel.pages[key]) {
                        if (r.kind === "header" || r.kind === "page") continue
                        if (r.label.toLowerCase().indexOf(q) >= 0) hits.push(r)
                    }
                hits.sort((a, b) => a.label.localeCompare(b.label))
                return hits
            }

            // What the list shows right now.
            readonly property var rows: {
                const q = MenuState.search.toLowerCase()

                if (q !== "") {
                    const hits = rowModel.localHits.slice()
                    if (hits.length > 0 || !root.offerPackages) return hits

                    /*
                     * Nothing installed matches — so answer the question the
                     * empty list does not.
                     *
                     * A start menu that shrugs at "inkscape" has told the
                     * person typing it precisely nothing: they cannot tell a
                     * misspelling from a missing package from a menu that
                     * hides things. Both rows below are answers. The first is
                     * what to install to get it (MenuState asks `synpkg
                     * provides`, debounced — see there); the second is the
                     * wider search, on the sources that are too slow to ask
                     * while somebody types.
                     *
                     * ⚠ ONLY WHEN THE LOCAL LIST IS EMPTY. Appending "you
                     * could also install…" under a search that already found
                     * the application would be a menu arguing with itself, and
                     * the row it pushed down is the row that was wanted.
                     */
                    if (MenuState.pkgTerm === MenuState.search
                        && MenuState.pkgResults.length > 0) {
                        hits.push({ kind: "header", label: I18n.tr("AVAILABLE TO INSTALL") })
                        for (const p of MenuState.pkgResults)
                            hits.push({
                                kind:  "install",
                                pkg:   p.name,
                                label: p.desc ? p.name + " — " + p.desc : p.name
                            })
                    }

                    // Last, and present even when `provides` found nothing:
                    // this row is the reason an empty search is never a dead
                    // end. The repositories are not everything, and the AUR and
                    // Flathub are exactly where the answer is when they have
                    // come up empty.
                    hits.push({
                        kind:  "exec",
                        label: I18n.tr("Search every source for “%1”…").arg(MenuState.search),
                        argv:  ["synpkg", "gui", "all",
                                "--search", MenuState.search]
                    })
                    return hits
                }

                const page = MenuState.page
                const out = []
                if (page !== "") out.push({ kind: "back", label: I18n.tr("Back") })

                for (const r of (rowModel.pages[page] || [])) out.push(r)

                if (page === "") {
                    // The two fixed submenus sit under SYSTEM, before the
                    // scanned categories — same order menu.c emitted them in.
                    // ⛔ `page` IS THE KEY rowModel.pages is indexed by. Only
                    // `label` is drawn — see catLabel().
                    out.push({ kind: "page", label: I18n.tr("System Tools"),
                               page: "System Tools" })
                    out.push({ kind: "page", label: I18n.tr("Settings"),
                               page: "Settings" })

                    out.push({ kind: "header", label: I18n.tr("APPLICATIONS") })
                    for (const c of rowModel.categoryNames) {
                        // Settings already has its fixed root row above; a
                        // second one from a Settings-category .desktop would
                        // double it.
                        if (c === "Settings") continue
                        out.push({ kind: "page", label: rowModel.catLabel(c), page: c })
                    }

                    out.push({ kind: "header", label: I18n.tr("POWER") })
                    out.push({ kind: "action", label: I18n.tr("Lock Screen"), action: "lock" })
                    out.push({ kind: "action", label: I18n.tr("Log Out"),     action: "quit" })
                    out.push({ kind: "exec",   label: I18n.tr("Reboot"),
                               argv: ["sudo", "systemctl", "reboot"] })
                    out.push({ kind: "exec",   label: I18n.tr("Shut Down"),
                               argv: ["sudo", "systemctl", "poweroff"] })
                }

                return out
            }
        }

        // ── Activation ───────────────────────────────────
        //
        // execDetached, NOT a shared Process object — the rule PostItState.qml
        // already writes down for two deletions in a row, and the menu is where
        // it was not applied.
        //
        // A Process runs ONE command at a time. Assigning `command` while it is
        // still running does not start a second child; it QUEUES, and the queued
        // command runs the moment the first child exits. Every row here launches
        // something that lives as long as its window does — `synpkg gui` and
        // `syn-update-gui` both exec quickshell in the FOREGROUND, a terminal stays
        // up — so one shared Process meant the start menu could only ever have
        // one launched application alive at a time.
        //
        // What that looked like: open Software Manager, then click SynapseOS
        // Updates, and nothing happens — no window, no error, and the row looks
        // broken. Close Software and the Updates window appears instantly,
        // which is the queued command finally running. Any pair collided the
        // same way: Terminal and Software, System Status and Network Setup, two
        // terminals. Reported as "updates doesn't want software open at the
        // same time", which is exactly what it does.
        //
        // Detaching is also what stops a bar restart from taking every
        // application launched from the menu with it.

        function activate(row) {
            if (!row) return
            switch (row.kind) {
            case "header":
                return
            case "back":
                // The on-screen Back row lands where Left and Escape land.
                // It is the same journey by a different input, and a mouse
                // that lost your place while an arrow key kept it would be
                // the harder pair to explain.
                list.returningTo = MenuState.page
                MenuState.page = ""
                return
            case "page":
                MenuState.page = row.page
                return
            case "action":
                // The optional arg is one word from this file's own tables — a
                // control-panel category — never anything scanned off disk, and
                // it goes through argv rather than a shell either way.
                Quickshell.execDetached(row.arg
                    ? ["synctl", "dispatch", row.action, row.arg]
                    : ["synctl", "dispatch", row.action])
                break
            case "exec":
                // argv, not a shell string. Nothing here needs a shell, and a
                // menu that runs /bin/sh -c is a menu one hostile .desktop name
                // away from being an injection — the exact hazard synui-cursor
                // had to defend against for theme names.
                Quickshell.execDetached(row.argv)
                break
            case "install":
                /*
                 * A TERMINAL, and a deliberately un-automated one.
                 *
                 * `synpkg install` asks before it does anything and
                 * authenticates through polkit; both of those need somewhere to
                 * ask, and the terminal is it. That is the opposite of the rule
                 * a GUI front-end follows — a button that shells out to synpkg
                 * must pass --noconfirm or it silently installs nothing — and
                 * the difference is the point: a button is a decision already
                 * taken, whereas this row was reached by typing four letters
                 * into a search box and pressing Return. Installing a package
                 * off the back of that without asking would be indefensible.
                 * --hold keeps the output after it finishes, the same as every
                 * other terminal row on this menu.
                 *
                 * `pkg` is a package NAME out of synpkg's own TSV, and it goes
                 * through argv, so there is no shell for it to be anything else
                 * in.
                 */
                Quickshell.execDetached(["syntty", "--hold", "-e",
                                         "synpkg", "install", row.pkg])
                break
            case "app":
                /*
                 * ⚠ Terminal=true IS HANDLED HERE, NOT LEFT TO execute().
                 *
                 * Reported as cliamp not opening from the menu. It is a
                 * terminal application, so its .desktop carries Terminal=true,
                 * and a launcher owes it a terminal. execute() goes looking for
                 * one the way GLib does — down a list compiled into libgio
                 * (xterm, konsole, gnome-terminal, …) that syntty, kitty and
                 * foot are not on and never will be. The row therefore did
                 * nothing, silently, on exactly the entries that need the most
                 * help.
                 *
                 * ⛔ AND IT FAILS SILENTLY, which is why it went unnoticed: a
                 * menu row that launches nothing looks identical to one whose
                 * program crashed on startup.
                 *
                 * syntty is the shipped terminal and what every static row in
                 * this file already hardcodes ("AI Shell", "Network Setup").
                 * The application page does the same thing in appgrid_launch(),
                 * so all three doors open a CLI program the same way.
                 *
                 * `command` is quickshell's PARSED argv with the field codes
                 * already resolved, so this stays argv-not-a-shell-string —
                 * the rule the "exec" case above states at length.
                 */
                if (row.entry.runInTerminal) {
                    const argv = row.entry.command || []
                    if (argv.length > 0)
                        Quickshell.execDetached(["syntty", "-e"].concat(argv))
                    else
                        row.entry.execute()   /* nothing to wrap; let it try */
                    break
                }
                // execute() handles the field codes (%f %u %i %c) and Path=
                // that menu.c had to implement by hand.
                row.entry.execute()
                break
            }
            MenuState.close()
        }

        // ── Keyboard ─────────────────────────────────────
        Item {
            id: keys
            anchors.fill: parent
            focus: true

            function selectable(i) {
                const r = rowModel.rows[i]
                return r !== undefined && r.kind !== "header"
            }

            // Bounded by the row count, so a page of nothing but headers cannot
            // spin here.
            function step(dir) {
                const n = rowModel.rows.length
                if (n === 0) return
                let i = list.selected
                for (let tries = 0; tries < n; tries++) {
                    i = (i + dir + n) % n
                    if (keys.selectable(i)) { list.selected = i; break }
                }
                list.positionViewAtIndex(list.selected, ListView.Contain)
            }

            Keys.onPressed: (e) => {
                switch (e.key) {
                case Qt.Key_Escape:
                    // Escape unwinds one step at a time — clear the search, then
                    // leave the page, then close. Closing outright from inside a
                    // category would mean the only way back to the root is to
                    // reopen and start again.
                    // Escape lands where Left lands. The two keys already
                    // unwind in the same order, and one of them keeping your
                    // place while the other threw it away would be the more
                    // confusing pair.
                    if (MenuState.search !== "")      MenuState.search = ""
                    else if (MenuState.page !== "") {
                        list.returningTo = MenuState.page
                        MenuState.page = ""
                    }
                    else                              MenuState.close()
                    e.accepted = true
                    return
                case Qt.Key_Down:  keys.step(1);  e.accepted = true; return
                case Qt.Key_Up:    keys.step(-1); e.accepted = true; return
                case Qt.Key_Right: {
                    // Descend, and ONLY descend. Right is not Enter: on a leaf
                    // row activate() launches the thing and closes the menu,
                    // which is far too much to hang off an arrow key pressed
                    // while looking for the next level. On anything that is not
                    // a submenu this does nothing on purpose.
                    const r = rowModel.rows[list.selected]
                    if (r && r.kind === "page") MenuState.page = r.page
                    e.accepted = true
                    return
                }
                case Qt.Key_Left:
                    // Come back up, unwinding in Escape's order — search first,
                    // then the page — because that order is already what this
                    // menu teaches and two keys that disagree about what "back"
                    // means is worse than one extra press.
                    //
                    // ⚠ It stops there rather than closing. Escape is the key
                    // that closes; a Left at the root that dismissed the whole
                    // menu would make overshooting while walking back up cost
                    // the menu itself.
                    if (MenuState.search !== "")      MenuState.search = ""
                    else if (MenuState.page !== "") {
                        list.returningTo = MenuState.page
                        MenuState.page = ""
                    }
                    e.accepted = true
                    return
                case Qt.Key_Return:
                case Qt.Key_Enter:
                    panel.activate(rowModel.rows[list.selected])
                    e.accepted = true
                    return
                case Qt.Key_Backspace:
                    MenuState.search = MenuState.search.slice(0, -1)
                    e.accepted = true
                    return
                }
                // Type-to-search. e.text is empty for modifiers and the arrows,
                // so this cannot eat navigation; the 0x20 floor keeps Tab and
                // the other control characters out of the box.
                if (e.text.length > 0 && e.text.charCodeAt(0) >= 0x20) {
                    MenuState.search += e.text
                    e.accepted = true
                }
            }
        }

        // Put the selection back on the first selectable row whenever the list
        // changes under it. A stale index into a shorter list selects nothing,
        // and Enter then does nothing — which reads as the menu ignoring you.
        //
        // ⚠ EXCEPT WHEN COMING BACK UP. Walking into a category and pressing
        // Left rebuilt the root list and dropped the selection at the top, so
        // browsing with the arrows lost your place every time you looked into
        // a category and stepped back out — the one moment you most want it
        // kept. `returningTo` names the page just left, and the row that
        // leads back into it is found BY PAGE ID rather than by remembering
        // an index: the root list is not the same list it was, and an index
        // into it means something different once a search or a rebuild has
        // moved things around.
        Connections {
            target: rowModel
            function onRowsChanged() {
                if (list.returningTo !== "") {
                    const want = list.returningTo
                    list.returningTo = ""
                    for (let i = 0; i < rowModel.rows.length; i++) {
                        const r = rowModel.rows[i]
                        if (r && r.kind === "page" && r.page === want) {
                            list.selected = i
                            list.positionViewAtIndex(i, ListView.Contain)
                            return
                        }
                    }
                    // The category is gone — renamed, filtered out, or the
                    // menu rebuilt underneath. Fall through to the top rather
                    // than leaving the selection pointing at nothing.
                }
                list.selected = 0
                if (!keys.selectable(0)) keys.step(1)
                list.positionViewAtBeginning()
            }
        }

        // ── Search box ───────────────────────────────────
        Item {
            id: searchBox
            anchors { top: parent.top; left: parent.left; right: parent.right; margins: 1 }
            height: 30

            Text {
                anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: MenuState.search !== "" ? MenuState.search
                    : MenuState.page   !== "" ? MenuState.page
                                              : I18n.tr("Type to search…")
                color: MenuState.search !== "" ? Theme.popupFgOn(panel.backdrop)
                                               : Theme.popupFgDimOn(panel.backdrop)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Theme.magenta
                opacity: 0.5
            }
        }

        // ── Rows ─────────────────────────────────────────
        ListView {
            // A view that scrolls says so — see SynScrollBar.qml.
            ScrollBar.vertical: SynScrollBar {}
            id: list
            anchors {
                top: searchBox.bottom; left: parent.left
                right: parent.right;   bottom: parent.bottom
                margins: 1
            }
            clip: true
            model: rowModel.rows

            property int selected: 0
            // The page a Left or an Escape has just stepped out of, so the
            // rebuild below can put the selection back on the row that leads
            // into it. Empty at every other moment.
            property string returningTo: ""

            delegate: Item {
                id: rowItem
                required property int index
                required property var modelData

                width: list.width
                height: rowItem.modelData.kind === "header" ? 22 : 24

                readonly property bool current: list.selected === rowItem.index
                readonly property bool header:  rowItem.modelData.kind === "header"

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radius
                    visible: rowItem.current && !rowItem.header
                    color: Theme.activeBg
                    border.width: 1
                    border.color: Theme.magenta
                }

                Text {
                    anchors {
                        left: parent.left; right: parent.right
                        leftMargin: rowItem.header ? 10 : 16
                        rightMargin: 10
                        verticalCenter: parent.verticalCenter
                    }
                    elide: Text.ElideRight
                    // The "+" marks a row that will FETCH something rather
                    // than start something already here. It is the one kind of
                    // row on this menu that costs a download and a password,
                    // and the section header scrolls out of sight the moment
                    // there is more than a screenful.
                    text: rowItem.modelData.kind === "page" ? rowItem.modelData.label + "  ▸"
                        : rowItem.modelData.kind === "back" ? I18n.tr("◂  Back")
                        : rowItem.modelData.kind === "install" ? "+  " + rowItem.modelData.label
                                                            : rowItem.modelData.label
                    color: rowItem.header ? Theme.magenta : Theme.popupFgOn(panel.backdrop)
                    opacity: rowItem.header ? 1.0 : (rowItem.current ? 1.0 : 0.82)
                    font.family: Theme.fontFamily
                    font.pixelSize: rowItem.header ? 10 : Theme.fontSize
                    font.bold: rowItem.header
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: !rowItem.header
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onEntered: list.selected = rowItem.index
                    onClicked: (m) => {
                        // ⚠ RIGHT DOES NOT ACTIVATE. A context menu that also
                        // launched the thing it is a menu about would make the
                        // Uninstall row unreachable — the application would be
                        // in front of it before the menu drew.
                        if (m.button === Qt.RightButton) {
                            list.selected = rowItem.index
                            ctx.openFor(rowItem.modelData,
                                        rowItem.mapToItem(panel, m.x, m.y))
                            return
                        }
                        panel.activate(rowItem.modelData)
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // The right-click menu on an application row.
        //
        // ⚠ ONE ROW, AND IT IS THE DANGEROUS ONE. This menu exists to carry
        // Uninstall and nothing else has been put beside it — a context menu
        // whose rows are Open / Pin / Uninstall is a menu where the destructive
        // row is one slot away from the two anybody uses without looking. If
        // more rows are ever wanted here, Uninstall goes LAST and behind a
        // separator, the way dock.c orders Quit All Windows.
        //
        // ⚠ IT ASKS WHO OWNS THE APPLICATION RATHER THAN ASSUMING. Plenty of
        // what is on this menu came from nowhere a package manager knows —
        // a .desktop somebody wrote in ~/.local/share/applications, a Wine
        // shortcut, a script. Offering to uninstall those would be an offer
        // that cannot be kept. The row therefore says what it found: the
        // package name while it can act, and why not when it cannot.
        Item {
            id: ctx
            visible: ctx.entry !== null
            z: 10

            property var entry: null      /* the DesktopEntry, or null */
            property string appName: ""
            property string owner: ""     /* package name once known */
            property string phase: ""     /* "asking" | "owned" | "unowned" */
            // ⛔ NOT `state`. That is QQuickItem's own property — the one
            // its state machine reads — and shadowing it with a string is a
            // clash Qt resolves in ways nobody here intends. qmllint named
            // it; nothing on screen would have.

            function openFor(row, pt) {
                if (!row || row.kind !== "app" || !row.entry) return
                ctx.entry = row.entry
                ctx.appName = row.label || row.entry.name || ""
                ctx.owner = ""
                ctx.phase = "asking"

                // Clamped so a right-click near an edge does not put the menu
                // half off the panel, where its own rows cannot be reached.
                const w = 230, h = 62
                ctx.x = Math.max(4, Math.min(pt.x, panel.width  - w - 4))
                ctx.y = Math.max(4, Math.min(pt.y, panel.height - h - 4))
                ctx.width = w
                ctx.height = h

                // ⚠ ASKED HERE, NOT WHEN THE ROW WAS DRAWN. There are several
                // hundred entries on this menu and this is a subprocess per
                // question; asking on hover, or eagerly for the whole list,
                // would put a fork storm on the arrow keys.
                ownerProc.command = ["synpkg", "owner", "-q", ctx.entry.id]
                ownerProc.running = true
            }

            function close() {
                ctx.entry = null
                ctx.phase = ""
            }

            // ⛔ AND IT GOES WHEN THE MENU DOES. Without this the context menu
            // survives the start menu closing — so the next time the menu is
            // summoned it comes up with a menu about an application nobody
            // right-clicked, sitting wherever the pointer happened to be last
            // time, and its dismiss catcher swallowing the first click at the
            // list behind it. Found by a rig that reopened the menu and could
            // no longer launch anything from it, and only visible in the
            // screenshot: the stale menu looks exactly like a fresh one.
            //
            // ⚠ A BOUND PROPERTY, not a Connections on MenuState. The singleton
            // is reached from inside a nested component here, which is the
            // scope qmllint keeps warning about — mirroring it onto this Item
            // makes the change signal this object's own and leaves nothing to
            // resolve at runtime.
            readonly property bool menuOpen: MenuState.open
            onMenuOpenChanged: ctx.close()

            Process {
                id: ownerProc
                stdout: StdioCollector {
                    onStreamFinished: {
                        // ⚠ TRIMMED, and the exit status is what decides. An
                        // empty answer with a zero status cannot happen, but a
                        // non-empty one with a failure could — synpkg prints
                        // the name on stdout and its reasons on stderr, so
                        // reading stdout alone would take a warning for a
                        // package name.
                        ctx.owner = this.text.trim()
                    }
                }
                onExited: (code) => {
                    ctx.phase = (code === 0 && ctx.owner !== "") ? "owned" : "unowned"
                }
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.popupBg
                border.width: 1
                border.color: Theme.magenta
                radius: Theme.radius
            }

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                Text {
                    width: parent.width
                    text: ctx.appName
                    elide: Text.ElideRight
                    color: Theme.magenta
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    font.bold: true
                }

                Rectangle {
                    width: parent.width; height: 26
                    radius: Theme.radius
                    color: uninstallHover.hovered && ctx.phase === "owned"
                           ? Theme.activeBg : "transparent"

                    Text {
                        anchors {
                            left: parent.left; right: parent.right
                            leftMargin: 6; rightMargin: 6
                            verticalCenter: parent.verticalCenter
                        }
                        elide: Text.ElideRight
                        text: ctx.phase === "asking"
                              ? I18n.tr("Checking…")
                              : ctx.phase === "owned"
                                ? I18n.tr("Uninstall %1").arg(ctx.owner)
                                : I18n.tr("Not from a package")
                        color: Theme.popupFgOn(panel.backdrop)
                        opacity: ctx.phase === "owned" ? 1.0 : 0.55
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }

                    HoverHandler { id: uninstallHover }

                    MouseArea {
                        anchors.fill: parent
                        // ⚠ Only once the answer is in. A row that acted while
                        // it still said "Checking…" would run `remove --owner`
                        // on an application nothing owns, and the refusal would
                        // arrive in a terminal the user did not ask for.
                        enabled: ctx.phase === "owned"
                        onClicked: {
                            /*
                             * A TERMINAL, and deliberately not a silent one —
                             * the same rule the "install" row states at length,
                             * and it matters more here.
                             *
                             * `synpkg remove` prints what it is about to take
                             * (an uninstall is -Rns, so it takes the unneeded
                             * dependencies with it), asks, and authenticates
                             * through polkit. All three need somewhere to
                             * happen. A GUI front-end passing --noconfirm would
                             * remove an application and everything it dragged
                             * out with it on ONE right-click and ONE left
                             * click, with no list and no second thought.
                             *
                             * ⚠ --owner, so the id goes through argv and synpkg
                             * resolves it. The menu never builds a package name
                             * and there is no shell for a .desktop id to be
                             * anything but one argument in.
                             */
                            Quickshell.execDetached(
                                ["syntty", "--hold", "-e",
                                 "synpkg", "remove", "--owner", ctx.entry.id])
                            ctx.close()
                            MenuState.close()
                        }
                    }
                }
            }
        }

        // Anywhere else on the panel closes the context menu rather than the
        // whole menu — a right-click that opened something by mistake should
        // cost one click to undo, not the place you had got to.
        MouseArea {
            anchors.fill: parent
            z: 9
            visible: ctx.entry !== null
            enabled: ctx.entry !== null
            acceptedButtons: Qt.AllButtons
            onPressed: ctx.close()
        }
    }
}
