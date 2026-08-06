pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Services.Pipewire

Singleton {
    id: root

    readonly property var sink: Pipewire.defaultAudioSink
    readonly property var audio: sink?.audio ?? null
    readonly property string audioDeviceName: sink?.description || sink?.name || "No Device Detected"

    property real volume: 0.0
    property bool muted: false

    PwObjectTracker {
        objects: [root.sink]
    }

    Connections {
        target: root.audio
        enabled: root.audio !== null

        function onVolumeChanged() {
            if (Math.abs(root.volume - root.audio.volume) > 0.001) {
                root.volume = root.audio.volume;
            }
        }
        function onMutedChanged() {
            if (root.muted !== root.audio.muted) {
                root.muted = root.audio.muted;
            }
        }
    }
    onAudioChanged: {
        if (audio) {
            volume = audio.volume;
            muted = audio.muted;
        }
    }
    onVolumeChanged: {
        if (audio && Math.abs(audio.volume - volume) > 0.001) {
            audio.volume = volume;
        }
    }
    onMutedChanged: {
        if (audio && audio.muted !== muted) {
            audio.muted = muted;
        }
    }
}
