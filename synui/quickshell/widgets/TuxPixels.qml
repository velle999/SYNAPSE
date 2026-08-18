import QtQuick
import "tuxart.js" as TuxArt

/*
 * TuxPixels — one sprite from TuxArt, drawn at a whole-number zoom.
 *
 * One Canvas per sprite, repainted only when the picture changes. A Repeater of
 * Rectangles was the obvious alternative and is 288 scene-graph nodes for one
 * penguin, live on every frame whether or not anything moved; this is one
 * texture redrawn twice a second.
 *
 * `zoom`, NOT `scale`: Item already has a scale, it is the transform, and a
 * property of that name here would be a silent fight with it every time the pet
 * grew up.
 *
 * Plain QtQuick and TuxArt, nothing else — no Theme, no Quickshell. That is
 * what lets tests/tux_screen.qml render the pet with the `qml` tool and no
 * compositor at all; see TuxScreen.qml, which keeps the same rule.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
Canvas {
    id: px

    property var rows: []
    property int zoom: 4
    property var tint: null              // a mood's palette, or null for Tux's
    property bool flip: false

    implicitWidth:  (rows && rows.length) ? rows[0].length * zoom : 0
    implicitHeight: (rows && rows.length) ? rows.length * zoom : 0
    width: implicitWidth
    height: implicitHeight

    // Immediate and Image: these canvases are a few hundred pixels each and
    // there are twenty of them on screen. A threaded renderer per sprite would
    // be twenty threads to draw a heart.
    renderTarget: Canvas.Image
    renderStrategy: Canvas.Immediate

    onRowsChanged:    requestPaint()
    onTintChanged:    requestPaint()
    onFlipChanged:    requestPaint()
    onZoomChanged:    requestPaint()
    onVisibleChanged: if (visible) requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        if (!px.rows || px.rows.length === 0) return
        const w = px.rows[0].length
        for (let y = 0; y < px.rows.length; y++) {
            const line = px.rows[y]
            let x = 0
            while (x < line.length) {
                const c = line[x]
                if (c === ".") { x++; continue }
                // Run-length: a row of eight identical pixels is one fillRect
                // rather than eight, which is most of them.
                let run = 1
                while (x + run < line.length && line[x + run] === c) run++
                const col = (px.tint && px.tint[c]) || TuxArt.pal[c]
                if (col) {
                    ctx.fillStyle = col
                    const dx = px.flip ? (w - x - run) : x
                    ctx.fillRect(dx * px.zoom, y * px.zoom, run * px.zoom, px.zoom)
                }
                x += run
            }
        }
    }
}
