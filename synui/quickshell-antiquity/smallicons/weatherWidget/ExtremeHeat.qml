// Generated from SVG file extreme_heat.svg
import QtQuick
import QtQuick.VectorImage
import QtQuick.VectorImage.Helpers
import QtQuick.Shapes
import QtQuick.Effects

Item {
    implicitWidth: 768
    implicitHeight: 655
    component AnimationsInfo : QtObject
    {
        property bool paused: false
        property int loops: 1
        signal restart()
    }
    property AnimationsInfo animations : AnimationsInfo {}
    transform: [
        Scale { xScale: width / 216.886; yScale: height / 184.923 }
    ]
    objectName: "svg1"
    id: _qt_node0
    transformOrigin: Item.TopLeft
    Item { // Structure node
        objectName: "g225"
        id: _qt_node11
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node11_transform_base_group
            Translate { x: 8.93803; y: -37.8189}
        }
        Shape {
            objectName: "path225"
            id: _qt_node12
            transformOrigin: Item.TopLeft
            opacity: 0.875872
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node12_fill_stroke
                objectName: "svg_path:path225"
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: 99.5051
                    y1: 30.7006
                    x2: 99.5051
                    y2: 189.379
                    GradientStop { position: 0; color: "#ffffb1a0" }
                    GradientStop { position: 0.6505728; color: "#00ff3600" }
                }
                fillTransform: PlanarTransform.fromAffineMatrix(1.36683, 0, 0, 1.28026, -36.5018, -10.5991)
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 100.384 37.8189 L -8.93803 222.617 L 207.948 222.742 L 100.384 37.8189 " }
            }
        }
    }
}
