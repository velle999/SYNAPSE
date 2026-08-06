// Generated from SVG file moon_mood_awake.svg
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
            objectName: "path38"
            id: _qt_node3
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node3_stroke
                objectName: "svg_stroke_path:path38"
                strokeColor: lineColor
                strokeWidth: 0.6
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.BevelJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 172.968 142.247 Q 175.498 142.323 178.745 141.598 Q 180.369 141.235 181.487 140.857 " }
            }
        }
        Shape {
            objectName: "path39"
            id: _qt_node4
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node4_fill_stroke
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
            id: _qt_node5
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node5_stroke
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
            id: _qt_node6
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node6_stroke
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
            id: _qt_node7
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node7_stroke
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
            id: _qt_node8
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node8_stroke
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
            objectName: "path44"
            id: _qt_node9
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node9_stroke
                objectName: "svg_stroke_path:path44"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.BevelJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 173.457 142.249 Q 175.539 143.104 177.903 144.795 Q 179.085 145.641 179.851 146.315 " }
            }
        }
        Shape {
            objectName: "path45"
            id: _qt_node10
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node10_stroke
                objectName: "svg_stroke_path:path45"
                strokeColor: lineColor
                strokeWidth: 2.5
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 144.774 111.579 Q 144.633 112.359 144.331 113.619 Q 143.882 115.501 144.078 115.982 Q 144.358 116.672 146.102 116.625 Q 147.169 116.596 150.389 115.98 Q 157.709 114.58 160.199 115.429 Q 163.004 116.385 165.571 117.608 Q 168.12 118.821 168.829 119.53 " }
            }
        }
        Shape {
            objectName: "path64"
            id: _qt_node11
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node11_fill
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
            id: _qt_node12
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node12_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.887095, -0.461588, 0.461588, 0.887095, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node12_fill_stroke
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
            id: _qt_node13
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node13_fill_stroke
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
            id: _qt_node14
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node14_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.983492, -0.180953, 0.180953, 0.983492, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node14_fill_stroke
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
            id: _qt_node15
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node15_fill
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
            id: _qt_node16
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node16_fill
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
            id: _qt_node17
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node17_fill
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
            id: _qt_node18
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node18_fill
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
            id: _qt_node19
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node19_fill
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
            id: _qt_node20
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node20_fill
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
            id: _qt_node21
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node21_fill
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
            id: _qt_node22
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node22_fill
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
            id: _qt_node23
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node23_fill
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
            id: _qt_node24
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node24_fill
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
            id: _qt_node25
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node25_fill
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
            id: _qt_node26
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node26_fill
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
            id: _qt_node27
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node27_fill
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
            id: _qt_node28
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node28_stroke
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
            id: _qt_node29
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node29_stroke
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
            id: _qt_node30
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node30_stroke
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
            id: _qt_node31
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node31_stroke
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
            id: _qt_node32
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node32_stroke
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
            id: _qt_node33
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node33_stroke
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
            id: _qt_node34
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node34_stroke
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
            id: _qt_node35
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node35_stroke
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
            id: _qt_node36
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node36_stroke
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
            id: _qt_node37
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node37_stroke
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
            id: _qt_node38
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node38_stroke
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
            id: _qt_node39
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node39_stroke
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
            id: _qt_node40
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node40_stroke
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
            id: _qt_node41
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node41_stroke
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
            id: _qt_node42
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node42_stroke
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
            id: _qt_node43
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node43_stroke
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
            id: _qt_node44
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node44_stroke
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
            id: _qt_node45
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node45_stroke
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
            id: _qt_node46
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node46_stroke
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
            id: _qt_node47
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node47_stroke
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
            id: _qt_node48
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node48_stroke
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
            id: _qt_node49
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node49_stroke
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
            id: _qt_node50
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node50_stroke
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
            id: _qt_node51
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node51_stroke
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
            id: _qt_node52
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node52_stroke
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
            id: _qt_node53
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node53_stroke
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
            id: _qt_node54
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node54_stroke
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
            id: _qt_node55
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node55_stroke
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
            id: _qt_node56
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node56_stroke
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
            id: _qt_node57
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node57_stroke
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
            id: _qt_node58
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node58_stroke
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
            id: _qt_node59
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node59_stroke
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
            id: _qt_node60
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node60_stroke
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
            id: _qt_node61
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node61_stroke
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
            id: _qt_node62
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node62_stroke
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
            id: _qt_node63
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node63_stroke
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
            id: _qt_node64
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node64_stroke
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
            id: _qt_node65
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node65_stroke
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
            id: _qt_node66
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node66_stroke
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
            id: _qt_node67
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node67_stroke
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
            id: _qt_node68
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node68_stroke
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
            id: _qt_node69
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node69_stroke
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
            id: _qt_node70
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node70_stroke
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
            id: _qt_node71
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node71_stroke
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
            id: _qt_node72
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node72_stroke
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
            id: _qt_node73
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node73_stroke
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
            id: _qt_node74
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node74_stroke
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
            objectName: "path128"
            id: _qt_node75
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node75_stroke
                objectName: "svg_stroke_path:path128"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 152.641 134.499 Q 152.761 134.333 152.85 134.16 Q 152.908 134.046 152.97 134.006 Q 153.039 133.961 153.342 133.845 " }
            }
        }
        Shape {
            objectName: "path129"
            id: _qt_node76
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node76_stroke
                objectName: "svg_stroke_path:path129"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 155.026 134.92 Q 155.354 134.756 155.579 134.556 Q 155.688 134.459 155.768 134.449 Q 155.866 134.438 156.195 134.499 " }
            }
        }
        Shape {
            objectName: "path130"
            id: _qt_node77
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node77_stroke
                objectName: "svg_stroke_path:path130"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 157.879 136.09 Q 158.483 135.612 158.768 135.496 Q 158.956 135.42 159.22 135.457 Q 159.395 135.482 159.516 135.482 " }
            }
        }
        Shape {
            objectName: "path131"
            id: _qt_node78
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node78_stroke
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
            id: _qt_node79
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node79_stroke
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
            id: _qt_node80
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node80_stroke
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
            id: _qt_node81
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node81_stroke
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
    Item { // Structure node
        objectName: "layer2"
        id: _qt_node82
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node82_transform_base_group
            Translate { x: 10.4538; y: -46.0065}
        }
        Shape {
            objectName: "path2"
            id: _qt_node83
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node83_fill_stroke
                objectName: "svg_path:path2"
                strokeColor: lineColor
                strokeWidth: 0.804133
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "#bdf8f8ff"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 173.597 140.992 Q 174.953 141.96 170.851 143.489 Q 167.5 144.738 163.292 145.516 Q 160.091 146.107 155.379 145.829 Q 150.215 145.524 149.088 144.321 Q 148.173 143.345 147.702 141.897 Q 147.028 139.828 147.604 137.502 Q 148.099 135.505 149.572 134.262 Q 150.89 133.149 152.214 133.189 Q 157.679 133.352 162.207 134.882 Q 167.588 136.701 173.597 140.992 " }
            }
        }
        Shape {
            objectName: "path3"
            id: _qt_node84
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node84_stroke
                objectName: "svg_stroke_path:path3"
                strokeColor: lineColor
                strokeWidth: 0.649999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 155.809 135.362 Q 156.928 136.555 156.404 138.776 Q 156.015 140.427 155.335 141.235 Q 153.729 143.143 151.166 143.815 Q 149.884 144.15 148.923 144.104 " }
            }
        }
        Shape {
            objectName: "path4"
            id: _qt_node85
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node85_fill_stroke
                objectName: "svg_path:path4"
                strokeColor: "transparent"
                fillColor: lineColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 157.155 135.355 Q 156.79 136.371 156.406 138.141 Q 155.89 140.524 155.162 141.248 Q 153.837 142.564 151.394 143.48 Q 150.173 143.937 149.217 144.132 Q 148.918 143.989 148.586 143.563 Q 147.92 142.711 147.747 141.296 Q 147.516 139.413 147.666 138.073 Q 147.846 136.476 148.58 135.443 Q 150.212 133.145 152.296 133.179 Q 152.434 133.181 152.879 133.152 Q 153.8 133.092 154.325 133.11 Q 155.247 133.141 155.816 133.375 Q 156.654 133.719 156.988 134.572 Q 157.155 134.998 157.155 135.355 " }
            }
        }
        Shape {
            objectName: "path5"
            id: _qt_node86
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node86_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.926589, 0.376074, -0.403474, 0.914991, 0, 0)}
            }
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node86_fill
                objectName: "svg_fill_path:path5"
                strokeColor: "transparent"
                fillColor: "#dbffffff"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 193.685 69.5619 Q 193.685 69.9068 193.467 70.1522 Q 193.249 70.3976 192.939 70.4019 Q 192.629 70.4063 192.405 70.1671 Q 192.182 69.9279 192.174 69.5831 Q 192.166 69.2384 192.378 68.9869 Q 192.591 68.7355 192.901 68.7224 Q 193.211 68.7093 193.44 68.9422 Q 193.669 69.175 193.684 69.5195 L 193.685 69.5619 " }
            }
        }
        Shape {
            objectName: "path6"
            id: _qt_node87
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node87_stroke
                objectName: "svg_stroke_path:path6"
                strokeColor: lineColor
                strokeWidth: 0.649999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 153.584 134.696 Q 154.468 135.875 154.419 137.276 Q 154.363 138.858 153.073 140.053 Q 151.5 141.51 149.504 141.719 Q 148.505 141.824 147.821 141.638 " }
            }
        }
        Shape {
            objectName: "path9"
            id: _qt_node88
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node88_fill_stroke
                objectName: "svg_path:path9"
                strokeColor: lineColor
                strokeWidth: 1
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: backgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 150.01 134.121 Q 157.089 135.419 169.478 140.689 Q 173.338 142.33 173.471 142.156 L 173.608 141.988 Q 173.893 141.646 173.839 141.45 Q 173.757 141.154 173.009 140.618 Q 167.483 136.663 161.987 135.021 Q 158.723 134.046 155.778 133.609 Q 153.818 133.317 152.133 133.284 Q 151.17 133.265 150.494 133.691 Q 150.156 133.904 150.01 134.121 " }
            }
        }
        Shape {
            objectName: "path11"
            id: _qt_node89
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node89_stroke
                objectName: "svg_stroke_path:path11"
                strokeColor: lineColor
                strokeWidth: 0.649999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 154.542 136.674 L 155.857 136.834 L 156.55 136.915 " }
            }
        }
        Shape {
            objectName: "path12"
            id: _qt_node90
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node90_stroke
                objectName: "svg_stroke_path:path12"
                strokeColor: lineColor
                strokeWidth: 0.649999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 154.3 138.532 L 155.249 139.068 L 155.871 139.423 " }
            }
        }
        Shape {
            objectName: "path13"
            id: _qt_node91
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node91_stroke
                objectName: "svg_stroke_path:path13"
                strokeColor: lineColor
                strokeWidth: 0.649999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 153.59 139.755 L 155.282 140.918 " }
            }
        }
        Shape {
            objectName: "path14"
            id: _qt_node92
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node92_stroke
                objectName: "svg_stroke_path:path14"
                strokeColor: lineColor
                strokeWidth: 0.649999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 152.472 140.827 L 153.318 141.936 L 153.756 142.489 " }
            }
        }
        Shape {
            objectName: "path15"
            id: _qt_node93
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node93_stroke
                objectName: "svg_stroke_path:path15"
                strokeColor: lineColor
                strokeWidth: 0.649999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 151.098 141.477 Q 151.43 142.428 151.747 143.531 " }
            }
        }
        Shape {
            objectName: "path16"
            id: _qt_node94
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node94_stroke
                objectName: "svg_stroke_path:path16"
                strokeColor: lineColor
                strokeWidth: 0.649999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 149.663 141.975 L 149.663 143.893 " }
            }
        }
        Shape {
            objectName: "path17"
            id: _qt_node95
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node95_fill_stroke
                objectName: "svg_path:path17"
                strokeColor: "transparent"
                fillColor: lineColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 149.099 126.772 Q 149.366 127.31 152.081 127.131 Q 154.568 126.967 156.077 127.053 Q 158.707 127.202 160.922 128.03 Q 164.383 129.324 170.309 133.135 Q 171.578 133.951 171.937 134.152 Q 173.588 135.077 174.469 135.283 Q 175.835 135.602 174.342 133.773 Q 172.682 131.738 168.134 128.6 Q 163.093 125.12 159.924 124.292 Q 158.925 124.031 155.664 124.22 Q 151.637 124.454 150.058 125.506 Q 149.523 125.862 149.258 126.353 Q 149.125 126.598 149.099 126.772 " }
            }
        }
        Shape {
            objectName: "path18"
            id: _qt_node96
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
        }
        Shape {
            objectName: "path33"
            id: _qt_node97
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node97_fill_stroke
                objectName: "svg_path:path33"
                strokeColor: "transparent"
                fillColor: lineColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 149.497 144.15 Q 153.134 144.778 156.691 144.945 Q 159.972 145.099 161.852 144.815 Q 163.557 144.557 167.701 143.251 Q 171.477 142.062 171.85 141.824 Q 172.058 141.692 172.383 142.077 Q 172.545 142.27 172.666 142.489 L 166.685 144.754 L 160.643 145.721 L 155.236 145.781 L 150.735 145.087 L 149.497 144.15 " }
            }
        }
        Shape {
            objectName: "path34"
            id: _qt_node98
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
        }
        Shape {
            objectName: "path35"
            id: _qt_node99
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node99_stroke
                objectName: "svg_stroke_path:path35"
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
        }
        Shape {
            objectName: "path36"
            id: _qt_node100
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node100_stroke
                objectName: "svg_stroke_path:path36"
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
        }
        Shape {
            objectName: "path37"
            id: _qt_node101
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                id: _qt_node101_stroke
                objectName: "svg_stroke_path:path37"
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
        }
    }
    Shape {
        preferredRendererType: Shape.CurveRenderer
        objectName: "layer3"
        id: _qt_node102
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node102_transform_base_group
            Translate { x: 10.4538; y: -46.0065}
        }
        ShapePath {
            id: _qt_node103_stroke
            objectName: "svg_stroke_path:path46"
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
            id: _qt_node104_stroke
            objectName: "svg_stroke_path:path47"
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
            id: _qt_node105_stroke
            objectName: "svg_stroke_path:path48"
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
            id: _qt_node106_stroke
            objectName: "svg_stroke_path:path49"
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
            id: _qt_node107_stroke
            objectName: "svg_stroke_path:path50"
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
            id: _qt_node108_stroke
            objectName: "svg_stroke_path:path51"
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
            id: _qt_node109_stroke
            objectName: "svg_stroke_path:path52"
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
            id: _qt_node110_stroke
            objectName: "svg_stroke_path:path53"
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
            id: _qt_node111_stroke
            objectName: "svg_stroke_path:path54"
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
            id: _qt_node112_stroke
            objectName: "svg_stroke_path:path55"
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
            id: _qt_node113_stroke
            objectName: "svg_stroke_path:path56"
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
            id: _qt_node114_stroke
            objectName: "svg_stroke_path:path57"
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
            id: _qt_node115_stroke
            objectName: "svg_stroke_path:path58"
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
            id: _qt_node116_stroke
            objectName: "svg_stroke_path:path59"
            strokeColor: lineColor
            strokeWidth: 2
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            miterLimit: 4
            fillColor: "transparent"
            fillRule: ShapePath.WindingFill
            pathHints: ShapePath.PathQuadratic
            PathSvg { path: "M 8.9998 96.9967 L 98.8973 96.9967 " }
        }
        ShapePath {
            id: _qt_node117_stroke
            objectName: "svg_stroke_path:path60"
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
            id: _qt_node118_stroke
            objectName: "svg_stroke_path:path61"
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
            id: _qt_node119_stroke
            objectName: "svg_stroke_path:path62"
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
            id: _qt_node120_stroke
            objectName: "svg_stroke_path:path63"
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
    }
}
