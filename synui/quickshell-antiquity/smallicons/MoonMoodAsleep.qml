// Generated from SVG file moon_mood_asleep.svg
import QtQuick
import QtQuick.VectorImage
import QtQuick.VectorImage.Helpers
import QtQuick.Shapes
import QtQuick.Effects
import ".."

Item {
    implicitWidth: 707
    implicitHeight: 699
    property string backgroundColor: Config.colors.cbodyMoonBackground
    property string lineColor: "#000000"
    component AnimationsInfo : QtObject
    {
        property bool paused: false
        property int loops: 1
        signal restart()
    }
    property AnimationsInfo animations : AnimationsInfo {}
    transform: [
        Scale { xScale: width / 199.77; yScale: height / 197.538 }
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
            Translate { x: 10.4538; y: -46.0065}
        }
        Shape {
            objectName: "path1"
            id: _qt_node2
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node2_fill_stroke
                objectName: "svg_path:path1"
                strokeColor: lineColor
                strokeWidth: 3
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: backgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 111.19 51.9324 Q 151.502 62.1141 173.119 96.5886 Q 194.735 131.063 184.993 169.636 Q 175.25 208.208 139.856 228.283 Q 104.462 248.358 64.15 238.176 Q 41.8674 232.549 24.1474 218.205 Q 41.7518 222.507 61.6238 220.093 Q 80.6031 217.788 95.8078 210.166 Q 97.3581 209.388 99.5469 204.712 Q 102.064 199.335 104.008 199.071 Q 108.962 198.399 112.857 195.715 Q 115.707 193.752 120.423 188.704 Q 121.136 187.941 120.345 185.971 Q 119.951 184.99 119.93 184.573 Q 119.896 183.883 120.553 183.557 Q 121.402 183.136 121.889 182.518 Q 122.377 181.899 122.297 181.342 Q 122.267 181.131 122.191 180.743 Q 122.003 179.778 122.166 179.413 Q 122.423 178.837 123.633 178.527 L 124.845 178.25 Q 129.195 177.311 131.032 175.575 Q 131.37 175.256 131.109 174.352 Q 130.956 173.819 130.413 172.637 Q 129.977 171.687 129.88 171.347 Q 129.026 168.346 121.924 165.585 Q 118.262 164.161 116.951 163.244 Q 114.781 161.725 114.96 159.612 Q 115.138 159.23 115.345 158.467 Q 115.877 156.507 117.372 155.331 Q 118.033 154.81 120.71 153.85 Q 123.424 152.876 124.223 152.315 L 127.415 150.099 Q 135.41 144.58 138.293 141.992 Q 143.142 137.638 142.065 135.199 L 140.596 131.639 Q 139.554 128.987 138.819 127.687 Q 137.689 125.689 135.393 122.966 Q 134.787 122.247 132.404 120.341 Q 129.811 118.267 128.896 117.286 Q 127.348 115.625 127.921 114.759 Q 129.091 112.993 131.582 109.945 Q 134.444 106.443 135.226 104.821 Q 136.49 102.2 135.417 99.9339 Q 128.11 84.5134 112.312 73.605 Q 111.477 73.0282 109.047 72.1921 Q 106.608 71.3531 105.748 70.7693 Q 104.457 69.8931 101.855 67.9399 Q 95.075 62.8495 91.5574 61.7976 Q 87.4616 60.5727 79.7056 56.7347 Q 70.5957 52.2268 65.4682 50.9317 L 64.9859 50.8106 Q 88.1114 46.1035 111.19 51.9324 " }
            }
        }
        Shape {
            objectName: "path39"
            id: _qt_node3
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node3_fill_stroke
                objectName: "svg_path:path39"
                strokeColor: "transparent"
                fillColor: "#36000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 68.5226 50.922 Q 90.1861 52.6186 108.508 57.33 Q 127.384 62.1839 137.216 68.8644 Q 146.273 75.0188 158.362 90.9505 Q 169.402 105.499 170.025 109.705 Q 170.123 110.367 170.843 113.318 Q 174.345 127.677 175.232 138.208 Q 176.866 157.59 171.392 170.708 Q 163.534 189.54 148.908 206.024 L 143.71 212.061 Q 138.246 218.803 119.744 226.052 Q 102.799 232.69 97.0595 232.225 L 91.3311 231.911 Q 62.316 230.529 49.555 227.099 Q 33.6632 222.827 28.0242 220.263 L 35.2011 226.928 L 55.3649 235.984 L 78.9463 240.94 L 104.066 240.598 L 129.014 233.763 L 146.444 224.023 L 162.335 210.694 L 174.468 195.315 L 183.866 174.81 L 187.455 158.747 L 187.967 138.754 L 185.575 124.742 L 180.619 109.705 L 171.563 93.6419 L 157.893 77.5792 L 146.444 68.3517 L 131.235 58.7825 L 108.338 51.2638 L 91.5913 48.8715 L 75.1869 49.2132 L 68.5226 50.922 " }
            }
        }
        Shape {
            objectName: "path40"
            id: _qt_node4
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node4_stroke
                objectName: "svg_stroke_path:path40"
                strokeColor: lineColor
                strokeWidth: 2
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.BevelJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 136.661 183.396 Q 133.67 186.002 133.286 186.088 L 133.414 189.719 " }
            }
        }
        Shape {
            objectName: "path41"
            id: _qt_node5
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node5_stroke
                objectName: "svg_stroke_path:path41"
                strokeColor: lineColor
                strokeWidth: 2
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.BevelJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 124.817 165.597 Q 128.744 164.027 130.798 164.329 Q 131.161 165.054 130.859 166.624 Q 130.67 167.606 130.021 168.603 Q 129.696 169.102 129.409 169.404 " }
            }
        }
        Shape {
            objectName: "path42"
            id: _qt_node6
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node6_stroke
                objectName: "svg_stroke_path:path42"
                strokeColor: lineColor
                strokeWidth: 2
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.BevelJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 131.644 175.687 Q 135.269 173.61 137.414 170.272 Q 138.486 168.603 138.834 167.349 " }
            }
        }
        Shape {
            objectName: "path43"
            id: _qt_node7
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node7_stroke
                objectName: "svg_stroke_path:path43"
                strokeColor: lineColor
                strokeWidth: 2
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.BevelJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 149.027 171.62 Q 147.038 172.979 146.458 175.605 Q 146.112 177.174 146.184 180.629 Q 146.257 184.114 145.879 185.591 Q 145.25 188.042 143.084 189.055 " }
            }
        }
        Shape {
            objectName: "path45"
            id: _qt_node8
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node8_stroke
                objectName: "svg_stroke_path:path45"
                strokeColor: lineColor
                strokeWidth: 1.5
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 141.613 114.793 Q 143.187 113.957 144.307 114.094 Q 145.033 114.182 146.476 114.98 Q 147.303 115.436 147.804 115.677 Q 155.713 113.568 160.312 115.135 Q 162.056 115.73 165.714 116.422 Q 169.279 117.096 169.783 117.599 " }
            }
        }
        Shape {
            objectName: "path64"
            id: _qt_node9
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node9_fill
                objectName: "svg_fill_path:path64"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 153.725 191.95 Q 154.782 192.952 154.824 194.17 Q 154.866 195.388 153.88 196.421 Q 152.893 197.454 151.202 197.962 Q 149.511 198.471 147.671 198.288 Q 145.831 198.105 144.447 197.291 Q 143.063 196.477 142.589 195.299 L 153.725 191.95 " }
            }
        }
        Shape {
            objectName: "path65"
            id: _qt_node10
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node10_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.887095, -0.461588, 0.461588, 0.887095, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node10_fill_stroke
                objectName: "svg_path:path65"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M -14.1209 239.984 Q -14.1209 241.5 -15.1312 242.808 Q -16.1415 244.116 -17.886 244.859 Q -21.651 246.464 -25.3457 244.768 Q -27.0575 243.983 -28.0116 242.651 Q -28.9656 241.318 -28.9011 239.804 L -21.5129 239.984 L -14.1209 239.984 " }
            }
        }
        Shape {
            objectName: "path66"
            id: _qt_node11
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node11_fill_stroke
                objectName: "svg_path:path66"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 137.779 73.3716 Q 139.619 74.1952 140.384 75.9777 Q 141.149 77.7602 140.435 79.5615 Q 139.721 81.3629 137.905 82.2329 Q 136.089 83.1029 134.128 82.5828 L 135.541 77.8315 L 137.779 73.3716 " }
            }
        }
        Shape {
            objectName: "path67"
            id: _qt_node12
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node12_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.983492, -0.180953, 0.180953, 0.983492, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node12_fill_stroke
                objectName: "svg_path:path67"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 129.823 176.166 Q 132.588 176.918 134.381 179.22 Q 136.173 181.521 136.293 184.472 Q 136.412 187.423 134.811 189.87 Q 133.211 192.318 130.516 193.305 Q 127.821 194.293 125.085 193.434 Q 122.349 192.576 120.641 190.207 L 127.611 184.846 L 129.823 176.166 " }
            }
        }
        Shape {
            objectName: "path68"
            id: _qt_node13
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node13_fill
                objectName: "svg_fill_path:path68"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 122.102 208.99 Q 126.238 215.987 138.336 214.174 L 122.102 208.99 " }
            }
        }
        Shape {
            objectName: "path69"
            id: _qt_node14
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node14_fill
                objectName: "svg_fill_path:path69"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 145.017 91.9368 Q 150.22 95.6785 150.758 99.7945 Q 151.027 101.852 150.255 103.162 L 145.017 91.9368 " }
            }
        }
        Shape {
            objectName: "path70"
            id: _qt_node15
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node15_fill
                objectName: "svg_fill_path:path70"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 140.992 144.29 Q 142.387 147.637 139.603 150.322 Q 139.073 150.833 139.372 150.866 Q 139.67 150.898 141.993 150.583 L 142.984 150.449 Q 143.884 150.328 145.647 150.281 Q 147.814 150.225 148.051 149.914 Q 148.416 149.435 146.105 147.834 L 140.992 144.29 " }
            }
        }
        Shape {
            objectName: "path71"
            id: _qt_node16
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node16_fill
                objectName: "svg_fill_path:path71"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 122.367 154.155 Q 125.674 156.139 127.725 156.867 L 122.367 154.155 " }
            }
        }
        Shape {
            objectName: "path72"
            id: _qt_node17
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node17_fill
                objectName: "svg_fill_path:path72"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 126.865 151.84 Q 128.982 153.427 131.231 154.486 L 126.865 151.84 " }
            }
        }
        Shape {
            objectName: "path73"
            id: _qt_node18
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node18_fill
                objectName: "svg_fill_path:path73"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 130.701 148.996 L 135.332 151.708 L 130.701 148.996 " }
            }
        }
        Shape {
            objectName: "path74"
            id: _qt_node19
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node19_fill
                objectName: "svg_fill_path:path74"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 133.016 147.077 Q 136.919 148.466 140.028 148.202 L 133.016 147.077 " }
            }
        }
        Shape {
            objectName: "path75"
            id: _qt_node20
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node20_fill
                objectName: "svg_fill_path:path75"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 136.125 143.77 Q 139.697 145.556 142.078 145.622 L 136.125 143.77 " }
            }
        }
        Shape {
            objectName: "path76"
            id: _qt_node21
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node21_fill
                objectName: "svg_fill_path:path76"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 140.028 141.124 Q 144.16 142.927 145.452 142.712 L 140.028 141.124 " }
            }
        }
        Shape {
            objectName: "path77"
            id: _qt_node22
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node22_fill
                objectName: "svg_fill_path:path77"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 129.974 149.326 Q 131.958 150.525 135.067 150.939 Q 136.621 151.145 137.779 151.112 L 129.974 149.326 " }
            }
        }
        Shape {
            objectName: "path78"
            id: _qt_node23
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node23_fill
                objectName: "svg_fill_path:path78"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 124.947 152.303 Q 126.022 153.75 128.7 154.759 Q 130.04 155.263 131.164 155.478 L 124.947 152.303 " }
            }
        }
        Shape {
            objectName: "path79"
            id: _qt_node24
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node24_fill
                objectName: "svg_fill_path:path79"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 128.32 150.12 Q 129.519 151.608 131.851 152.634 Q 133.016 153.146 133.943 153.361 L 128.32 150.12 " }
            }
        }
        Shape {
            objectName: "path80"
            id: _qt_node25
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node25_fill
                objectName: "svg_fill_path:path80"
                strokeColor: "transparent"
                fillColor: "#3b000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 120.978 154.75 Q 122.632 156.28 125.178 157.057 Q 126.452 157.446 127.394 157.528 L 120.978 154.75 " }
            }
        }
        Shape {
            objectName: "path81"
            id: _qt_node26
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node26_stroke
                objectName: "svg_stroke_path:path81"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 120.647 153.956 Q 122.921 155.197 126.278 156.487 Q 127.956 157.132 129.18 157.528 " }
            }
        }
        Shape {
            objectName: "path82"
            id: _qt_node27
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node27_stroke
                objectName: "svg_stroke_path:path82"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 124.285 153.361 L 129.908 155.478 " }
            }
        }
        Shape {
            objectName: "path83"
            id: _qt_node28
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node28_stroke
                objectName: "svg_stroke_path:path83"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 127.923 151.245 L 130.569 152.435 " }
            }
        }
        Shape {
            objectName: "path84"
            id: _qt_node29
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node29_stroke
                objectName: "svg_stroke_path:path84"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 126.137 152.104 L 129.246 153.295 " }
            }
        }
        Shape {
            objectName: "path85"
            id: _qt_node30
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node30_stroke
                objectName: "svg_stroke_path:path85"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 130.106 149.26 L 133.347 150.186 " }
            }
        }
        Shape {
            objectName: "path86"
            id: _qt_node31
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node31_stroke
                objectName: "svg_stroke_path:path86"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 133.149 147.077 L 136.191 148.4 " }
            }
        }
        Shape {
            objectName: "path87"
            id: _qt_node32
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node32_stroke
                objectName: "svg_stroke_path:path87"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 136.721 145.358 L 139.962 146.217 " }
            }
        }
        Shape {
            objectName: "path88"
            id: _qt_node33
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node33_stroke
                objectName: "svg_stroke_path:path88"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 138.308 142.778 L 141.351 143.77 " }
            }
        }
        Shape {
            objectName: "path89"
            id: _qt_node34
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node34_stroke
                objectName: "svg_stroke_path:path89"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 140.359 140.529 L 143.468 141.852 " }
            }
        }
        Shape {
            objectName: "path90"
            id: _qt_node35
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node35_stroke
                objectName: "svg_stroke_path:path90"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 124.748 55.3992 Q 124.418 57.3836 123.491 59.8971 Q 123.028 61.1539 122.632 62.0138 " }
            }
        }
        Shape {
            objectName: "path91"
            id: _qt_node36
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node36_stroke
                objectName: "svg_stroke_path:path91"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 131.363 58.3096 Q 130.834 61.4846 129.643 64.395 " }
            }
        }
        Shape {
            objectName: "path92"
            id: _qt_node37
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node37_stroke
                objectName: "svg_stroke_path:path92"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 139.83 62.1461 L 136.258 69.819 " }
            }
        }
        Shape {
            objectName: "path93"
            id: _qt_node38
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node38_stroke
                objectName: "svg_stroke_path:path93"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 134.67 60.9555 Q 134.538 64.2628 133.48 66.2471 " }
            }
        }
        Shape {
            objectName: "path94"
            id: _qt_node39
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node39_stroke
                objectName: "svg_stroke_path:path94"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 143.931 66.3794 Q 141.946 69.819 139.83 72.2003 " }
            }
        }
        Shape {
            objectName: "path95"
            id: _qt_node40
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node40_stroke
                objectName: "svg_stroke_path:path95"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 142.343 74.0523 L 147.734 70.712 L 148.958 69.9513 " }
            }
        }
        Shape {
            objectName: "path96"
            id: _qt_node41
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node41_stroke
                objectName: "svg_stroke_path:path96"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 146.973 77.2273 L 154.382 73.92 " }
            }
        }
        Shape {
            objectName: "path97"
            id: _qt_node42
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node42_stroke
                objectName: "svg_stroke_path:path97"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 152.53 80.27 L 157.16 78.1534 " }
            }
        }
        Shape {
            objectName: "path98"
            id: _qt_node43
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node43_stroke
                objectName: "svg_stroke_path:path98"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 155.44 83.445 Q 156.101 83.3624 158.416 82.8001 L 160.599 82.2544 " }
            }
        }
        Shape {
            objectName: "path99"
            id: _qt_node44
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node44_stroke
                objectName: "svg_stroke_path:path99"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 157.821 86.3555 L 163.774 85.5617 L 165.23 85.4294 " }
            }
        }
        Shape {
            objectName: "path100"
            id: _qt_node45
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node45_stroke
                objectName: "svg_stroke_path:path100"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 120.25 55.7961 Q 120.78 56.4575 126.071 61.0878 " }
            }
        }
        Shape {
            objectName: "path101"
            id: _qt_node46
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node46_stroke
                objectName: "svg_stroke_path:path101"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 128.188 58.8388 L 131.76 64.2628 " }
            }
        }
        Shape {
            objectName: "path102"
            id: _qt_node47
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node47_stroke
                objectName: "svg_stroke_path:path102"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 133.48 61.4846 L 135.728 65.5857 " }
            }
        }
        Shape {
            objectName: "path103"
            id: _qt_node48
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node48_stroke
                objectName: "svg_stroke_path:path103"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 136.919 62.6753 L 139.036 67.9669 " }
            }
        }
        Shape {
            objectName: "path104"
            id: _qt_node49
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node49_stroke
                objectName: "svg_stroke_path:path104"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 141.549 65.0565 Q 141.549 65.5857 143.269 71.5388 " }
            }
        }
        Shape {
            objectName: "path105"
            id: _qt_node50
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node50_stroke
                objectName: "svg_stroke_path:path105"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 146.18 68.4961 L 146.973 73.92 " }
            }
        }
        Shape {
            objectName: "path106"
            id: _qt_node51
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node51_stroke
                objectName: "svg_stroke_path:path106"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 150.148 72.068 L 150.545 77.6242 " }
            }
        }
        Shape {
            objectName: "path107"
            id: _qt_node52
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node52_stroke
                objectName: "svg_stroke_path:path107"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 154.646 74.9784 L 154.382 81.3284 " }
            }
        }
        Shape {
            objectName: "path108"
            id: _qt_node53
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node53_stroke
                objectName: "svg_stroke_path:path108"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 158.615 78.9471 L 157.689 84.3711 " }
            }
        }
        Shape {
            objectName: "path109"
            id: _qt_node54
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node54_stroke
                objectName: "svg_stroke_path:path109"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 162.319 82.9159 L 160.864 88.2075 " }
            }
        }
        Shape {
            objectName: "path110"
            id: _qt_node55
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node55_stroke
                objectName: "svg_stroke_path:path110"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 74.7651 235.808 L 75.2438 236.799 Q 76.2552 238.905 76.7148 239.377 Q 77.2526 239.93 78.7875 240.298 " }
            }
        }
        Shape {
            objectName: "path111"
            id: _qt_node56
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node56_stroke
                objectName: "svg_stroke_path:path111"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 84.7743 236.089 Q 83.9558 237.609 84.9848 239.199 Q 85.4993 239.994 86.1775 240.485 " }
            }
        }
        Shape {
            objectName: "path112"
            id: _qt_node57
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node57_stroke
                objectName: "svg_stroke_path:path112"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 90.8547 236.369 Q 90.1531 237.363 90.7611 238.93 Q 91.0652 239.713 91.5095 240.298 " }
            }
        }
        Shape {
            objectName: "path113"
            id: _qt_node58
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node58_stroke
                objectName: "svg_stroke_path:path113"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 96.2803 236.556 Q 95.6956 237.55 96.3036 239.117 Q 96.6077 239.901 97.0286 240.485 " }
            }
        }
        Shape {
            objectName: "path114"
            id: _qt_node59
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node59_stroke
                objectName: "svg_stroke_path:path114"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 101.332 236.556 Q 100.572 237.843 100.876 238.918 Q 101.028 239.456 101.332 239.737 " }
            }
        }
        Shape {
            objectName: "path115"
            id: _qt_node60
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node60_stroke
                objectName: "svg_stroke_path:path115"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 79.91 236.556 Q 79.6177 237.667 80.5765 239.14 Q 81.0559 239.877 81.5938 240.392 " }
            }
        }
        Shape {
            objectName: "path116"
            id: _qt_node61
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node61_stroke
                objectName: "svg_stroke_path:path116"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 104.419 236.276 Q 104.536 237.328 106.219 238.217 Q 107.061 238.661 107.88 238.895 " }
            }
        }
        Shape {
            objectName: "path117"
            id: _qt_node62
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node62_stroke
                objectName: "svg_stroke_path:path117"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 110.873 234.685 Q 111.399 235.913 113.013 236.732 Q 113.82 237.141 114.521 237.305 " }
            }
        }
        Shape {
            objectName: "path118"
            id: _qt_node63
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node63_stroke
                objectName: "svg_stroke_path:path118"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 115.27 233.75 Q 116.614 234.685 118.789 235.153 Q 119.877 235.387 120.695 235.434 " }
            }
        }
        Shape {
            objectName: "path119"
            id: _qt_node64
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node64_stroke
                objectName: "svg_stroke_path:path119"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 151.284 202.413 Q 153.517 205.171 154.091 206.166 Q 154.526 206.922 154.371 207.464 " }
            }
        }
        Shape {
            objectName: "path120"
            id: _qt_node65
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node65_stroke
                objectName: "svg_stroke_path:path120"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 156.149 203.442 Q 153.155 204.938 150.91 206.903 " }
            }
        }
        Shape {
            objectName: "path121"
            id: _qt_node66
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node66_stroke
                objectName: "svg_stroke_path:path121"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 149.881 144.93 Q 149.835 145.433 149.646 145.501 Q 149.593 145.52 149.61 145.583 Q 149.63 145.654 149.788 146.005 " }
            }
        }
        Shape {
            objectName: "path122"
            id: _qt_node67
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node67_stroke
                objectName: "svg_stroke_path:path122"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 151.284 145.21 Q 151.319 145.472 151.254 145.774 Q 151.213 145.964 151.249 146.047 Q 151.303 146.171 151.565 146.333 " }
            }
        }
        Shape {
            objectName: "path123"
            id: _qt_node68
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node68_stroke
                objectName: "svg_stroke_path:path123"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 153.904 145.772 Q 154.077 146.014 154.177 146.29 Q 154.237 146.456 154.311 146.517 Q 154.417 146.604 154.745 146.707 " }
            }
        }
        Shape {
            objectName: "path124"
            id: _qt_node69
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node69_stroke
                objectName: "svg_stroke_path:path124"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 157.084 145.818 Q 157.253 146.089 157.368 146.437 Q 157.444 146.666 157.524 146.743 Q 157.643 146.858 157.973 146.941 " }
            }
        }
        Shape {
            objectName: "path125"
            id: _qt_node70
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node70_stroke
                objectName: "svg_stroke_path:path125"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 160.405 145.678 Q 160.647 145.901 160.696 146.197 Q 160.725 146.377 160.826 146.45 Q 160.983 146.564 161.481 146.66 " }
            }
        }
        Shape {
            objectName: "path126"
            id: _qt_node71
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node71_stroke
                objectName: "svg_stroke_path:path126"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 163.726 145.491 Q 164.213 145.83 164.47 146.126 Q 164.561 146.231 164.629 146.26 Q 164.713 146.296 164.989 146.333 " }
            }
        }
        Shape {
            objectName: "path127"
            id: _qt_node72
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node72_stroke
                objectName: "svg_stroke_path:path127"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 166.485 144.696 L 166.763 144.898 Q 167.206 145.221 167.348 145.289 Q 167.513 145.367 167.842 145.397 " }
            }
        }
        Shape {
            objectName: "path131"
            id: _qt_node73
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node73_stroke
                objectName: "svg_stroke_path:path131"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 143.427 148.063 Q 143.943 148.977 145.154 149.426 Q 145.678 149.62 145.611 149.82 Q 145.522 150.087 144.456 150.963 " }
            }
        }
        Shape {
            objectName: "path132"
            id: _qt_node74
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node74_stroke
                objectName: "svg_stroke_path:path132"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 150.381 150.243 Q 150.933 150.57 151.883 151.314 Q 153.179 152.331 153.985 152.677 Q 155.319 153.25 157.072 153.101 " }
            }
        }
        Shape {
            objectName: "path133"
            id: _qt_node75
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node75_stroke
                objectName: "svg_stroke_path:path133"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 103.577 209.428 Q 105.093 209.508 105.86 209.925 Q 106.202 210.11 106.541 210.068 Q 107.012 210.011 108.441 209.522 " }
            }
        }
        Shape {
            objectName: "path134"
            id: _qt_node76
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node76_stroke
                objectName: "svg_stroke_path:path134"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 122.519 192.965 Q 123.427 193.469 123.811 194.015 Q 123.963 194.232 124.144 194.264 Q 124.371 194.304 125.232 194.227 Q 126.782 194.09 127.816 193.9 Q 128.805 193.719 129.629 193.432 " }
            }
        }
    }
    Shape {
        preferredRendererType: Shape.CurveRenderer
        objectName: "g153"
        id: _qt_node78
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node78_transform_base_group
            Translate { x: 10.4538; y: -46.0065}
        }
        ShapePath {
            id: _qt_node79_fill_stroke
            objectName: "svg_path:path147"
            strokeColor: "transparent"
            fillColor: lineColor
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
            PathSvg { path: "M 149.099 126.772 Q 149.366 127.31 152.081 127.131 Q 154.568 126.967 156.077 127.053 Q 158.707 127.202 160.922 128.03 Q 164.383 129.324 170.309 133.135 Q 171.578 133.951 171.937 134.152 Q 173.588 135.077 174.469 135.283 Q 175.835 135.602 174.342 133.773 Q 172.682 131.738 168.134 128.6 Q 163.093 125.12 159.924 124.292 Q 158.925 124.031 155.664 124.22 Q 151.637 124.454 150.058 125.506 Q 149.523 125.862 149.258 126.353 Q 149.125 126.598 149.099 126.772 " }
        }
        ShapePath {
            id: _qt_node81_fill
            objectName: "svg_fill_path:path149"
            strokeColor: "transparent"
            fillColor: lineColor
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
            PathSvg { path: "M 152.143 143.654 Q 156.88 144.472 161.249 143.795 L 161.719 143.723 Q 164.529 143.298 167.057 142.797 Q 170.731 142.068 172.804 141.361 Q 174.195 140.887 173.926 141.713 Q 173.792 142.127 173.379 142.635 Q 172.558 142.977 169.672 143.863 L 166.685 144.754 L 160.643 145.721 L 155.236 145.781 L 150.735 145.086 Q 149.289 144.617 149.2 144.085 Q 149.126 143.647 149.923 143.325 Q 150.669 143.024 151.404 143.088 Q 152.205 143.157 152.143 143.654 " }
        }
        ShapePath {
            id: _qt_node83_stroke
            objectName: "svg_stroke_path:path151"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 122.179 182.478 Q 124.729 183.105 129.177 184.574 L 133.115 185.917 " }
        }
        ShapePath {
            id: _qt_node84_stroke
            objectName: "svg_stroke_path:path152"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 123.863 178.315 Q 123.983 178.758 125.13 179.193 Q 126.905 179.865 127.658 180.872 Q 128.863 182.483 131.144 184.336 Q 132.285 185.262 133.185 185.867 " }
        }
        ShapePath {
            id: _qt_node85_stroke
            objectName: "svg_stroke_path:path153"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 120.215 186.219 Q 122.87 187.104 124.878 186.214 Q 127.384 185.105 130.489 185.48 Q 132.042 185.668 133.094 186.078 " }
        }
        ShapePath {
            id: _qt_node86_stroke
            objectName: "svg_stroke_path:path154"
            strokeColor: lineColor
            strokeWidth: 0.65
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 153.555 137.321 Q 156.812 137.434 157.522 137.63 Q 157.831 137.715 160.665 137.585 Q 163.614 137.451 165.828 138.294 Q 167.105 138.78 168.636 139.901 " }
        }
    }
    Shape {
        preferredRendererType: Shape.CurveRenderer
        objectName: "g172"
        id: _qt_node88
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node88_transform_base_group
            Translate { x: 10.4538; y: -46.0065}
        }
        ShapePath {
            id: _qt_node89_stroke
            objectName: "svg_stroke_path:path155"
            strokeColor: lineColor
            strokeWidth: 2.25099
            capStyle: ShapePath.FlatCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 187.424 144.776 Q 187.424 184.86 159.049 213.383 Q 130.673 241.905 90.2913 242.411 Q 49.9094 242.918 20.8168 215.116 Q -8.27589 187.315 -9.29633 147.243 Q -10.3168 107.172 17.3237 77.9473 Q 44.9641 48.7224 85.3202 47.2034 Q 125.676 45.6843 155.467 72.7474 Q 185.259 99.8104 187.299 139.843 " }
        }
        ShapePath {
            id: _qt_node90_stroke
            objectName: "svg_stroke_path:path156"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.FlatCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 14.5298 208.281 Q 19.1211 214.413 22.0454 217.053 Q 26.4181 220.999 29.0596 221.663 " }
        }
        ShapePath {
            id: _qt_node91_stroke
            objectName: "svg_stroke_path:path157"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 18.6078 203.478 L 85.0643 203.478 " }
        }
        ShapePath {
            id: _qt_node92_stroke
            objectName: "svg_stroke_path:path158"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 11.358 190.719 L 31.1741 190.719 " }
        }
        ShapePath {
            id: _qt_node93_stroke
            objectName: "svg_stroke_path:path159"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 42.2072 190.719 L 100.904 190.719 " }
        }
        ShapePath {
            id: _qt_node94_stroke
            objectName: "svg_stroke_path:path160"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 3.24671 177.315 L 58.099 177.315 " }
        }
        ShapePath {
            id: _qt_node95_stroke
            objectName: "svg_stroke_path:path161"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 68.01 177.315 L 111.413 177.315 " }
        }
        ShapePath {
            id: _qt_node96_stroke
            objectName: "svg_stroke_path:path162"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 41.2072 144.776 L 116.198 144.906 " }
        }
        ShapePath {
            id: _qt_node97_stroke
            objectName: "svg_stroke_path:path163"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 30.6728 144.841 L -4.61374 144.841 " }
        }
        ShapePath {
            id: _qt_node98_stroke
            objectName: "svg_stroke_path:path164"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M -1.14992 129.747 L 68.2065 129.747 " }
        }
        ShapePath {
            id: _qt_node99_stroke
            objectName: "svg_stroke_path:path165"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 87.0559 129.747 L 124.513 129.747 " }
        }
        ShapePath {
            id: _qt_node100_stroke
            objectName: "svg_stroke_path:path166"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 5.73739 106.663 L 49.7195 106.663 " }
        }
        ShapePath {
            id: _qt_node101_stroke
            objectName: "svg_stroke_path:path167"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 68.2065 106.663 L 117.626 106.663 " }
        }
        ShapePath {
            id: _qt_node102_stroke
            objectName: "svg_stroke_path:path168"
            strokeColor: lineColor
            strokeWidth: 2.05717
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 3.72906 96.9967 L 98.8395 96.9967 " }
        }
        ShapePath {
            id: _qt_node103_stroke
            objectName: "svg_stroke_path:path169"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 108.926 96.9967 L 123.063 96.9967 " }
        }
        ShapePath {
            id: _qt_node104_stroke
            objectName: "svg_stroke_path:path170"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 28.0242 75.7395 L 69.5177 75.7395 " }
        }
        ShapePath {
            id: _qt_node105_stroke
            objectName: "svg_stroke_path:path171"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 76.6946 75.7395 L 95.4914 75.7395 " }
        }
        ShapePath {
            id: _qt_node106_stroke
            objectName: "svg_stroke_path:path172"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 46.9616 64.9741 L 79.0015 64.9741 " }
        }
        ShapePath {
            id: _qt_node107_stroke
            objectName: "svg_stroke_path:path188"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 8.1993 176.711 Q 12.5214 173.449 16.0045 169.302 L 16.1513 169.127 L 16.7983 168.376 Q 18.9248 166.053 21.9576 165.201 Q 26.1858 164.014 31.2181 165.995 Q 34.0007 167.091 36.3774 168.905 " }
        }
        ShapePath {
            id: _qt_node108_stroke
            objectName: "svg_stroke_path:path189"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 31.0799 177.53 Q 31.3948 176.848 31.9218 176.314 Q 32.1916 176.04 32.8002 175.569 L 33.0443 175.378 Q 34.8613 173.943 35.8506 172.291 Q 36.0596 171.942 36.4298 171.218 L 36.599 170.888 Q 37.5513 169.052 38.5634 168.362 Q 39.1497 167.963 40.5117 167.526 L 40.5278 167.521 L 40.6982 167.466 Q 42.0125 167.043 42.6794 166.866 Q 45.4144 166.138 47.7211 166.367 Q 49.0304 166.497 50.2007 166.936 Q 51.4405 167.403 52.408 168.175 Q 54.8598 170.134 55.7756 174.069 Q 56.0446 175.225 56.1497 176.407 " }
        }
        ShapePath {
            id: _qt_node109_stroke
            objectName: "svg_stroke_path:path190"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 28.6477 164.995 Q 29.6088 163.711 30.8928 162.75 Q 33.6057 160.719 36.8338 160.647 Q 40.2475 160.571 42.7729 162.656 Q 43.2875 163.081 43.7043 163.563 Q 44.1415 164.069 44.4567 164.621 Q 44.8123 165.243 45.018 165.93 " }
        }
        ShapePath {
            id: _qt_node110_stroke
            objectName: "svg_stroke_path:path191"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 48.8533 143.573 Q 49.3489 140.484 51.3237 137.902 Q 53.2985 135.319 56.1497 134.032 Q 57.7838 133.294 59.8531 132.926 Q 61.507 132.632 63.7268 132.535 Q 66.3846 132.419 68.0241 132.777 Q 70.4038 133.296 71.7716 134.78 Q 72.3365 135.393 72.8006 136.277 Q 73.3184 137.263 73.8296 138.896 Q 74.5799 141.293 75.0457 143.76 " }
        }
        ShapePath {
            id: _qt_node111_stroke
            objectName: "svg_stroke_path:path192"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 73.9232 144.696 Q 74.5632 143.714 75.3263 142.825 Q 80.7247 136.534 89.4515 136.277 Q 90.8033 136.237 92.2579 136.37 Q 94.684 136.594 96.4267 137.265 Q 97.4649 137.665 98.3289 138.241 Q 99.2557 138.858 99.9285 139.644 Q 101.797 141.828 101.612 144.696 " }
        }
        ShapePath {
            id: _qt_node112_stroke
            objectName: "svg_stroke_path:path193"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 71.0233 134.032 Q 73.7762 130.745 78.0391 130.29 Q 79.8515 130.096 81.3132 130.571 Q 82.1093 130.829 83.5704 131.654 Q 84.4261 132.137 84.8679 132.348 L 85.432 132.605 Q 86.5432 133.097 87.0194 133.47 Q 88.4312 134.577 88.4225 136.37 " }
        }
        ShapePath {
            id: _qt_node113_stroke
            objectName: "svg_stroke_path:path196"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 15.0785 82.1221 Q 20.1407 81.8123 24.6554 84.123 Q 29.1702 86.4337 31.8795 90.7211 Q 33.55 93.3645 34.2608 96.4096 " }
        }
        ShapePath {
            id: _qt_node114_stroke
            objectName: "svg_stroke_path:path197"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 29.1014 86.62 Q 30.6065 85.5549 32.4087 85.1648 Q 32.6794 85.1062 33.2256 85.0141 L 33.8639 84.9003 Q 34.5735 84.76 35.9611 84.3477 Q 37.2011 83.9792 37.8326 83.8419 Q 38.9045 83.6089 40.0704 83.5458 Q 41.1629 83.4868 42.3306 83.5773 Q 47.6148 83.9872 50.6649 87.0169 Q 52.1554 88.4974 52.9139 90.4565 " }
        }
        ShapePath {
            id: _qt_node115_stroke
            objectName: "svg_stroke_path:path198"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 46.167 97.0711 Q 47.4408 94.24 49.8712 92.3086 Q 54.9224 88.2944 61.2483 89.9273 Q 63.4179 90.4874 64.6878 91.6471 Q 65.024 91.9541 65.3317 92.3342 Q 65.6165 92.686 65.8785 93.1023 Q 66.7284 94.4534 67.0691 96.0128 " }
        }
        ShapePath {
            id: _qt_node116_stroke
            objectName: "svg_stroke_path:path199"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 67.2014 93.8961 L 77.1233 96.0128 " }
        }
        ShapePath {
            id: _qt_node117_stroke
            objectName: "svg_stroke_path:path200"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 14.6154 176.115 Q 15.3944 173.52 17.9227 172.543 Q 18.1629 172.451 18.6558 172.299 Q 19.3245 172.093 19.6425 171.948 Q 20.1055 171.737 20.5024 171.419 " }
        }
        ShapePath {
            id: _qt_node118_stroke
            objectName: "svg_stroke_path:path201"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 23.8759 170.559 L 27.5139 171.088 L 27.5181 171.089 Q 27.7814 171.125 27.9108 171.154 Q 28.6278 171.319 29.1014 171.882 " }
        }
        ShapePath {
            id: _qt_node119_stroke
            objectName: "svg_stroke_path:path202"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 25.7279 174.065 L 29.4983 176.446 " }
        }
        ShapePath {
            id: _qt_node120_stroke
            objectName: "svg_stroke_path:path203"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 38.5602 176.115 L 41.4707 171.287 " }
        }
        ShapePath {
            id: _qt_node121_stroke
            objectName: "svg_stroke_path:path204"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 43.9842 170.559 L 48.7467 170.824 " }
        }
        ShapePath {
            id: _qt_node122_stroke
            objectName: "svg_stroke_path:path205"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 50.5326 173.073 L 52.9139 175.52 " }
        }
        ShapePath {
            id: _qt_node123_stroke
            objectName: "svg_stroke_path:path206"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 42.7936 176.38 Q 42.8998 175.562 43.4496 174.946 Q 43.9994 174.331 44.8007 174.133 Q 45.6019 173.935 46.3748 174.225 Q 47.1477 174.514 47.6222 175.189 Q 48.0005 175.728 48.0852 176.38 " }
        }
        ShapePath {
            id: _qt_node124_stroke
            objectName: "svg_stroke_path:path207"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 31.8795 165.4 Q 32.3154 164.83 32.9965 164.525 Q 33.6775 164.22 34.3931 164.275 L 34.5633 164.292 Q 34.7148 164.308 34.7909 164.312 Q 34.9014 164.318 34.9909 164.311 Q 35.0974 164.302 35.1868 164.275 Q 35.3628 164.222 35.538 164.079 Q 35.6515 163.987 35.826 163.795 L 35.845 163.775 Q 36.0224 163.58 36.1199 163.494 Q 36.2832 163.349 36.4436 163.283 Q 36.7743 163.147 37.105 163.283 " }
        }
        ShapePath {
            id: _qt_node125_stroke
            objectName: "svg_stroke_path:path208"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 39.4201 164.077 L 42.066 166.656 " }
        }
        ShapePath {
            id: _qt_node126_stroke
            objectName: "svg_stroke_path:path209"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 53.7176 143.854 Q 52.8901 141.762 53.7632 139.688 Q 54.6363 137.615 56.711 136.745 Q 58.0249 136.193 60.2657 136.183 Q 61.4787 136.177 62.6978 136.277 " }
        }
        ShapePath {
            id: _qt_node127_stroke
            objectName: "svg_stroke_path:path210"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 59.3302 143.667 Q 59.2204 143.185 59.3995 142.725 Q 59.5786 142.264 59.9851 141.983 Q 60.1949 141.838 60.6728 141.654 Q 60.9023 141.565 61.014 141.515 Q 61.2279 141.419 61.6336 141.184 Q 61.9043 141.026 62.043 140.954 Q 63.4857 140.203 65.1713 140.406 Q 66.7682 140.598 68.1234 141.609 Q 69.2931 142.481 69.9943 143.76 " }
        }
        ShapePath {
            id: _qt_node128_stroke
            objectName: "svg_stroke_path:path211"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 66.2525 136.651 L 69.7137 137.961 " }
        }
        ShapePath {
            id: _qt_node129_stroke
            objectName: "svg_stroke_path:path212"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 82.2486 143.573 Q 82.2441 142.753 82.5292 141.983 Q 82.8884 141.014 83.6752 140.241 Q 84.4326 139.496 85.4291 139.083 Q 86.0624 138.821 86.7387 138.709 " }
        }
        ShapePath {
            id: _qt_node130_stroke
            objectName: "svg_stroke_path:path213"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 88.6096 138.99 L 94.69 141.235 " }
        }
        ShapePath {
            id: _qt_node131_stroke
            objectName: "svg_stroke_path:path214"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 96.8415 141.609 L 98.6189 143.947 " }
        }
        ShapePath {
            id: _qt_node132_stroke
            objectName: "svg_stroke_path:path215"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 83.4647 137.68 Q 82.629 135.648 80.6122 134.776 Q 78.5955 133.904 76.5424 134.686 Q 76.0097 134.89 75.2328 135.341 Q 74.4695 135.785 73.7361 136.277 " }
        }
        ShapePath {
            id: _qt_node133_stroke
            objectName: "svg_stroke_path:path216"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 73.549 139.738 Q 73.7339 139.232 74.1576 138.899 Q 74.5814 138.567 75.1168 138.507 Q 75.6523 138.448 76.1387 138.679 Q 76.6251 138.911 76.9166 139.364 Q 77.1378 139.708 77.1972 140.112 " }
        }
        ShapePath {
            id: _qt_node134_stroke
            objectName: "svg_stroke_path:path217"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 11.9033 86.4177 Q 13.3206 84.7159 15.5304 84.8632 Q 17.7402 85.0105 18.9191 86.8854 L 19.0218 87.0544 Q 19.1742 87.3093 19.2644 87.427 Q 19.4926 87.7249 19.761 87.8208 Q 19.9393 87.8846 20.1897 87.8736 Q 20.289 87.8692 20.4866 87.8452 L 20.622 87.8295 Q 20.8837 87.8019 21.0553 87.8297 Q 21.2961 87.8687 21.4448 88.0079 Q 21.586 88.1401 21.6952 88.4659 L 21.7085 88.5058 Q 21.8118 88.8167 21.9125 88.9434 Q 22.0635 89.1333 22.3003 89.1859 Q 22.5372 89.2386 22.7544 89.1304 " }
        }
        ShapePath {
            id: _qt_node135_stroke
            objectName: "svg_stroke_path:path218"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 25.3737 90.8142 L 29.9573 95.9592 " }
        }
        ShapePath {
            id: _qt_node136_stroke
            objectName: "svg_stroke_path:path219"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 22.3802 96.4269 Q 20.178 95.5619 18.8256 93.6206 L 18.6546 93.3683 Q 18.4454 93.0551 18.3305 92.9059 Q 18.0239 92.5075 17.703 92.311 Q 17.2655 92.0429 16.7531 92.0685 Q 16.2407 92.0942 15.8321 92.4045 " }
        }
        ShapePath {
            id: _qt_node137_stroke
            objectName: "svg_stroke_path:path220"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 7.69378 91.469 Q 11.0392 92.0279 13.4935 94.3689 Q 14.216 95.058 14.8031 95.8656 " }
        }
        ShapePath {
            id: _qt_node138_stroke
            objectName: "svg_stroke_path:path221"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 33.4185 90.6272 Q 34.3869 89.6145 35.6635 89.0369 Q 36.6435 88.5935 37.7951 88.4231 Q 38.8615 88.2654 40.023 88.3486 Q 41.9107 88.4838 44.3632 89.3175 Q 46.4822 90.0379 47.6372 90.9078 Q 47.8962 91.1029 48.3862 91.5285 Q 48.934 92.0043 49.2275 92.2174 Q 49.6705 92.5391 50.1629 92.7787 " }
        }
        ShapePath {
            id: _qt_node139_stroke
            objectName: "svg_stroke_path:path222"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 48.0114 95.3044 Q 45.1298 93.5929 41.8375 92.9658 L 40.1911 92.7016 Q 38.9775 92.5249 38.3763 92.4045 L 38.1898 92.3664 Q 37.5693 92.2386 37.2538 92.2174 Q 36.2464 92.1497 35.3591 92.6314 Q 34.4718 93.1131 33.9797 93.9947 " }
        }
        ShapePath {
            id: _qt_node140_stroke
            objectName: "svg_stroke_path:path223"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 50.0694 93.527 L 56.8046 96.1463 " }
        }
        ShapePath {
            id: _qt_node141_stroke
            objectName: "svg_stroke_path:path224"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 54.7466 94.9302 Q 55.246 93.5127 56.4047 92.5555 Q 57.5633 91.5984 59.0496 91.3755 Q 60.126 91.2141 61.5753 91.4691 Q 62.3811 91.6108 63.1656 91.8432 " }
        }
        ShapePath {
            id: _qt_node142_stroke
            objectName: "svg_stroke_path:path225"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 60.6399 95.8656 Q 60.6793 95.9678 60.7715 96.0269 Q 60.8637 96.0861 60.973 96.0793 Q 61.0824 96.0725 61.1665 96.0025 Q 61.2507 95.9325 61.2773 95.8262 Q 61.3039 95.7199 61.2625 95.6185 Q 61.2212 95.5171 61.1279 95.4597 Q 61.0346 95.4023 60.9255 95.4111 Q 60.8162 95.4199 60.7334 95.4914 Q 60.6531 95.5608 60.6274 95.6637 Q 60.6017 95.7667 60.6399 95.8656 Q 60.6955 96.0098 60.8472 96.0701 Q 60.9873 96.1258 61.1409 96.0774 Q 61.2109 96.0554 61.2749 96.0122 Q 61.3364 95.9707 61.3871 95.9133 Q 61.4354 95.8587 61.4729 95.791 Q 61.5083 95.7271 61.5328 95.6538 Q 61.6052 95.4368 61.5834 95.0606 L 61.5767 94.9563 Q 61.5548 94.6261 61.5753 94.4624 Q 61.6112 94.1767 61.7722 93.9179 Q 61.9285 93.6666 62.1698 93.4922 Q 62.4022 93.3244 62.6935 93.2402 Q 62.9733 93.1593 63.2726 93.1652 Q 63.557 93.1708 63.8486 93.2536 Q 64.1236 93.3316 64.3904 93.4739 Q 64.8594 93.7241 65.3171 94.1818 Q 65.6229 94.4876 65.8783 94.8366 " }
        }
        ShapePath {
            id: _qt_node143_stroke
            objectName: "svg_stroke_path:path226"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 12.9323 128.887 Q 13.4876 125.684 15.8121 123.411 Q 18.1365 121.138 21.3512 120.655 Q 21.7924 120.589 22.6829 120.515 Q 23.9237 120.412 24.5318 120.281 Q 25.6839 120.033 27.8086 118.989 L 29.0219 118.41 Q 29.9598 117.989 30.7588 117.764 Q 31.7135 117.496 32.5766 117.474 Q 34.3349 117.43 36.2031 118.353 Q 36.9887 118.742 37.8175 119.318 Q 38.5005 119.793 39.3118 120.468 Q 43.652 124.078 46.6082 128.887 " }
        }
        ShapePath {
            id: _qt_node144_stroke
            objectName: "svg_stroke_path:path227"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 20.9771 130.009 Q 21.2164 127.628 22.9871 126.018 Q 24.7579 124.409 27.151 124.397 Q 27.805 124.393 29.1042 124.592 Q 30.668 124.831 31.454 124.771 Q 31.9775 124.73 33.0023 124.492 L 33.3169 124.419 Q 34.4787 124.156 35.1958 124.21 Q 35.7549 124.251 36.3134 124.465 Q 36.8412 124.667 37.3294 125.006 Q 38.1945 125.608 38.9376 126.642 Q 39.627 127.601 40.0601 128.7 " }
        }
        ShapePath {
            id: _qt_node145_stroke
            objectName: "svg_stroke_path:path228"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 29.3961 128.887 L 37.2538 128.326 " }
        }
        ShapePath {
            id: _qt_node146_stroke
            objectName: "svg_stroke_path:path229"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 42.3052 122.152 Q 42.3288 122.292 42.4185 122.41 Q 42.5083 122.527 42.6377 122.587 Q 42.7671 122.646 42.9148 122.638 Q 43.0624 122.63 43.1846 122.557 Q 43.3068 122.484 43.3833 122.357 Q 43.4599 122.231 43.4681 122.089 Q 43.4764 121.946 43.4149 121.812 Q 43.3535 121.677 43.2406 121.59 Q 43.0974 121.48 42.917 121.47 Q 42.7372 121.459 42.5816 121.552 Q 42.4267 121.645 42.351 121.809 Q 42.2753 121.973 42.3052 122.152 Q 42.3388 122.352 42.4926 122.5 Q 42.6361 122.639 42.8432 122.689 Q 43.2187 122.779 43.6173 122.55 Q 43.7788 122.458 43.9387 122.31 Q 44.0671 122.192 44.2158 122.017 L 44.3884 121.808 Q 44.613 121.531 44.7373 121.403 Q 45.0105 121.123 45.3618 120.911 Q 45.6996 120.707 46.0869 120.58 Q 46.4565 120.459 46.8646 120.409 Q 47.2513 120.362 47.6618 120.38 Q 48.9211 120.437 50.7242 121.216 Q 56.9197 123.895 61.014 129.261 " }
        }
        ShapePath {
            id: _qt_node147_stroke
            objectName: "svg_stroke_path:path230"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 45.4857 125.706 Q 47.7556 124.685 50.2261 124.988 Q 52.6966 125.29 54.653 126.829 Q 55.4909 127.488 56.1497 128.326 " }
        }
    }
}
