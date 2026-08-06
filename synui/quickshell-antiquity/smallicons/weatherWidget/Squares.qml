// Generated from SVG file squares.svg
import QtQuick
import QtQuick.VectorImage
import QtQuick.VectorImage.Helpers
import QtQuick.Shapes
import QtQuick.Effects

Item {
    implicitWidth: 502
    implicitHeight: 502
    property string lineColor: '#000000'
    property string centerColor: '#2e2e2e'
    property string leftAndRightColor: '#332421'
    property string topColor: '#ff4236'
    property string bottomColor: '#ceb56b'
    component AnimationsInfo : QtObject
    {
        property bool paused: false
        property int loops: 1
        signal restart()
    }
    property AnimationsInfo animations : AnimationsInfo {}
    transform: [
        Scale { xScale: width / 141.921; yScale: height / 141.921 }
    ]
    objectName: "svg1"
    id: _qt_node0
    transformOrigin: Item.TopLeft
    Item { // Structure node
        objectName: "layer1"
        id: _qt_node1
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node1_transform_base_group
            Translate { x: -28.9448; y: -100.571}
        }
        Shape {
            objectName: "rect26"
            id: _qt_node2
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node2_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.707107, -0.707107, 0.707107, 0.707107, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node2_fill_stroke
                objectName: "svg_path:rect26"
                strokeColor: lineColor
                strokeWidth: 0
                capStyle: ShapePath.SquareCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                cosmeticStroke: true
                fillColor: leftAndRightColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M -0.647285 141.935 L -100.647 141.935 L -100.647 241.935 L -0.647285 241.935 L -0.647285 141.935 M -26.0157 167.303 L -26.0157 216.565 L -75.2767 216.564 L -75.277 167.303 L -26.0157 167.303 " }
            }
        }
        Shape {
            objectName: "rect27"
            id: _qt_node3
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node3_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.707107, -0.707107, 0.707107, 0.707107, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node3_fill_stroke
                objectName: "svg_path:rect27"
                strokeColor: "transparent"
                fillColor: bottomColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M -75.277 191.957 L -100.67 191.957 L -100.67 241.958 L -50.6698 241.958 L -50.6698 216.565 L -75.2767 216.564 L -75.277 191.957 " }
            }
        }
        Shape {
            objectName: "rect28"
            id: _qt_node4
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node4_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.707107, -0.707107, 0.707107, 0.707107, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node4_fill_stroke
                objectName: "svg_path:rect28"
                strokeColor: "transparent"
                fillColor: topColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M -0.646188 141.934 L -50.6464 141.934 L -50.6464 167.303 L -26.0157 167.303 L -26.0157 191.934 L -0.646188 191.934 L -0.646188 141.934 " }
            }
        }
        Shape {
            objectName: "rect4"
            id: _qt_node5
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node5_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.707107, -0.707107, 0.707107, 0.707107, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node5_fill_stroke
                objectName: "svg_path:rect4"
                strokeColor: lineColor
                strokeWidth: 0.5
                capStyle: ShapePath.SquareCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M -75.2768 167.303 L -26.0157 167.303 L -26.0157 216.564 L -75.2768 216.564 L -75.2768 167.303 " }
            }
        }
        Shape {
            objectName: "rect424"
            id: _qt_node6
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node6_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.707107, -0.707107, 0.707107, 0.707107, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node6_fill_stroke
                objectName: "svg_path:rect424"
                strokeColor: lineColor
                strokeWidth: 0.5
                capStyle: ShapePath.SquareCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M -100.647 141.935 L -0.64743 141.935 L -0.64743 241.935 L -100.647 241.935 L -100.647 141.935 " }
            }
        }
    }
}
