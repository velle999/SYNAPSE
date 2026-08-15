import QtQuick
import Quickshell

/*
 * The SYNAPSE start menu.
 *
 * Ported from src/menu.c (synui pkgrel 95-161), which existed for one reason:
 * waybar sets keyboard_interactivity NONE once at startup and never revises it,
 * so a GTK menu there could map, draw, and never receive a key. That was never a
 * limitation of layer-shell or of synui — a quickshell PanelWindow with
 * `focusable: true` takes EXCLUSIVE keyboard focus and arrow-navigates fine
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

    // The whole point of the port. Without it the menu is deaf, which is exactly
    // the waybar failure it replaces.
    focusable: true

    color: "transparent"

    onVisibleChanged: if (visible) keys.forceActiveFocus()

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

        color: Theme.popupBg
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

            function categoryOf(entry) {
                const cats = entry.categories || []
                for (const pair of rowModel.catTable)
                    if (cats.indexOf(pair[0]) >= 0) return pair[1]
                // Wine writes a shortcut per imported program, most with no
                // category at all. Left alone they scatter into "Other" among
                // the genuinely uncategorised, which on a box with a few games
                // installed is most of the menu.
                return (entry.id || "").indexOf("wine-") === 0 ? "Wine" : "Other"
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
                for (const e of DesktopEntries.applications.values) {
                    if (e.noDisplay) continue
                    const c = rowModel.categoryOf(e)
                    if (!out[c]) out[c] = []
                    out[c].push(e)
                }
                for (const k in out)
                    out[k].sort((a, b) => a.name.localeCompare(b.name))
                return out
            }

            // "Other" sinks to the bottom; the rest are alphabetical.
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
                    { kind: "header", label: "SYSTEM" },
                    { kind: "action", label: "Control Panel", action: "control" },
                    { kind: "action", label: "Task Manager",  action: "taskmgr" },
                    { kind: "exec",   label: "Terminal",      argv: ["syntty"] }
                ]

                p["System Tools"] = [
                    { kind: "exec", label: "AI Shell (synsh)", argv: ["syntty", "-e", "synsh"] },
                    // ⚠ kitty, NOT syntty, and deliberately: these are the rows
                    // whose whole value is the output STAYING on screen after
                    // the command finishes, and `--hold` is a kitty/foot flag
                    // syntty does not have yet. syntty is the default terminal
                    // everywhere the window is meant to close with the command;
                    // when it grows --hold these move too, in one change.
                    { kind: "exec", label: "System Status",    argv: ["kitty", "--hold", "syn", "status"] },
                    { kind: "exec", label: "Network Setup",    argv: ["syntty", "-e", "nmtui"] },
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
                    { kind: "exec", label: "Software Manager",
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
                    // ⚠ kitty, NOT syntty, and deliberately: these are the rows
                    // whose whole value is the output STAYING on screen after
                    // the command finishes, and `--hold` is a kitty/foot flag
                    // syntty does not have yet. syntty is the default terminal
                    // everywhere the window is meant to close with the command;
                    // when it grows --hold these move too, in one change.
                    { kind: "exec", label: "Update System",
                      argv: MenuState.synpkgPresent
                          ? ["kitty", "--hold", "synpkg", "upgrade"]
                          : ["kitty", "--hold", "sudo", "pacman", "-Syu"] },
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
                    { kind: "exec", label: "SynapseOS Updates", argv: ["syn-update-gui"] }
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
                    { kind: "action", label: "Control Panel",  action: "control" },
                    { kind: "action", label: "Appearance",     action: "control", arg: "appearance" },
                    { kind: "action", label: "Windows",        action: "control", arg: "windows" },
                    { kind: "action", label: "Desktop",        action: "control", arg: "desktop" },
                    { kind: "action", label: "Input",          action: "control", arg: "input" },
                    { kind: "action", label: "Display",        action: "control", arg: "display" },
                    { kind: "action", label: "Sound",          action: "control", arg: "sound" },
                    { kind: "action", label: "Network",        action: "control", arg: "network" },
                    { kind: "action", label: "Power",          action: "control", arg: "power" },
                    { kind: "action", label: "System",         action: "control", arg: "system" },
                    { kind: "action", label: "Shortcuts",      action: "control", arg: "shortcuts" },
                    // Lock stays a direct row. It is the one thing on this page
                    // nobody wants two clicks away, and it is an action rather
                    // than a setting — the control panel lists it under Power
                    // for findability, not because it belongs to a category.
                    { kind: "action", label: "Lock Screen",    action: "lock" }
                ]

                for (const c of rowModel.categoryNames) {
                    if (!p[c]) p[c] = []
                    for (const e of rowModel.apps[c])
                        p[c].push({ kind: "app", label: e.name, entry: e })
                }

                return p
            }

            // What the list shows right now.
            readonly property var rows: {
                const q = MenuState.search.toLowerCase()

                if (q !== "") {
                    // Search reaches EVERY page. Searching only the page you are
                    // looking at would make the submenus a place things can
                    // hide, which is the one thing they must not be. Page rows
                    // drop out: they launch nothing, and "Games" matching "gam"
                    // ahead of the games is not an answer to the question.
                    const hits = []
                    for (const key in rowModel.pages)
                        for (const r of rowModel.pages[key]) {
                            if (r.kind === "header" || r.kind === "page") continue
                            if (r.label.toLowerCase().indexOf(q) >= 0) hits.push(r)
                        }
                    hits.sort((a, b) => a.label.localeCompare(b.label))
                    return hits
                }

                const page = MenuState.page
                const out = []
                if (page !== "") out.push({ kind: "back", label: "Back" })

                for (const r of (rowModel.pages[page] || [])) out.push(r)

                if (page === "") {
                    // The two fixed submenus sit under SYSTEM, before the
                    // scanned categories — same order menu.c emitted them in.
                    out.push({ kind: "page", label: "System Tools", page: "System Tools" })
                    out.push({ kind: "page", label: "Settings",     page: "Settings" })

                    out.push({ kind: "header", label: "APPLICATIONS" })
                    for (const c of rowModel.categoryNames) {
                        // Settings already has its fixed root row above; a
                        // second one from a Settings-category .desktop would
                        // double it.
                        if (c === "Settings") continue
                        out.push({ kind: "page", label: c, page: c })
                    }

                    out.push({ kind: "header", label: "POWER" })
                    out.push({ kind: "action", label: "Lock Screen", action: "lock" })
                    out.push({ kind: "action", label: "Log Out",     action: "quit" })
                    out.push({ kind: "exec",   label: "Reboot",      argv: ["sudo", "systemctl", "reboot"] })
                    out.push({ kind: "exec",   label: "Shut Down",   argv: ["sudo", "systemctl", "poweroff"] })
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
        // `syn-update-gui` both exec quickshell in the FOREGROUND, `kitty` stays
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
            case "app":
                // execute() handles the field codes (%f %u %i %c), Terminal=
                // and Path= that menu.c had to implement by hand.
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
                    if (MenuState.search !== "")      MenuState.search = ""
                    else if (MenuState.page !== "")   MenuState.page = ""
                    else                              MenuState.close()
                    e.accepted = true
                    return
                case Qt.Key_Down:  keys.step(1);  e.accepted = true; return
                case Qt.Key_Up:    keys.step(-1); e.accepted = true; return
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
        Connections {
            target: rowModel
            function onRowsChanged() {
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
                                              : "Type to search…"
                color: MenuState.search !== "" ? Theme.fg : Theme.fgDim
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
            id: list
            anchors {
                top: searchBox.bottom; left: parent.left
                right: parent.right;   bottom: parent.bottom
                margins: 1
            }
            clip: true
            model: rowModel.rows

            property int selected: 0

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
                    text: rowItem.modelData.kind === "page" ? rowItem.modelData.label + "  ▸"
                        : rowItem.modelData.kind === "back" ? "◂  Back"
                                                            : rowItem.modelData.label
                    color: rowItem.header ? Theme.magenta : Theme.fg
                    opacity: rowItem.header ? 1.0 : (rowItem.current ? 1.0 : 0.82)
                    font.family: Theme.fontFamily
                    font.pixelSize: rowItem.header ? 10 : Theme.fontSize
                    font.bold: rowItem.header
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: !rowItem.header
                    hoverEnabled: true
                    onEntered: list.selected = rowItem.index
                    onClicked: panel.activate(rowItem.modelData)
                }
            }
        }
    }
}
