import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Services.Mpris
import Quickshell.Services.UPower
import ".."

/*
 * The body of the Control Panel — the pane upstream left empty.
 *
 * MainMenu.qml is titled "Control Panel" and its whole left-hand pane was a
 * bare Rectangle with nothing in it: a bordered void beside the volume slider
 * and the three launcher buttons. Everything the panel actually did was in that
 * narrow right-hand strip, so the panel read as broken rather than as sparse.
 *
 * WHAT GOES IN A PANEL THIS SIZE. The strip beside it already covers the things
 * you press — network editor, settings, mixer — so this side is the things you
 * READ, plus the one control that is genuinely awkward to reach any other way.
 * In order of how often you want it: the time, what is playing, and how much
 * battery is left.
 *
 * EVERY SOURCE HERE IS EVENT-DRIVEN. SystemClock ticks, Mpris and UPower are
 * D-Bus services with change signals, and PipeWire pushes. Nothing in this file
 * polls, which matters more than it looks: the panel is instantiated once per
 * monitor (RadialTaskbar is inside a Variants over Quickshell.screens) and
 * stays alive while hidden, so a timer here would be N timers running all day
 * to fill a panel nobody has open. It is the same reason SynWorkspaces.qml
 * insists on one poller for three consumers.
 *
 * ROWS HIDE THEMSELVES when they have nothing to say — no player, no battery.
 * That is the rule the SYNAPSE bar's Media and Battery modules already follow,
 * and it is what lets one layout be right on a laptop playing music and on a
 * silent desktop: an empty "Now playing" heading is worse than no heading.
 *
 * The glyphs are \u escapes into Material Symbols Sharp (the font shell.qml
 * loads), NOT literal private-use characters. quickshell/Icons.qml carries the
 * scar tissue for that one: two earlier versions of it lost every glyph in
 * transit and shipped modules whose icon was the empty string. Codepoints
 * verified against the shipped TTF's own cmap by glyph name.
 */
