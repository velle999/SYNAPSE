pragma Singleton

import QtQuick
import Quickshell

/*
 * PluginHost — the object an Omarchy plugin knows as `shell`.
 *
 * ⚠ THE BAR WAS NEVER THE WHOLE HOST, AND THREE INSTALLED WIDGETS PROVED IT.
 * Plugins.qml hosts the `bar-widget` kind and only that kind, which is why a
 * plugin declaring `panel` or `service` appeared on the bar, drew its glyph,
 * and did nothing whatever when clicked:
 *
 *   flappy-pipes  `bar.shell.toggle(moduleName, "")` — bar.shell was undefined,
 *                 so the guard `typeof … === "function"` failed and the click
 *                 was swallowed in silence. Its game lives in a `panel` entry
 *                 point nothing mounted.
 *   chess         `bar.shell.serviceFor(id)` for its whole state, and
 *                 `bar.shell.toggle()` to open. Both entry points — `service`
 *                 AND `panel` — were unmounted, so the widget read "♞ Chess"
 *                 forever and the click did nothing.
 *   tetris        `bar.run(cmd)` to start the game. Its panel opened (it owns
 *                 that itself, being a bar-widget), and Start threw
 *                 "Property 'run' … is not a function" into a log on tty1.
 *
 * ⛔ EVERY ONE OF THOSE IS A GUARDED CALL THAT FAILS SILENTLY. A plugin author
 * writing `if (shell && typeof shell.toggle === "function")` is being careful,
 * and the reward on a host missing the member is a dead button with nothing in
 * any log. Only tetris said anything, and only because `bar` existed and `run`
 * did not — the one unguarded call of the three. So a member that goes missing
 * here costs a feature and reports nothing: this file is the roster, and
 * tests/plugin_host.sh is what counts it.
 *
 * ── What a panel and a service are ──────────────────────────────────────────
 *
 * A bar widget is instantiated PER MONITOR — that is what Bar.qml's Repeater
 * does, and it is right for a thing that appears on every bar. A panel and a
 * service are mounted ONCE PER SESSION, which is not a detail: flappy's game
 * loop running per screen would be three simulations racing each other over one
 * best score, and chess would have three boards disagreeing about whose turn it
 * is. Their own headers say so. Hence PluginMount, instantiated from shell.qml
 * where there is exactly one of everything.
 *
 * ── The members, and who asks for each ──────────────────────────────────────
 *
 *   serviceFor(id)            chess's bar widget, for `snapshot`
 *   toggle/show/hide(id, …)   every panel-kind widget's click
 *   shellConfig               flappy, to read its own stored entry back
 *   updateEntryInline(id, e)  flappy, to write a new best score
 *   run(cmd)                  tetris, to launch its TUI
 */
