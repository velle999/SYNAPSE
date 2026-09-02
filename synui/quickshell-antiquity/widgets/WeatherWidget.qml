import Quickshell
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Quickshell.Wayland
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import ".."

import "../utils" as Utils
import "../smallicons/" as Smallicons
import "../smallicons/weatherWidget/" as Weather

Rectangle {
    id: root

    property bool widgetBackground: false
    width: 500
    height: 500

    color: Qt.rgba(0, 0, 0, 0.0)
    opacity: 1

    function getWet() {
        switch (Config.weatherData?.weather[0]?.main) {
        case "Snow":
            return [true, Config.colors.humorWet];
        case "Rain":
            return [true, Config.colors.humorWet];
        case "Drizzle":
            return [true, Config.colors.humorWet];
        case "Thunderstorm":
            return [true, Config.colors.humorWet];
        default:
            return [false, Config.colors.humorDry];
        }
    }
    // Behold my extremely hippocratic code:
    function getElement() {
        // Decide between fire or water element, because they are obviously opposites.
        if (getWet()[0] == true) {
            return Config.colors.elementWater;
        } else if (parseFloat(Config.weatherData?.main?.temp) >= 32) {
            return Config.colors.elementFire;
        }
        // Earth is not wet, but also not hot enough to be fire.
        if (parseFloat(Config.weatherData?.main?.temp) >= 25) {
            return Config.colors.elementEarth;
        }
        //If we aren't wet, fire, or earth, then all that remains is air...
        return Config.colors.elementAir;
    }
    function getTemp() {
        // parseInt gives NaN (not null) when there's no data yet, so check for that
        // instead — otherwise we'd render a lovely "NaN°C" while waiting for the API.
        var temp = parseFloat(Config.weatherData?.main?.temp);
        if (isNaN(temp)) {
            return "...";
        }

        switch (Config.settings.openWeatherMap.unit) {
        case "metric":
            return parseInt(temp) + "°C";
        case "standard":
            return parseInt(temp + 273.15) + "K";
        case "imperial":
            return parseInt(((temp * 1.8) + 32)) + "°F";
        default:
            return temp + "°C";
        }
    }
    function fogVisible() {
        switch (Config.weatherData?.weather[0]?.main) {
        case "Atmosphere":
            return true;
        case "Rain":
            return true;
        case "Drizzle":
            return true;
        case "Thunderstorm":
            return true;
        case "Snow":
            return true;
        default:
            return false;
        }
    }
    // Behold, my EXTREMELY hippocratic function...
    function getDominantHumor() {
        var wet = getWet()[0];
        var cold = parseInt(Config.weatherData?.main?.temp) < 13 ? true : false;
        var hot = parseInt(Config.weatherData?.main?.temp) > 23 ? true : false;

        if (wet && cold) {
            return [I18n.tr("phlegmatic"), ""];
        }
        if (wet && hot) {
            return [I18n.tr("sanguine"), ""];
        }
        if (!wet && hot) {
            return [I18n.tr("choleric"), ""];
        }
        if (!wet && cold) {
            return [I18n.tr("melancholic"), ""];
        }
        if (wet && !cold && !hot) {
            return [I18n.tr("sanguine"), ""];
        }
        return [I18n.tr("humoral eukrasia"), ""];
    }

    RectangularShadow {
        anchors.centerIn: humorSquare
        width: 225
        height: 225
        blur: 25
        opacity: 0.8
        transform: Rotation {
            angle: 45
            origin.x: 225 / 2
            origin.y: 225 / 2
        }
    }
    Weather.Squares {
        id: humorSquare
        anchors.centerIn: parent
        width: 320
        height: 320
        lineColor: Config.colors.cbodyStroke
        centerColor: Config.colors.base
        leftAndRightColor: getElement()
        topColor: parseFloat(Config.weatherData?.main?.temp) > 25 ? Config.colors.humorHot : Config.colors.humorCold
        bottomColor: getWet()[1]
        opacity: widgetBackground ? 1.0 : 0.7
    }
    Weather.WaterElementDrawing {
        id: waterElement
        anchors.centerIn: humorSquare
        width: implicitWidth / 1.57
        height: implicitHeight / 1.57
        backgroundColor: "#202020"
        opacity: 1
        visible: root.getElement() == Config.colors.elementWater
        transform: Translate {
            y: -3
        }
    }
    Weather.FireElementDrawing {
        id: fireElement
        anchors.centerIn: humorSquare
        width: implicitWidth / 1.57
        height: implicitHeight / 1.57
        backgroundColor: "#050505"
        opacity: 1
        visible: root.getElement() == Config.colors.elementFire
        transform: Translate {
            y: -3
        }
    }
    Weather.AirElementDrawing {
        id: airElement
        anchors.centerIn: humorSquare
        width: implicitWidth / 1.57
        height: implicitHeight / 1.57
        backgroundColor: "#202020"
        opacity: 1
        visible: root.getElement() == Config.colors.elementAir
        transform: Translate {
            y: -3
        }
    }
    Weather.EarthElementDrawing {
        id: earthElement
        anchors.centerIn: humorSquare
        width: implicitWidth / 1.6
        height: implicitHeight / 1.6
        backgroundColor: "#000000"
        opacity: 1
        visible: root.getElement() == Config.colors.elementEarth
        transform: Translate {
            y: -3
        }
    }
    Text {
        anchors.centerIn: humorSquare
        Layout.fillWidth: true
        text: root.getTemp()
        font.pixelSize: 34
        font.family: fontRecia.name
        font.weight: 800
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: parseFloat(Config.weatherData?.main?.temp) > 25 ? Config.colors.humorHot : Config.colors.humorCold
    }

    Text {
        anchors.centerIn: humorSquare
        Layout.fillWidth: true
        text: root.getDominantHumor()[1]
        font.pixelSize: 38
        font.family: iconFont.name
        font.weight: 800
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: Config.colors.text
        transform: Translate {
            y: 106
        }
    }
    Text {
        anchors.centerIn: humorSquare
        Layout.fillWidth: true
        text: root.getDominantHumor()[0]
        font.pixelSize: 19
        font.family: fontRecia.name
        //font.weight: 800
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: Config.colors.text
        transform: Translate {
            y: 162
        }
        DropShadow {
            anchors.fill: parent
            color: "Black"
            opacity: 0.3
            source: parent
            radius: 5
        }
    }
    RectangularShadow {
        anchors.centerIn: sun
        width: 100
        height: 100
        blur: 110
        radius: width / 2
        color: Config.colors.cbodyBackground
        opacity: 0.48
        visible: parseFloat(Config.weatherData?.main?.temp) < 35 ? true : false
        transform: Translate {
            y: -125
        }
    }
    RectangularShadow {
        anchors.centerIn: sun
        width: 160
        height: 160
        blur: 100
        radius: width / 2
        color: Config.colors.humorHot
        opacity: 1
        visible: parseFloat(Config.weatherData?.main?.temp) >= 35 ? true : false
        transform: Translate {
            y: -125
        }
    }
    Weather.Sun {
        id: sun
        width: 180
        height: 180
        anchors.centerIn: humorSquare
        bodyColor: parseFloat(Config.weatherData?.main?.temp) >= 35 ? Config.colors.humorHot : Config.colors.cbodyBackground
        lineColor: Config.colors.cbodyStroke
        transform: [
            Translate {
                y: -125
            }
        ]

        Weather.FaceContent {
            width: 34
            height: 34
            anchors.centerIn: sun
            opacity: 0.7
            visible: parseFloat(Config.weatherData?.main?.temp) < 32 ? true : false
            lineColor: Config.colors.cbodyStroke
            transform: Translate {
                y: -49
                x: -48
            }
        }
        Weather.FaceLivid {
            width: 32
            height: 32
            anchors.centerIn: sun
            opacity: 0.7
            visible: parseFloat(Config.weatherData?.main?.temp) >= 32 ? true : false
            lineColor: Config.colors.cbodyStroke
            transform: Translate {
                y: -49
                x: -48
            }
        }
    }
    Weather.RegularClouds {
        width: 310
        height: 150
        anchors.centerIn: humorSquare
        visible: parseInt(Config.weatherData?.clouds?.all) >= 11 ? true : false
        transform: Translate {
            y: -29
            x: -0
        }
    }
    Weather.ThickWindyClouds {
        width: 320
        height: 70
        anchors.centerIn: humorSquare
        visible: parseInt(Config.weatherData?.clouds?.all) >= 50 ? true : false
        transform: Translate {
            y: -78
            x: -0
        }
    }
    Weather.FogAtmosphere {
        width: 500
        height: 200
        anchors.centerIn: humorSquare
        visible: root.fogVisible()
        transform: Translate {
            y: 50
            x: -11
        }
    }
    Weather.ExtremeHeat {
        width: 500
        height: 400
        anchors.centerIn: humorSquare
        opacity: 0.5
        visible: (root.fogVisible() == false && parseFloat(Config.weatherData?.main?.temp) > 31) ? true : false
        transform: Translate {
            y: -50
            x: -2
        }
    }
    Weather.SnowLines {
        width: 300
        height: 200
        anchors.centerIn: humorSquare
        visible: (Config.weatherData?.weather[0]?.main == "Snow") ? true : false
        transform: Translate {
            y: 33
            x: 0
        }
    }
    Weather.RainLines {
        width: 300
        height: 200
        opacity: 0.62
        anchors.centerIn: humorSquare
        visible: (Config.weatherData?.weather[0]?.main != "Snow" && getWet()[0] == true) ? true : false
        transform: Translate {
            y: 43
            x: 10
        }
    }
}
