import Quickshell
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Quickshell.Wayland
import ".."
import "../utils" as Utils

import "../smallicons/" as Smallicons

FloatingWindow {
    id: settingsWindow
    title: "RetroismSettingsWindow"
    reloadableId: "RetroismSettingsWindow"
    visible: Config.openSettingsWindow

    color: Config.colors.shadow

    property int currentTab: 0

    RowLayout {
        anchors.fill: parent
        Rectangle {
            implicitWidth: 220
            Layout.fillHeight: true
            color: Config.colors.base
            ColumnLayout {
                anchors.fill: parent

                Rectangle {
                    implicitHeight: 50
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    color: Config.colors.shadow

                    Item {
                        anchors.fill: parent
                        Text {
                            anchors.centerIn: parent
                            height: parent.height
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: Config.colors.accent
                            text: "Settings"
                            font.family: fontQuilon.name
                            font.weight: 600
                            font.pixelSize: 18
                        }
                    }
                }

                // ----------- General Tab Button
                Rectangle {
                    implicitHeight: 50
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    color: "Transparent"

                    Item {

                        anchors.fill: parent
                        Text {
                            id: generalIcon
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            anchors.left: parent.left
                            leftPadding: 20
                            height: parent.height
                            color: generalHover.hovered || settingsWindow.currentTab == 0 ? Config.colors.accent : Config.colors.textLight
                            text: ""
                            font.family: iconFont.name
                            font.weight: 200
                            font.pixelSize: 22
                            Layout.leftMargin: 10
                        }
                        Text {
                            anchors.left: generalIcon.right
                            height: parent.height
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: generalHover.hovered || settingsWindow.currentTab == 0 ? Config.colors.accent : Config.colors.textLight
                            text: "General"
                            leftPadding: 10
                            font.family: fontQuilon.name
                            font.weight: 200
                            font.pixelSize: 18
                        }
                    }

                    MouseArea {

                        anchors.fill: parent
                        HoverHandler {
                            id: generalHover
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            cursorShape: Qt.PointingHandCursor
                        }
                        onReleased: {
                            settingsWindow.currentTab = 0;
                        }
                    }
                }
                // ------------ Widgets Tab
                Rectangle {
                    implicitHeight: 50
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    //Layout.alignment: Qt.AlignTop
                    color: "Transparent"

                    Item {

                        anchors.fill: parent
                        Text {
                            id: widgetsIcon
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            anchors.left: parent.left
                            leftPadding: 20
                            height: parent.height
                            color: widgetsHover.hovered || settingsWindow.currentTab == 1 ? Config.colors.accent : Config.colors.textLight
                            text: ""
                            font.family: iconFont.name
                            font.weight: 200
                            font.pixelSize: 22
                            Layout.leftMargin: 10
                        }
                        Text {
                            anchors.left: widgetsIcon.right
                            height: parent.height
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: widgetsHover.hovered || settingsWindow.currentTab == 1 ? Config.colors.accent : Config.colors.textLight
                            text: "Widgets"
                            leftPadding: 10
                            font.family: fontQuilon.name
                            font.weight: 200
                            font.pixelSize: 18
                        }
                    }

                    MouseArea {

                        anchors.fill: parent
                        HoverHandler {
                            id: widgetsHover
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            cursorShape: Qt.PointingHandCursor
                        }
                        onReleased: {
                            settingsWindow.currentTab = 1;
                        }
                    }
                }
                /*
                // ------------ Info Tab
                Rectangle {
                    implicitHeight: 50
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    //Layout.alignment: Qt.AlignTop
                    color: "Transparent"

                    Item {

                        anchors.fill: parent
                        Text {
                            id: infoIcon
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            anchors.left: parent.left
                            leftPadding: 20
                            height: parent.height
                            color: infoHover.hovered || settingsWindow.currentTab == 2 ? Config.colors.accent : Config.colors.textLight
                            text: ""
                            font.family: iconFont.name
                            font.weight: 200
                            font.pixelSize: 22
                            Layout.leftMargin: 10
                        }
                        Text {
                            anchors.left: infoIcon.right
                            height: parent.height
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: infoHover.hovered || settingsWindow.currentTab == 2 ? Config.colors.accent : Config.colors.textLight
                            text: "Info"
                            leftPadding: 10
                            font.family: fontQuilon.name
                            font.weight: 200
                            font.pixelSize: 18
                        }
                    }

                    MouseArea {

                        anchors.fill: parent
                        HoverHandler {
                            id: infoHover
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            cursorShape: Qt.PointingHandCursor
                        }
                        onReleased: {
                            settingsWindow.currentTab = 2;
                        }
                    }
                  }
                  */
                Item {
                    Layout.fillHeight: true
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Config.colors.base
            // ---- General Tab
            Item {
                anchors.fill: parent
                visible: settingsWindow.currentTab == 0 ? true : false
                Rectangle {
                    id: titlebar
                    implicitHeight: 50
                    anchors.top: parent.top
                    width: parent.width
                    color: Config.colors.shadow

                    Item {
                        anchors.centerIn: parent
                        Text {
                            anchors.centerIn: parent
                            height: parent.height
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: Config.colors.accent
                            text: "General Settings"
                            font.family: fontQuilon.name
                            font.weight: 600
                            font.pixelSize: 18
                        }
                    }
                }
                ScrollView {
                    anchors.top: titlebar.bottom
                    height: parent.height
                    width: parent.width
                    contentWidth: availableWidth
                    ColumnLayout {
                        spacing: 30
                        width: parent.width
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillWidth: true
                            Layout.leftMargin: 25
                            Layout.rightMargin: 25
                            Layout.topMargin: 25
                            height: 100
                            clip: true

                            radius: Config.settings.defaultWindowRadius
                            color: Config.colors.base
                            border.width: 1
                            border.color: Config.colors.highlight
                            RowLayout {
                                anchors.fill: parent
                                ColumnLayout {
                                    Text {
                                        Layout.leftMargin: 25
                                        height: parent.height
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: Config.colors.accent
                                        text: "App Launcher Background"
                                        font.family: fontQuilon.name
                                        font.weight: 600
                                        font.pixelSize: 18
                                    }
                                    Text {
                                        Layout.leftMargin: 25
                                        height: parent.height
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: Config.colors.textLight
                                        opacity: 0.5
                                        text: "Adds a fullscreen background to the application launcher."
                                        font.family: fontRecia.name
                                        font.weight: 700
                                        font.pixelSize: 13
                                    }
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                Switch {
                                    Layout.rightMargin: 25
                                    checked: Config.settings.appLauncherBackground
                                    onClicked: Config.settings.appLauncherBackground = !Config.settings.appLauncherBackground
                                }
                            }
                        }
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillWidth: true
                            Layout.leftMargin: 25
                            Layout.rightMargin: 25
                            Layout.topMargin: 25
                            height: 330
                            clip: true

                            radius: Config.settings.defaultWindowRadius
                            color: Config.colors.base
                            border.width: 1
                            border.color: Config.colors.highlight
                            ColumnLayout {
                                anchors.fill: parent
                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right

                                    ColumnLayout {
                                        Text {
                                            Layout.leftMargin: 25
                                            height: parent.height
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            color: Config.colors.accent
                                            text: "Enable OpenWeatherMap"
                                            font.family: fontQuilon.name
                                            font.weight: 600
                                            font.pixelSize: 18
                                        }
                                        Text {
                                            Layout.leftMargin: 25
                                            height: parent.height
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            color: Config.colors.textLight
                                            opacity: 0.5
                                            text: "The weather widget uses Open Weather Map, a service that grants enough free API calls."
                                            font.family: fontRecia.name
                                            font.weight: 700
                                            font.pixelSize: 13
                                        }
                                    }
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                    Switch {
                                        Layout.rightMargin: 25
                                        checked: Config.settings.openWeatherMap.enableWeather
                                        onClicked: {
                                            Config.settings.openWeatherMap.enableWeather = !Config.settings.openWeatherMap.enableWeather;
                                            Config.fetchWeatherData();
                                        }
                                    }
                                }
                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right

                                    ColumnLayout {
                                        Text {
                                            Layout.leftMargin: 25
                                            height: parent.height
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            color: Config.colors.textLight
                                            text: "API Key"
                                            font.family: fontQuilon.name
                                            font.weight: 600
                                            font.pixelSize: 18
                                        }
                                        Text {
                                            Layout.leftMargin: 25
                                            height: parent.height
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            color: Config.colors.textLight
                                            opacity: 0.5
                                            text: "Create an account and get a key at: openweathermap.org"
                                            font.family: fontRecia.name
                                            font.weight: 700
                                            font.pixelSize: 13
                                        }
                                    }
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                    TextField {
                                        implicitWidth: 320
                                        implicitHeight: 30
                                        placeholderText: "API Key"
                                        font.pixelSize: 14
                                        font.family: fontRecia.name
                                        font.weight: 700
                                        text: Config.settings.openWeatherMap.apiKey
                                        color: Config.colors.accent
                                        selectionColor: Config.colors.textLight
                                        Layout.rightMargin: 25

                                        padding: 2
                                        selectByMouse: true
                                        cursorVisible: false
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter

                                        background: Rectangle {
                                            color: "transparent"
                                            border.width: 1
                                            border.color: Config.colors.highlight
                                            radius: 6
                                        }

                                        onAccepted: {
                                            Config.settings.openWeatherMap.apiKey = text;
                                        }
                                        onTextChanged: {
                                            Config.settings.openWeatherMap.apiKey = text;
                                        }
                                    }
                                }
                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right

                                    ColumnLayout {
                                        Text {
                                            Layout.leftMargin: 25
                                            height: parent.height
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            color: Config.colors.textLight
                                            text: "City"
                                            font.family: fontQuilon.name
                                            font.weight: 600
                                            font.pixelSize: 18
                                        }
                                        Text {
                                            Layout.leftMargin: 25
                                            height: parent.height
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            color: Config.colors.textLight
                                            opacity: 0.5
                                            text: "City that weather data will be gathered from every 10 minutes."
                                            font.family: fontRecia.name
                                            font.weight: 700
                                            font.pixelSize: 13
                                        }
                                    }
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                    TextField {
                                        implicitWidth: 320
                                        implicitHeight: 30
                                        placeholderText: "City Name"
                                        font.pixelSize: 14
                                        font.family: fontRecia.name
                                        font.weight: 700
                                        text: Config.settings.openWeatherMap.city
                                        color: Config.colors.accent
                                        selectionColor: Config.colors.textLight
                                        Layout.rightMargin: 25

                                        padding: 2
                                        selectByMouse: true
                                        cursorVisible: false
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter

                                        background: Rectangle {
                                            color: "transparent"
                                            border.width: 1
                                            border.color: Config.colors.highlight
                                            radius: 6
                                        }

                                        onAccepted: {
                                            Config.settings.openWeatherMap.city = text;
                                        }
                                        onTextChanged: {
                                            Config.settings.openWeatherMap.city = text;
                                        }
                                    }
                                }
                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right

                                    ColumnLayout {
                                        Text {
                                            Layout.leftMargin: 25
                                            height: parent.height
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            color: Config.colors.textLight
                                            text: "Unit"
                                            font.family: fontQuilon.name
                                            font.weight: 600
                                            font.pixelSize: 18
                                        }
                                        Text {
                                            Layout.leftMargin: 25
                                            height: parent.height
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            color: Config.colors.textLight
                                            opacity: 0.5
                                            text: "Temperature unit | available options: metric, standard, imperial"
                                            font.family: fontRecia.name
                                            font.weight: 700
                                            font.pixelSize: 13
                                        }
                                    }
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                    TextField {
                                        implicitWidth: 320
                                        implicitHeight: 30
                                        placeholderText: "Unit"
                                        font.pixelSize: 14
                                        font.family: fontRecia.name
                                        font.weight: 700
                                        text: Config.settings.openWeatherMap.unit
                                        color: Config.colors.accent
                                        selectionColor: Config.colors.textLight
                                        Layout.rightMargin: 25

                                        padding: 2
                                        selectByMouse: true
                                        cursorVisible: false
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter

                                        background: Rectangle {
                                            color: "transparent"
                                            border.width: 1
                                            border.color: Config.colors.highlight
                                            radius: 6
                                        }

                                        onAccepted: {
                                            Config.settings.openWeatherMap.unit = text;
                                        }
                                        onTextChanged: {
                                            Config.settings.openWeatherMap.unit = text;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Item {
                anchors.fill: parent
                visible: settingsWindow.currentTab == 1 ? true : false
                Rectangle {
                    id: titlebarWidgets
                    implicitHeight: 50
                    anchors.top: parent.top
                    width: parent.width
                    color: Config.colors.shadow

                    Item {
                        anchors.centerIn: parent
                        Text {
                            anchors.centerIn: parent
                            height: parent.height
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: Config.colors.accent
                            text: "Widgets"
                            font.family: fontQuilon.name
                            font.weight: 600
                            font.pixelSize: 18
                        }
                    }
                }
                ListView {
                    anchors.top: titlebarWidgets.bottom
                    height: parent.height
                    width: parent.width
                    model: Quickshell.screens
                    spacing: 0
                    bottomMargin: 50
                    clip: true

                    delegate: Item {
                        width: parent.width
                        height: 430
                        property int margin: 50

                        Rectangle {
                            id: monitorTitle
                            anchors.centerIn: parent
                            width: parent.width - margin
                            height: parent.height - margin

                            radius: Config.settings.defaultWindowRadius
                            color: Config.colors.base
                            border.width: 1
                            border.color: Config.colors.highlight
                            clip: true
                            Rectangle {
                                id: widgetBoxTitlebar
                                anchors.top: parent.top
                                width: parent.width
                                height: 50

                                radius: Config.settings.defaultWindowRadius
                                bottomLeftRadius: 0
                                bottomRightRadius: 0
                                color: "Transparent"
                                border.width: 1

                                clip: true
                                border.color: Config.colors.highlight
                                Text {
                                    anchors.left: parent.left
                                    height: parent.height
                                    leftPadding: 25
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    color: Config.colors.accent
                                    text: "Monitor:   " + modelData.name
                                    font.family: fontQuilon.name
                                    font.weight: 600
                                    font.pixelSize: 18
                                }
                                Rectangle {
                                    anchors.right: parent.right
                                    topRightRadius: Config.settings.defaultWindowRadius
                                    height: parent.height
                                    width: parent.height
                                    color: "Transparent"
                                    border.width: 0
                                    Text {
                                        anchors.fill: parent
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: Config.colors.accent
                                        text: ""
                                        font.family: iconFont.name
                                        font.weight: 600
                                        font.pixelSize: 30
                                        opacity: widgetAddHover.hovered ? 0.5 : 1
                                        Behavior on opacity {
                                            NumberAnimation {
                                                duration: 64
                                            }
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        HoverHandler {
                                            id: widgetAddHover
                                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                            cursorShape: Qt.PointingHandCursor
                                        }
                                        onReleased: {
                                            Config.addWidget(modelData.name, 0, Config.widgetTypes[0], 0, 0, true);
                                            console.log("Adding widget.");
                                        }
                                    }
                                }
                            }
                            GridView {
                                anchors.top: widgetBoxTitlebar.bottom
                                height: parent.height
                                width: parent.width
                                model: Object.values(Config.widgets[modelData.name])
                                cellWidth: 250
                                cellHeight: 330
                                bottomMargin: 50
                                clip: true
                                delegate: Item {
                                    width: 250
                                    height: 330
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width - 20
                                        height: parent.height - 20
                                        color: "Transparent"
                                        border.width: 1
                                        radius: 8
                                        border.color: Config.colors.highlight

                                        Rectangle {
                                            id: widgetSwapper
                                            anchors.top: parent.top
                                            width: parent.width
                                            height: 45
                                            radius: 8
                                            color: "Transparent"
                                            border.width: 1
                                            border.color: Config.colors.highlight
                                            bottomLeftRadius: 0
                                            bottomRightRadius: 0

                                            Item {
                                                anchors.right: parent.right
                                                height: parent.height
                                                width: parent.height
                                                Text {
                                                    anchors.fill: parent
                                                    horizontalAlignment: Text.AlignHCenter
                                                    verticalAlignment: Text.AlignVCenter
                                                    color: Config.colors.accent
                                                    text: ""
                                                    font.family: iconFont.name
                                                    font.weight: 600
                                                    font.pixelSize: 40
                                                    opacity: forwardButton.hovered ? 0.5 : 1
                                                    Behavior on opacity {
                                                        NumberAnimation {
                                                            duration: 64
                                                        }
                                                    }
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    HoverHandler {
                                                        id: forwardButton
                                                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                                        cursorShape: Qt.PointingHandCursor
                                                    }
                                                    onReleased: {
                                                        if (modelData.widgetType < Config.widgetTypes.length - 1) {
                                                            Config.updateWidget(modelData.monitorName, modelData.widgetId, Config.widgetTypes[modelData.widgetType + 1], modelData.x, modelData.y, modelData.enableBackground, modelData.widgetType + 1);
                                                        }
                                                    }
                                                }
                                            }
                                            Item {
                                                anchors.left: parent.left
                                                height: parent.height
                                                width: parent.height
                                                Text {
                                                    anchors.fill: parent
                                                    horizontalAlignment: Text.AlignHCenter
                                                    verticalAlignment: Text.AlignVCenter
                                                    color: Config.colors.accent
                                                    text: ""
                                                    font.family: iconFont.name
                                                    font.weight: 600
                                                    font.pixelSize: 40
                                                    opacity: backButton.hovered ? 0.5 : 1
                                                    Behavior on opacity {
                                                        NumberAnimation {
                                                            duration: 64
                                                        }
                                                    }
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    HoverHandler {
                                                        id: backButton
                                                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                                        cursorShape: Qt.PointingHandCursor
                                                    }
                                                    onReleased: {
                                                        if (modelData.widgetType > 0) {
                                                            Config.updateWidget(modelData.monitorName, modelData.widgetId, Config.widgetTypes[modelData.widgetType - 1], modelData.x, modelData.y, modelData.enableBackground, modelData.widgetType - 1);
                                                        }
                                                    }
                                                }
                                            }
                                            Text {
                                                anchors.fill: parent
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                                color: Config.colors.accent
                                                text: modelData.widgetName
                                                font.family: fontQuilon.name
                                                font.weight: 600
                                                font.pixelSize: 20
                                            }
                                            Rectangle {
                                                id: xPosBox
                                                anchors.top: widgetSwapper.bottom
                                                width: parent.width
                                                height: 50

                                                color: "Transparent"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    Text {
                                                        Layout.alignment: Qt.AlignLeft
                                                        Layout.leftMargin: 15

                                                        horizontalAlignment: Text.AlignHCenter
                                                        verticalAlignment: Text.AlignVCenter
                                                        color: Config.colors.accent
                                                        text: "X Offset"
                                                        font.family: fontRecia.name
                                                        font.weight: 800
                                                        font.pixelSize: 14
                                                    }
                                                    TextField {
                                                        id: xPos
                                                        Layout.alignment: Qt.AlignRight
                                                        Layout.rightMargin: 15
                                                        placeholderText: "x offset"
                                                        validator: IntValidator {
                                                            bottom: 0
                                                            top: 8000
                                                        }
                                                        color: Config.colors.textLight
                                                        text: modelData.x
                                                        background: Rectangle {
                                                            color: Config.colors.base
                                                            border.color: Config.colors.highlight
                                                            radius: 4
                                                        }
                                                        onEditingFinished: {
                                                            Config.updateWidget(modelData.monitorName, modelData.widgetId, modelData.widgetName, parseInt((xPos.text)), modelData.y, modelData.enableBackground, modelData.widgetType);
                                                        }
                                                    }
                                                }
                                            }
                                            Rectangle {
                                                id: yPosBox
                                                anchors.top: xPosBox.bottom
                                                width: parent.width
                                                height: 50

                                                color: "Transparent"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    Text {
                                                        Layout.alignment: Qt.AlignLeft
                                                        Layout.leftMargin: 15

                                                        horizontalAlignment: Text.AlignHCenter
                                                        verticalAlignment: Text.AlignVCenter
                                                        color: Config.colors.accent
                                                        text: "Y Offset"
                                                        font.family: fontRecia.name
                                                        font.weight: 800
                                                        font.pixelSize: 14
                                                    }
                                                    TextField {
                                                        id: yPos
                                                        Layout.alignment: Qt.AlignRight
                                                        Layout.rightMargin: 15
                                                        placeholderText: "y offset"
                                                        validator: IntValidator {
                                                            bottom: 0
                                                            top: 8000
                                                        }
                                                        color: Config.colors.textLight
                                                        text: modelData.y
                                                        background: Rectangle {
                                                            color: Config.colors.base
                                                            border.color: Config.colors.highlight
                                                            radius: 4
                                                        }
                                                        onEditingFinished: {
                                                            Config.updateWidget(modelData.monitorName, modelData.widgetId, modelData.widgetName, modelData.x, parseInt(yPos.text), modelData.enableBackground, modelData.widgetType);
                                                        }
                                                    }
                                                }
                                            }
                                            Rectangle {
                                                anchors.top: yPosBox.bottom
                                                width: parent.width
                                                height: 50

                                                color: "Transparent"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    Text {
                                                        Layout.alignment: Qt.AlignLeft
                                                        Layout.leftMargin: 15

                                                        horizontalAlignment: Text.AlignHCenter
                                                        verticalAlignment: Text.AlignVCenter
                                                        color: Config.colors.accent
                                                        text: "Background"
                                                        font.family: fontRecia.name
                                                        font.weight: 800
                                                        font.pixelSize: 14
                                                    }
                                                    Switch {
                                                        Layout.alignment: Qt.AlignRight
                                                        Layout.rightMargin: 5
                                                        checked: modelData.enableBackground
                                                        onClicked: {
                                                            Config.updateWidget(modelData.monitorName, modelData.widgetId, modelData.widgetName, modelData.x, modelData.y, checked, modelData.widgetType);
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            width: 50
                                            height: 50
                                            color: "Transparent"
                                            Text {
                                                anchors.fill: parent
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                                color: Config.colors.danger
                                                text: ""
                                                font.family: iconFont.name
                                                font.weight: 600
                                                font.pixelSize: 30
                                                opacity: removeHover.hovered ? 0.5 : 1
                                                Behavior on opacity {
                                                    NumberAnimation {
                                                        duration: 64
                                                    }
                                                }
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                HoverHandler {
                                                    id: removeHover
                                                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                                    cursorShape: Qt.PointingHandCursor
                                                }
                                                onReleased: {
                                                    Config.removeWidget(modelData.monitorName, modelData.widgetId);
                                                    console.log("Deleting Widget: " + modelData.monitorName + " | " + modelData.widgetId);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Item {
                anchors.fill: parent
                visible: settingsWindow.currentTab == 2 ? true : false
                Rectangle {
                    implicitHeight: 50

                    anchors.top: parent.top
                    width: parent.width
                    color: Config.colors.shadow

                    Item {
                        anchors.centerIn: parent
                        Text {
                            anchors.centerIn: parent
                            height: parent.height
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: Config.colors.accent
                            text: "Information"
                            font.family: fontQuilon.name
                            font.weight: 600
                            font.pixelSize: 18
                        }
                    }
                }
            }
        }
    }

    onClosed: {
        Config.openSettingsWindow = false;
    }
}