Singleton {
    id: root

    /*
     * ⚠ REBUILT, NEVER MUTATED. Assigning into a `var` object and reassigning
     * the SAME object does not notify a binding — the engine sees no change —
     * so a widget bound to serviceFor() would keep the null it read before the
     * service loaded. It is the same trap Plugins.registerBar() writes down for
     * its array, one type over.
     */
    property var services: ({})
    property var panels: ({})

    /*
     * Which ids a panel is COMING for, declared by the mount before its Loader
     * has resolved.
     *
     * ⛔ WITHOUT IT, "IS IT OPEN" IS TRUE FOR A PLUGIN THAT HAS NO PANEL.
     * `wanted` is the fallback answer while a panel is still loading, and on its
     * own it cannot tell a panel that has not arrived yet from one that never
     * will: `plugin toggle terminal.tetris` — a bar-widget-only plugin that owns
     * its popup itself — recorded an intent nothing could act on and then
     * reported "open" for a panel that does not exist. Caught in the headless
     * rig, which is the only place either answer is visible.
     */
    property var expected: ({})
    function expectPanel(id, yes) { root.expected = root.put(root.expected, id, yes ? true : null) }

    /* Which panels the shell believes are open, and what they were opened with.
     * The panel's own `opened` is the display; this is the intent, and it is
     * what survives a panel that has not finished loading yet. */
    property var wanted: ({})

    function put(map, id, obj) {
        const next = {}
        for (const k in map) next[k] = map[k]
        if (obj === null) delete next[id]
        else next[id] = obj
        return next
    }

    function registerService(id, obj) { root.services = root.put(root.services, id, obj) }
    function registerPanel(id, obj) {
        root.panels = root.put(root.panels, id, obj)
        /* A click that arrived while the Loader was still resolving. The
         * Loaders are asynchronous so that a heavy panel cannot stall the bar's
         * first frame, which means "open it" genuinely can land before there is
         * anything to open. */
        if (obj && root.wanted[id] !== undefined) root.applyOpen(id)
    }

    function serviceFor(id) { return root.services[id] || null }
    function panelFor(id)   { return root.panels[id] || null }

    /* Whether the shell believes this plugin's panel is up. Asked of the panel
     * when there is one — chess derives `opened` from its window's visibility,
     * and the window is the truth — and of the intent otherwise. */
    function isOpen(id) {
        const p = root.panels[id]
        if (p && p.opened !== undefined) return p.opened === true
        /* Only a panel that is on its way can be "opening". Anything else has
         * nothing to be open. */
        return root.expected[id] === true && root.wanted[id] !== undefined
    }

    function applyOpen(id) {
        const p = root.panels[id]
        if (!p || typeof p.open !== "function") return
        p.open(root.wanted[id])
    }

    /*
     * ⚠ `payload` IS A JSON STRING AND NOT AN OBJECT. Both installed panels
     * name the argument `payloadJson`, and flappy's caller passes "" — so an
     * object handed over here would be a different contract wearing the same
     * name. It is passed through untouched; parsing it is the panel's job.
     */
    function show(id, payload) {
        if (!id) return
        /* Nothing to open, and saying so beats recording an intent no mount
         * will ever collect. A bar-widget-only plugin owns its own popup. */
        if (!root.panels[id] && root.expected[id] !== true) return
        root.wanted = root.put(root.wanted, id, payload === undefined ? "" : payload)
        root.applyOpen(id)
    }

    function hide(id) {
        if (!id) return
        const p = root.panels[id]
        root.wanted = root.put(root.wanted, id, null)
        if (p && typeof p.close === "function") p.close()
    }

    function toggle(id, payload) {
        if (root.isOpen(id)) root.hide(id)
        else root.show(id, payload)
    }

    /* Omarchy spells the pair both ways depending on the widget, and a host that
     * answers to only one of them is a host half the corpus cannot close. */
    function open(id, payload) { root.show(id, payload) }
    function close(id)         { root.hide(id) }

    // ── Settings, which are PluginConfig's file and not this file's ─────────
    readonly property var shellConfig: PluginConfig.shellConfig
    function updateEntryInline(id, entry) { PluginConfig.updateEntryInline(id, entry) }
    function settingsFor(id)              { return PluginConfig.settingsFor(id) }

    /*
     * Run a command line, the way Omarchy's `bar.run` does.
     *
     * ⚠ A SHELL STRING, WHICH IS THEIR SIGNATURE AND NOT A SHORTCUT. tetris
     * calls `bar.run("omarchy-launch-or-focus-tui --app-id=… " + path)` — one
     * string, already quoted by the caller — so splitting it into argv here
     * would break every widget written against the real thing. That it reaches
     * /bin/sh is not a new hazard: the string comes from QML this process has
     * already loaded and is running, so a plugin that wanted to run something
     * could do it without asking us.
     *
     * ⛔ execDetached, NEVER A SHARED Process. A Process runs ONE child at a
     * time and assigning `command` while it is busy QUEUES rather than starts —
     * which is how the start menu could only ever have one launched application
     * alive at once (see StartMenu.activate). Every one of these launches
     * something that lives as long as its window.
     */
    function run(cmd) {
        if (!cmd) return
        if (Array.isArray(cmd)) { Quickshell.execDetached(cmd); return }
        Quickshell.execDetached(["sh", "-c", String(cmd)])
    }
}
