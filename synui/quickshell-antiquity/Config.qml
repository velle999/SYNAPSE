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
            "defaultWallpaperPath": "/usr/share/backgrounds/antiquity-carnation-collage.jpg",
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
            "defaultWallpaperPath": "/usr/share/backgrounds/antiquity-georges-riom-collage.jpg",
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
            "defaultWallpaperPath": "/usr/share/backgrounds/antiquity-georges-riom-collage.jpg",
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
            "defaultWallpaperPath": "/usr/share/backgrounds/antiquity-the-blackboard.png",
            // The one palette whose taskbar needs light ink — see `barText`
            // below. Its glassTintColor is a DARK purple, unlike the other
            // four, so `text` (#121212) lands at 1.10 contrast on the bar.
            "barText": "#d0daed",
            "barOutline": "#8f86a8",
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
            "defaultWallpaperPath": "/usr/share/backgrounds/antiquity-the-blackboard.png",
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
     * THE TASKBAR'S INK — and why it is not simply `text` or simply `textLight`.
     *
     * The taskbar is `glassTintColor` at 0.2 alpha (Bar.qml), which means it is
     * EIGHTY PERCENT WALLPAPER. Its legibility is therefore not a property of
     * the palette at all; it is a property of the palette paired with whatever
     * is on the desktop behind it. Measured over the three wallpapers that ship
     * (WCAG ratio against the composited strip, sampled where the bar sits):
     *
     *              carnation   georges-riom   blackboard
     *   `text`       6.1–9.2      4.4–7.0       1.1–1.8
     *   `textLight`  1.0–2.2      1.4–3.0       5.5–12.2
     *
     * Neither is right. Whichever one is hardcoded, the other two-thirds of the
     * shipped wallpapers get illegible text — and 1.03 (eris on carnation) is
     * not "hard to read", it is a blank strip.
     *
     * So the pairing is what gets fixed, not the colour: `defaultWallpaperPath`
     * above — a field upstream declared and left empty in every palette — now
     * names the wallpaper each palette was drawn against, and applyTheme() puts
     * it on screen. Every palette then sits on a known background and upstream's
     * own `text`/`outline` are correct again, which is what these two fall back
     * to.
     *
     * `eros` is the exception and the reason a fallback exists at all: its
     * glassTintColor is #3c2d66, a DARK purple, so its bar reads dark on any
     * wallpaper and dark ink cannot work. It overrides both keys. `hades` needs
     * no override for the opposite reason — its `text` is already #eaeaea,
     * because that palette was drawn for a dark bar to begin with.
     *
     * NOT a luma calculation. The number that decides this is the luma of the
     * composited strip, and QML cannot sample the wallpaper; deriving it from
     * `glassTintColor` alone would be deriving it from the 20% and ignoring the
     * 80%. Two declared keys are honest about that, and a palette added by hand
     * gets upstream's default rather than a wrong guess.
     *
     * The residual hole, accepted deliberately: Super+W can still put a pale
     * wallpaper under `hades`, or a dark one under `helios`, and nothing here
     * notices. Closing that means the bar owning its own background — a much
     * higher tint alpha — which is a different look from the one upstream drew.
     */
    readonly property color barText: colors.barText !== undefined ? colors.barText
                                                                  : colors.text
    readonly property color barOutline: colors.barOutline !== undefined ? colors.barOutline
                                                                        : colors.outline

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
     * It also sets the palette's wallpaper (applyWallpaper below). That is not
     * decoration: this shell's taskbar is 20% tint over the desktop, so the
     * wallpaper is most of the bar's colour and picking one without the other
     * is what made the bar illegible in the first place — see `barText` above.
     *
     * AND the compositor's own surfaces — the control panel, the desktop menu,
     * the wallpaper picker, the task manager, the window borders. Those are
     * drawn by synui itself, in C, and until now nothing here could reach them:
     * applying a palette recoloured this shell and every toolkit app, and left
     * the desktop underneath on whatever Super+T last picked. Half a theme.
     *
     * `synctl dispatch theme <accent> <surface> <ink>` is the leg that closes
     * it. Three colours and not a name, because synui's presets are a table in
     * C and these palettes are a table in QML that Config.qml explicitly invites
     * you to add to — the moment anyone accepts that invitation, a name would
     * refer to nothing. The compositor derives its captions, borders and frame
     * face from the three, with a contrast check on each pairing.
     *
     * `panelInk` and not `text` or `textLight` picked once: synui's panels are
     * drawn ON `base`, and which of the two inks can be read on it flips per
     * palette — hades has a light `text` (#eaeaea) because it was drawn for a
     * dark bar, the other four have #121212 because theirs read pale. That is
     * the same mistake 273 spent a release fixing one surface over; a hardcoded
     * pick here would be it again, in a control panel this time.
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

        Quickshell.execDetached(["synctl", "dispatch", "theme", t.accent, t.base, panelInk(t)]);

        applyWallpaper(name);
    }

    /*
     * Which of a palette's two inks can actually be read on its `base`.
     *
     * WCAG relative luminance rather than a 601 luma average, because the whole
     * question is legibility and 601 flatters dark colours: helios's `text`
     * (#121212) on its `base` (#181818) is a 1.03 contrast ratio — a blank
     * surface — and a luma comparison alone rates it as merely "close".
     *
     * Ties go to `text`, since that is what upstream's own panels use and it is
     * right for four of the five shipped palettes.
     */
    function panelInk(t): string {
        const lum = hex => {
            const c = Qt.color(hex);
            const lin = v => v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
            return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
        };
        const ratio = (a, b) => {
            const la = lum(a), lb = lum(b);
            return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
        };
        return ratio(t.text, t.base) >= ratio(t.textLight, t.base) ? t.text : t.textLight;
    }

    /*
     * Put a palette's own wallpaper on the desktop.
     *
     * Not a nicety — see the `barText` note above. The taskbar is 80% wallpaper,
     * so a palette and the picture behind it are ONE choice, and this is the leg
     * that makes `defaultWallpaperPath` mean something instead of sitting empty
     * the way upstream left it.
     *
     * `synctl dispatch wallpaper <path>` rather than writing wallpaper.state and
     * hoping: synui owns that file, holds the decoded buffer per output, and has
     * to stop linux-wallpaperengine if a Workshop wallpaper is up — none of
     * which a JSON write from out here would do. The compositor refuses a path
     * it cannot read and leaves the current wallpaper alone, so a box without
     * the wallpapers installed loses the picture, not the desktop.
     *
     * An empty path is a palette that declines to bring one. That is the honest
     * default for a hand-added scheme: whatever is on screen stays.
     */
    function applyWallpaper(name: string): void {
        const wp = themes[name] != null ? themes[name].defaultWallpaperPath : "";
        if (!wp)
            return;
        Quickshell.execDetached(["synctl", "dispatch", "wallpaper", wp]);
    }

    /*
     * The FIRST Antiquity login, which the theme picker cannot reach.
     *
     * A fresh install has never opened the picker, so applyTheme() has never
     * run: `currentTheme` is the declared default (helios) and the wallpaper is
     * still SYNAPSE's own dark one. That is precisely the bad pairing — helios's
     * dark ink on a bar composited over a dark picture — and it would be the
     * out-of-the-box look for everyone who switches `bar_shell` and nothing
     * else. So the pairing is established once, here, rather than waiting for
     * the user to go and pick the theme they already have.
     *
     * Once EVER, tracked in settings.json. Doing it at every startup would
     * silently revert a wallpaper chosen with Super+W on the next login, which
     * is the sort of thing that reads as the picker being broken.
     *
     * Driven from the FileView's `loaded`, not Component.onCompleted: the
     * singleton completes before the file has been read, so the flag would
     * still be at its default `false` and the seed would fire on every start.
     *
     * The whole of applyTheme(), not just the wallpaper it was written for. The
     * same argument now covers the compositor's own panels and the toolkits:
     * with nothing but the wallpaper seeded, a fresh `bar_shell = antiquity`
     * came up as an Antiquity bar on an Antiquity wallpaper, in front of a
     * SYNAPSE-neon control panel and SYNAPSE-neon window borders — and stayed
     * that way until the user happened to pick the theme they were already
     * running. Still once ever, and still nothing a later Super+W or Super+T
     * cannot override.
     */
    function seedWallpaperOnce(): void {
        if (settings.wallpaperSeeded)
            return;
        settings.wallpaperSeeded = true;
        applyTheme(settings.currentTheme);
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

    /*
     * A tidy-up, NOT a bug fix — recorded as such because it looks like one.
     *
     * Upstream's un-favourite branch was `delete favoriteApps[appName]; return;`
     * — an in-place mutation of a JsonAdapter `property var` with no assignment
     * after it, which by the usual QML rules notifies nobody and never reaches
     * disk. It was A/B tested headless against this version anyway, and the two
     * are indistinguishable: the binding re-evaluates and favoriteapps.json ends
     * up correct either way. Quickshell's adapter tracks more than a plain
     * property does. Do not "re-fix" this expecting a behaviour change.
     *
     * What is kept is the shape: one copy, both branches, one assignment at the
     * end, and no leading `updated[appName] = {}` placeholder write. It does not
     * lean on that adapter behaviour, which is the only reason to prefer it.
     */
    function toggleFavoriteApp(appName: string, exec: list<string>, iconPath: string) {
        const updated = Object.assign({}, favoriteApps);
        if (updated[appName] != null)
            delete updated[appName];
        else
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

        // Establish the theme/wallpaper pairing on a first-ever start. Here and
        // not in Component.onCompleted, which runs before this file is read —
        // see seedWallpaperOnce().
        onLoaded: root.seedWallpaperOnce()

        JsonAdapter {
            id: settingsJsonAdapter
            property JsonObject settings: JsonObject {
                property string version: "0.1"
                property bool militaryTimeClockFormat: true
                property string currentTheme: "helios"
                // Set once, the first time this shell ever starts, so the theme
                // brings its wallpaper without stamping on a Super+W choice on
                // every subsequent login. See seedWallpaperOnce().
                property bool wallpaperSeeded: false
                property int defaultWindowRadius: 12
                property bool appLauncherBackground: true
                property JsonObject openWeatherMap: JsonObject {
                    property string apiKey: ""
                    property string city: "Umeå"
                    property string unit: "metric" //standard = kelvin, metric = c, imperial = F
                    property bool enableWeather: false
                }
                property JsonObject execCommands: JsonObject {
                    property string terminal: "syntty"
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
