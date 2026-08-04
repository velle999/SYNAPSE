import QtQuick
import QtQuick.Shapes
import Quickshell
import Quickshell.Io
import ".."

/*
 * The pizza slice.
 *
 * A widget with exactly one job: you click it and it opens pizza near you. It
 * is the only thing on this desktop that is not information, and that is the
 * point — everything else here reports something (the clock, the meters, the
 * spectrum) or holds something you wrote (the note). This is a button shaped
 * like lunch.
 *
 * NO CHROME. Every other widget is a HUD card because a card is what makes a
 * number readable across a room; a slice of pizza in a chamfered panel labelled
 * PIZZA is a joke explaining itself. So this one is the artwork and nothing
 * else: `chrome: false` drops the panel, the shadow, the scanlines and the
 * header, and the slice sits on the wallpaper as a sticker. It carries its own
 * dark outline, which is why it still reads on a light wallpaper with no panel
 * behind it.
 *
 * IT DOES NOT MOVE AT REST, and that is deliberate rather than unfinished. A
 * breathing slice would look better in a screenshot and would recomposite the
 * whole output for as long as the widget is on — the exact cost the visualiser
 * documents and is opt-in because of. Nothing here animates until the pointer
 * is on it, so a switched-on pizza is as free as a wallpaper.
 *
 * `interactive`, with the price QuickLaunch and PostIt already pay: while this
 * is on, clicks inside its rectangle go to it rather than the desktop
 * underneath, so the desktop right-click menu is unreachable there. Unavoidable
 * for anything meant to be clicked. The card is kept tight around the artwork so
 * the bite taken out of the desktop is as small as the slice.
 */
