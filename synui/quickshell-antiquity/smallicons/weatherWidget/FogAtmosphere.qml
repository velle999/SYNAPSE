// Generated from SVG file fog_atmosphere.svg
import QtQuick
import QtQuick.VectorImage
import QtQuick.VectorImage.Helpers
import QtQuick.Shapes
import QtQuick.Effects

Item {
    implicitWidth: 598
    implicitHeight: 343
    component AnimationsInfo : QtObject
    {
        property bool paused: false
        property int loops: 1
        signal restart()
    }
    property AnimationsInfo animations : AnimationsInfo {}
    transform: [
        Scale { xScale: width / 168.835; yScale: height / 96.9291 }
    ]
    objectName: "svg1"
    id: _qt_node0
    transformOrigin: Item.TopLeft
    Item { // Structure node
        objectName: "layer9"
        id: _qt_node7
        transformOrigin: Item.TopLeft
        opacity: 0.759661
        transform: TransformGroup {
            id: _qt_node7_transform_base_group
            Translate { x: -11.7193; y: -121.677}
        }
        Shape {
            objectName: "path152"
            id: _qt_node8
            transformOrigin: Item.TopLeft
            opacity: 0.875872
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node8_fill_stroke
                objectName: "svg_path:path152"
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: 100.166
                    y1: 81.7716
                    x2: 100.459
                    y2: 229.574
                    GradientStop { position: 0; color: "#d39592a1" }
                    GradientStop { position: 0.5756241; color: "#00444673" }
                }
                fillTransform: PlanarTransform.fromAffineMatrix(1.26656, 0, 0, 1.15382, -27.0378, -15.7552)
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 100.148 37.8189 L 20.1661 182.163 L 178.844 182.26 L 100.148 37.8189 " }
            }
        }
    }
}
