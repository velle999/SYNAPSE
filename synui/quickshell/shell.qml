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
