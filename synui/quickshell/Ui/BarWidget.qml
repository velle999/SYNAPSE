import QtQuick

/*
 * BarWidget — the base type every bar plugin extends.
 *
 * ⚠ THIS IS OMARCHY'S CONTRACT, REIMPLEMENTED, NOT A DESIGN OF OUR OWN. Their
 * shell/Ui/BarWidget.qml declares exactly these members, and a widget written
 * against it has to find them here or it is not the same contract — which would
 * make "SynapseOS reads Omarchy's plugin format" a claim rather than a fact.
 * Their file is 45 lines and is the whole of what a bar widget is promised:
 *
 *     bar         the host, for geometry and for reaching sibling instances
 *     moduleName  the widget's canonical id
 *     settings    per-widget overrides out of the host's config
 *     vertical    is the bar a column
 *     barSize     the bar's thickness
 *     broadcast() run a method on every instance of this widget
 *     setting()   one settings value, with a fallback
 *
 * Reimplemented rather than copied: it is somebody else's project under
 * somebody else's licence, and 45 lines of contract is a thing to honour rather
 * than a thing to vendor. What is written here is behaviour, from their
 * documented description of each member.
 *
 * ⛔ AND IT IS THE ONLY PART OF THEIR SHELL THAT IS PORTABLE. Their own widgets
 * also `import qs.Ui` and `import qs.Commons` — Style.qml alone is 23KB of API
 * that has no counterpart on synui — and several `import Quickshell.Hyprland`,
 * which speaks to a compositor socket this desktop does not have. A widget that
 * does either is refused by synui-plugins with the import named, rather than
 * loaded into a bar where it would fail silently. See that script's header.
 */
Item {
    id: root

    /* The host bar. `null` until the loader has placed this widget, so every
     * read below is guarded — a widget instantiated for measurement, or one
     * whose loader is still resolving, must not divide by a missing host. */
    property QtObject bar: null

    /* The manifest's `id`. The host looks settings up by it and, on a desktop
     * with a bar per monitor, uses it to find this widget's peers. */
    property string moduleName: ""

    /* Per-widget overrides. An empty object rather than undefined, so
     * `settings.foo` in a widget body is a miss and not a type error. */
    property var settings: ({})

    /*
     * Bar geometry, lifted off the host so a widget can read it without the
     * `bar ? bar.x : fallback` ternary in every expression — which is the
     * reason Omarchy's base defines them and not a convenience we added.
     *
     * The fallback for barSize is synui's own bar thickness rather than
     * Omarchy's Style.bar.sizeHorizontal: a widget asking how tall the bar is
     * wants the bar it is actually in.
     */
    readonly property bool vertical: root.bar ? root.bar.vertical : false
    readonly property int  barSize:  root.bar && root.bar.barSize > 0
                                     ? root.bar.barSize : 28

    /*
     * Run `method` on every live instance of this widget.
     *
     * A bar surface exists per monitor, so a widget that has just learned
     * something — a click, an IPC call, a file change — has peers holding the
     * old answer. Without this, a refresh lands on one screen and leaves the
     * others stale, which is Omarchy's own stated reason for it.
     *
     * Falls back to [root] when the host cannot enumerate: one instance
     * refreshed is the honest answer, and better than none.
     */
    function broadcast(method) {
        const items = (root.bar && typeof root.bar.moduleWidgets === "function")
                      ? root.bar.moduleWidgets(root.moduleName) : [root]
        for (let i = 0; i < items.length; i++) {
            const it = items[i]
            if (it && typeof it[method] === "function") it[method]()
        }
    }

    /*
     * One value out of this widget's settings, with a fallback.
     *
     * ⚠ null AND undefined BOTH TAKE THE FALLBACK. A settings file is written by
     * a human, and a key present-but-null is how "I cleared this" is spelt in
     * JSON — treating it as a value would hand the widget a null where it
     * expects a number.
     */
    function setting(name, fallback) {
        const value = root.settings ? root.settings[name] : undefined
        return (value === undefined || value === null) ? fallback : value
    }
}
