//@ pragma UseQApplication
//@ pragma Env QS_NO_RELOAD_POPUP=1
//@ pragma Env QT_QUICK_CONTROLS_STYLE=Basic
//@ pragma Env QT_QUICK_FLICKABLE_WHEEL_DECELERATION=10000

/*
 * Upstream hard-pins `buuf-nestort` here. SYNAPSE does not ship that icon
 * theme: it is vendored into linux-antiquity as 7,500 loose files with no
 * licence of any kind, and Buuf is Paul Davey's non-commercial artwork, so
 * redistributing it on an ISO is not ours to do.
 *
 * A pragma is static, so pinning any theme here would also override the one
 * synui-apply-theme just wrote for GTK/Qt and split the desktop in two.
 * Deliberately absent instead: with no pragma the shell follows the system
 * icon theme and a theme switch carries it along. To get the upstream look,
 * install buuf-nestort and set `bar_icon_theme` in synuirc — synui-bar exports
 * QS_ICON_THEME from it, which is the dynamic equivalent of this pragma.
 */

import QtQuick
import Quickshell
import Quickshell.Io

import "taskbar" as Taskbar
import "popups" as Popups
import "widgets" as Widgets

Scope {
    id: root

    /*
     * The Super tap. synui runs `synui-bar ipc call menu toggle <output>` and
     * has no idea which shell is up, so this is the same target and the same
     * three functions the SYNAPSE bar's shell.qml exposes — that is what makes
     * `bar_shell` a setting rather than a fork of the compositor.
     *
     * Once for the whole shell, not once per screen: the launcher is one
     * logical thing and the call names its monitor. See LauncherState.qml.
     */
    IpcHandler {
        target: "menu"

        function toggle(output: string): void {
            LauncherState.toggle(output);
        }
        function open(output: string): void {
            LauncherState.show(output);
        }
        function close(): void {
            LauncherState.close();
        }
    }
    FontLoader {
        id: iconFont
        source: "fonts/MaterialSymbolsSharp_Filled_36pt-Regular.ttf"
    }
    /*
     * THREE upstream fonts are deliberately absent, and this is the only place
     * that records why — see FONTS.md for the full provenance.
     *
     *   Monaco.ttf    © Apple / Type Solutions / The Font Bureau — proprietary.
     *                 Its one use (taskbar/ClockWidget.qml) now takes
     *                 Config.fontMono, which names a font synui already depends
     *                 on rather than shipping one.
     *   Charcoal.ttf  © The Font Bureau — proprietary, and loaded but NEVER
     *                 referenced upstream, so dropping it changes no pixels.
     *   DOMINICA.TTF  © Harold Lohner, donationware with no redistribution
     *                 grant. Its three decorative sites now take fontRecia,
     *                 which keeps the serif-against-Boska contrast that the
     *                 original pairing was there for.
     *
     * The remaining four ARE redistributable: Boska/Recia/Quilon are Indian
     * Type Foundry faces whose licence obliges us to credit ITF by name (done
     * in FONTS.md), and Material Symbols is Apache-2.0.
     */
    FontLoader {
        id: fontBoska
        source: "fonts/Boska-Variable.ttf"
    }
    FontLoader {
        id: fontBoskaItalic
        source: "fonts/Boska-VariableItalic.ttf"
    }
    FontLoader {
        id: fontRecia
        source: "fonts/Recia-Variable.ttf"
    }
    FontLoader {
        id: fontReciaItalic
        source: "fonts/Recia-VariableItalic.ttf"
    }
    FontLoader {
        id: fontQuilon
        source: "fonts/Quilon-Variable.ttf"
    }

    Widgets.WidgetScreen {}
    Taskbar.RadialTaskbar {}
    Taskbar.Sidebar {}
    Taskbar.Bar {}

    Popups.SettingsWindow {
        id: settingsWindow
        visible: Config.openSettingsWindow
        reloadableId: "Antiquity Settings Window"
        title: "Antiquity Settings"
    }
}
