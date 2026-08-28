import Quickshell
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Quickshell.Wayland
import ".."
import "../utils" as Utils

import "../smallicons/" as Smallicons

PanelWindow {
    id: root

    property int menuWidth: 0
    property int popupWidth: 600
    property int screenHeight: 0
    property var currentApps: Utils.AppSearch.fuzzyQuery("A")
    property var closeCallback: function () {}
    property int currentPopup: Config.SystemPopup.AppLauncher
    WlrLayershell.layer: WlrLayer.Top
    exclusionMode: ExclusionMode.Ignore //Ignore
    WlrLayershell.namespace: "diinki_celestialantiquity:bars"
    /*
     * EXCLUSIVE, not OnDemand. OnDemand hands a layer surface the keyboard when
     * it is clicked, which was enough upstream because the only way to open
     * this was to click the taskbar button — the click that opened it was also
     * the click that focused it. Summoned from the keyboard by the Super tap
     * there is no click, so the search field came up unfocused and every
     * keystroke went to whatever was behind it. The launcher is modal while it
     * is up; Exclusive is what that means in layer-shell terms, and it is what
     * the SYNAPSE bar's start menu uses (`focusable: true`) for the same reason.
     */
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive

    anchors {
        left: true
        right: true
        top: true
        bottom: true
    }
    property color glassColor: Config.colors.appLauncherBackground
    color: Config.settings.appLauncherBackground ? Qt.rgba(glassColor.r, glassColor.g, glassColor.b, 0.3) : "transparent"

    visible: currentPopup == Config.SystemPopup.AppLauncher ? true : false

    /*
     * The window is built once and only hidden between uses, so
     * `Component.onCompleted: forceActiveFocus()` in the search field fired
     * exactly once — at startup, while the launcher was invisible — and never
     * again. Re-focus and clear on every open instead, so that summoning it
     * never resurrects the last search and the caret is always where you are
     * about to type.
     */
    onVisibleChanged: {
        if (visible) {
            searchInput.text = "";
            searchInput.forceActiveFocus();
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            root.closeCallback();
        }
    }

    Rectangle {
        id: frame
        opacity: 1
        //anchors.fill: parent
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        implicitHeight: 350
        implicitWidth: 600
        color: "transparent"
        layer.enabled: true

        Rectangle {
            id: startMenuFrame
            anchors.fill: parent
            property color glassColor: Config.colors.glassTintColor
            color: Qt.rgba(glassColor.r, glassColor.g, glassColor.b, 0.2)
            border.width: 1
            border.color: Config.colors.outline

            radius: Config.settings.defaultWindowRadius
            Item {
                id: content
                anchors.fill: startMenuFrame
                anchors.margins: 10
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        implicitHeight: 48
                        color: Config.colors.base
                        border.width: 0
                        radius: 6
                        clip: true
                        Smallicons.BackgroundPatternOne {
                            lineColor: Config.colors.patternLineColor
                            x: -320
                            y: -10
                            property real scale: 0.36
                            width: implicitWidth * scale
                            height: implicitHeight * scale
                        }
                        TextField {
                            id: searchInput
                            width: parent.width
                            anchors.centerIn: parent
                            text: ""
                            font.pixelSize: 17
                            font.family: fontRecia.name
                            font.weight: 700
                            color: Config.colors.accent
                            selectionColor: Config.colors.textLight

                            padding: 2
                            selectByMouse: true
                            cursorVisible: false
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            focus: true

                            background: Rectangle {
                                color: "transparent"
                            }

                            Keys.onEscapePressed: {
                                root.closeCallback();
                            }

                            Component.onCompleted: {
                                searchInput.forceActiveFocus();
                            }
                            onAccepted: {
                                if (root.currentApps.length >= 1) {
                                    root.currentApps[0].execute();
                                    root.closeCallback();
                                }
                            }
                            onTextChanged: {
                                root.currentApps = Utils.AppSearch.fuzzyQuery(searchInput.text);
                                //console.log(Utils.AppSearch.fuzzyQuery(searchInput.text)[0].name);
                            }
                        }
                        Rectangle {
                            implicitHeight: parent.height
                            implicitWidth: parent.height
                            topLeftRadius: 6
                            bottomLeftRadius: 6
                            Layout.alignment: Qt.AlignLeft
                            color: Config.colors.base
                            border.width: 0
                            Text {
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                anchors.fill: parent
                                font.family: iconFont.name
                                color: Config.colors.accent
                                font.pixelSize: 24
                                text: "\ue8b6"
                            }
                        }
                    }

                    Rectangle {
                        implicitWidth: parent.width
                        Layout.fillHeight: true
                        Layout.topMargin: 3
                        color: Config.colors.base
                        border.width: 0
                        radius: 6
                        clip: true
                        Smallicons.BackgroundPatternOne {
                            lineColor: Config.colors.patternLineColor
                            x: -650
                            y: -30
                            property real scale: 0.46
                            width: implicitWidth * scale
                            height: implicitHeight * scale
                        }
                        ListView {
                            // A view that scrolls says so — see ../SynScrollBar.qml.
                            ScrollBar.vertical: SynScrollBar {}
                            id: appsView
                            model: root.currentApps

                            anchors.fill: parent
                            anchors.margins: 8
                            anchors.bottomMargin: 1

                            flickableDirection: Flickable.VerticalFlick
                            boundsBehavior: Flickable.DragOverBounds
                            maximumFlickVelocity: 3500
                            clip: true

                            spacing: 9

                            delegate: Item {
                                width: parent.width
                                height: 48
                                Rectangle {
                                    width: parent.width
                                    height: 48
                                    opacity: 1
                                    anchors.fill: parent
                                    color: Config.colors.base
                                    radius: 4
                                    border.width: 1
                                    border.color: mouse.hovered ? Config.colors.accent : Config.colors.highlight
                                    Behavior on border.color {
                                        ColorAnimation {
                                            duration: 64
                                        }
                                    }
                                    Behavior on opacity {
                                        NumberAnimation {
                                            duration: 64
                                        }
                                    }
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 0
                                        property var iconPath: Utils.AppSearch.getIcon(modelData.icon)
                                        Image {
                                            Layout.leftMargin: 8
                                            asynchronous: true
                                            Layout.maximumWidth: 38
                                            Layout.maximumHeight: 38
                                            antialiasing: true
                                            source: parent.iconPath
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 12
                                            Layout.alignment: Qt.AlignLeft
                                            color: mouse.hovered ? Config.colors.accent : Config.colors.textLight
                                            text: modelData.name
                                            font.family: fontQuilon.name
                                            font.weight: 200
                                            font.pixelSize: 15
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent

                                        HoverHandler {
                                            id: mouse
                                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                            cursorShape: Qt.PointingHandCursor
                                        }
                                        onReleased: {
                                            modelData.execute();
                                            root.closeCallback();
                                        }
                                    }

                                    Rectangle {
                                        height: parent.height
                                        width: parent.height
                                        anchors.right: parent.right
                                        //color: Config.favoriteApps[modelData.name] == null ? "white" : "yellow"
                                        color: "transparent"
                                        z: 100

                                        Text {
                                            anchors.fill: parent
                                            font.family: iconFont.name
                                            // THE ONLY WAY TO ADD A FAVOURITE is
                                            // this star, and its un-favourited
                                            // state was `text` — #121212 in four
                                            // of the five palettes, on an
                                            // appLauncherBackground of #252525.
                                            // 1.3 contrast: the control was there
                                            // and invisible, so Favorite Apps
                                            // looked like a menu that was simply
                                            // broken and empty. Unlike the taskbar
                                            // (see Config.qml `barText`) this
                                            // background is a solid dark panel and
                                            // no wallpaper shows through it, so
                                            // light ink is right unconditionally.
                                            color: Config.favoriteApps[modelData.name] != null || favoritesHovered.hovered ? Config.colors.accent : Config.colors.textLight
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            font.weight: 700
                                            font.pixelSize: 30
                                            text: ""
                                        }

                                        MouseArea {

                                            anchors.fill: parent
                                            HoverHandler {
                                                id: favoritesHovered
                                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                                cursorShape: Qt.PointingHandCursor
                                            }
                                            onReleased: {
                                                Config.toggleFavoriteApp(modelData.name, modelData.command, modelData.icon);
                                            }
                                        }
                                    }
                                }
                            }

                            ScrollIndicator.horizontal: ScrollIndicator {
                                active: appsView.moving
                            }
                        }
                    }
                }
            }
        }
    }
}
