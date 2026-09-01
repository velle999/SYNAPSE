import QtQuick
import Quickshell
import Quickshell.Io
import "../components"
import ".."

/*
 * The focus timer, on the bar.
 *
 * ⛔ THE BAR IS A READER, NOT THE TIMER. The session lives in vibe's companion
 * database and its deadline is published to ~/.config/synui/pomodoro.state —
 * the same file the chat window reads. That split is the whole design: `vibe
 * serve` exists only while the chat window is open, so a timer owned by the
 * assistant's process would stop the moment somebody closed the window they
 * started it from, and a timer owned by THIS module would vanish on a bar
 * reload and be invisible to the CLI. One fact, three readers.
 *
 * ⚠ THE FILE CARRIES A DEADLINE, NOT A COUNTDOWN, so the seconds tick in
 * arithmetic here with nothing spawned and nothing rewritten. A state file
 * that had to be updated every second to stay true would be a disk write a
 * second for twenty-five minutes.
 *
 * ⚠ AND THE FILE IS USUALLY ABSENT. No timer running is the ordinary state of
 * this desktop, not an error — printErrors stays off and a failed load simply
 * means nothing to show.
 */
BarModule {
    id: root

    property int ends: 0          // epoch seconds; 0 == nothing running
    property int now: 0
    property string task: ""
    /* ⛔ ONE RECONCILE PER SESSION. A timer that has run out is still a row in
     * the database until something notices, and noticing is what sends the
     * desktop notification — so this fires `vibe pom status` exactly once, at
     * the deadline, and the engine closes the session, clears the file and says
     * so. Firing it every tick would be a process a second and a notification
     * race between every monitor's bar. */
    property bool collected: false

    readonly property int remain: Math.max(0, root.ends - root.now)
    readonly property bool done: root.ends > 0 && root.remain <= 0

    moduleVisible: root.ends > 0

    icon: root.done ? Icons.focusDone : Icons.focus
    iconColor: root.done ? root.pal.red : root.pal.accent
    text: {
        if (root.ends <= 0) return ""
        if (root.done) return I18n.tr("done")
        const m = Math.floor(root.remain / 60), s = root.remain % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }
    // ⚠ TWO SENTENCES, NOT ONE WITH A HOLE IN IT. The task name is optional,
    // and "Focus — %1" with an empty %1 would leave a dangling dash in every
    // language. A translator gets the whole sentence either way.
    tooltipText: root.done
        ? (root.task ? I18n.tr("Focus timer finished — %1").arg(root.task)
                     : I18n.tr("Focus timer finished"))
        : (root.task ? I18n.tr("Focus — %1 · click to stop").arg(root.task)
                     : I18n.tr("Focus · click to stop"))

    /* A stray click costs a focus session, so the tooltip says what the click
     * does before it is made — and stopping is recoverable, which is why this
     * is a plain click rather than a chord nobody would find. */
    onClicked: stopper.running = true

    Process { id: stopper; command: ["vibe", "pom", "stop"] }
    Process { id: collector; command: ["vibe", "pom", "status"] }

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/pomodoro.state"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            const e = this.text().match(/^\s*ends\s*=\s*(\d+)\s*$/m)
            const t = this.text().match(/^\s*task\s*=\s*(.*)$/m)
            root.ends = e ? parseInt(e[1]) : 0
            root.task = t ? t[1].trim() : ""
            root.now = Math.floor(Date.now() / 1000)
            root.collected = false
        }
        onLoadFailed: { root.ends = 0; root.task = ""; root.collected = false }
    }

    Timer {
        running: root.ends > 0
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            root.now = Math.floor(Date.now() / 1000)
            if (root.done && !root.collected) {
                root.collected = true
                collector.running = true
            }
        }
    }
}