ColumnLayout {
    id: root

    spacing: 0

    // The first player that is actually playing wins; failing that, the first
    // that exists, so a paused player still offers its transport. Same
    // precedence as the SYNAPSE bar's Media module — two shells disagreeing
    // about which player is "the" player would be its own small madness.
    readonly property var player: {
        const ps = Mpris.players ? Mpris.players.values : [];
        if (!ps || ps.length === 0)
            return null;
        for (const p of ps)
            if (p.isPlaying)
                return p;
        return ps[0];
    }

    readonly property var battery: UPower.displayDevice
    readonly property bool hasBattery: battery ? (battery.isLaptopBattery && battery.isPresent) : false

    /*
     * THE BACKLIGHT, and why it is a stepper rather than a slider.
     *
     * The step goes through `synctl dispatch brightness_up|down`, which is the
     * SAME action XF86MonBrightnessUp is bound to — so the panel and the
     * keyboard key cannot drift apart, and neither needs a privilege. synui
     * sets it through logind's Session.SetBrightness (see src/logind.c), which
     * an active local session may call for its own seat; writing
     * /sys/class/backlight from here would need root or a video-group udev rule.
     *
     * That dispatch is a ±5 STEP and there is no absolute setter, which is what
     * decides the control: a slider would have to walk the value in fives while
     * being dragged, firing a synctl per frame to land somewhere approximate.
     *
     * The readout IS read from sysfs, but nothing polls it. sysfs attributes
     * deliver no inotify events (OsdState.qml pays for a 300ms poll to notice
     * external changes), and this panel does not need to: it is opened, read,
     * and closed, so the value is refreshed when the panel appears and again
     * after each press — which are the only two moments it can have changed
     * while you are looking at it.
     *
     * Absent on a desktop. `blDev` is empty when /sys/class/backlight is, the
     * row hides, and nothing is ever read.
     */
    property string blDev: ""
    property int blCur: -1
    property int blMax: 0
    readonly property int blPct: (blMax > 0 && blCur >= 0) ? Math.round(blCur * 100 / blMax) : -1

    function brightnessStep(dir) {
        stepper.command = ["synctl", "dispatch", dir > 0 ? "brightness_up" : "brightness_down"];
        stepper.running = true;
        blSettle.restart();
    }

    Process {
        id: stepper
    }

    // One shot after a press: logind has written by the time the dispatch
    // returns, but synctl is a separate process and the read has to follow it.
    Timer {
        id: blSettle
        interval: 120
        onTriggered: blCurFile.reload()
    }

    Process {
        id: blProbe
        running: true
        command: ["sh", "-c", "ls -1 /sys/class/backlight 2>/dev/null | head -1"]
        stdout: StdioCollector {
            onStreamFinished: root.blDev = this.text.trim()
        }
    }

    FileView {
        path: root.blDev ? "/sys/class/backlight/" + root.blDev + "/max_brightness" : ""
        onLoaded: root.blMax = parseInt(this.text().trim(), 10) || 0
    }

    FileView {
        id: blCurFile
        path: root.blDev ? "/sys/class/backlight/" + root.blDev + "/brightness" : ""
        onLoaded: {
            const v = parseInt(this.text().trim(), 10);
            if (!isNaN(v))
                root.blCur = v;
        }
    }

    /*
     * Whether the panel this sits in is actually on screen, handed down by
     * MainMenu. NOT this item's own `visible`: an Item stays visible when the
     * window around it is hidden, so binding to it here would fire once at
     * construction and never again — the same trap AppLauncher's
     * `Component.onCompleted: forceActiveFocus()` fell into.
     */
    property bool active: false

    // Re-read whenever the panel comes up, since the keyboard keys may have
    // moved the backlight since it was last on screen.
    onActiveChanged: if (active && blDev)
        blCurFile.reload()

    /*
     * Track metadata is third-party text: a title comes out of a file's tags or
     * off a stream's server. A newline or a stray control code in one wrecks
     * the row it is drawn in, so it is trimmed on the way in — the same guard
     * the SYNAPSE bar puts on the same data.
     */
    function clean(s) {
        if (!s)
            return "";
        return String(s).replace(/[\x00-\x1f\x7f]/g, " ").replace(/\s+/g, " ").trim();
    }

    // ── The time ─────────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.topMargin: 12
        spacing: 8

        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true

            Text {
                text: Qt.formatDateTime(Time.now, Config.settings.militaryTimeClockFormat ? "HH:mm" : "h:mm AP")
                font.family: fontQuilon.name
                font.pixelSize: 34
                font.weight: 600
                color: Config.colors.accent
            }
            Text {
                text: Qt.formatDateTime(Time.now, "dddd, d MMMM yyyy")
                font.family: fontRecia.name
                font.pixelSize: 13
                font.weight: 700
                color: Config.colors.textLight
                opacity: 0.55
            }
        }

        // Battery, on the laptops that have one. UPower always exposes a
        // displayDevice, so `hasBattery` is what stands in for the detection a
        // desktop would otherwise need.
        RowLayout {
            spacing: 5
            visible: root.hasBattery
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

            Text {
                text: root.battery && root.battery.state === UPowerDeviceState.Charging ? "\ue1a3" // battery_charging_full
                                                                                        : "\ue1a4" // battery_full
                font.family: iconFont.name
                font.pixelSize: 20
                color: {
                    if (!root.battery)
                        return Config.colors.textLight;
                    const pct = root.battery.percentage * 100;
                    if (root.battery.state === UPowerDeviceState.Charging)
                        return Config.colors.accent;
                    return pct <= 15 ? Config.colors.danger : pct <= 30 ? Config.colors.warning : Config.colors.textLight;
                }
            }
            Text {
                text: root.battery ? Math.round(root.battery.percentage * 100) + "%" : ""
                font.family: fontQuilon.name
                font.pixelSize: 16
                font.weight: 600
                color: Config.colors.textLight
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.topMargin: 10
        implicitHeight: 1
        color: Config.colors.highlight
    }

    // ── Now playing ──────────────────────────────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.topMargin: 8
        spacing: 2
        visible: root.player !== null

        Text {
            text: "NOW PLAYING"
            font.family: fontRecia.name
            font.pixelSize: 10
            font.weight: 800
            font.letterSpacing: 1.4
            color: Config.colors.textLight
            opacity: 0.45
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: root.player ? (root.clean(root.player.trackTitle) || "(unknown track)") : ""
                    font.family: fontQuilon.name
                    font.pixelSize: 15
                    font.weight: 600
                    color: Config.colors.accent
                    // elide without wrap: this row must not grow the panel when
                    // a track with a long name comes on.
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: root.player ? root.clean(root.player.trackArtist) : ""
                    visible: text !== ""
                    font.family: fontRecia.name
                    font.pixelSize: 12
                    font.weight: 700
                    color: Config.colors.textLight
                    opacity: 0.6
                    elide: Text.ElideRight
                }
            }

            // Transport. Each button hides when the player says it cannot do
            // that — MPRIS players genuinely differ here, and a next-track
            // button that does nothing on a radio stream is a lie.
            Repeater {
                model: [
                    {
                        "glyph": "\ue045", // skip_previous
                        "act": "prev"
                    },
                    {
                        "glyph": "\ue037", // play_arrow
                        "act": "toggle"
                    },
                    {
                        "glyph": "\ue044", // skip_next
                        "act": "next"
                    }
                ]

                delegate: Text {
                    required property var modelData

                    // play_arrow flips to pause while playing; the other two are
                    // whatever the table said.
                    text: modelData.act === "toggle" && root.player && root.player.isPlaying ? "\ue034" // pause
                                                                                             : modelData.glyph
                    font.family: iconFont.name
                    font.pixelSize: 22
                    color: transportHover.hovered ? Config.colors.accent : Config.colors.textLight
                    Layout.alignment: Qt.AlignVCenter
                    opacity: enabledFor ? 1 : 0.25

                    readonly property bool enabledFor: {
                        if (!root.player)
                            return false;
                        switch (modelData.act) {
                        case "prev":
                            return root.player.canGoPrevious;
                        case "next":
                            return root.player.canGoNext;
                        default:
                            return root.player.canTogglePlaying;
                        }
                    }

                    Behavior on color {
                        ColorAnimation {
                            duration: 64
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        HoverHandler {
                            id: transportHover
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            cursorShape: Qt.PointingHandCursor
                        }
                        onReleased: {
                            if (!parent.enabledFor)
                                return;
                            switch (parent.modelData.act) {
                            case "prev":
                                root.player.previous();
                                break;
                            case "next":
                                root.player.next();
                                break;
                            default:
                                root.player.togglePlaying();
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Brightness ───────────────────────────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.topMargin: 10
        spacing: 8
        visible: root.blDev !== "" && root.blPct >= 0

        Text {
            text: "\ue518"   // light_mode
            font.family: iconFont.name
            font.pixelSize: 18
            color: Config.colors.textLight
            opacity: 0.7
        }
        Text {
            Layout.fillWidth: true
            text: "Brightness"
            font.family: fontRecia.name
            font.pixelSize: 12
            font.weight: 700
            color: Config.colors.textLight
            opacity: 0.55
        }

        Repeater {
            model: [
                {
                    "glyph": "\ue15b",   // remove
                    "dir": -1
                },
                {
                    "glyph": "\ue145",   // add
                    "dir": 1
                }
            ]

            delegate: Text {
                required property var modelData

                text: modelData.glyph
                font.family: iconFont.name
                font.pixelSize: 18
                color: stepHover.hovered ? Config.colors.accent : Config.colors.textLight
                Layout.alignment: Qt.AlignVCenter

                Behavior on color {
                    ColorAnimation {
                        duration: 64
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    HoverHandler {
                        id: stepHover
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        cursorShape: Qt.PointingHandCursor
                    }
                    onReleased: root.brightnessStep(parent.modelData.dir)
                }
            }
        }

        Text {
            // Fixed width, so stepping from 9% to 10% does not shuffle the two
            // buttons beside it sideways.
            Layout.preferredWidth: 34
            horizontalAlignment: Text.AlignRight
            text: root.blPct + "%"
            font.family: fontQuilon.name
            font.pixelSize: 13
            font.weight: 600
            color: Config.colors.textLight
            opacity: 0.7
        }
    }

    // Pushes everything above to the top and the output line below to the
    // bottom, so the panel does not re-flow as rows appear and disappear.
    Item {
        Layout.fillHeight: true
        Layout.fillWidth: true
    }

    // ── What the volume slider next door is actually driving ─────────────────
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 16
        Layout.bottomMargin: 10
        spacing: 6

        Text {
            text: AudioSystem.muted ? "\ue04f" // volume_off
                                    : "\ue050" // volume_up
            font.family: iconFont.name
            font.pixelSize: 16
            color: AudioSystem.muted ? Config.colors.danger : Config.colors.textLight
            opacity: 0.7
        }
        Text {
            Layout.fillWidth: true
            text: AudioSystem.audioDeviceName
            font.family: fontRecia.name
            font.pixelSize: 12
            font.weight: 700
            color: Config.colors.textLight
            opacity: 0.55
            elide: Text.ElideRight
        }
        Text {
            text: Math.round(AudioSystem.volume * 100) + "%"
            font.family: fontQuilon.name
            font.pixelSize: 13
            font.weight: 600
            color: Config.colors.textLight
            opacity: 0.7
        }
    }
}
