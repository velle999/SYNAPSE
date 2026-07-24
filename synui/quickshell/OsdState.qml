pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Services.Pipewire

/*
 * OsdState — what the on-screen display is currently showing, and what makes
 * it show.
 *
 * The state is a singleton and the WINDOW (Osd.qml) is per-screen, because the
 * OSD is one logical thing that happens to be drawn on whichever monitor has
 * focus. Putting the state in the window would give three independent OSDs on
 * a three-monitor desk, each with its own hide timer.
 *
 * Nothing here reaches for a keybind. synui owns the media keys (input.c
 * dispatches XF86Audio* ahead of the modal panels) and drives wpctl; this
 * watches the RESULT instead. That makes the OSD source-agnostic for free — the
 * USB knob, a media key, pavucontrol and a script all move the same PipeWire
 * property, and all of them raise the OSD. An IPC message from synui would have
 * covered only the keybind.
 */
QtObject {
    id: root

    // ── What is on screen ────────────────────────────────
    property bool   showing: false
    property string icon:    ""
    property real   value:   0      // 0..1, already clamped
    property string label:   ""
    property bool   muted:   false  // draws the bar dim rather than accent

    // The output the OSD should appear on. Empty until the first probe answers,
    // which is why Osd.qml also accepts an empty string as "primary".
    property string output: ""

    // ── Startup arming ───────────────────────────────────
    // Every binding below fires once as soon as it first resolves — PipeWire
    // publishing the sink, the first backlight read. Un-armed, that pops the OSD
    // at login for something the user did not do. The delay only has to outlast
    // the initial resolve, not any real interaction.
    property bool armed: false
    property Timer armTimer: Timer {
        interval: 1500
        running: true
        onTriggered: root.armed = true
    }

    property Timer hideTimer: Timer {
        interval: 1600
        onTriggered: root.showing = false
    }

    function show(icon, value, label, muted) {
        if (!root.armed) return
        root.icon  = icon
        root.value = Math.max(0, Math.min(1, value))
        root.label = label
        root.muted = muted === true
        // Ask which monitor has focus for NEXT time as well as this one: the
        // cached value paints immediately (no blank frame), and the probe
        // corrects it within a frame or two if focus moved.
        outputProbe.running = true
        root.showing = true
        hideTimer.restart()
    }

    // synui is the only thing that knows which output has focus; there is no
    // Wayland protocol that tells a layer-shell client. Probed on demand rather
    // than polled — an idle desktop must not wake for this.
    property Process outputProbe: Process {
        command: ["synctl", "outputs"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const outs = JSON.parse(this.text)
                    for (const o of outs) {
                        if (o.focused) { root.output = o.name; return }
                    }
                    // No focused output (possible mid-switch): leave the last
                    // known one rather than blanking the OSD to nowhere.
                } catch (e) { /* keep the cached output */ }
            }
        }
    }

    // ── Volume ───────────────────────────────────────────
    readonly property var sink: Pipewire.defaultAudioSink
    readonly property var audio: sink ? sink.audio : null

    // Without a tracker the node stays unbound and every property reads null,
    // so the OSD would simply never fire. Same trap as modules/Volume.qml.
    property PwObjectTracker tracker: PwObjectTracker {
        objects: [Pipewire.defaultAudioSink]
    }

    function showVolume() {
        if (!root.audio) return
        const v = root.audio.volume
        const m = root.audio.muted
        const pct = Math.round(v * 100)
        root.show(m    ? Icons.volMuted
                  : v >= 0.66 ? Icons.volHigh
                  : v >= 0.33 ? Icons.volMed
                              : Icons.volLow,
                  v, m ? "muted" : pct + "%", m)
    }

    property Connections volumeConn: Connections {
        target: root.audio
        function onVolumeChanged() { root.showVolume() }
        function onMutedChanged()  { root.showVolume() }
    }

    // ── Brightness ───────────────────────────────────────
    // POLLED, deliberately. sysfs attribute files do not deliver inotify
    // events, so FileView's watchChanges cannot see a backlight change — it
    // would look like it worked and then never fire once. logind emits no
    // signal for SetBrightness either. A 4-byte read three times a second is
    // the honest cost of noticing.
    //
    // The whole thing stays dormant where there is no backlight: blDev is empty
    // on a desktop, the timer never runs, and nothing is read.
    property string blDev: ""
    property int    blCur: -1
    property int    blMax: 0

    property Process blProbe: Process {
        running: true
        command: ["sh", "-c", "ls -1 /sys/class/backlight 2>/dev/null | head -1"]
        stdout: StdioCollector {
            onStreamFinished: root.blDev = this.text.trim()
        }
    }

    property FileView blMaxFile: FileView {
        path: root.blDev ? "/sys/class/backlight/" + root.blDev + "/max_brightness" : ""
        onLoaded: root.blMax = parseInt(this.text().trim(), 10) || 0
    }

    property FileView blCurFile: FileView {
        path: root.blDev ? "/sys/class/backlight/" + root.blDev + "/brightness" : ""
        onLoaded: {
            const v = parseInt(this.text().trim(), 10)
            if (isNaN(v) || root.blMax <= 0) return
            const prev = root.blCur
            root.blCur = v
            if (prev < 0 || prev === v) return      // first read, or no change
            const frac = v / root.blMax
            root.show(frac >= 0.5 ? Icons.brightnessHigh : Icons.brightnessLow,
                      frac, Math.round(frac * 100) + "%", false)
        }
    }

    property Timer blPoll: Timer {
        interval: 300
        repeat: true
        running: root.blDev !== ""
        triggeredOnStart: true
        onTriggered: {
            if (root.blMax <= 0) blMaxFile.reload()
            blCurFile.reload()
        }
    }
}
