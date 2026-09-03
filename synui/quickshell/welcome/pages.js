.pragma library

/*
 * pages.js — what the welcome guide SAYS, page by page.
 *
 * A `.pragma library` JS file rather than a QML singleton, for the reason
 * TuxArt learned the hard way: importing a QML directory module instantiates
 * EVERY singleton its qmldir declares, so a table of strings placed in
 * quickshell/ would drag Theme, BarConfig, MenuState and the whole bar in
 * behind it and become unloadable from anything that is not the bar. A .js is
 * loaded once, shared by every importer, and owes nothing to any module.
 *
 * ⚠ IT NEEDS ITS OWN INSTALL RULE. synui's PKGBUILD globs `*.qml` per
 * directory; a tree without a `*.js` glob installs, the guide starts, and every
 * page is `undefined` — which draws nothing and says nothing.
 *
 * ── The `key` field is a FALLBACK, not the answer ───────────────────────────
 *
 * Every chord below is what the row shows when NOTHING is bound to its action.
 * The live chord comes from `synctl binds`, which reports the compositor's own
 * table — see Guide.qml's bindProbe. This matters: the command bar has been on
 * Super+Space, on Super+=, and back on Super+Space, and each move left the old
 * welcome menu's hand-typed column naming the previous one. Anyone who rebinds
 * a key in the shortcuts palette is in the same position.
 *
 * ── Page shape ──────────────────────────────────────────────────────────────
 *
 *   { id, nav, title, blurb, rows }
 *
 * `nav` is the RAIL's label and `title` is the page's own heading, and they are
 * two fields because they are two jobs: the rail is 180px wide and a title that
 * reads well as a heading ("The keys worth knowing") elides there to "The keys
 * worth k…", which is the contents page failing at the one thing it is for.
 *
 * ── Row shapes ──────────────────────────────────────────────────────────────
 *
 *   { label, desc, action, arg?, key }   a row that DOES something, through
 *                                        `synctl dispatch <action> [arg]`
 *   { label, desc, action: "…",          … and whose right-hand chip is a live
 *     live: "ai_backend" }               value rather than a key
 *   { kind: "note", text }               a line of prose, not selectable
 *
 * Actions are dispatched rather than reimplemented, exactly as the start menu's
 * static rows are: a row must take the same path its keyboard shortcut does, or
 * there are two definitions of "open the display settings" waiting to drift.
 */

/*
 * ── WHY THIS IS A FUNCTION AND NOT A TABLE ──────────────────────────────────
 *
 * ⛔ A `.pragma library` JS FILE CANNOT SEE A QML SINGLETON. It is loaded once,
 * outside any QML context, with no imports of its own to reach I18n through —
 * which is precisely why the header above insists it stay a .js. So the
 * translator is PASSED IN: GuideState calls `Pages.pages(I18n.tr)` and every
 * drawn string below goes through it.
 *
 * ⚠ AND THE STRINGS STAY HERE, which is the point. tools/qml-xgettext.py reads
 * this file (it is listed in po-bar/POTFILES) exactly as it reads a .qml, so a
 * page added here reaches a translator without anything else being touched.
 * Written the other way — English here, tr() at the draw site in Guide.qml —
 * the argument would be a variable and the extractor would take nothing.
 *
 * ⛔ WHAT IS NOT TRANSLATED, AND WHY EACH ONE WOULD BREAK:
 *   `id`      the page's own name; GuideState matches on it.
 *   `action`  what `synctl dispatch <action>` is given. A translated one
 *             dispatches an action the compositor does not have.
 *   `arg`     the argument beside it, same reason.
 *   `kind`    the row-shape sentinel — Guide.qml draws a note when it reads
 *             "note" and a row otherwise.
 *   `live`    the name of the value the right-hand chip shows.
 *   `key`     a picture of keycaps, and only a FALLBACK at that: the live
 *             chord comes from `synctl binds`. Every keyboard this ships on
 *             has Super printed on it.
 */
