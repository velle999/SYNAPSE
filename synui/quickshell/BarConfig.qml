pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * BarConfig — what the bar shows, PER MONITOR.
 *
 * Per monitor because a 1080-wide portrait panel cannot hold what a 2560-wide
 * landscape one can: with the desktop switcher on the left, the clock centred
 * and the status group on the right, the clock ends up drawn straight through
 * the tray and the media title. One global setting would mean giving up the
 * workspace pills on the wide monitors to fix the narrow one.
 *
 * Written by the bar itself (the right-click menu) rather than by a helper —
 * unlike widgets.state, where synui's keybind and the control panel also need a
 * way in. Nothing outside the bar sets these, so a CLI would be a second writer
 * with nobody to use it.
 *
 * Unknown monitors simply are not in the file and fall back to the defaults
 * below, so plugging in a new screen gives a working bar with no setup.
 */
QtObject {
    id: root

    // Everything on, nothing hidden: the bar a fresh install has always had.
    readonly property var defaults: ({
        "autohide":   false,
        "workspaces": true,
        "media":      true,
        "clock":      true,
        "tray":       true,
        "updates":    true,     // syn-update's pending count
        "sysinfo":    true,     // cpu + memory
        "volume":     true,
        "netbt":      true      // network + bluetooth
    })

    // Human labels for the menu, in menu order.
    readonly property var rows: [
        { key: "autohide",   label: "Auto-hide bar" },
        { key: "workspaces", label: "Desktop switcher" },
        { key: "media",      label: "Media preview" },
        { key: "clock",      label: "Clock" },
        { key: "tray",       label: "System tray" },
        { key: "updates",    label: "Update notifier" },
        { key: "sysinfo",    label: "CPU + memory" },
        { key: "volume",     label: "Volume" },
        { key: "netbt",      label: "Network + Bluetooth" }
    ]

    property var perOutput: ({})

    /* ── Which edge the bar sits on ──────────────────────────────────────────
     *
     * GLOBAL, unlike everything above it, and read out of synui's config rather
     * than bar.json. Both of those are deliberate.
     *
     * Global because it is the bar's answer to the dock's edge, and that is one
     * setting for the whole desktop: furniture on the bottom of one monitor and
     * the top of the next is a layout nobody asks for by accident, and the
     * control panel row that sets it has no monitor to scope itself to.
     *
     * Out of synui's config because the control panel is the writer. bar.json is
     * the bar's OWN file — the right-click menu writes it and nothing else does,
     * which is exactly why it can be written back without a second writer to
     * race. A control-panel row writing bar.json would introduce one.
     *
     * So the flow is the same as every other setting on that panel: the row
     * writes `bar_edge` into settings.state through settings.c, and this reads
     * it back. synuirc first, then settings.state on top, because that is
     * synui's own precedence (synui_config_load) — a value someone changed in a
     * panel a minute ago beats the line they wrote in synuirc last year.
     */
    readonly property bool atBottom: root.edge === "bottom"
    property string edge: "top"

    /* Which face the analog clock widget draws — `widget_clock_face`, read the
     * same way and for the same reason `widget_glass` beside it is: it is a
     * Desktop row on the control panel, and settings.state is what that panel
     * writes. See AnalogClock.qml for why the face is a setting rather than
     * something you click the widget to change. */
    property string clockFace: "minimal"

    /* ── The desktop's corner radius ─────────────────────────────────────────
     *
     * Global for the same reason the edge is, and read the same way: the bar's
     * panels are furniture on the same desktop as the compositor's, and
     * `corner_radius` is one setting for the lot. Without this the bar was the
     * exception — turning corners on rounded every window, every menu and every
     * picker, and left the strip across the top of the screen square.
     *
     * The RAW setting. Theme.panelRadius is what the panels actually use; it
     * applies the retro chromes' override on top (see there).
     *
     * THREE sources, in synui's own order. uifx.state last-and-winning is not a
     * detail: the control panel's window-effects page is where the corner slider
     * lives and uifx.state is what it writes, while settings.state carries the
     * same key from the settings app. synui_config_load() reads synuirc, then
     * settings.state, then uifx.state, so a value read in the other order would
     * disagree with the compositor drawing two inches below — which is the exact
     * class of bug this whole change is about.
     */
    property int cornerRadius: 12      // config.c's default, for a box with none

    /*
     * What the bar DOES with that radius: "full-width", "rounded-ends" or
     * "floating-pill". syn_bar_shape_t's spellings, and like bar_edge it is
     * parsed by the compositor purely so the key has one spelling — nothing over
     * there acts on it.
     *
     * Held as the raw string rather than parsed to an int here. The bar is the
     * only reader, Theme turns it into geometry once, and a number would mean a
     * second copy of the order in syn_bar_shape_names[] — the same drift the
     * enum's own comment is about. An unknown value falls through every test in
     * Theme and behaves as full-width, which is what a bar reading a settings
     * file from a NEWER synui should do.
     */
    property string barShape: "full-width"

    /* ── The dock's glass, borrowed by the desktop widgets ───────────────────
     *
     * Read exactly like bar_shape: settings.state over synuirc, because the
     * control panel row is the writer and settings.state is what it writes.
     * Neither of these is a bar setting at all — they belong to the widgets
     * (WidgetFrame.qml), which have no config singleton of their own and read
     * everything through Theme, which reads it through here.
     *
     * Raw strings and raw reals, resolved in Theme: `widget_glass` is a
     * three-position auto/off/on whose "auto" needs theme.state to answer, and
     * that file is Theme's to watch.
     */
    property string widgetGlass: "auto"

    /* ── Is there a bar at all ───────────────────────────────────────────────
     *
     * Global, read the same pair-and-order as bar_shape, and the one setting on
     * this object that decides whether the bar's window is MAPPED (see Bar.qml).
     *
     * It lives here rather than in a stop/start command pair — which is what it
     * used to be — because this process is not only the bar. The desktop
     * widgets, the OSD, the start menu, the mixer and the post-it notes are all
     * windows of the SAME quickshell instance (shell.qml), so the old default
     * `bar_stop_cmd = pkill -x quickshell` turned the bar off by killing every
     * one of them: switching off the strip across the top took the visualiser,
     * the clock, the notes and Tux with it, and nothing said so.
     *
     * A bar that can unmap its own window has no such reach. The compositor's
     * row now just writes the key, this reads it back through a watch that was
     * already here, and the strip goes away on its own while the rest of the
     * shell carries on. The command pair survives for a FOREIGN bar (waybar),
     * which cannot be asked this way — it is empty by default now, so nothing
     * is killed unless someone names something to kill.
     */
    property bool barEnabled: true

    /* ── How much of the wallpaper the DOCK (and, through it, the widgets)
     * let through ────────────────────────────────────────────────────────────
     *
     * NEGATIVE IS THE DEFAULT, the same sentinel and the same reasoning as
     * barOpacity below: "nobody has chosen" is not a number this side can
     * draw with, and 0.00 is itself a real answer — a dock with no background
     * at all. Resolved in Theme against theme.json's own dockAlpha, which is
     * where a theme's opinion (frosted, on the two Prisms) lives; this alone
     * used to default to a flat 0.72 with no such fallback, which is why the
     * desktop widgets kept a slab of a card on a glass desktop that had asked
     * `dock_opacity = auto` — the dock body beside them, drawn in-process,
     * had a theme-aware auto and this did not. */
    property real dockOpacity: -1

    /* ── How much of the wallpaper the BAR lets through ──────────────────────
     *
     * The user's override on the theme's opinion, read the same way as the
     * three above and resolved in Theme (which is where theme.json's own
     * barAlpha lives, and this only means anything against that).
     *
     * NEGATIVE IS THE DEFAULT and means "the theme decides" — it is not a
     * number the bar can draw with. It has to be out of band because 0.00 is
     * itself a real answer here: a bar with no background at all, taking its
     * ink off the wallpaper. Same sentinel and same reasoning as config.c's
     * `bar_opacity`, restated because this side must not read an absent key as
     * a request for a fully transparent bar.
     */
    property real barOpacity: -1

    /*
     * ── May a surface overrule its own alpha to stay readable? ───────────────
     *
     * The shell's half of `glass_legibility`. popupAlphaOn() in Theme is the QML
     * twin of render.c's panel_alpha_floor(), and both have to answer to the same
     * switch or half the desktop would obey it — the compositor's thirty panels —
     * while the start menu, the mixer and the widgets quietly kept correcting
     * themselves. Exported to theme.state rather than read out of settings.state
     * for the reason bar_opacity below is: it is a resolved answer, and the file
     * the shell already watches for glass_surfaces is where resolved answers go.
     */
    property bool legibility: true

    // Where a popup hanging off the bar starts, given its height. One place
    // rather than a `Theme.barHeight + 2` at each anchor site: a bottom bar's
    // popups have to go UP, and every one of those sites would otherwise be a
    // separate chance to forget.
    //
    // barSpan and not barHeight on the top-bar side: the window is measured from
    // the screen edge, and a floating pill starts a gap's worth in from it, so
    // its underside is at barSpan. Left as barHeight, every popup on a pilled top
    // bar overlapped it by exactly the gap. The bottom-bar side needs no such
    // thing — there the gap is below the strip, so the window's top edge and the
    // strip's top edge are still the same line.
    function popupY(h) {
        return root.atBottom ? -h - 2 : Theme.barSpan + 2
    }

    // `key = value`, synuirc's language, which is also settings.state's. Only
    // ever asked for keys the bar cares about, so this does not need to be a
    // parser — it needs to find one line and ignore everything it does not
    // understand, including the repeated keys (bind, autostart) that a real
    // parser would have to model.
    function readKey(text, key) {
        if (!text) return ""
        for (const raw of text.split("\n")) {
            const line = raw.trim()
            if (!line || line.startsWith("#")) continue
            const eq = line.indexOf("=")
            if (eq < 0 || line.slice(0, eq).trim() !== key) continue
            let val = line.slice(eq + 1).trim()
            // Inline comment, whitespace-preceded — config.c's rule, and the
            // reason it is not simply "cut at the first #" is that a value can
            // legitimately BEGIN with one (`border_color_focus = #ff296d`).
            const hash = val.search(/\s#/)
            if (hash >= 0) val = val.slice(0, hash).trim()
            return val
        }
        return ""
    }

    function applyGlobals() {
        /*
         * ⚠ HOISTED, AND GUARDED, BECAUSE THIS RUNS DURING CONSTRUCTION. The
         * three FileViews above are blockLoading, so their onLoaded fires while
         * this object is still being built — and themeStateFile is declared
         * LAST, so it is the one that may not exist yet. `themeStateFile.text()`
         * inline would throw a TypeError there, and a QML exception inside a
         * signal handler is silent: the first pass would simply not run, and
         * every value it resolves would sit at its declared default until some
         * other file happened to change. Read once, defensively, and the rest of
         * this function is plain string work.
         */
        const themeState = root.themeStateFile ? root.themeStateFile.text() : ""
        // settings.state wins where it has the key, synuirc where it does not.
        const v = root.readKey(settingsFile.text(), "bar_edge")
                  || root.readKey(synuircFile.text(), "bar_edge")
        root.edge = (v === "bottom") ? "bottom" : "top"

        // Highest-precedence source first; "" is the only falsy result readKey
        // can return, so a legitimate "0" still stops the chain.
        const r = root.readKey(uifxFile.text(), "corner_radius")
                  || root.readKey(settingsFile.text(), "corner_radius")
                  || root.readKey(synuircFile.text(), "corner_radius")
        // config.c clamps to 0..48 and ignores a value it cannot parse; the bar
        // has to do the same or a typo'd synuirc line gives the compositor its
        // default and the bar a NaN, which silently paints every panel square.
        const n = parseInt(r, 10)
        if (!isNaN(n)) root.cornerRadius = Math.max(0, Math.min(48, n))

        // Shape. settings.state over synuirc, the same pair and the same order
        // bar_edge uses — it is the control panel's row and the control panel
        // writes settings.state. Not uifx.state: the corner SLIDER lives on the
        // effects page, this is a Desktop row, and reading a file its writer
        // never touches would only invent a precedence question.
        const s = root.readKey(settingsFile.text(), "bar_shape")
                  || root.readKey(synuircFile.text(), "bar_shape")
        root.barShape = s || "full-width"

        // The widgets' two, same pair of files and same order.
        const w = root.readKey(settingsFile.text(), "widget_glass")
                  || root.readKey(synuircFile.text(), "widget_glass")
        root.widgetGlass = (w === "on" || w === "off") ? w : "auto"

        // Which dial the analog clock widget draws. Same pair, same order —
        // it is a control panel row and the control panel writes settings.state.
        // An unrecognised word falls back rather than blanking the widget: a
        // typo in a config file must not leave a card with nothing on it and no
        // way to tell why.
        const cf = root.readKey(settingsFile.text(), "widget_clock_face")
                   || root.readKey(synuircFile.text(), "widget_clock_face")
        root.clockFace = ["minimal", "classic", "roman", "neon"].indexOf(cf) >= 0
                         ? cf : "minimal"

        // Is there a bar at all. Same pair and order again. Only the exact
        // string "off" turns it off: an absent key, a typo and a synuirc from a
        // synui too old to know the key all have to mean the bar everyone
        // already has, or an unreadable file would leave the desktop with no
        // bar and no obvious way back.
        const be = root.readKey(settingsFile.text(), "bar_enabled")
                   || root.readKey(synuircFile.text(), "bar_enabled")
        root.barEnabled = be !== "off"

        /*
         * ⚠ theme.state COMES FIRST ON THESE TWO, AND IT IS THE WHOLE REASON THE
         * GLASS SLIDER REACHES THE BAR AT ALL.
         *
         * `dock_opacity` and `bar_opacity` are the two surfaces the slider
         * drives that this process draws. The compositor resolves the level onto
         * its own config and exports the answer to theme.state — but the slider
         * does NOT write settings.state, because a synced value is not a value
         * anybody chose, so reading only the two files below meant the level
         * moved and the bar and the widgets stayed exactly where they were.
         *
         * Precedence and pinning are the same mechanism here: theme.c writes
         * these keys ONLY for the rows the sync still owns (syn_glass_drives), so
         * a row the user has taken hold of is simply absent and settings.state —
         * where the control panel wrote their number — shows through. This side
         * needs no idea that pinning exists.
         */
        const d = root.readKey(themeState, "dock_opacity")
                  || root.readKey(settingsFile.text(), "dock_opacity")
                  || root.readKey(synuircFile.text(), "dock_opacity")
        const dn = parseFloat(d)
        // ⚠ NOT re-floored at 0.20. config.c clamps this to 0.00-1.00 and that
        // is the range; the 0.20 that used to be here was a third copy of a
        // floor the compositor had already dropped, and it is what stopped the
        // widgets following the dock all the way down.
        //
        // `auto` and an absent key are the same instruction as bar_opacity's
        // below, and now resolve the same way — Theme falls through to
        // theme.json's dockAlpha rather than to a flat compiled number, so
        // this has to be able to say "nobody has chosen" too. Assigned on
        // every pass for the reason bar_opacity is: the control panel drops
        // the key from settings.state when the row returns to its default.
        root.dockOpacity = (d === "auto" || isNaN(dn)) ? -1
                                                       : Math.max(0.0, Math.min(1.0, dn))

        // The bar's own. Same pair, same order. Assigned on every pass rather
        // than only when it parses, because unlike the four above this one has
        // to be able to go BACK to "the theme decides" — the control panel drops
        // the key from settings.state when the row returns to its default, and a
        // guard that only ever wrote a parsed number would leave the bar on the
        // last figure the user scrolled past.
        const b = root.readKey(themeState, "bar_opacity")
                  || root.readKey(settingsFile.text(), "bar_opacity")
                  || root.readKey(synuircFile.text(), "bar_opacity")
        const bn = parseFloat(b)
        // `auto` and an absent key are the same instruction; so is a typo, for
        // the reason cornerRadius clamps rather than trusting the file.
        root.barOpacity = (b === "auto" || isNaN(bn)) ? -1
                                                      : Math.max(0, Math.min(1, bn))

        // An export, so theme.state only. Absent means a synui too old to write
        // it, and the honest answer for that is the behaviour those machines
        // already have — the correction on.
        root.legibility =
            root.readKey(themeState, "glass_legibility") !== "off"
    }

    property FileView synuircFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/synuirc"
        watchChanges: true
        // Same reason as bar.json below: the edge decides which side the
        // exclusive zone is reserved on, and that has to be right at the
        // surface's first configure.
        blockLoading: true
        onFileChanged: reload()
        onLoaded: root.applyGlobals()
        onLoadFailed: root.applyGlobals()
    }

    property FileView settingsFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/settings.state"
        watchChanges: true
        blockLoading: true
        onFileChanged: reload()
        onLoaded: root.applyGlobals()
        // Absent is the normal case — settings.state only exists once something
        // on the control panel has been changed.
        onLoadFailed: root.applyGlobals()
    }

    /* The corner slider's own file, and the highest-precedence source for it.
     *
     * blockLoading LIKE the two above, which it did not used to be. The old
     * reasoning was sound and its PREMISE EXPIRED: a radius was read only by
     * panels that did not exist yet, nothing on screen at startup was drawn with
     * it, so arriving a frame late cost nothing. bar_shape changed that. The
     * floating pill lifts the bar off the screen edge, so the exclusive zone now
     * depends on the radius — zero means the shape does not apply and the strip
     * reserves its own height, non-zero means it reserves the gap as well — and
     * the zone is the one value that cannot arrive late (see Bar.qml). Async, a
     * desktop with the corners off reserved the pill's taller zone for the whole
     * session, having decided the shape from this property's default.
     */
    property FileView uifxFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/uifx.state"
        watchChanges: true
        blockLoading: true
        printErrors: false      // absent until an effects row is changed
        onFileChanged: reload()
        onLoaded: root.applyGlobals()
        onLoadFailed: root.applyGlobals()
    }

    /* theme.state — the compositor's resolved answers, and the highest-precedence
     * source for the two opacity keys above. Watched, not blocking: unlike the
     * corner radius none of these decides an exclusive zone, so arriving a frame
     * late costs a repaint rather than a session-long wrong layout. */
    property FileView themeStateFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/theme.state"
        watchChanges: true
        printErrors: false      // absent until a theme has been picked
        onFileChanged: reload()
        onLoaded: root.applyGlobals()
        onLoadFailed: root.applyGlobals()
    }

    function get(output, key) {
        const o = root.perOutput[output]
        if (o && o[key] !== undefined) return o[key] === true
        return root.defaults[key] === true
    }

    function set(output, key, value) {
        // Rebuild rather than mutate: assigning a NEW object is what makes every
        // binding on perOutput re-evaluate. Mutating in place changes the data
        // and repaints nothing.
        const next = {}
        for (const k in root.perOutput) next[k] = Object.assign({}, root.perOutput[k])
        if (!next[output]) next[output] = {}
        next[output][key] = value === true
        root.perOutput = next

        // Persist. The reload this triggers parses back the same values, so the
        // watch is harmless rather than a write loop.
        configFile.setText(JSON.stringify(next, null, 2) + "\n")
    }

    function toggle(output, key) { root.set(output, key, !root.get(output, key)) }

    /* Every screen at once, in ONE write.
     *
     * The control panel's "Bar auto-hide" row is a master over a per-monitor
     * setting — the compositor has no business deciding which monitor a row on a
     * panel means — and it reaches this through the `bar` IPC handler in
     * shell.qml. Looping set() from over there would be one bar.json write per
     * screen, each triggering a watch reload the next one races.
     *
     * Quickshell.screens rather than the keys already in perOutput: a monitor
     * nobody has ever touched is not in the file, and leaving it out is exactly
     * how a master switch ends up not covering the one screen it was reached for.
     */
    function setAll(key, value) {
        const next = {}
        for (const k in root.perOutput) next[k] = Object.assign({}, root.perOutput[k])
        for (const screen of Quickshell.screens) {
            if (!next[screen.name]) next[screen.name] = {}
            next[screen.name][key] = value === true
        }
        root.perOutput = next
        configFile.setText(JSON.stringify(next, null, 2) + "\n")
    }

    function parse(text) {
        try {
            const j = JSON.parse(text)
            if (j && typeof j === "object") root.perOutput = j
        } catch (e) {
            // Keep what is in memory. Blanking every monitor's layout because
            // one write raced a read would be far worse than ignoring a bad
            // parse.
        }
    }

    property FileView configFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/bar.json"
        watchChanges: true
        // The bar is both reader and writer here, so a torn read is a real
        // possibility rather than a theoretical one.
        atomicWrites: true

        /*
         * THE FIRST READ MUST BE SYNCHRONOUS, and this is not a preference.
         *
         * `autohide` decides the bar's exclusive zone, and quickshell only ever
         * sends set_exclusive_zone ONCE per surface unless the value changes
         * AFTER that surface's first configure. An async read landed inside
         * exactly that window — traced: get_layer_surface,
         * set_exclusive_zone(28), then this file loading and flipping the
         * property to 0, and only THEN the first configure. Wayland never saw
         * the 0. So an auto-hiding bar reserved its 28px forever: it slid out of
         * sight and left the strip of bare desktop above every maximized window
         * that auto-hide exists to avoid, with the QML property reading 0 the
         * whole time.
         *
         * Loading before any window exists also means the per-monitor module
         * switches are right on the first frame instead of popping in.
         */
        blockLoading: true

        onFileChanged: reload()
        onLoaded: root.parse(this.text())
        // Absent file is the normal case: nobody has changed anything yet.
        onLoadFailed: root.perOutput = ({})
    }

    // Forces the three blocking reads above to happen NOW, for the reason given
    // on configFile: without a reader a FileView sits idle and `blockLoading`
    // has nothing to block on. The edge is read here for the same reason
    // `autohide` is — it decides which side the exclusive zone is reserved on,
    // and quickshell sends that once per surface.
    Component.onCompleted: {
        root.parse(configFile.text())
        root.applyGlobals()
    }
}
