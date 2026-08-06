pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

Singleton {
    id: root

    /*
     * WHERE THE THREE STATE FILES LIVE, and why they are not where upstream put
     * them.
     *
     * Upstream wrote settings.json / widgets.json / favoriteapps.json with
     * `Qt.resolvedUrl("./x.json")` — beside shell.qml. That works when the
     * shell is a git checkout in the user's own ~/.config/quickshell, which is
     * how linux-antiquity is installed. It CANNOT work here: SYNAPSE ships this
     * tree as a package, so shell.qml sits in a root-owned
     * /usr/share/synui/quickshell-antiquity and the files were never there to
     * begin with. Every write failed silently and every read failed at startup,
     * which is why the theme picker "didn't take" (it wrote currentTheme and
     * then reloaded straight back off disk), why the weather unit snapped back
     * to metric, and why a widget could be added but never survived a restart.
     *
     * A FileView will not create a file that does not exist — no `saved`, no
     * `saveFailed`, nothing (see reference_quickshell_fileview_missing_path), so
     * `onLoadFailed: writeAdapter()` below could never have bootstrapped them
     * even on a writable path. synui-bar seeds all three with `{}` before
     * starting quickshell for exactly that reason; the onLoadFailed handlers
     * are kept for a hand-run `quickshell -p` on a source tree.
     *
     * ~/.config/synui/antiquity/ rather than ~/.config/quickshell/antiquity/:
     * this is synui configuration that happens to be rendered by quickshell,
     * it sits beside synuirc and settings.state where the rest of the desktop's
     * state already is, and it must survive a user copying the packaged tree
     * into ~/.config/quickshell to hack on it — which would otherwise silently
     * fork their settings the moment they did.
     */
    readonly property string stateDir: (Quickshell.env("XDG_CONFIG_HOME") || (Quickshell.env("HOME") + "/.config")) + "/synui/antiquity"

    /*
     * The one font named by family rather than loaded from fonts/.
     *
     * Upstream shipped Apple's Monaco for the taskbar clock, which SYNAPSE
     * cannot redistribute (see FONTS.md). This names a face synui already hard-
     * depends on via ttf-dejavu, so the clock renders on a stock install with
     * nothing extra to install and nothing extra to license. It lives on the
     * Config singleton because every file already imports it — the alternative,
     * a FontLoader id in shell.qml, only resolves for types instantiated inside
     * that document, and a family string needs no loader at all.
     */
    readonly property string fontMono: "DejaVu Sans Mono"

    //*=======================================================================*/
    // READ THIS NOTE:
    // Simply add to this list in order to create your
    // own color schemes, they will automatically show up in the theme picker.
    property var colors: themes[themes[settings.currentTheme] == null ? 'helios' : settings.currentTheme]
    property var themes: {
        "helios": {
            "base": "#181818",
            "shadow": "#121212",
            "highlight": "#333333",
            "urgent": "#ff723e",
            "accent": "#fccf8a",
            "accentDark": "#87704f",
            "text": "#121212",
            "textLight": "#d0daed",
            "outline": "#121212",
            "outlineGradientFade": "#161616",
            "defaultWallpaperPath": "",
            "danger": "#fc5870",
            "warning": "#fcd37b",
            "cbodyBackground": "#fccf8a",
            "cbodyBackgroundShadow": "#d1a67b",
            "cbodyMoonBackground": "#484a5e",
            "cbodyMoonBackgroundShadow": "#5e5e5e",
            "cbodyStroke": "#000000",
            "cbodyPowerMenu": "#fccf8a",
            "cbodyThemingMenu": "#a0675d",
            "cbodyFavoriteApps": "#5e5e5e",
            "cbodyMainMenu": "#666c93",
            "glassTintColor": "#fce2ab",
            "appLauncherBackground": "#252525",
            "patternLineColor": "#87704f",
            "humorWet": "#425682",
            "humorDry": "#c4a78f",
            "humorCold": "#92bbcc",
            "humorHot": "#c44444",
            "elementAir": "#ccdde2",
            "elementWater": "#6f8ebc",
            "elementEarth": "#473e39",
            "elementFire": "#c15555"
        },
        "eris": {
            "base": "#1b1c1e",
            "shadow": "#121212",
            "highlight": "#2f2f33",
            "urgent": "#ff723e",
            "accent": "#c7cfe5",
            "accentDark": "#989daa",
            "text": "#121212",
            "textLight": "#b6aae2",
            "outline": "#121212",
            "danger": "#fc5870",
            "warning": "#fcd37b",
            "cbodyBackground": "#d2dddc",
            "cbodyBackgroundShadow": "#97a09f",
            "cbodyMoonBackground": "#484a5e",
            "cbodyMoonBackgroundShadow": "#5e5e5e",
            "cbodyStroke": "#000000",
            "cbodyPowerMenu": "#d2dddc",
            "cbodyThemingMenu": "#767d7f",
            "cbodyFavoriteApps": "#536868",
            "cbodyMainMenu": "#68877f",
            "glassTintColor": "#b9c5c9",
            "appLauncherBackground": "#252525",
            "patternLineColor": "#505160",
            "humorWet": "#425682",
            "humorDry": "#c4a78f",
            "humorCold": "#92bbcc",
            "humorHot": "#c44444",
            "elementAir": "#ccdde2",
            "elementWater": "#6f8ebc",
            "elementEarth": "#473e39",
            "elementFire": "#c15555"
        },
        "priapus": {
            "base": "#1f211e",
            "shadow": "#121410",
            "highlight": "#393d2d",
            "urgent": "#ff723e",
            "accent": "#a7b777",
            "accentDark": "#747c5c",
            "text": "#121212",
            "textLight": "#d0daed",
            "outline": "#141612",
            "danger": "#fc5870",
            "warning": "#fcd37b",
            "cbodyBackground": "#f2efb3",
            "cbodyBackgroundShadow": "#aaa87c",
            "cbodyMoonBackground": "#484a5e",
            "cbodyMoonBackgroundShadow": "#5e5e5e",
            "cbodyStroke": "#000000",
            "cbodyPowerMenu": "#f2d793",
            "cbodyThemingMenu": "#666c75",
            "cbodyFavoriteApps": "#5b5050",
            "cbodyMainMenu": "#5b6b54",
            "glassTintColor": "#a6bf85",
            "appLauncherBackground": "#252525",
            "patternLineColor": "#4a5437",
            "humorWet": "#425682",
            "humorDry": "#cdce8c",
            "humorCold": "#bcd8d6",
            "humorHot": "#c44444",
            "elementAir": "#c7e0ca",
            "elementWater": "#6f8ebc",
            "elementEarth": "#3e3f2f",
            "elementFire": "#c15555"
        },
        "eros": {
            "base": "#15101c",
            "shadow": "#110c16",
            "highlight": "#2c243d",
            "urgent": "#ff723e",
            "accent": "#fccb7b",
            "accentDark": "#87704f",
            "text": "#121212",
            "textLight": "#d0daed",
            "outline": "#0c0b0c",
            "danger": "#eda1a6",
            "warning": "#fcd37b",
            "cbodyBackground": "#f9997f",
            "cbodyBackgroundShadow": "#b78487",
            "cbodyMoonBackground": "#484a5e",
            "cbodyMoonBackgroundShadow": "#5e5e5e",
            "cbodyStroke": "#000000",
            "cbodyPowerMenu": "#f9997f",
            "cbodyThemingMenu": "#705a7a",
            "cbodyFavoriteApps": "#ad7082",
            "cbodyMainMenu": "#53475e",
            "glassTintColor": "#3c2d66",
            "appLauncherBackground": "#252525",
            "patternLineColor": "#725c3b",
            "humorWet": "#425682",
            "humorDry": "#c4a78f",
            "humorCold": "#92bbcc",
            "humorHot": "#c44444",
            "elementAir": "#ccdde2",
            "elementWater": "#6f8ebc",
            "elementEarth": "#473e39",
            "elementFire": "#c15555"
        },
        "hades": {
            "base": "#181818",
            "shadow": "#121212",
            "highlight": "#333333",
            "urgent": "#ff723e",
            "accent": "#d1ceca",
            "accentDark": "#969593",
            "text": "#eaeaea",
            "textLight": "#f7f8f9",
            "outline": "#e3e7e8",
            "danger": "#aa3a3a",
            "warning": "#c6c479",
            "cbodyBackground": "#181818",
            "cbodyBackgroundShadow": "#121212",
            "cbodyMoonBackground": "#484a5e",
            "cbodyMoonBackgroundShadow": "#5e5e5e",
            "cbodyStroke": "#eaeaea",
            "cbodyPowerMenu": "#181818",
            "cbodyThemingMenu": "#181818",
            "cbodyFavoriteApps": "#181818",
            "cbodyMainMenu": "#181818",
            "glassTintColor": "#d7dddb",
            "appLauncherBackground": "#252525",
            "patternLineColor": "#7f7f7f",
            "humorWet": "#425682",
            "humorDry": "#c6c479",
            "humorCold": "#8da5c9",
            "humorHot": "#c44444",
            "elementAir": "#cccccc",
            "elementWater": "#66929e",
            "elementEarth": "#423f3d",
            "elementFire": "#c15555"
        }
    }

    /*
     * PICKING A THEME, and carrying it past this shell's own windows.
     *
     * `colors` above is a live binding on `settings.currentTheme`, so setting
     * the name is all it takes to recolour every Antiquity surface — upstream's
     * `Quickshell.reload(true)` after the assignment tore the whole shell down
     * and built it again to achieve what the binding already did, and it did so
     * while the settings write was still in flight.
     *
     * The rest of the desktop is the part that was missing. A theme picker that
     * recolours the bar and leaves Dolphin, GTK apps, kitty and Firefox on the
     * old palette has not really switched the theme, so this hands the same
     * colours to `synui-apply-theme` — the one script that knows how to write
     * kdeglobals, GTK settings.ini and the rest, and the same one synui's own
     * theme.c spawns on Super+T. Best effort by design: it is absent on a box
     * where synui is not installed (running this tree under another
     * compositor), and Quickshell.execDetached on a missing binary is a no-op.
     *
     * SCHEME IS DERIVED, NOT LISTED. All five shipped themes are dark, so a
     * hardcoded "dark" would be correct today and quietly wrong the moment
     * somebody takes Config.qml's invitation to add a scheme of their own —
     * they would get a light palette with dark app windows and no clue why.
     * Rec. 601 luma of `base` against the usual midpoint answers it instead.
     *
     * NOT the compositor's own borders and titlebars: synui has no "set theme
     * <name>" dispatch, only a `theme` action that opens its picker, and those
     * are a different set of five presets anyway. Super+T still owns them.
     */
    function applyTheme(name: string): void {
        if (themes[name] == null) {
            console.warn("applyTheme: no such theme: " + name);
            return;
        }
        settings.currentTheme = name;

        const t = themes[name];
        const accent = Qt.color(t.accent);
        const base = Qt.color(t.base);
        const text = Qt.color(t.textLight);
        const c255 = c => [c.r, c.g, c.b].map(v => String(Math.round(v * 255)));
        const luma = 0.299 * base.r + 0.587 * base.g + 0.114 * base.b;

        Quickshell.execDetached(["synui-apply-theme", luma < 0.5 ? "dark" : "light"].concat(c255(accent))
            // The glyph triple. synui-apply-theme defaults it to the accent
            // when omitted, but the base/text pair is positional and cannot be
            // reached without it, so it is passed explicitly.
            .concat(c255(accent)).concat(c255(base)).concat(c255(text)));
    }

    enum SystemPopup {
        Startmenu,
        ThemePicker,
        AppLauncher,
        None
    }
    enum SidebarPopup {
        PowerMenu,
        FavoriteAppsMenu,
        ThemingMenu,
        MainMenu,
        None
    }
    // TODO: Finish adding all the other widgets
    readonly property var widgetTypes: ["Weather", "Clock"]
    readonly property var widgetPaths: {
        "Weather": "WeatherWidget.qml",
        "Clock": "ClockWidget.qml"
        //"CPUTemp": "CPUTemperatureWidget.qml",
        //"GPUTemp": "GPUTemperatureWidget.qml",
        //"RAM": "RAMWidget.qml",
        //"TheDate": "DateWidget.qml"
    }

    // Weather stuff
    property var weatherData: undefined
    function fetchWeatherData() {
        var xmlhttp = new XMLHttpRequest();
        xmlhttp.onreadystatechange = function () {
            if (xmlhttp.readyState !== XMLHttpRequest.DONE) {
                return;
            }
            if (xmlhttp.status == 200) {
                try {
                    weatherData = JSON.parse(xmlhttp.responseText);
                } catch (e) {
                    console.warn("Weather: got an unparsable response from openweathermap: " + e);
                }
            } else {
                // Most likely a wrong API key/city name, or no network. Keep the old
                // data around if we had any, a stale reading beats an empty widget.
                console.warn("Weather: request failed with status " + xmlhttp.status + " - check your API key and city name in settings.");
            }
        };

        // encodeURIComponent so city names with spaces or special characters work (e.g. "São Paulo").
        const url = `https://api.openweathermap.org/data/2.5/weather` + `?q=${encodeURIComponent(settings.openWeatherMap.city)}` + `&appid=${settings.openWeatherMap.apiKey}` + `&units=metric` + `&lang=en`;

        xmlhttp.open("GET", url, true);
        xmlhttp.send();
    }
    Timer {
        interval: 10 * 60 * 1000
        running: Config.settings.openWeatherMap.enableWeather
        repeat: Config.settings.openWeatherMap.enableWeather
        triggeredOnStart: Config.settings.openWeatherMap.enableWeather
        onTriggered: Config.fetchWeatherData()
    }

    function toggleFavoriteApp(appName: string, exec: list<string>, iconPath: string) {
        let updated = Object.assign({}, favoriteApps);
        if (favoriteApps[appName] != null) {
            delete favoriteApps[appName];
            return;
        }
        if (!favoriteApps[appName]) {
            updated[appName] = {};
            favoriteApps = updated;
        }
        updated[appName] = {
            "name": appName,
            "execCommand": exec,
            "icon": iconPath
        };
        favoriteApps = updated;
    }
    property alias favoriteApps: favoriteAppsAdapter.apps
    FileView {
        path: root.stateDir + "/favoriteapps.json"
        // when changes are made on disk, reload the file's content
        watchChanges: true
        onFileChanged: reload()
        // when changes are made to properties in the adapter, save them
        onAdapterUpdated: writeAdapter()

        onLoadFailed: error => {
            if (error == FileViewError.FileNotFound) {
                writeAdapter();
            }
        }
        JsonAdapter {
            id: favoriteAppsAdapter

            // IGNORE WARNING, do not wrap in ()
            property var apps: ({})
        }
    }
    //Widgets
    /*
     * The widgets on one monitor, as a list.
     *
     * `Object.values(widgets[name])` is what both callers used to write inline,
     * and it throws "Value is undefined and could not be converted to an
     * object" for any monitor that has never had a widget put on it — which is
     * every monitor on a fresh install, and every monitor at all while
     * widgets.json was unreadable. A thrown binding leaves the model unset, so
     * the desktop and the Widgets tab of the settings window both came up
     * empty and stayed that way even after a widget was added.
     *
     * One function rather than a guard at each call site: the two callers are
     * the desktop (WidgetScreen) and the editor for it (SettingsWindow), and
     * they must not be able to disagree about what is on a screen.
     */
    function widgetsOn(monitorName: string): var {
        const m = widgets[monitorName];
        return m ? Object.values(m) : [];
    }
    function addWidget(monitorName: string, widgetType: int, widgetName: string, x: int, y: int, enableBackground: bool) {
        let updated = Object.assign({}, widgets);
        if (!widgets[monitorName]) {
            updated[monitorName] = {};
            widgets = updated;
        }
        updated[monitorName] = Object.assign({}, updated[monitorName]);
        updated[monitorName][Object.keys(updated[monitorName]).length] = {
            "widgetName": widgetName,
            "widgetType": widgetType,
            "x": x,
            "y": y,
            "enableBackground": enableBackground,
            "monitorName": monitorName,
            "widgetId": Object.keys(updated[monitorName]).length.toString()
        };
        widgets = updated;
    }
    function updateWidget(monitorName: string, widgetId: int, widgetName: string, x: int, y: int, enableBackground: bool, widgetType: int) {
        let updated = Object.assign({}, widgets);
        if (!widgets[monitorName]) {
            console.error("Monitor not found!");
            return;
        }
        updated[monitorName] = Object.assign({}, updated[monitorName]);
        updated[monitorName][widgetId] = {
            "widgetName": widgetName,
            "widgetType": widgetType,
            "x": x,
            "y": y,
            "enableBackground": enableBackground,
            "monitorName": monitorName,
            "widgetId": widgetId
        };
        widgets = updated;
    }
    function removeWidget(monitorName: string, widgetId: string) {
        let updated = Object.assign({}, widgets);
        delete updated[monitorName][widgetId];
        widgets = updated;
    }
    property alias widgets: widgetsAdapter.monitors
    FileView {
        path: root.stateDir + "/widgets.json"
        // when changes are made on disk, reload the file's content
        watchChanges: true
        onFileChanged: reload()
        // when changes are made to properties in the adapter, save them
        onAdapterUpdated: writeAdapter()

        onLoadFailed: error => {
            if (error == FileViewError.FileNotFound) {
                writeAdapter();
            }
        }
        JsonAdapter {
            id: widgetsAdapter

            // IGNORE WARNING, do not wrap in ()
            property var monitors: ({})
        }
    }

    property bool openSettingsWindow: false
    property alias settings: settingsJsonAdapter.settings
    FileView {
        path: root.stateDir + "/settings.json"
        // when changes are made on disk, reload the file's content
        watchChanges: true
        onFileChanged: reload()
        // when changes are made to properties in the adapter, save them
        onAdapterUpdated: writeAdapter()

        onLoadFailed: error => {
            if (error == FileViewError.FileNotFound) {
                writeAdapter();
            }
        }

        JsonAdapter {
            id: settingsJsonAdapter
            property JsonObject settings: JsonObject {
                property string version: "0.1"
                property bool militaryTimeClockFormat: true
                property string currentTheme: "helios"
                property int defaultWindowRadius: 12
                property bool appLauncherBackground: true
                property JsonObject openWeatherMap: JsonObject {
                    property string apiKey: ""
                    property string city: "Umeå"
                    property string unit: "metric" //standard = kelvin, metric = c, imperial = F
                    property bool enableWeather: false
                }
                property JsonObject execCommands: JsonObject {
                    property string terminal: "kitty"
                    property string files: "nemo"
                }
                property JsonObject bar: JsonObject {
                    property int fontSize: 12
                    property double workspacePadding: 0.032
                    property int trayIconSize: 12
                    property bool monochromeTrayIcons: true
                }
                onCurrentThemeChanged: {
                    console.info("Updated theme to: " + currentTheme);
                }
            }
        }
    }
}