function pages(I18n) { return [
    {
        id:    "welcome",
        nav: I18n.tr("Welcome"),
        title: I18n.tr("Welcome to SynapseOS"),
        blurb: I18n.tr("A Wayland desktop built as one piece — the compositor, the bar, "
             + "the files, the settings and the AI all ship together and all "
             + "answer to the same theme."),
        rows: [
            { label: I18n.tr("Terminal"), key: "Super+Enter", action: "term",
              desc: I18n.tr("syntty, the shipped terminal.") },
            { label: I18n.tr("AI Command Bar"), key: "Super+Space", action: "cmdbar",
              desc: I18n.tr("Type a command, or ask for one in plain English.") },
            { label: I18n.tr("Control Panel"), key: "Super+C", action: "control",
              desc: I18n.tr("Every desktop setting, in one place.") },
            { kind: "note",
              text: I18n.tr("Arrow keys move · Enter opens · Esc closes the guide.") }
        ]
    },
    {
        id:    "keys",
        nav: I18n.tr("The keys"),
        title: I18n.tr("The keys worth knowing"),
        blurb: I18n.tr("synui is driven from the keyboard first. These are the handful "
             + "that get you everywhere else."),
        rows: [
            { label: I18n.tr("Keyboard Shortcuts"), key: "Super+/", action: "keys",
              desc: I18n.tr("The full palette — searchable, and the place to rebind.") },
            { label: I18n.tr("Start menu"), key: "Super", action: "start_menu",
              desc: I18n.tr("Tap Super on its own. Applications, settings, power.") },
            { label: I18n.tr("Applications"), key: "", action: "apps",
              desc: I18n.tr("The full grid of everything installed — also the dock's "
                  + "grid-of-dots.") },
            { label: I18n.tr("Task Manager"), key: "Ctrl+Alt+Del", action: "taskmgr",
              desc: I18n.tr("What is running, and what it is costing.") },
            { kind: "note",
              text: I18n.tr("Super+1…9 switch desktop · Super+Tab cycle layout · "
                  + "Super+Q close window") },
            { kind: "note",
              text: I18n.tr("Super+E filters · Super+O move window to the next monitor · "
                  + "Super+Shift+Q log out") }
        ]
    },
    {
        /*
         * ⚠ THE SEVEN LAYOUTS ARE NOT SEVEN DOORS. Only three of them can be
         * reached by dispatching an action — `retile`, `cascade` and
         * `layout_cycle`; there is no `layout_set <name>`, so a row per layout
         * would be a row Enter does nothing with, which is the dead spot
         * GuideRow's header refuses to draw. They are prose, and the control
         * panel's Layout row is the door that reaches all seven.
         *
         * The doors come FIRST and the prose after, so the page opens with the
         * selection on its first row rather than scrolled past the
         * explanations to find one — GuideState.firstSelectable() skips notes,
         * and the ListView follows the selection.
         *
         * The order the notes are in is the CYCLE order (layout.c's
         * syn_layout_t ordinals), because Super+Tab walks it and a list in any
         * other order would be a list you cannot follow with the key.
         *
         * ⚠ AND EVERY LINE HERE IS ONE LINE ON PURPOSE. The card is 620px tall
         * whatever the monitor is, the arrow keys step from row to row and SKIP
         * prose, and nothing in the guide scrolls the list from the keyboard —
         * so a note past the bottom of the card is reachable by wheel alone.
         * The first draft of this page ran three notes over and lost spiral and
         * cascade off the end. Anything added here has to fit, or it has to
         * replace something.
         */
        id:    "layouts",
        nav: I18n.tr("Layouts"),
        title: I18n.tr("How the windows are arranged"),
        blurb: I18n.tr("Each desktop keeps its own layout and remembers it across a "
             + "restart. Super+Tab walks the one you are on through the seven below, "
             + "in order."),
        rows: [
            { label: I18n.tr("Cycle layout"), key: "Super+Tab", action: "layout_cycle",
              desc: I18n.tr("The next layout for this desktop only.") },
            { label: I18n.tr("Tile this desktop"), key: "Super+Shift+T", action: "retile",
              desc: I18n.tr("Straight to tiling, from any layout, taking stray windows "
                  + "back.") },
            { label: I18n.tr("Cascade this desktop"), key: "Super+Shift+Y", action: "cascade",
              desc: I18n.tr("Straight to cascade, the same way.") },
            { label: I18n.tr("Pick a layout"), key: "", action: "control", arg: "Desktop",
              desc: I18n.tr("Control Panel ▸ Desktop ▸ Layout names all seven.") },
            { kind: "note",
              text: I18n.tr("Tiling — the first window takes the left 60%, the rest "
                  + "share the right.") },
            { kind: "note",
              text: I18n.tr("Floating — nothing is placed for you; windows stay where "
                  + "you put them.") },
            { kind: "note",
              text: I18n.tr("Monocle — one window per monitor, filling the screen; "
                  + "Alt+Tab changes it.") },
            { kind: "note",
              text: I18n.tr("AI — asks the local model where each window goes. Tiling "
                  + "when the AI is off.") },
            { kind: "note",
              text: I18n.tr("niri — an endless strip of columns; a new window lengthens "
                  + "the strip.") },
            { kind: "note",
              text: I18n.tr("Spiral — each window halves what is left, winding inward.") },
            { kind: "note",
              text: I18n.tr("Cascade — overlapping cards, offset so every titlebar "
                  + "stays reachable.") },
            /* Last, and last for a reason: it is the only line here that is not
             * one of the seven, so it is the one that may fall off the bottom
             * on a desktop whose UI font is larger than the default. */
            { kind: "note",
              text: I18n.tr("Dragging, snapping or maximizing a window takes it out "
                  + "of the layout.") }
        ]
    },
    {
        id:    "look",
        nav: I18n.tr("Make it yours"),
        title: I18n.tr("Make it yours"),
        blurb: I18n.tr("One theme moves the whole desktop: the compositor's panels, the "
             + "bar, the widgets, GTK and Qt applications, and the cursor."),
        rows: [
            { label: I18n.tr("Appearance"), key: "Super+T", action: "theme",
              desc: I18n.tr("Themes, accent colour, glass and corners.") },
            { label: I18n.tr("Wallpaper"), key: "Super+W", action: "wallpaper",
              desc: I18n.tr("Stills, slideshows and animated wallpapers.") },
            { label: I18n.tr("Display Settings"), key: "Super+D", action: "displays",
              desc: I18n.tr("Resolution, refresh rate, scale and monitor layout.") },
            { label: I18n.tr("Desktop Widgets"), key: "Super+Shift+A", action: "widgets",
              desc: I18n.tr("Clock, system monitor, music, notes — on the desktop.") },
            { label: I18n.tr("Screen Filters"), key: "Super+E", action: "filters",
              desc: I18n.tr("Night light, colour blindness filters, CRT.") },
            { label: I18n.tr("Screensaver"), key: "Super+Z", action: "saver",
              desc: I18n.tr("What the desktop does when you walk away.") }
        ]
    },
    {
        id:    "ai",
        nav: I18n.tr("The AI"),
        title: I18n.tr("The AI is local"),
        blurb: I18n.tr("synapd runs a language model on this machine. Nothing here calls "
             + "out to a service, and turning it off really does stop it."),
        rows: [
            { label: I18n.tr("AI Command Bar"), key: "Super+Space", action: "cmdbar",
              desc: I18n.tr("Ask for a command; it answers with one you can run.") },
            { label: I18n.tr("Neural Overlay"), key: "Super+A", action: "overlay",
              desc: I18n.tr("The full-screen conversation view.") },
            { label: I18n.tr("AI Model"), key: "", action: "aimodel",
              desc: I18n.tr("Which model is loaded, and how to fetch another.") },
            { label: I18n.tr("AI Backend"), live: "ai_backend", action: "ai_backend",
              desc: I18n.tr("GPU, CPU or off. Off masks the sockets, so it holds.") }
        ]
    },
    {
        id:    "system",
        nav: I18n.tr("Everything else"),
        title: I18n.tr("Everything else"),
        blurb: I18n.tr("The rest of the desktop, and where it lives."),
        rows: [
            { label: I18n.tr("Network / Wi-Fi"), key: "Super+I", action: "network",
              desc: I18n.tr("Wireless, wired and the firewall.") },
            { label: I18n.tr("Power Saving"), key: "Super+P", action: "power",
              desc: I18n.tr("Idle, sleep, and what the lid does.") },
            { label: I18n.tr("Calculator"), key: "Super+X", action: "calc",
              desc: I18n.tr("Also answers from the command bar.") },
            { label: I18n.tr("News"), key: "Super+R", action: "news",
              desc: I18n.tr("Headlines, fetched on the desktop's own schedule.") },
            { label: I18n.tr("Game Mode"), key: "Super+G", action: "game",
              desc: I18n.tr("Bar out of the way, effects off, the machine to the game.") },
            { label: I18n.tr("Cat Mode"), key: "Super+Shift+C", action: "cat",
              desc: I18n.tr("There is a cat. That is the whole feature.") },
            { label: I18n.tr("Lock Screen"), key: "Super+L", action: "lock",
              desc: I18n.tr("Locks now; the screensaver locks on its own timer.") }
        ]
    },
    {
        id:    "done",
        nav: I18n.tr("You're set"),
        title: I18n.tr("You're set"),
        blurb: I18n.tr("This guide is on Super+Escape whenever you want it back — and "
             + "the shortcuts palette on Super+/ is the longer version of "
             + "page two."),
        rows: [
            { label: I18n.tr("Keyboard Shortcuts"), key: "Super+/", action: "keys",
              desc: I18n.tr("Everything this guide skipped.") },
            { label: I18n.tr("Control Panel"), key: "Super+C", action: "control",
              desc: I18n.tr("Every setting, grouped.") },
            { label: I18n.tr("About this system"), key: "", action: "about",
              desc: I18n.tr("Version, hardware and what is installed.") },
            { kind: "note",
              text: I18n.tr("Documentation and downloads: soslinux.org") }
        ]
    }
] }
