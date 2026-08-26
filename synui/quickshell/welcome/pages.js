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

var pages = [
    {
        id:    "welcome",
        nav:   "Welcome",
        title: "Welcome to SynapseOS",
        blurb: "A Wayland desktop built as one piece — the compositor, the bar, "
             + "the files, the settings and the AI all ship together and all "
             + "answer to the same theme.",
        rows: [
            { label: "Terminal", key: "Super+Enter", action: "term",
              desc: "syntty, the shipped terminal." },
            { label: "AI Command Bar", key: "Super+Space", action: "cmdbar",
              desc: "Type a command, or ask for one in plain English." },
            { label: "Control Panel", key: "Super+C", action: "control",
              desc: "Every desktop setting, in one place." },
            { kind: "note",
              text: "Arrow keys move · Enter opens · Esc closes the guide." }
        ]
    },
    {
        id:    "keys",
        nav:   "The keys",
        title: "The keys worth knowing",
        blurb: "synui is driven from the keyboard first. These are the handful "
             + "that get you everywhere else.",
        rows: [
            { label: "Keyboard Shortcuts", key: "Super+/", action: "keys",
              desc: "The full palette — searchable, and the place to rebind." },
            { label: "Start menu", key: "Super", action: "start_menu",
              desc: "Tap Super on its own. Applications, settings, power." },
            { label: "Applications", key: "", action: "apps",
              desc: "The full grid of everything installed — also the dock's "
                  + "grid-of-dots." },
            { label: "Task Manager", key: "Ctrl+Alt+Del", action: "taskmgr",
              desc: "What is running, and what it is costing." },
            { kind: "note",
              text: "Super+1…9 switch desktop · Super+Tab cycle layout · "
                  + "Super+Q close window" },
            { kind: "note",
              text: "Super+E filters · Super+O move window to the next monitor · "
                  + "Super+Shift+Q log out" }
        ]
    },
    {
        id:    "look",
        nav:   "Make it yours",
        title: "Make it yours",
        blurb: "One theme moves the whole desktop: the compositor's panels, the "
             + "bar, the widgets, GTK and Qt applications, and the cursor.",
        rows: [
            { label: "Appearance", key: "Super+T", action: "theme",
              desc: "Themes, accent colour, glass and corners." },
            { label: "Wallpaper", key: "Super+W", action: "wallpaper",
              desc: "Stills, slideshows and animated wallpapers." },
            { label: "Display Settings", key: "Super+D", action: "displays",
              desc: "Resolution, refresh rate, scale and monitor layout." },
            { label: "Desktop Widgets", key: "Super+Shift+A", action: "widgets",
              desc: "Clock, system monitor, music, notes — on the desktop." },
            { label: "Screen Filters", key: "Super+E", action: "filters",
              desc: "Night light, colour blindness filters, CRT." },
            { label: "Screensaver", key: "Super+Z", action: "saver",
              desc: "What the desktop does when you walk away." }
        ]
    },
    {
        id:    "ai",
        nav:   "The AI",
        title: "The AI is local",
        blurb: "synapd runs a language model on this machine. Nothing here calls "
             + "out to a service, and turning it off really does stop it.",
        rows: [
            { label: "AI Command Bar", key: "Super+Space", action: "cmdbar",
              desc: "Ask for a command; it answers with one you can run." },
            { label: "Neural Overlay", key: "Super+A", action: "overlay",
              desc: "The full-screen conversation view." },
            { label: "AI Model", key: "", action: "aimodel",
              desc: "Which model is loaded, and how to fetch another." },
            { label: "AI Backend", live: "ai_backend", action: "ai_backend",
              desc: "GPU, CPU or off. Off masks the sockets, so it holds." }
        ]
    },
    {
        id:    "system",
        nav:   "Everything else",
        title: "Everything else",
        blurb: "The rest of the desktop, and where it lives.",
        rows: [
            { label: "Network / Wi-Fi", key: "Super+I", action: "network",
              desc: "Wireless, wired and the firewall." },
            { label: "Power Saving", key: "Super+P", action: "power",
              desc: "Idle, sleep, and what the lid does." },
            { label: "Calculator", key: "Super+X", action: "calc",
              desc: "Also answers from the command bar." },
            { label: "News", key: "Super+R", action: "news",
              desc: "Headlines, fetched on the desktop's own schedule." },
            { label: "Game Mode", key: "Super+G", action: "game",
              desc: "Bar out of the way, effects off, the machine to the game." },
            { label: "Cat Mode", key: "Super+Shift+C", action: "cat",
              desc: "There is a cat. That is the whole feature." },
            { label: "Lock Screen", key: "Super+L", action: "lock",
              desc: "Locks now; the screensaver locks on its own timer." }
        ]
    },
    {
        id:    "done",
        nav:   "You're set",
        title: "You're set",
        blurb: "This guide is on Super+Escape whenever you want it back — and "
             + "the shortcuts palette on Super+/ is the longer version of "
             + "page two.",
        rows: [
            { label: "Keyboard Shortcuts", key: "Super+/", action: "keys",
              desc: "Everything this guide skipped." },
            { label: "Control Panel", key: "Super+C", action: "control",
              desc: "Every setting, grouped." },
            { label: "About this system", key: "", action: "about",
              desc: "Version, hardware and what is installed." },
            { kind: "note",
              text: "Documentation and downloads: soslinux.org" }
        ]
    }
]