WidgetFrame {
    id: root

    widgetId: "pizza"
    shown: WidgetState.pizza
    accent: Theme.yellow
    chrome: false
    interactive: true

    /*
     * Above the clock in the bottom-right, which is the same arithmetic BigClock
     * itself does for the visualiser and for the same reason: a widget's home is
     * expressed relative to whatever else is already claiming that corner, so
     * turning two on does not stack them. `clockClearance` is the clock's card
     * plus a gap; `visualizerClearance` matches the number BigClock hops by, so
     * the pair move together.
     *
     * Only while nobody has dragged it — WidgetFrame stops applying home margins
     * the moment there is a stored position, so a slice put somewhere on purpose
     * stays put when the clock is switched on underneath it.
     */
    readonly property int visualizerClearance: 100
    readonly property int clockClearance: 124

    homeEdgeH: "right"; homeEdgeV: "bottom"
    homeMarginX: 26
    homeMarginY: 24 + (WidgetState.visualizer ? visualizerClearance : 0)
                    + (WidgetState.clock      ? clockClearance      : 0)

    readonly property int sliceSize: 154
    // Clear space around the artwork, and it is load-bearing twice over: the
    // slice grows past its own box on hover, and the glow has to reach zero
    // BEFORE the card's edge. A layer surface clips at its own boundary, so a
    // halo wider than the card is not a soft edge, it is a straight cut with a
    // seam down it — which is what this looked like at cardWidth == sliceSize.
    readonly property int halo: 16
    // WidgetFrame puts the grip 4px down from the card's top edge and it is 18
    // tall, so 24 is the first row the artwork can start on without the dots
    // landing on the crust. Every strip here is transparent: what anyone sees
    // is the slice and its own margin.
    readonly property int gripStrip: 24
    readonly property int captionStrip: 18

    cardWidth: sliceSize + halo * 2
    bodyHeight: gripStrip + sliceSize + captionStrip

    /*
     * Where a click goes.
     *
     * The default is a maps search rather than any one chain's site: "local"
     * here means wherever the machine actually is, and the map is the only thing
     * in the stack that knows. It is opened with xdg-open, so it lands in
     * whatever the user has set as their browser — the same handoff every other
     * link on this desktop makes.
     *
     * One line of override at ~/.config/synui/pizza.url, because the honest
     * version of this widget for someone who has a pizza place is a shortcut to
     * THAT place, not to a search for it. Plain text with `#` comments, like
     * postit.txt is plain text: a file a human is expected to edit should not
     * have a format.
     */
    readonly property string defaultUrl:
        "https://www.google.com/maps/search/pizza+near+me/"

    property string url: defaultUrl

    // What the caption says the click will do. The host is enough to tell a
    // configured slice from a default one at a glance, and short enough to fit.
    readonly property string destination: {
        if (root.url === root.defaultUrl) return "pizza near me"
        const m = /^[a-z]+:\/\/(?:www\.)?([^/?#]+)/i.exec(root.url)
        return m ? m[1] : "pizza"
    }

    property bool opening: false

    FileView {
        path: Quickshell.env("HOME") + "/.config/synui/pizza.url"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: {
            for (const raw of String(this.text()).split("\n")) {
                const line = raw.trim()
                if (line === "" || line.startsWith("#")) continue
                // Anything that is not a URL would be handed to xdg-open as a
                // path, which is a different and much less obvious thing to do
                // than nothing. A file with junk in it falls back to the search.
                root.url = /^https?:\/\//i.test(line) ? line : root.defaultUrl
                return
            }
            root.url = root.defaultUrl
        }
        // No file is the normal case: it means nobody has named a pizza place.
        onLoadFailed: root.url = root.defaultUrl
    }

    /*
     * The glow — an oven behind the slice, under the pointer only.
     *
     * A real gradient rather than WidgetFrame's stacked-outline trick, and that
     * is not an inconsistency: that trick works because it stacks a STROKE, so
     * each pass lands somewhere the last one did not. Stacking filled discs
     * instead makes every pass overlap every smaller one, and the result reads
     * as a dartboard — three visible rings, which is what the first cut of this
     * actually looked like. A fill needs a gradient.
     *
     * Still no QtQuick.Effects: this is one gradient-filled circle drawn by the
     * same Shapes renderer the widget frames already use, not an offscreen
     * layer for the artwork to be resampled through. It is entirely transparent
     * at rest, so a switched-on pizza nobody is pointing at costs a static
     * texture and nothing else.
     */
    Shape {
        id: glow
        anchors.centerIn: slice
        // Exactly the card, so its outermost stop — which is fully transparent
        // — is the pixel the surface clips at. Anything wider comes back as a
        // seam.
        width: root.cardWidth
        height: width
        readonly property real r: width / 2

        preferredRendererType: Shape.CurveRenderer
        opacity: mouse.containsMouse ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }

        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.r; centerY: glow.r
                focalX: centerX; focalY: centerY
                centerRadius: glow.r
                // Held near full strength through the middle and taken to
                // nothing well before the edge: a straight falloff from the
                // centre leaves a faint disc with a visible rim, which is the
                // ring problem again in one pass.
                GradientStop { position: 0.00; color: Qt.rgba(1.0, 0.60, 0.15, 0.42) }
                GradientStop { position: 0.45; color: Qt.rgba(1.0, 0.52, 0.12, 0.26) }
                GradientStop { position: 1.00; color: Qt.rgba(1.0, 0.45, 0.10, 0.0) }
            }
            PathAngleArc {
                centerX: glow.r; centerY: glow.r
                radiusX: glow.r; radiusY: glow.r
                startAngle: 0; sweepAngle: 360
            }
        }
    }

    Image {
        id: slice
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.gripStrip - (mouse.containsMouse ? 4 : 0)
        width: root.sliceSize
        height: root.sliceSize

        // Relative, not /usr/share/synui/…: this resolves against the QML file
        // either way, so the widget works from the packaged tree and from a
        // source tree run with `quickshell -p` without either path being a
        // special case. That is why the artwork ships beside the .qml rather
        // than in data/ with the compositor's own assets.
        source: "pizza.png"
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        smooth: true
        // 512px of artwork drawn at 154 is a 3x downscale, which aliases the
        // cheese without this. The extra resolution is what the hover scale
        // spends.
        mipmap: true

        scale: mouse.pressed ? 0.95 : (mouse.containsMouse ? 1.07 : 1.0)
        Behavior on scale { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutBack } }
        Behavior on y     { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutQuad } }
    }

    // Says what the click does, and only while there is a pointer to read it.
    // A permanent label under a pizza would be the caption of a joke.
    Text {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        horizontalAlignment: Text.AlignHCenter
        // Elided, because a host is as long as somebody's domain and the
        // surface is only as wide as the slice — an over-wide label is not
        // clipped by the card, it is clipped by the WINDOW, so it loses
        // characters off BOTH ends and reads as neither. There is no wrap here
        // and the width is a real one, so this is not the elide-against-an-
        // anchored-height case that paints nothing.
        elide: Text.ElideRight
        text: root.opening ? "opening…" : root.destination
        color: root.opening ? Theme.yellow : Theme.fg
        font.family: Theme.fontFamily
        font.pixelSize: 10
        font.letterSpacing: 1.2
        opacity: (mouse.containsMouse || root.opening) ? 0.9 : 0.0
        Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
        // Cheap stand-in for a shadow so the word survives a bright wallpaper,
        // the same trick BigClock uses on the time.
        style: Text.Outline
        styleColor: Qt.rgba(0, 0, 0, 0.55)
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            open.command = ["xdg-open", root.url]
            open.running = true
            root.opening = true
            settle.restart()
        }
    }

    Process { id: open }

    // The browser takes a moment to show itself, and a slice that reacted to a
    // click by doing nothing visible for two seconds reads as a broken one.
    Timer {
        id: settle
        interval: 2000
        onTriggered: root.opening = false
    }
}
