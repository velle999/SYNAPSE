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

import "taskbar" as Taskbar
import "popups" as Popups
import "widgets" as Widgets

Scope {
    id: root
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
