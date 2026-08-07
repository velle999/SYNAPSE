pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Services.Pipewire

/*
 * EqState.qml — what the bar knows about the system equalizer.
 *
 * The DSP, the state file and every decision about what an equalizer is belong
 * to synui-eq(1); the ten-band editor belongs to synui's own Super-panel
 * (src/eq.c). This singleton is neither. It is the bar's read-only view of that
 * state plus the two verbs the mixer needs — switch it on, or open the editor —
 * so the mixer can link to the equalizer instead of reimplementing it. A second
 * band editor in QML would be a second thing to keep in step with the script's
 * preset table, and it would drift.
 *
 * WHY THE STATE FILE AND NOT `synui-eq status`
 *
 * Same answer as eq.c's: the file is the state, and a probe per open is a
 * process per open with nothing live about it. FileView gives the same values
 * and updates while the panel is on screen, so moving a band in synui's editor
 * changes the mixer's readout underneath it.
 *
 * WHY `running` COMES FROM PIPEWIRE AND NOT FROM THE FILE
 *
 * eq.state says what was ASKED for. It is written before the filter chain is
 * known to have come up and it survives the chain dying — after a crash or a
 * logout the file still says `enabled=on` while nothing is filtering anything,
 * which is precisely the case where a readout saying "on" is a lie. The chain
 * registers a sink node under a name synui-eq fixes (NODE_NAME), so the graph
 * can be asked instead, and it answers about now.
 *
 * That also separates a third state the file cannot express: the chain is up
 * but the user has since picked a different output device in this very mixer,
 * so the audio no longer goes through it. That is `bypassed`, and it is worth
 * saying out loud — the alternative is a mixer that claims the equalizer is on
 * while the radio two rows above it says the sound comes out somewhere else.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
QtObject {
    id: root

    // synui-eq's NODE_NAME: "effect_input.${SINK_NAME}". The capture side of
    // the filter chain, which is what appears as a sink. Duplicated from the
    // script because there is nothing to ask without spawning it — and if it
    // ever moves, the symptom here is the honest one (the equalizer reads as
    // "not running"), not a wrong number.
    readonly property string chainSink: "effect_input.synui_eq"

    // ── From eq.state ────────────────────────────────────
    // What was last asked for. Not what is happening — see `running`.
    property bool enabled: false
    property string preset: "flat"
    property int preamp: 0

    // ── From the graph ───────────────────────────────────
    // Untracked nodes still report `name`, so this needs no PwObjectTracker of
    // its own; it does need SOMETHING in the process to have opened the
    // connection, which Volume.qml's tracker does from the moment the bar
    // starts. Filter-and-index rather than find(): `values` is what the mixer
    // already filters, so this uses the call that is known to work on it.
    readonly property var chainNode: {
        const m = Pipewire.nodes.values.filter(n => n.name === root.chainSink)
        return m.length > 0 ? m[0] : null
    }
    readonly property bool running: chainNode !== null
    readonly property bool active: running && Pipewire.defaultAudioSink === chainNode

    // One word for the row to print. The three failure shapes are distinct
    // enough to name: nothing is filtering (the chain is gone), or something
    // is filtering but not this desktop's audio (the default moved away).
    readonly property string status: {
        if (!root.enabled) return "off"
        if (!root.running) return "not running"
        if (!root.active)  return "bypassed"
        return "on"
    }
    // True when the state file and the graph disagree, i.e. when the readout is
    // reporting a problem rather than a setting.
    readonly property bool warning: root.enabled && !root.active

    // ── Reading ──────────────────────────────────────────
    // QtObject has no default property, so the watcher is held by a named one —
    // the same shape Theme.qml uses for the palette.
    property FileView stateFile: FileView {
        path: (Quickshell.env("HOME") || "") + "/.config/synui/eq.state"
        watchChanges: true
        // No eq.state is the normal case on a box where the equalizer has never
        // been switched on. A WARN per bar start for an expected miss is how a
        // log becomes something nobody reads.
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.parse(this.text())
        onLoadFailed: {
            root.enabled = false
            root.preset = "flat"
            root.preamp = 0
        }
    }

    // Ask the file again. Called when the mixer opens and after our own spawn
    // lands, because watchChanges cannot watch a path that does not exist yet:
    // the FIRST `synui-eq on` creates eq.state, and inotify has nothing to have
    // been watching. Without this the row would sit at "off" until the next
    // change to a file that by then does exist.
    function refresh() { stateFile.reload() }

    // key=value, one per line, defaults for anything absent — so a state file
    // written before a key existed still loads. Only the three keys the bar
    // shows are taken; the bands are the editor's business.
    function parse(text) {
        let enabled = false, preset = "flat", preamp = 0
        for (const line of (text || "").split("\n")) {
            const i = line.indexOf("=")
            if (i < 0) continue
            const k = line.slice(0, i).trim()
            const v = line.slice(i + 1).trim()
            if (k === "enabled")     enabled = (v === "on")
            else if (k === "preset") preset = v || "flat"
            else if (k === "preamp") preamp = parseInt(v, 10) || 0
        }
        root.enabled = enabled
        root.preset = preset
        root.preamp = preamp
    }

    // ── Acting ───────────────────────────────────────────
    // `toggle`, not `on`/`off`: the script decides from the file it owns, so a
    // stale reading here cannot turn a switch the wrong way. Turning it on
    // takes a moment (the chain has to come up and the streams have to be
    // moved), which is why nothing is assumed about the result — the file and
    // the graph both say when it has happened.
    property Process toggleProc: Process {
        command: ["synui-eq", "toggle"]
        onExited: root.refresh()
    }

    // The ten-band editor is synui's, drawn by the compositor and reachable
    // from Control panel ▸ Sound ▸ Equalizer. Dispatching the same action the
    // keybind takes means there is one editor, not two.
    property Process panelProc: Process {
        command: ["synctl", "dispatch", "equalizer"]
    }

    function toggle() { toggleProc.running = true }
    function openPanel() { panelProc.running = true }
}
