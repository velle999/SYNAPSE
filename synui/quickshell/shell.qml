import Quickshell
import "widgets"

/*
 * SYNAPSE shell — a quickshell replacement for waybar.
 *
 * Run with:  quickshell -c synapse
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
}
