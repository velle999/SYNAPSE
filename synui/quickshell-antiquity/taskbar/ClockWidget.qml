import QtQuick
import ".."

Text {
    text: Time.time
    color: Config.colors.text
    font.pixelSize: 11
    font.family: Config.fontMono
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
}
