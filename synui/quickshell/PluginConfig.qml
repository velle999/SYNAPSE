pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * PluginConfig — a plugin's own settings, and the file they live in.
 *
 * ⚠ THIS EXISTS BECAUSE THREE WIDGETS ASKED FOR IT, NOT BECAUSE THE FORMAT HAS
 * A SLOT FOR IT. Bar.qml's plugin loader used to leave `settings` at its empty
 * default with a note saying synui had no equivalent of Omarchy's shell.json
 * and that inventing one would be a second settings format for a feature
 * nothing had asked for. Then three did, in the same afternoon:
 *
 *   flappy-pipes  writes its best score through shell.updateEntryInline() and
 *                 reads it back out of shell.shellConfig — a RECORD, not a
 *                 preference, so there is nowhere else it could go
 *   chess         declares `barWidget.defaults` and a `schema`, and reads them
 *                 back through setting()
 *   tetris        keeps sound/theme/look, which its own panel offers as controls
 *
 * ⛔ AND updateEntryInline() SILENTLY DOES NOTHING WITHOUT AN ENTRY TO UPDATE.
 * flappy's writeBest() returns early when entrySettings() finds no row for its
 * id — it updates an entry, it does not create one — so `shellConfig` below
 * synthesises a row for every ACTIVE plugin whether or not the file has one.
 * Without that, the first best score ever scored is dropped and the second run
 * still says 0, which is exactly the shape of the post-it bug in
 * reference_quickshell_fileview_missing_path.
 *
 * The file is OURS and its path is synui's:
 *
 *     ~/.config/synui/plugins.json
 *     { "plugins": [ { "id": "…", …settings }, … ] }
 *
 * The `plugins` array shape is Omarchy's, because that is what their widgets
 * read — flappy walks `config.bar.layout` first and `config.plugins` second,
 * and it is the second one a desktop with no shell.json can honestly answer.
 * What is NOT copied is the rest of shell.json: this holds plugin settings and
 * nothing else, so it cannot become a rival to bar.json for anything bar.json
 * already owns.
 */
Singleton {
    id: root

    readonly property string path: Quickshell.env("HOME") + "/.config/synui/plugins.json"

    /* id -> settings object, as read off disk. The array is flattened on load
     * because every lookup here is by id and a linear walk per bar widget per
     * repaint is a walk nobody needs. */
    property var entries: ({})

    /* Bumped on every load and every write, so a binding that reads through
     * settingsFor() re-evaluates. ⚠ A `var` REASSIGNED TO A MUTATED SAME OBJECT
     * DOES NOT NOTIFY — the entries map is rebuilt rather than edited for that
     * reason, and this counter covers the callers that hold a reference. */
    property int revision: 0

    /* This plugin's settings, never undefined: `settings.foo` in a widget body
     * has to be a miss and not a type error, which is the same rule
     * BarWidget.setting() follows one level down. */
    function settingsFor(id) {
        const e = root.entries[id]
        return (e && typeof e === "object") ? e : ({})
    }

    /*
     * What a panel sees as `shell.shellConfig`.
     *
     * Rebuilt on demand rather than cached: it has one caller per open panel
     * and caching it would need the same invalidation the revision counter
     * already provides.
     *
     * ⚠ EVERY ACTIVE PLUGIN GETS A ROW, present in the file or not — see the
     * header. A row is `{ id }` and whatever has been stored under it.
     */
    readonly property var shellConfig: {
        root.revision            // the dependency, so this re-evaluates on a write
        const out = []
        const seen = {}
        for (let i = 0; i < Plugins.active.length; i++) {
            const id = Plugins.active[i].id
            if (seen[id]) continue
            seen[id] = true
            out.push(root.rowFor(id))
        }
        /* Anything stored for a plugin that is currently off still round-trips:
         * turning a widget off and on again must not lose its high score. */
        for (const id in root.entries)
            if (!seen[id]) out.push(root.rowFor(id))
        return ({ plugins: out })
    }

    function rowFor(id) {
        const row = { id: id }
        const e = root.entries[id]
        if (e && typeof e === "object")
            for (const k in e) if (k !== "id") row[k] = e[k]
        return row
    }

    /*
     * Omarchy's writer, by its name: replace one plugin's inline entry.
     *
     * The `entry` is the WHOLE row and not a patch — that is their contract, and
     * flappy relies on it: writeBest() copies every key it read back before
     * adding bestScore, precisely so a writer that replaces cannot drop one.
     */
    function updateEntryInline(id, entry) {
        if (!id) return
        const next = {}
        for (const k in root.entries) next[k] = root.entries[k]

        const row = {}
        if (entry && typeof entry === "object")
            for (const k in entry) if (k !== "id") row[k] = entry[k]
        next[id] = row

        root.entries = next
        root.revision++
        root.flush()
    }

    /* One key, for the callers that have one to set. Same write path. */
    function setSetting(id, key, value) {
        const row = root.rowFor(id)
        row[key] = value
        root.updateEntryInline(id, row)
    }

    function flush() {
        root.ensureFile()
        const out = []
        for (const id in root.entries) out.push(root.rowFor(id))
        configFile.setText(JSON.stringify({ plugins: out }, null, 2) + "\n")
    }

    /*
     * ⛔ A FileView WILL NOT WRITE A PATH THAT DOES NOT EXIST — no `saved`, no
     * `saveFailed`, nothing. It is what made the post-it lose the first thing
     * ever typed into it, and it would lose the first score ever scored here for
     * the same reason. Once, and remembered, so `rm plugins.json` stays removed
     * until something actually writes again.
     */
    property bool born: false
    function ensureFile() {
        if (root.born) return
        root.born = true
        Quickshell.execDetached(["touch", root.path])
    }

    function parse(text) {
        try {
            const j = JSON.parse(text)
            const rows = (j && Array.isArray(j.plugins)) ? j.plugins : []
            const next = {}
            for (let i = 0; i < rows.length; i++) {
                const r = rows[i]
                if (!r || typeof r !== "object" || !r.id) continue
                const row = {}
                for (const k in r) if (k !== "id") row[k] = r[k]
                next[String(r.id)] = row
            }
            root.entries = next
            root.revision++
        } catch (e) {
            /* Keep what is in memory. Blanking every plugin's settings because
             * one write raced a read is worse than ignoring a bad parse — the
             * same call BarConfig.parse() makes about bar.json. */
        }
    }

    property FileView configFile: FileView {
        path: root.path
        watchChanges: true
        /* Both reader and writer, so a torn read is real rather than
         * theoretical. */
        atomicWrites: true

        onFileChanged: reload()
        onLoaded: root.parse(this.text())
        /* No file is the ordinary case: nobody has changed a plugin setting. */
        onLoadFailed: { root.entries = ({}); root.revision++ }
    }
}
