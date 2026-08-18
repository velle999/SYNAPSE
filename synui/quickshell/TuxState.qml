pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

/*
 * TuxState — the pet itself: what it needs, what it is doing, and how long it
 * has been alive.
 *
 * A singleton for the reason PostItState is one. Variants builds a Tuxagotchi
 * per monitor, and a pet simulated once per screen would be three pets sharing
 * one save file, each overwriting the others' hunger — the classic version of
 * this bug, where the widget looks fine on a laptop and is nonsense on a desk.
 * There is ONE pet. The windows are windows onto it.
 *
 *
 * TIME IS THE WHOLE PROBLEM
 *
 * A virtual pet is a clock with a face. Everything below is arithmetic on the
 * gap between now and `last` — which is why `last` is written on every tick and
 * read before anything else on startup, and why nothing here counts ticks.
 * Counting ticks would mean the pet aged only while the compositor was up, at
 * whatever rate the timer happened to fire, and a laptop lid would stop time.
 *
 * The gap is applied in three regimes, and the middle one is a deliberate
 * un-authenticity:
 *
 *   under a minute   nothing happens; the tick is the resolution
 *   up to 12 hours   simulated at full rate, in five-minute steps
 *   beyond that      the pet AGES but does not suffer: it comes back hungry
 *                    and dirty, never sick and never dead
 *
 * The real toy would be dead after a weekend and that is exactly right for a
 * toy you carry. This one lives on a machine that gets shut down, updated,
 * taken to a conference and left off for a fortnight, and none of that is
 * neglect. A widget that greets you with a grave for going on holiday gets
 * switched off once and never on again, and then it has taught nobody anything
 * about looking after anything. So: it can die of being ignored while you are
 * SITTING THERE — four hours of untreated illness, half a day of starvation —
 * and it cannot die of your absence. Both halves matter. Removing the first
 * would leave a pet that is not looked after so much as owned.
 *
 *
 * ONE WRITER, AND IT IS THIS FILE
 *
 * ~/.config/synui/tuxagotchi.state is ours, the way widgets.pos is the bar's
 * and widgets.state is the script's (see WidgetLayout.qml, which explains why
 * the desktop's files are split by who writes them rather than by what they
 * describe). It is the same flat `key = value` text everything else in that
 * directory uses, so a stuck pet can be fixed with an editor, and deleting the
 * file is how you start over.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
QtObject {
    id: pet

    // ── How fast a life goes ─────────────────────────────────
    /*
     * Minutes of real time per heart. Slow on purpose: the pet has to be
     * survivable by someone who works at this desk, and the failure mode of a
     * demanding one is that it becomes an interruption with a face. Four hearts
     * at 45 minutes is three hours from full to calling, which is about one
     * meal per working session.
     */
    readonly property int minsPerHunger: 45
    readonly property int minsPerHappy:  55
    readonly property int minsPerPoop:   70

    // Asleep, everything slows to a quarter rather than stopping. A pet whose
    // needs freeze overnight is a pet you never feed in the morning.
    readonly property real sleepFactor: 0.25

    readonly property int bedHour:  21
    readonly property int wakeHour:  8

    // The three ways time is spent. See the header.
    readonly property int catchUpCapMin: 12 * 60
    readonly property int catchUpStepMin: 5

    // Untreated illness that ends it, and starvation that ends it.
    readonly property int sickFatalMin:  4 * 60
    readonly property int starveFatalMin: 12 * 60
    // Illness gets worse before it kills, so there is a visible warning.
    readonly property int sickWorsenMin: 60

    // Age at which each stage begins, in minutes since the egg was laid.
    readonly property var stageAt: ({
        baby:    3,
        child:   60,
        teen:    8 * 60,
        adult:   24 * 60,
        senior:  7 * 24 * 60
    })

    // How long an unanswered call waits before it counts as a care mistake,
    // and how often it beeps in the meantime.
    readonly property int callRepeatMin: 10
    readonly property int careMissMin:   20

    // ── The pet ──────────────────────────────────────────────
    property real born:      0     // epoch seconds; 0 means "no pet yet"
    property real last:      0     // epoch seconds of the last simulated moment
    property real hunger:    4.0   // hearts, 4 = full
    property real happy:     4.0
    property int  weight:    10
    property int  discipline: 0    // 0..100
    property int  poops:     0
    property int  sick:      0     // 0 well, 1 ill, 2 badly ill
    property int  careMisses: 0
    property bool light:     true
    property bool muted:     false
    property real died:      0     // epoch seconds, 0 while alive
    property real sickSince:   0
    property real starveSince: 0
    property real callSince:   0   // when the current need started calling
    property real callNext:    0   // when to beep about it again
    property bool spoilt:    false // calling for nothing, and wants telling off

    readonly property bool alive: pet.born > 0 && pet.died === 0
    readonly property bool hatched: pet.alive && pet.ageMin >= pet.stageAt.baby

    // Minutes lived. Bound to `last` rather than to a clock of its own: `last`
    // moves on every tick, so this updates with the pet and never disagrees
    // with the arithmetic that produced it.
    readonly property real ageMin: pet.born > 0 ? Math.max(0, (pet.last - pet.born) / 60) : 0
    readonly property int  ageDays: Math.floor(pet.ageMin / 1440)
    readonly property int  ageHours: Math.floor((pet.ageMin - pet.ageDays * 1440) / 60)

    readonly property string stage: {
        if (pet.born <= 0) return "none"
        if (pet.died > 0)  return "gone"
        const m = pet.ageMin
        if (m < pet.stageAt.baby)   return "egg"
        if (m < pet.stageAt.child)  return "baby"
        if (m < pet.stageAt.teen)   return "child"
        if (m < pet.stageAt.adult)  return "teen"
        if (m < pet.stageAt.senior) return "adult"
        return "senior"
    }

    // Asleep is a fact about the clock, not a stored flag — storing it would be
    // a second thing to keep true, and the one it disagreed with would be the
    // one on screen. The egg does not sleep; it has not hatched.
    readonly property bool nightTime: {
        const h = clock.hour
        return h >= pet.bedHour || h < pet.wakeHour
    }
    readonly property bool asleep: pet.alive && pet.hatched && pet.nightTime

    // What it is calling about, "" for nothing. Order is triage: illness first,
    // because it is the one with a deadline.
    readonly property string need: {
        if (!pet.alive || !pet.hatched) return ""
        if (pet.sick > 0)      return "sick"
        if (pet.hunger <= 0)   return "hungry"
        if (pet.happy  <= 0)   return "sad"
        if (pet.spoilt)        return "spoilt"
        return ""
    }
    readonly property bool attention: pet.need !== "" && !pet.asleep

    // Whole hearts, which is what the face shows. Floor, not round: three and
    // three quarters is three full hearts and a pet that is getting peckish.
    readonly property int hungerHearts: Math.max(0, Math.min(4, Math.floor(pet.hunger)))
    readonly property int happyHearts:  Math.max(0, Math.min(4, Math.floor(pet.happy)))

    // ── What it is doing right this second ───────────────────
    /*
     * A short-lived animation the buttons set: "eat", "cheer", "sad", "refuse",
     * "clean". Cleared by a timer rather than by the view, so every monitor
     * showing this pet is showing the same thing at the same time — the view
     * owns no state at all, which is what lets three of them exist.
     */
    property string doing: ""
    property real   doingUntil: 0

    function act(what, ms) {
        pet.doing = what
        pet.doingUntil = Date.now() + ms
        actTimer.restart()
    }

    property Timer actTimer: Timer {
        interval: 900
        onTriggered: {
            if (Date.now() >= pet.doingUntil) pet.doing = ""
            else restart()
        }
    }

    // The wall clock, once, for everybody. Two timers asking what time it is
    // would be two answers.
    property QtObject clock: QtObject {
        property int hour: new Date().getHours()
        property Timer t: Timer {
            interval: 30000
            running: WidgetState.tux
            repeat: true
            triggeredOnStart: true
            onTriggered: pet.clock.hour = new Date().getHours()
        }
    }

    // ── The guessing game ────────────────────────────────────
    /*
     * The one from the toy: he turns left or right, you say which, best of
     * three. It is in here rather than in the widget for the same reason
     * everything else is — a game in the view would be a different game on each
     * monitor, and pressing left on one screen would not be seen by the other.
     */
    property bool playing:   false
    property int  gameRound: 0     // 1..3 while a round is live
    property int  gameWins:  0
    property int  gameFace:  0     // -1 left, +1 right, 0 not yet turned
    property string gameResult: "" // "win" / "lose" for the round just played

    function startGame() {
        if (!pet.alive || !pet.hatched || pet.asleep) { pet.refuse(); return }
        if (pet.sick > 0) { pet.refuse(); return }
        pet.playing = true
        pet.gameRound = 1
        pet.gameWins = 0
        pet.gameFace = 0
        pet.gameResult = ""
        pet.beep("click")
    }

    // dir is -1 or +1. The pet's turn is rolled HERE, when the guess arrives,
    // so there is nothing on screen for a sharp-eyed player to read early.
    function guess(dir) {
        if (!pet.playing || pet.gameFace !== 0) return
        pet.gameFace = Math.random() < 0.5 ? -1 : 1
        const won = pet.gameFace === dir
        if (won) pet.gameWins++
        pet.gameResult = won ? "win" : "lose"
        pet.beep(won ? "happy" : "sad")
        roundTimer.restart()
    }

    property Timer roundTimer: Timer {
        interval: 1100
        onTriggered: {
            if (pet.gameRound >= 3) { pet.endGame(); return }
            pet.gameRound++
            pet.gameFace = 0
            pet.gameResult = ""
        }
    }

    function endGame() {
        const won = pet.gameWins >= 2
        pet.playing = false
        pet.gameFace = 0
        pet.gameResult = ""
        if (won) {
            pet.happy = Math.min(4, pet.happy + 1)
            // Running about is the only thing that takes weight off, which is
            // the whole reason snacks are allowed to be free.
            pet.weight = Math.max(5, pet.weight - 1)
            pet.answered()
            pet.act("cheer", 1400)
            pet.beep("happy")
        } else {
            pet.act("sad", 1200)
            pet.beep("sad")
        }
        pet.save()
    }

    function stopGame() {
        pet.playing = false
        pet.gameFace = 0
        pet.gameResult = ""
        roundTimer.stop()
    }

    // ── The buttons ──────────────────────────────────────────
    /*
     * Every one of these ends in save(). The state file is the pet: a press
     * that changed only the properties would be undone by the next restart, and
     * the bug would look like the widget forgetting one specific thing.
     */
    function feed(kind) {
        if (!pet.alive || !pet.hatched) { pet.refuse(); return }
        if (pet.asleep) { pet.refuse(); return }

        if (kind === "meal") {
            // A full pet turns its head away. This is the one refusal that is
            // not a failure — it is how you learn the hearts mean something.
            if (pet.hunger >= 4) { pet.refuse(); return }
            pet.hunger = Math.min(4, pet.hunger + 1)
            pet.weight += 1
        } else {
            // Snacks never fill it up and always please it, and that is the
            // trap: they are two ounces each.
            pet.happy = Math.min(4, pet.happy + 1)
            pet.weight += 2
        }
        // Answering a spoilt call with food is how a pet gets spoilt.
        if (pet.spoilt) { pet.discipline = Math.max(0, pet.discipline - 8); pet.spoilt = false }
        pet.answered()
        pet.act("eat", 1600)
        pet.beep("eat")
        pet.save()
    }

    function toggleLight() {
        pet.light = !pet.light
        pet.beep(pet.light ? "wake" : "sleep")
        pet.save()
    }

    function medicine() {
        if (!pet.alive || !pet.hatched) { pet.refuse(); return }
        if (pet.sick <= 0) { pet.refuse(); return }
        pet.sick -= 1
        if (pet.sick <= 0) {
            pet.sickSince = 0
            pet.answered()
            pet.act("cheer", 1400)
        } else {
            pet.act("eat", 1000)
        }
        pet.beep("medicine")
        pet.save()
    }

    function clean() {
        if (pet.poops <= 0) { pet.refuse(); return }
        // One press, one poop — the toy's own rhythm, and it means the button
        // does something visible every time it is pressed.
        pet.poops -= 1
        pet.act("clean", 900)
        pet.beep("clean")
        pet.save()
    }

    /*
     * Telling it off. Correct EXACTLY when it is calling for nothing; doing it
     * to a pet that actually needs something is unfair and it knows.
     */
    function scold() {
        if (!pet.alive || !pet.hatched || pet.asleep) { pet.refuse(); return }
        if (pet.spoilt) {
            pet.discipline = Math.min(100, pet.discipline + 12)
            pet.spoilt = false
            pet.answered()
            pet.act("sad", 900)
            pet.beep("sad")
        } else {
            pet.happy = Math.max(0, pet.happy - 1)
            pet.careMisses++
            pet.act("sad", 1200)
            pet.beep("sad")
        }
        pet.save()
    }

    // A press that does nothing, said out loud. Silence here reads as a broken
    // button, which is the one thing a toy must never do.
    function refuse() {
        pet.act("refuse", 700)
        pet.beep("sad")
    }

    // Whatever it was calling about has been dealt with.
    function answered() {
        pet.callSince = 0
        pet.callNext = 0
    }

    // A new egg, after a death or on the very first run.
    function newEgg() {
        const now = Date.now() / 1000
        pet.born = now
        pet.last = now
        pet.died = 0
        pet.hunger = 4
        pet.happy = 4
        pet.weight = 10
        pet.discipline = 0
        pet.poops = 0
        pet.sick = 0
        pet.sickSince = 0
        pet.starveSince = 0
        pet.careMisses = 0
        pet.spoilt = false
        pet.light = true
        pet.answered()
        pet.stopGame()
        pet.act("cheer", 1600)
        pet.beep("hatch")
        pet.save()
    }

    // ── The clock, applied ───────────────────────────────────
    /*
     * `mins` minutes of life. Called with 1 from the tick and with
     * catchUpStepMin from the catch-up, so both paths run the SAME arithmetic —
     * a separate "what happened while you were away" routine would be a second
     * pet with slightly different rules, and it would be the one nobody tested.
     */
    function advance(mins) {
        if (!pet.alive || mins <= 0) return

        const slow = pet.asleep ? pet.sleepFactor : 1.0
        const wasHatched = pet.ageMin >= pet.stageAt.baby

        if (wasHatched) {
            pet.hunger = Math.max(0, pet.hunger - mins * slow / pet.minsPerHunger)
            // Illness is miserable, and being left in the dark during the day
            // is too.
            const gloom = (!pet.light && !pet.nightTime) ? 1.6 : 1.0
            const ill   = pet.sick > 0 ? 2.0 : 1.0
            pet.happy = Math.max(0, pet.happy - mins * slow * gloom * ill / pet.minsPerHappy)

            // A light left on at night is the classic care mistake: it cannot
            // sleep, so it is no better rested in the morning.
            if (pet.nightTime && pet.light) {
                pet.happy = Math.max(0, pet.happy - mins / (pet.minsPerHappy * 2))
                pet.lightBurn += mins
                while (pet.lightBurn >= 60) { pet.careMisses++; pet.lightBurn -= 60 }
            } else {
                pet.lightBurn = 0
            }

            if (!pet.asleep && pet.poops < 4) {
                pet.poopIn -= mins
                if (pet.poopIn <= 0) {
                    pet.poops++
                    // Jittered so it is never quite predictable, which is the
                    // difference between a pet and a countdown.
                    pet.poopIn = pet.minsPerPoop * (0.7 + Math.random() * 0.6)
                }
            }

            pet.rollIllness(mins)
            pet.rollSpoilt(mins)
        }

        pet.last += mins * 60
        pet.checkFate()
    }

    // Not saved: a jitter and two burn-downs, all of which are reasonable to
    // restart from scratch after a reboot and none of which are worth a line in
    // a file a human is expected to read.
    property real poopIn: 40
    property real lightBurn: 0

    function rollIllness(mins) {
        if (pet.sick > 0) {
            pet.sickSince = pet.sickSince || pet.last
            if (pet.sick < 2 && (pet.last - pet.sickSince) / 60 >= pet.sickWorsenMin)
                pet.sick = 2
            return
        }
        /*
         * Per-minute chance, summed over the step. Filth is the biggest cause
         * by a distance, which is what makes the flush button matter — an empty
         * stomach is uncomfortable, a floor covered in mess is how a pet
         * actually gets ill.
         */
        let p = 0.0
        p += 0.0007 * pet.poops
        if (pet.hunger <= 0) p += 0.0012
        if (pet.happy  <= 0) p += 0.0006
        if (pet.weight > 30) p += 0.0005
        if (p <= 0) return
        // 1 - (1-p)^mins, so a five-minute step is not five times likelier than
        // it should be.
        if (Math.random() < 1 - Math.pow(1 - p, mins)) {
            pet.sick = 1
            pet.sickSince = pet.last
        }
    }

    /*
     * Calling for nothing. Rarer the better disciplined it is, which is the
     * only thing discipline is FOR — a number that went up and changed nothing
     * would be a score, and this is not a game with a score.
     */
    function rollSpoilt(mins) {
        if (pet.spoilt || pet.need !== "" || pet.asleep) return
        const p = 0.0009 * (1 - pet.discipline / 130)
        if (Math.random() < 1 - Math.pow(1 - p, mins)) pet.spoilt = true
    }

    /*
     * The two ways it can end, both of them slow and both of them loud first.
     * Nothing here can fire during a catch-up longer than the cap, because that
     * path never calls advance() — see restore().
     */
    function checkFate() {
        if (pet.sick > 0 && pet.sickSince > 0
                && (pet.last - pet.sickSince) / 60 >= pet.sickFatalMin) {
            pet.end()
            return
        }
        if (pet.hunger <= 0 && pet.happy <= 0) {
            if (pet.starveSince === 0) pet.starveSince = pet.last
            if ((pet.last - pet.starveSince) / 60 >= pet.starveFatalMin) pet.end()
        } else {
            pet.starveSince = 0
        }
    }

    function end() {
        pet.died = pet.last
        pet.stopGame()
        pet.spoilt = false
        pet.doing = ""
        pet.beep("die")
        pet.save()
    }

    // ── Beeps ────────────────────────────────────────────────
    /*
     * The pet's voice. Eleven square-wave chirps generated by tools/mkbeeps.py
     * and shipped beside the QML, resolved RELATIVE to this file so the same
     * line works from /usr/share and from a source tree — the rule Pizza.qml
     * writes out for its artwork, and the reason neither of them names
     * /usr/share/synui.
     *
     * NOT synui-sound. That plays sound-theme ids for desktop EVENTS and is off
     * until someone turns it on, which is right for a login chime and wrong for
     * a toy: the beeps are not decoration on top of this widget, they are what
     * it is. Switching the pet on is the consent, and the speaker in its corner
     * is how you take it back. What we do borrow is the VOLUME from that file,
     * so the desktop's one sound level still governs how loud a pet is.
     */
    property string player: ""        // "pw", "pa", or "" for a silent machine
    property int volume: 70

    property Process playerProbe: Process {
        running: true
        command: ["sh", "-c",
                  "command -v pw-play >/dev/null 2>&1 && echo pw && exit 0; " +
                  "command -v paplay >/dev/null 2>&1 && echo pa"]
        stdout: StdioCollector {
            onStreamFinished: pet.player = this.text.trim()
        }
    }

    property FileView soundsFile: FileView {
        path: Quickshell.env("HOME") + "/.config/synui/sounds.state"
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: {
            for (const raw of String(this.text()).split("\n")) {
                const m = /^\s*volume\s*=\s*(\d+)/.exec(raw)
                if (m) { pet.volume = Math.max(0, Math.min(100, parseInt(m[1], 10))); return }
            }
        }
        // No file means nobody has touched the desktop's sound settings, which
        // is the common case and not an error. 70 is what synui-sound itself
        // defaults to; the number is duplicated because reading a default out
        // of a shell script is not a thing QML can do.
        onLoadFailed: pet.volume = 70
    }

    function beep(name) {
        if (pet.muted || pet.player === "" || pet.volume <= 0) return
        // strip the file:// — pw-play takes a path, not a URL
        const f = String(Qt.resolvedUrl("widgets/sounds/" + name + ".wav")).replace("file://", "")
        /*
         * execDetached, never a shared Process: beeps overlap (a click while
         * the call is still chirping) and assigning `running = true` to a
         * Process that is already running is a silent no-op — the widget would
         * simply go quiet under exactly the fast pressing that most wants a
         * sound. Nothing here reads anything back, so there is nothing a
         * Process would buy.
         */
        if (pet.player === "pw")
            Quickshell.execDetached(["pw-play", "--volume=" + (pet.volume / 100).toFixed(3), f])
        else
            Quickshell.execDetached(["paplay", "--volume=" + Math.round(pet.volume * 655.35), f])
    }

    function toggleMute() {
        pet.muted = !pet.muted
        if (!pet.muted) pet.beep("click")
        pet.save()
    }

    // ── The save file ────────────────────────────────────────
    readonly property string path:
        Quickshell.env("HOME") + "/.config/synui/tuxagotchi.state"

    // What we last wrote, so the watcher can tell our own save from an edit.
    property string lastWritten: ""
    property bool loaded: false

    property FileView file: FileView {
        path: pet.path
        watchChanges: true
        atomicWrites: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: pet.restore(this.text())
        // No file is a machine that has never had a pet. Do NOT hatch one here:
        // an egg appears when the widget is switched on, not when quickshell
        // starts, or everybody who never asked for a pet would own one.
        onLoadFailed: { pet.loaded = true; pet.wake() }
    }

    /*
     * The one door in. Both facts this needs — the save file having been read,
     * and the widget being switched on — arrive asynchronously and in either
     * order (widgets.state and tuxagotchi.state are two FileViews racing at
     * startup). Whichever lands second calls this, and the first call with both
     * true is the one that does the work. Written as one function because the
     * version with the hatch in one handler and the catch-up in the other
     * hatched nothing at all on a desktop where the pet was already switched on
     * before quickshell started — which is every desktop, after the first.
     */
    function wake() {
        if (!pet.loaded || !WidgetState.tux) return
        if (pet.born <= 0) pet.newEgg()
        else pet.catchUp()
    }

    function restore(text) {
        // Our own write coming back round. Adopting it would be harmless today
        // and a source of loops the first time save() and restore() disagree
        // about a rounding.
        if (text === pet.lastWritten) { pet.loaded = true; return }

        const v = {}
        for (const raw of String(text).split("\n")) {
            const line = raw.trim()
            if (line === "" || line.startsWith("#")) continue
            const eq = line.indexOf("=")
            if (eq < 0) continue
            v[line.slice(0, eq).trim()] = line.slice(eq + 1).trim()
        }
        function num(k, d) { const n = parseFloat(v[k]); return isNaN(n) ? d : n }
        function bool(k, d) { return v[k] === undefined ? d : (v[k] === "on" || v[k] === "1") }

        pet.born        = num("born", 0)
        pet.last        = num("last", pet.born)
        pet.died        = num("died", 0)
        pet.hunger      = Math.max(0, Math.min(4, num("hunger", 4)))
        pet.happy       = Math.max(0, Math.min(4, num("happy", 4)))
        pet.weight      = Math.max(5, Math.round(num("weight", 10)))
        pet.discipline  = Math.max(0, Math.min(100, Math.round(num("discipline", 0))))
        pet.poops       = Math.max(0, Math.min(4, Math.round(num("poops", 0))))
        pet.sick        = Math.max(0, Math.min(2, Math.round(num("sick", 0))))
        pet.careMisses  = Math.max(0, Math.round(num("care_misses", 0)))
        pet.sickSince   = num("sick_since", 0)
        pet.starveSince = num("starve_since", 0)
        pet.light       = bool("light", true)
        pet.muted       = bool("muted", false)
        pet.spoilt      = bool("spoilt", false)
        pet.loaded      = true
        pet.wake()
    }

    /*
     * The gap. Three regimes, described in the header; this is the only place
     * that decides which one applies.
     */
    function catchUp() {
        if (!pet.alive) return
        const now = Date.now() / 1000
        let gap = (now - pet.last) / 60

        // A clock that went backwards — a timezone change, an NTP correction,
        // a VM resumed from a snapshot. Not a reason to punish anybody: take
        // the new time and carry on.
        if (gap < 0) { pet.last = now; pet.save(); return }
        if (gap < 1) return

        if (gap <= pet.catchUpCapMin) {
            // Full rate, in steps rather than one big jump: illness, poop and
            // spoiling are per-minute rolls, and a single 700-minute call would
            // make them all fire at once or not at all.
            while (gap > 0) {
                const step = Math.min(pet.catchUpStepMin, gap)
                pet.advance(step)
                if (!pet.alive) break
                gap -= step
            }
        } else {
            /*
             * A long absence. Age is real time and must not be faked, so `last`
             * jumps the whole way — the pet is genuinely older. Everything else
             * is set by hand to "hungry, dirty, glad to see you", and NOT run
             * through advance(), which is what makes death impossible here.
             */
            pet.last = now
            pet.hunger = Math.min(pet.hunger, 1)
            pet.happy  = Math.min(pet.happy, 1)
            pet.poops  = Math.min(4, pet.poops + 2)
            pet.sickSince = 0
            pet.starveSince = 0
            pet.spoilt = false
        }
        pet.save()
    }

    function save() {
        if (!pet.loaded) return
        const text =
            "# SynapseOS — Tuxagotchi. Written by the desktop widget.\n" +
            "# Delete this file to start again with a new egg.\n" +
            "version = 1\n" +
            "born = "         + Math.round(pet.born) + "\n" +
            "last = "         + Math.round(pet.last) + "\n" +
            "died = "         + Math.round(pet.died) + "\n" +
            "hunger = "       + pet.hunger.toFixed(2) + "\n" +
            "happy = "        + pet.happy.toFixed(2) + "\n" +
            "weight = "       + pet.weight + "\n" +
            "discipline = "   + pet.discipline + "\n" +
            "poops = "        + pet.poops + "\n" +
            "sick = "         + pet.sick + "\n" +
            "sick_since = "   + Math.round(pet.sickSince) + "\n" +
            "starve_since = " + Math.round(pet.starveSince) + "\n" +
            "care_misses = "  + pet.careMisses + "\n" +
            "light = "        + (pet.light ? "on" : "off") + "\n" +
            "muted = "        + (pet.muted ? "on" : "off") + "\n" +
            "spoilt = "       + (pet.spoilt ? "on" : "off") + "\n"
        pet.lastWritten = text
        file.setText(text)
    }

    // ── The heartbeat ────────────────────────────────────────
    /*
     * One minute, and only while the widget is on. A pet nobody is showing does
     * not need a timer — the gap is made up the moment it comes back, which is
     * the same code path a reboot takes.
     */
    property Timer tick: Timer {
        interval: 60000
        repeat: true
        running: WidgetState.tux && pet.alive
        onTriggered: {
            pet.advance(1)
            pet.callOut()
            pet.save()
        }
    }

    /*
     * The beep that makes it a virtual pet rather than a picture of one. It
     * repeats while the need is unmet, gives the desk twenty minutes' grace
     * before recording a care mistake, and NEVER fires while the pet is asleep
     * — a toy that wakes you at 3am is a toy in a drawer.
     */
    function callOut() {
        if (pet.need === "" || pet.asleep) { pet.callSince = 0; return }
        const now = pet.last
        if (pet.callSince === 0) { pet.callSince = now; pet.callNext = 0 }
        if (now >= pet.callNext) {
            pet.beep("call")
            pet.callNext = now + pet.callRepeatMin * 60
        }
        if ((now - pet.callSince) / 60 >= pet.careMissMin) {
            pet.careMisses++
            pet.callSince = now      // one mistake per twenty minutes ignored
        }
    }

    /*
     * Switching the widget on is what hatches the first egg, and what makes up
     * the time since the last one was looked at. Both belong here rather than
     * in the widget: with three monitors there are three widgets, and only one
     * of them may lay an egg.
     */
    property Connections onOff: Connections {
        target: WidgetState
        function onTuxChanged() { pet.wake() }
    }
}
