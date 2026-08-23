// synstudio — playback, kept in its own file ON PURPOSE.
//
// This is the only part of the window that imports QtMultimedia, and that
// import is the reason the file exists. A failed `import` does not disable the
// feature that needed it — it fails the WHOLE QML file it appears in, so an
// `import QtMultimedia` at the top of synstudio.qml turns "playback does not
// work" into "the editor does not open", on any machine without
// qt6-multimedia. quickshell does not depend on it, so that is not a
// hypothetical machine; it is every install that did not happen to pull it in
// for something else.
//
// Loaded through a Loader, a missing module leaves this one Item unavailable
// and costs the play button. The darkroom, the timeline, the monitor and the
// export all still work, which is the correct amount of damage.
//
// It also means nothing above has to know QtMultimedia's vocabulary: the enum
// values the caller needs are re-exported as plain ints below, so synstudio.qml
// never names a type it cannot be sure exists.
//
// SynapseOS Project
// SPDX-License-Identifier: GPL-2.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtMultimedia

Item {
    id: pb

    property alias source: mp.source
    readonly property int  position: mp.position          // milliseconds
    readonly property int  duration: mp.duration
    readonly property int  playbackState: mp.playbackState
    readonly property int  mediaStatus: mp.mediaStatus

    // The caller's whole vocabulary, so it never says `MediaPlayer.anything`.
    readonly property int stateStopped:   MediaPlayer.StoppedState
    readonly property int statusLoading:  MediaPlayer.LoadingMedia
    readonly property int statusLoaded:   MediaPlayer.LoadedMedia
    readonly property int statusBuffered: MediaPlayer.BufferedMedia

    signal positionMoved(int ms)
    signal statusMoved(int status)
    signal stateMoved(int state)
    signal failed(string text)

    // Monitoring level, which is NOT the mix: it is how loud this room is,
    // and it must never reach the exported file. Kept here with the player it
    // belongs to rather than anywhere near the timeline's own faders.
    property alias volume: ao.volume
    property alias muted:  ao.muted

    // Shuttle speed. The player's own rate on the rendered preview, so a
    // faster L is still the export, played — not a second renderer with its
    // own opinion about what the cut looks like.
    property alias rate: mp.playbackRate

    function play()  { mp.play() }
    function pause() { mp.pause() }
    // Assignment rather than a setter: `position` is documented read-only and
    // is writable in practice here, and this is the one place that relies on
    // it, so if a Qt release ever takes that away it breaks in a single
    // function with a name that says what it was for.
    function seek(ms) { mp.position = ms }

    MediaPlayer {
        id: mp
        videoOutput: vo
        audioOutput: AudioOutput { id: ao; volume: 1.0 }
        onPositionChanged: pb.positionMoved(mp.position)
        onMediaStatusChanged: pb.statusMoved(mp.mediaStatus)
        onPlaybackStateChanged: pb.stateMoved(mp.playbackState)
        onErrorOccurred: function (err, str) { pb.failed(str) }
    }

    VideoOutput {
        id: vo
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }
}
