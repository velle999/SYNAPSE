//@ pragma UseQApplication

import Quickshell
import Quickshell.Io
import "widgets"

/*
 * SYNAPSE shell — a quickshell replacement for waybar.
 *
 * Run with:  quickshell -c synapse
 *
 * UseQApplication is LOAD-BEARING, not a preference: DBusMenu popups
 * (QsMenuAnchor, which is the tray's whole interaction) refuse to open without
 * it — "Cannot call QsMenuAnchor.open() as quickshell was not started in
 * QApplication mode". The error goes to stderr, which is tty1 on a real
 * session, so the tray simply looks dead. That is not a cosmetic loss: NordVPN
 * sets ItemIsMenu, and Steam exports no Activate at all (see
 * modules/Tray.qml), so for both of them the menu is the ONLY way to interact.
 *
 * Variants gives one Bar per connected screen. Quickshell.screens is live, so
 * plugging or unplugging a monitor creates or destroys its bar without a
 * restart — which waybar could not do without being restarted by hand.
 */
ShellRoot {
    Variants {
        model: Quickshell.screens
        delegate: Bar {}
    }

    /*
     * A bar plugin's `panel` and `service` entry points.
     *
     * ⚠ HERE AND NOT IN Bar.qml BECAUSE THERE IS ONE OF THESE AND THREE BARS.
     * Everything above and below is per screen, which is right for a strip, an
     * OSD and a desktop widget. A plugin's panel is a game and its service is
     * the state that game is played on — mounted once for the session, exactly
     * as their own headers say the shell does it. Three copies of flappy's loop
     * would be three simulations racing over one best score.
     *
     * ⛔ THIS WAS THE MISSING HALF OF "synui READS OMARCHY'S PLUGIN FORMAT".
     * The format was read, the bar widget was hosted, and a plugin that put its
     * actual behaviour in a panel got a button on the bar that did nothing when
     * pressed — no error, because every caller guards. See PluginHost.
     */
    Variants {
        model: Plugins.sessionScoped
        delegate: PluginMount {}
    }

    // The OSD is also one-per-screen, but only the focused one is ever visible
    // (OsdState decides). Same Variants model so a hotplugged monitor gets both
    // a bar and an OSD without a restart.
    Variants {
        model: Quickshell.screens
        delegate: Osd {}
    }

    // The start menu, same one-per-screen-show-one arrangement as the OSD.
    Variants {
        model: Quickshell.screens
        delegate: StartMenu {}
    }

    // How the COMPOSITOR opens the menu.
    //
    // Super tap, super+escape and `synctl dispatch start_menu` all still run in
    // synui — it owns the keyboard, and input.c dispatches keybinds before
    // forwarding to the focused surface, so that keeps working with the menu
    // outside the compositor. What synui cannot do is *tell* us: its own IPC
    // (synctl) is request/response with no event stream, so there is nothing for
    // a client to subscribe to. quickshell's IPC goes the other way, which is
    // exactly the direction needed here, and needs no new synui protocol.
    //
    // The output is passed IN because synui is the only process that knows which
    // one has focus (no Wayland protocol tells a layer-shell client), and it is
    // answering a keypress it just handled — so it already knows.
    IpcHandler {
        target: "menu"

        function toggle(output: string): void { MenuState.toggle(output) }
        function open(output: string): void   { MenuState.show(output) }
        function close(): void                { MenuState.close() }
    }

    // …and the volume mixer, the same way and for a sharper reason.
    //
    // The mixer is a popup on the bar's volume module and was reachable ONLY by
    // right-clicking it. So the menu's "Volume Mixer" entry — which describes
    // this mixer word for word — dispatched `sounds` and opened synui's Event
    // sounds panel instead: a different thing, in a different process. There
    // was no way for it to open this one, because nothing could ask the bar.
    //
    // ONE handler here rather than one inside Volume.qml: that module is
    // instantiated per screen, so a handler in it would register the same IPC
    // target once per monitor.
    // …and the bar's own per-monitor switches, for the ONE of them that also has
    // a control-panel row.
    //
    // bar.json has exactly one writer — this process — and that is what lets the
    // bar write it back without racing its own watch (see BarConfig.qml). The
    // control panel's "Bar auto-hide" row therefore asks rather than writes, the
    // same direction the two handlers around it already go.
    //
    // No output argument, unlike those two: the setting is per monitor and a row
    // on a panel is not, so the row is a master and this applies to every screen.
    // Per-monitor control stays on the bar's right-click menu, which is where it
    // was already.
    IpcHandler {
        target: "bar"

        function autohide(mode: string): void {
            BarConfig.setAll("autohide", mode === "on")
        }
    }

    /*
     * …and a plugin's panel, for the same reason the two above exist: something
     * outside this process has to be able to open it.
     *
     * ⚠ IT IS ALSO THE ONLY WAY TO TEST THE PANEL PATH WITHOUT A POINTER. A
     * panel-kind plugin is opened by clicking its bar widget, and a headless
     * test session has nothing to click with — synthetic input on a live seat is
     * refused outright, and rightly. tests/plugin_host.sh drives this handler
     * instead, which exercises the same PluginHost call the click makes.
     *
     * ONE handler here rather than one per bar, exactly as the mixer's note
     * says: a handler inside Bar.qml would register the same target once per
     * monitor and quickshell would refuse all but the first.
     */
    IpcHandler {
        target: "plugin"

        function toggle(id: string): void { PluginHost.toggle(id, "") }
        function open(id: string): void   { PluginHost.show(id, "") }
        function close(id: string): void  { PluginHost.hide(id) }
        /* Answering rather than acting, so a test can assert on the result of
         * the three above and a script can ask before it acts. */
        function opened(id: string): string {
            return PluginHost.isOpen(id) ? "open" : "closed"
        }
        function mounted(id: string): string {
            return (PluginHost.panelFor(id) ? "panel" : "")
                 + (PluginHost.serviceFor(id) ? " service" : "")
        }
    }

    IpcHandler {
        target: "mixer"

        function toggle(output: string): void { MixerState.toggle(output) }
        function open(output: string): void   { MixerState.show(output) }
        function close(): void                { MixerState.close() }
    }

    // Desktop widgets. All OFF until widgets.state says otherwise, and each
    // one shows on the primary output only — see WidgetState. They are
    // instantiated per screen anyway so that unplugging a monitor cannot strand
    // one on a screen that no longer exists.
    Variants {
        model: Quickshell.screens
        delegate: Visualizer {}
    }
    Variants {
        model: Quickshell.screens
        delegate: SysMonitor {}
    }
    Variants {
        model: Quickshell.screens
        delegate: BigClock {}
    }
    Variants {
        model: Quickshell.screens
        delegate: QuickLaunch {}
    }
    Variants {
        model: Quickshell.screens
        delegate: Pizza {}
    }
    Variants {
        model: Quickshell.screens
        delegate: Tuxagotchi {}
    }
    Variants {
        model: Quickshell.screens
        delegate: AnalogClock {}
    }
    Variants {
        model: Quickshell.screens
        delegate: MusicPlayer {}
    }
    /*
     * The notes. One window per NOTE per screen, which is why this model is
     * built by hand instead of being Quickshell.screens like the others: the
     * count is the user's, it changes while the desktop is up, and every note
     * has to be a window of its own to be dragged and remembered on its own.
     * Both halves are live bindings, so plugging in a monitor and pressing +
     * arrive here by the same path.
     */
    Variants {
        model: {
            const out = []
            for (const s of Quickshell.screens)
                for (const id of PostItState.ids)
                    out.push({ screen: s, noteId: id })
            return out
        }
        delegate: PostIt {}
    }
    // The post-it's editor is its own surface, mapped only while typing —
    // mapping is the only moment layer.c hands a layer surface the keyboard.
    // See PostItEditor.qml.
    Variants {
        model: Quickshell.screens
        delegate: PostItEditor {}
    }
}
