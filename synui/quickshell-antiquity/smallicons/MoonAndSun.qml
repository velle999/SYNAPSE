// Generated from SVG file moon_and_sun.svg
import QtQuick
import QtQuick.VectorImage
import QtQuick.VectorImage.Helpers
import QtQuick.Shapes
import QtQuick.Effects
import ".."

Item {
    implicitWidth: 707
    implicitHeight: 699
    property string moonBackgroundColor: "#ffffbe5ba"
    property string sunBackgroundColor: "#ffffbe5ba"

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
        objectName: "layer9"
        id: _qt_node1
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node1_transform_base_group
            Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.827962, 0.560784, -0.560784, 0.827962, 74.099, -39.8753)}
        }
        Item { // Structure node
            objectName: "layer1"
            id: _qt_node2
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node2_transform_base_group
                Translate { x: 10.4538; y: -46.0065}
            }
            Shape {
                objectName: "path1"
                id: _qt_node3
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node3_fill_stroke
                    objectName: "svg_path:path1"
                    strokeColor: "#ff000000"
                    strokeWidth: 3
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: moonBackgroundColor
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                    PathSvg { path: "M 111.19 51.9324 Q 151.502 62.1141 173.119 96.5886 Q 194.735 131.063 184.993 169.636 Q 175.25 208.208 139.856 228.283 Q 104.462 248.358 64.15 238.176 Q 41.8674 232.549 24.1474 218.205 Q 41.7518 222.507 61.6238 220.093 Q 80.6031 217.788 95.8078 210.166 Q 97.3581 209.388 99.5469 204.712 Q 102.064 199.335 104.008 199.071 Q 108.962 198.399 112.857 195.715 Q 115.707 193.752 120.423 188.704 Q 121.136 187.941 120.345 185.971 Q 119.951 184.99 119.93 184.573 Q 119.896 183.883 120.553 183.557 Q 121.402 183.136 121.889 182.518 Q 122.377 181.899 122.297 181.342 Q 122.267 181.131 122.191 180.743 Q 122.003 179.778 122.166 179.413 Q 122.423 178.837 123.633 178.527 L 124.845 178.25 Q 129.195 177.311 131.032 175.575 Q 131.37 175.256 131.109 174.352 Q 130.956 173.819 130.413 172.637 Q 129.977 171.687 129.88 171.347 Q 129.026 168.346 121.924 165.585 Q 118.262 164.161 116.951 163.244 Q 114.781 161.725 114.96 159.612 Q 115.138 159.23 115.345 158.467 Q 115.877 156.507 117.372 155.331 Q 118.033 154.81 120.71 153.85 Q 123.424 152.876 124.223 152.315 L 127.415 150.099 Q 135.41 144.58 138.293 141.992 Q 143.142 137.638 142.065 135.199 L 140.596 131.639 Q 139.554 128.987 138.819 127.687 Q 137.689 125.689 135.393 122.966 Q 134.787 122.247 132.404 120.341 Q 129.811 118.267 128.896 117.286 Q 127.348 115.625 127.921 114.759 Q 129.091 112.993 131.582 109.945 Q 134.444 106.443 135.226 104.821 Q 136.49 102.2 135.417 99.9339 Q 128.11 84.5134 112.312 73.605 Q 111.477 73.0282 109.047 72.1921 Q 106.608 71.3531 105.748 70.7693 Q 104.457 69.8931 101.855 67.9399 Q 95.075 62.8495 91.5574 61.7976 Q 87.4616 60.5727 79.7056 56.7347 Q 70.5957 52.2268 65.4682 50.9317 L 64.9859 50.8106 Q 88.1114 46.1035 111.19 51.9324 " }
                }
            }
            Shape {
                objectName: "path39"
                id: _qt_node4
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
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
                asynchronous: true
                ShapePath {
                    id: _qt_node5_stroke
                    objectName: "svg_stroke_path:path40"
                    strokeColor: "#ff000000"
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
                asynchronous: true
                ShapePath {
                    id: _qt_node6_stroke
                    objectName: "svg_stroke_path:path41"
                    strokeColor: "#ff000000"
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
                asynchronous: true
                ShapePath {
                    id: _qt_node7_stroke
                    objectName: "svg_stroke_path:path42"
                    strokeColor: "#ff000000"
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
                asynchronous: true
                ShapePath {
                    id: _qt_node8_stroke
                    objectName: "svg_stroke_path:path43"
                    strokeColor: "#ff000000"
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
                id: _qt_node9
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node9_stroke
                    objectName: "svg_stroke_path:path45"
                    strokeColor: "#ff000000"
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
                id: _qt_node10
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node10_fill
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
                id: _qt_node11
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node11_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.887095, -0.461588, 0.461588, 0.887095, 0, 0)}
                }
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node11_fill_stroke
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
                id: _qt_node12
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node12_fill_stroke
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
                id: _qt_node13
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node13_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.983492, -0.180953, 0.180953, 0.983492, 0, 0)}
                }
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node13_fill_stroke
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
                id: _qt_node14
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node14_fill
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
                id: _qt_node15
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node15_fill
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
                id: _qt_node16
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node16_fill
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
                id: _qt_node17
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node17_fill
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
                id: _qt_node18
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node18_fill
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
                id: _qt_node19
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node19_fill
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
                id: _qt_node20
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node20_fill
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
                id: _qt_node21
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node21_fill
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
                id: _qt_node22
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node22_fill
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
                id: _qt_node23
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node23_fill
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
                id: _qt_node24
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node24_fill
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
                id: _qt_node25
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node25_fill
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
                id: _qt_node26
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node26_fill
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
                id: _qt_node27
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node27_stroke
                    objectName: "svg_stroke_path:path81"
                    strokeColor: "#ff000000"
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
                id: _qt_node28
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node28_stroke
                    objectName: "svg_stroke_path:path82"
                    strokeColor: "#ff000000"
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
                id: _qt_node29
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node29_stroke
                    objectName: "svg_stroke_path:path83"
                    strokeColor: "#ff000000"
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
                id: _qt_node30
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node30_stroke
                    objectName: "svg_stroke_path:path84"
                    strokeColor: "#ff000000"
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
                id: _qt_node31
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node31_stroke
                    objectName: "svg_stroke_path:path85"
                    strokeColor: "#ff000000"
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
                id: _qt_node32
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node32_stroke
                    objectName: "svg_stroke_path:path86"
                    strokeColor: "#ff000000"
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
                id: _qt_node33
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node33_stroke
                    objectName: "svg_stroke_path:path87"
                    strokeColor: "#ff000000"
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
                id: _qt_node34
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node34_stroke
                    objectName: "svg_stroke_path:path88"
                    strokeColor: "#ff000000"
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
                id: _qt_node35
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node35_stroke
                    objectName: "svg_stroke_path:path89"
                    strokeColor: "#ff000000"
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
                id: _qt_node36
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node36_stroke
                    objectName: "svg_stroke_path:path90"
                    strokeColor: "#ff000000"
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
                id: _qt_node37
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node37_stroke
                    objectName: "svg_stroke_path:path91"
                    strokeColor: "#ff000000"
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
                id: _qt_node38
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node38_stroke
                    objectName: "svg_stroke_path:path92"
                    strokeColor: "#ff000000"
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
                id: _qt_node39
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node39_stroke
                    objectName: "svg_stroke_path:path93"
                    strokeColor: "#ff000000"
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
                id: _qt_node40
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node40_stroke
                    objectName: "svg_stroke_path:path94"
                    strokeColor: "#ff000000"
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
                id: _qt_node41
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node41_stroke
                    objectName: "svg_stroke_path:path95"
                    strokeColor: "#ff000000"
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
                id: _qt_node42
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node42_stroke
                    objectName: "svg_stroke_path:path96"
                    strokeColor: "#ff000000"
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
                id: _qt_node43
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node43_stroke
                    objectName: "svg_stroke_path:path97"
                    strokeColor: "#ff000000"
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
                id: _qt_node44
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node44_stroke
                    objectName: "svg_stroke_path:path98"
                    strokeColor: "#ff000000"
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
                id: _qt_node45
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node45_stroke
                    objectName: "svg_stroke_path:path99"
                    strokeColor: "#ff000000"
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
                id: _qt_node46
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node46_stroke
                    objectName: "svg_stroke_path:path100"
                    strokeColor: "#ff000000"
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
                id: _qt_node47
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node47_stroke
                    objectName: "svg_stroke_path:path101"
                    strokeColor: "#ff000000"
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
                id: _qt_node48
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node48_stroke
                    objectName: "svg_stroke_path:path102"
                    strokeColor: "#ff000000"
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
                id: _qt_node49
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node49_stroke
                    objectName: "svg_stroke_path:path103"
                    strokeColor: "#ff000000"
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
                id: _qt_node50
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node50_stroke
                    objectName: "svg_stroke_path:path104"
                    strokeColor: "#ff000000"
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
                id: _qt_node51
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node51_stroke
                    objectName: "svg_stroke_path:path105"
                    strokeColor: "#ff000000"
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
                id: _qt_node52
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node52_stroke
                    objectName: "svg_stroke_path:path106"
                    strokeColor: "#ff000000"
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
                id: _qt_node53
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node53_stroke
                    objectName: "svg_stroke_path:path107"
                    strokeColor: "#ff000000"
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
                id: _qt_node54
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node54_stroke
                    objectName: "svg_stroke_path:path108"
                    strokeColor: "#ff000000"
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
                id: _qt_node55
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node55_stroke
                    objectName: "svg_stroke_path:path109"
                    strokeColor: "#ff000000"
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
                id: _qt_node56
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node56_stroke
                    objectName: "svg_stroke_path:path110"
                    strokeColor: "#ff000000"
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
                id: _qt_node57
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node57_stroke
                    objectName: "svg_stroke_path:path111"
                    strokeColor: "#ff000000"
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
                id: _qt_node58
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node58_stroke
                    objectName: "svg_stroke_path:path112"
                    strokeColor: "#ff000000"
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
                id: _qt_node59
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node59_stroke
                    objectName: "svg_stroke_path:path113"
                    strokeColor: "#ff000000"
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
                id: _qt_node60
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node60_stroke
                    objectName: "svg_stroke_path:path114"
                    strokeColor: "#ff000000"
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
                id: _qt_node61
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node61_stroke
                    objectName: "svg_stroke_path:path115"
                    strokeColor: "#ff000000"
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
                id: _qt_node62
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node62_stroke
                    objectName: "svg_stroke_path:path116"
                    strokeColor: "#ff000000"
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
                id: _qt_node63
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node63_stroke
                    objectName: "svg_stroke_path:path117"
                    strokeColor: "#ff000000"
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
                id: _qt_node64
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node64_stroke
                    objectName: "svg_stroke_path:path118"
                    strokeColor: "#ff000000"
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
                id: _qt_node65
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node65_stroke
                    objectName: "svg_stroke_path:path119"
                    strokeColor: "#ff000000"
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
                id: _qt_node66
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node66_stroke
                    objectName: "svg_stroke_path:path120"
                    strokeColor: "#ff000000"
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
                id: _qt_node67
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node67_stroke
                    objectName: "svg_stroke_path:path121"
                    strokeColor: "#ff000000"
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
                id: _qt_node68
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node68_stroke
                    objectName: "svg_stroke_path:path122"
                    strokeColor: "#ff000000"
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
                id: _qt_node69
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node69_stroke
                    objectName: "svg_stroke_path:path123"
                    strokeColor: "#ff000000"
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
                id: _qt_node70
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node70_stroke
                    objectName: "svg_stroke_path:path124"
                    strokeColor: "#ff000000"
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
                id: _qt_node71
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node71_stroke
                    objectName: "svg_stroke_path:path125"
                    strokeColor: "#ff000000"
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
                id: _qt_node72
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node72_stroke
                    objectName: "svg_stroke_path:path126"
                    strokeColor: "#ff000000"
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
                id: _qt_node73
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node73_stroke
                    objectName: "svg_stroke_path:path127"
                    strokeColor: "#ff000000"
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
                id: _qt_node74
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node74_stroke
                    objectName: "svg_stroke_path:path131"
                    strokeColor: "#ff000000"
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
                id: _qt_node75
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node75_stroke
                    objectName: "svg_stroke_path:path132"
                    strokeColor: "#ff000000"
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
                id: _qt_node76
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node76_stroke
                    objectName: "svg_stroke_path:path133"
                    strokeColor: "#ff000000"
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
                id: _qt_node77
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node77_stroke
                    objectName: "svg_stroke_path:path134"
                    strokeColor: "#ff000000"
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
            asynchronous: true
            objectName: "g153"
            id: _qt_node79
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node79_transform_base_group
                Translate { x: 10.4538; y: -46.0065}
            }
            ShapePath {
                id: _qt_node80_fill_stroke
                objectName: "svg_path:path147"
                strokeColor: "transparent"
                fillColor: "#ff000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 149.099 126.772 Q 149.366 127.31 152.081 127.131 Q 154.568 126.967 156.077 127.053 Q 158.707 127.202 160.922 128.03 Q 164.383 129.324 170.309 133.135 Q 171.578 133.951 171.937 134.152 Q 173.588 135.077 174.469 135.283 Q 175.835 135.602 174.342 133.773 Q 172.682 131.738 168.134 128.6 Q 163.093 125.12 159.924 124.292 Q 158.925 124.031 155.664 124.22 Q 151.637 124.454 150.058 125.506 Q 149.523 125.862 149.258 126.353 Q 149.125 126.598 149.099 126.772 " }
            }
            ShapePath {
                id: _qt_node82_fill
                objectName: "svg_fill_path:path149"
                strokeColor: "transparent"
                fillColor: "#ff000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 152.143 143.654 Q 156.88 144.472 161.249 143.795 L 161.719 143.723 Q 164.529 143.298 167.057 142.797 Q 170.731 142.068 172.804 141.361 Q 174.195 140.887 173.926 141.713 Q 173.792 142.127 173.379 142.635 Q 172.558 142.977 169.672 143.863 L 166.685 144.754 L 160.643 145.721 L 155.236 145.781 L 150.735 145.086 Q 149.289 144.617 149.2 144.085 Q 149.126 143.647 149.923 143.325 Q 150.669 143.024 151.404 143.088 Q 152.205 143.157 152.143 143.654 " }
            }
            ShapePath {
                id: _qt_node84_stroke
                objectName: "svg_stroke_path:path151"
                strokeColor: "#ff000000"
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
                id: _qt_node85_stroke
                objectName: "svg_stroke_path:path152"
                strokeColor: "#ff000000"
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
                id: _qt_node86_stroke
                objectName: "svg_stroke_path:path153"
                strokeColor: "#ff000000"
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
                id: _qt_node87_stroke
                objectName: "svg_stroke_path:path154"
                strokeColor: "#ff000000"
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
    }
    Item { // Structure node
        objectName: "g795"
        id: _qt_node89
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node89_transform_base_group
            Translate { x: 1.14598; y: 0.769378}
        }
        Shape {
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            objectName: "layer1-1"
            id: _qt_node90
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node90_transform_base_group
                Translate { x: -43.9931; y: -49.4304}
            }
            ShapePath {
                id: _qt_node91_fill
                objectName: "svg_fill_path:path3-7"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 105.246 85.8195 Q 105.084 84.8405 104.947 83.7173 Q 104.671 81.4709 104.791 80.75 Q 105.057 79.46 106.023 76.7746 Q 107.124 73.717 107.275 72.4152 Q 107.472 70.7256 106.829 68.014 Q 106.405 66.2295 106.43 65.3947 Q 106.496 63.2353 109.928 56.4276 Q 110.924 54.4534 110.502 50.2653 Q 110.342 48.6715 111.825 50.8576 Q 113.233 52.9346 113.583 54.1017 Q 113.982 55.4315 113.646 58.4871 Q 113.06 63.8213 114.671 67.2268 Q 115.873 69.7673 115.875 71.439 Q 115.876 72.4562 115.255 73.9675 Q 114.628 75.4925 114.55 76.5911 Q 114.399 78.7057 114.935 82.4589 Q 115.203 84.3356 115.502 85.7893 L 105.246 85.8195 " }
            }
            ShapePath {
                id: _qt_node91_stroke
                objectName: "svg_stroke_path:path3-7"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 105.246 85.8195 Q 105.084 84.8405 104.947 83.7173 Q 104.671 81.4709 104.791 80.75 Q 105.057 79.46 106.023 76.7746 Q 107.124 73.717 107.275 72.4152 Q 107.472 70.7256 106.829 68.014 Q 106.405 66.2295 106.43 65.3947 Q 106.496 63.2353 109.928 56.4276 Q 110.924 54.4534 110.502 50.2653 Q 110.342 48.6715 111.825 50.8576 Q 113.233 52.9346 113.583 54.1017 Q 113.982 55.4315 113.646 58.4871 Q 113.06 63.8213 114.671 67.2268 Q 115.873 69.7673 115.875 71.439 Q 115.876 72.4562 115.255 73.9675 Q 114.628 75.4925 114.55 76.5911 Q 114.399 78.7057 114.935 82.4589 Q 115.203 84.3356 115.502 85.7893 " }
            }
            ShapePath {
                id: _qt_node92_fill
                objectName: "svg_fill_path:path4-5"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 115.389 145.691 Q 115.551 146.67 115.689 147.793 Q 115.964 150.039 115.844 150.76 Q 115.578 152.05 114.612 154.736 Q 113.512 157.793 113.36 159.095 Q 113.163 160.785 113.807 163.496 Q 114.23 165.281 114.205 166.115 Q 114.14 168.275 110.707 175.083 Q 109.712 177.057 110.133 181.245 Q 110.294 182.839 108.811 180.652 Q 107.402 178.576 107.052 177.408 Q 106.653 176.079 106.989 173.023 Q 107.576 167.689 105.965 164.283 Q 104.763 161.743 104.761 160.071 Q 104.759 159.054 105.38 157.543 Q 106.007 156.018 106.085 154.919 Q 106.237 152.804 105.7 149.051 Q 105.432 147.175 105.134 145.721 L 115.389 145.691 " }
            }
            ShapePath {
                id: _qt_node92_stroke
                objectName: "svg_stroke_path:path4-5"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 115.389 145.691 Q 115.551 146.67 115.689 147.793 Q 115.964 150.039 115.844 150.76 Q 115.578 152.05 114.612 154.736 Q 113.512 157.793 113.36 159.095 Q 113.163 160.785 113.807 163.496 Q 114.23 165.281 114.205 166.115 Q 114.14 168.275 110.707 175.083 Q 109.712 177.057 110.133 181.245 Q 110.294 182.839 108.811 180.652 Q 107.402 178.576 107.052 177.408 Q 106.653 176.079 106.989 173.023 Q 107.576 167.689 105.965 164.283 Q 104.763 161.743 104.761 160.071 Q 104.759 159.054 105.38 157.543 Q 106.007 156.018 106.085 154.919 Q 106.237 152.804 105.7 149.051 Q 105.432 147.175 105.134 145.721 " }
            }
            ShapePath {
                id: _qt_node93_fill
                objectName: "svg_fill_path:path5-9"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 140.253 110.683 Q 141.232 110.522 142.355 110.384 Q 144.602 110.109 145.323 110.229 Q 146.613 110.495 149.298 111.461 Q 152.356 112.561 153.658 112.713 Q 155.347 112.909 158.059 112.266 Q 159.843 111.842 160.678 111.868 Q 162.838 111.933 169.645 115.366 Q 171.619 116.361 175.807 115.94 Q 177.401 115.779 175.215 117.262 Q 173.138 118.671 171.971 119.021 Q 170.641 119.419 167.586 119.083 Q 162.251 118.497 158.846 120.108 Q 156.306 121.31 154.634 121.312 Q 153.617 121.313 152.105 120.692 Q 150.58 120.066 149.482 119.987 Q 147.367 119.836 143.614 120.372 Q 141.737 120.641 140.283 120.939 L 140.253 110.683 " }
            }
            ShapePath {
                id: _qt_node93_stroke
                objectName: "svg_stroke_path:path5-9"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 140.253 110.683 Q 141.232 110.522 142.355 110.384 Q 144.602 110.109 145.323 110.229 Q 146.613 110.495 149.298 111.461 Q 152.356 112.561 153.658 112.713 Q 155.347 112.909 158.059 112.266 Q 159.843 111.842 160.678 111.868 Q 162.838 111.933 169.645 115.366 Q 171.619 116.361 175.807 115.94 Q 177.401 115.779 175.215 117.262 Q 173.138 118.671 171.971 119.021 Q 170.641 119.419 167.586 119.083 Q 162.251 118.497 158.846 120.108 Q 156.306 121.31 154.634 121.312 Q 153.617 121.313 152.105 120.692 Q 150.58 120.066 149.482 119.987 Q 147.367 119.836 143.614 120.372 Q 141.737 120.641 140.283 120.939 " }
            }
            ShapePath {
                id: _qt_node94_fill
                objectName: "svg_fill_path:path6-6"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 80.3822 120.827 Q 79.4032 120.988 78.28 121.126 Q 76.0336 121.402 75.3127 121.281 Q 74.0226 121.015 71.3373 120.049 Q 68.2797 118.949 66.9778 118.798 Q 65.2883 118.601 62.5766 119.244 Q 60.7921 119.668 59.9574 119.642 Q 57.7979 119.577 50.9903 116.145 Q 49.0161 115.149 44.828 115.571 Q 43.2342 115.731 45.4203 114.248 Q 47.4973 112.839 48.6643 112.489 Q 49.9942 112.091 53.0498 112.427 Q 58.384 113.013 61.7895 111.402 Q 64.3299 110.2 66.0016 110.198 Q 67.0189 110.197 68.5301 110.818 Q 70.0552 111.444 71.1538 111.523 Q 73.2683 111.674 77.0216 111.138 Q 78.8982 110.87 80.352 110.571 L 80.3822 120.827 " }
            }
            ShapePath {
                id: _qt_node94_stroke
                objectName: "svg_stroke_path:path6-6"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 80.3822 120.827 Q 79.4032 120.988 78.28 121.126 Q 76.0336 121.402 75.3127 121.281 Q 74.0226 121.015 71.3373 120.049 Q 68.2797 118.949 66.9778 118.798 Q 65.2883 118.601 62.5766 119.244 Q 60.7921 119.668 59.9574 119.642 Q 57.7979 119.577 50.9903 116.145 Q 49.0161 115.149 44.828 115.571 Q 43.2342 115.731 45.4203 114.248 Q 47.4973 112.839 48.6643 112.489 Q 49.9942 112.091 53.0498 112.427 Q 58.384 113.013 61.7895 111.402 Q 64.3299 110.2 66.0016 110.198 Q 67.0189 110.197 68.5301 110.818 Q 70.0552 111.444 71.1538 111.523 Q 73.2683 111.674 77.0216 111.138 Q 78.8982 110.87 80.352 110.571 " }
            }
            ShapePath {
                id: _qt_node95_fill
                objectName: "svg_fill_path:path7"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 80.3822 120.827 Q 79.4032 120.988 78.28 121.126 Q 76.0336 121.402 75.3127 121.281 Q 74.0226 121.015 71.3373 120.049 Q 68.2797 118.949 66.9778 118.798 Q 65.2883 118.601 62.5766 119.244 Q 60.7921 119.668 59.9574 119.642 Q 57.7979 119.577 50.9903 116.145 Q 49.0161 115.149 44.828 115.571 Q 43.2342 115.731 45.4203 114.248 Q 47.4973 112.839 48.6643 112.489 Q 49.9942 112.091 53.0498 112.427 Q 58.384 113.013 61.7895 111.402 Q 64.3299 110.2 66.0016 110.198 Q 67.0189 110.197 68.5301 110.818 Q 70.0552 111.444 71.1538 111.523 Q 73.2683 111.674 77.0216 111.138 Q 78.8982 110.87 80.352 110.571 L 80.3822 120.827 " }
            }
            ShapePath {
                id: _qt_node95_stroke
                objectName: "svg_stroke_path:path7"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 80.3822 120.827 Q 79.4032 120.988 78.28 121.126 Q 76.0336 121.402 75.3127 121.281 Q 74.0226 121.015 71.3373 120.049 Q 68.2797 118.949 66.9778 118.798 Q 65.2883 118.601 62.5766 119.244 Q 60.7921 119.668 59.9574 119.642 Q 57.7979 119.577 50.9903 116.145 Q 49.0161 115.149 44.828 115.571 Q 43.2342 115.731 45.4203 114.248 Q 47.4973 112.839 48.6643 112.489 Q 49.9942 112.091 53.0498 112.427 Q 58.384 113.013 61.7895 111.402 Q 64.3299 110.2 66.0016 110.198 Q 67.0189 110.197 68.5301 110.818 Q 70.0552 111.444 71.1538 111.523 Q 73.2683 111.674 77.0216 111.138 Q 78.8982 110.87 80.352 110.571 " }
            }
            ShapePath {
                id: _qt_node96_fill
                objectName: "svg_fill_path:path10"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 120.893 87.2943 Q 121.243 86.3656 121.685 85.324 Q 122.57 83.2409 123.034 82.6767 Q 123.91 81.6924 126.089 79.8499 Q 128.57 77.7519 129.353 76.7003 Q 130.368 75.3355 131.167 72.6655 Q 131.692 70.9083 132.131 70.1981 Q 133.268 68.3606 139.644 64.1813 Q 141.493 62.9692 143.222 59.1315 Q 143.88 57.671 144.071 60.3057 Q 144.253 62.8088 143.973 63.9945 Q 143.653 65.3455 141.834 67.8238 Q 138.659 72.1501 138.352 75.9049 Q 138.122 78.706 137.288 80.1548 Q 136.781 81.0364 135.487 82.0346 Q 134.182 83.0421 133.565 83.9542 Q 132.377 85.71 130.965 89.2285 Q 130.258 90.9877 129.79 92.3959 L 120.893 87.2943 " }
            }
            ShapePath {
                id: _qt_node96_stroke
                objectName: "svg_stroke_path:path10"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 120.893 87.2943 Q 121.243 86.3656 121.685 85.324 Q 122.57 83.2409 123.034 82.6767 Q 123.91 81.6924 126.089 79.8499 Q 128.57 77.7519 129.353 76.7003 Q 130.368 75.3355 131.167 72.6655 Q 131.692 70.9083 132.131 70.1981 Q 133.268 68.3606 139.644 64.1813 Q 141.493 62.9692 143.222 59.1315 Q 143.88 57.671 144.071 60.3057 Q 144.253 62.8088 143.973 63.9945 Q 143.653 65.3455 141.834 67.8238 Q 138.659 72.1501 138.352 75.9049 Q 138.122 78.706 137.288 80.1548 Q 136.781 81.0364 135.487 82.0346 Q 134.182 83.0421 133.565 83.9542 Q 132.377 85.71 130.965 89.2285 Q 130.258 90.9877 129.79 92.3959 " }
            }
            ShapePath {
                id: _qt_node97_fill
                objectName: "svg_fill_path:path11-2"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 133.707 96.3951 Q 134.474 95.7656 135.378 95.0847 Q 137.185 93.723 137.87 93.4667 Q 139.12 93.0519 141.929 92.5459 Q 145.127 91.9698 146.33 91.4502 Q 147.892 90.7759 149.918 88.8628 Q 151.252 87.6039 151.987 87.2084 Q 153.89 86.1853 161.502 85.7541 Q 163.709 85.629 167.126 83.1699 Q 168.426 82.2341 167.274 84.6114 Q 166.18 86.8699 165.344 87.7565 Q 164.392 88.7667 161.577 90.0036 Q 156.665 92.1628 154.521 95.2608 Q 152.922 97.572 151.475 98.4096 Q 150.595 98.9193 148.976 99.1372 Q 147.341 99.357 146.351 99.8383 Q 144.444 100.765 141.462 103.106 Q 139.971 104.276 138.861 105.261 L 133.707 96.3951 " }
            }
            ShapePath {
                id: _qt_node97_stroke
                objectName: "svg_stroke_path:path11-2"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 133.707 96.3951 Q 134.474 95.7656 135.378 95.0847 Q 137.185 93.723 137.87 93.4667 Q 139.12 93.0519 141.929 92.5459 Q 145.127 91.9698 146.33 91.4502 Q 147.892 90.7759 149.918 88.8628 Q 151.252 87.6039 151.987 87.2084 Q 153.89 86.1853 161.502 85.7541 Q 163.709 85.629 167.126 83.1699 Q 168.426 82.2341 167.274 84.6114 Q 166.18 86.8699 165.344 87.7565 Q 164.392 88.7667 161.577 90.0036 Q 156.665 92.1628 154.521 95.2608 Q 152.922 97.572 151.475 98.4096 Q 150.595 98.9193 148.976 99.1372 Q 147.341 99.357 146.351 99.8383 Q 144.444 100.765 141.462 103.106 Q 139.971 104.276 138.861 105.261 " }
            }
            ShapePath {
                id: _qt_node98_fill
                objectName: "svg_fill_path:path12-1"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 138.779 126.331 Q 139.707 126.68 140.749 127.122 Q 142.832 128.007 143.396 128.472 Q 144.38 129.347 146.223 131.526 Q 148.321 134.008 149.372 134.79 Q 150.737 135.805 153.407 136.604 Q 155.164 137.129 155.875 137.569 Q 157.712 138.705 161.892 145.081 Q 163.104 146.931 166.941 148.66 Q 168.402 149.318 165.767 149.509 Q 163.264 149.69 162.078 149.41 Q 160.727 149.09 158.249 147.272 Q 153.923 144.096 150.168 143.789 Q 147.367 143.56 145.918 142.726 Q 145.036 142.218 144.038 140.925 Q 143.031 139.62 142.119 139.002 Q 140.363 137.814 136.844 136.402 Q 135.085 135.696 133.677 135.227 L 138.779 126.331 " }
            }
            ShapePath {
                id: _qt_node98_stroke
                objectName: "svg_stroke_path:path12-1"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 138.779 126.331 Q 139.707 126.68 140.749 127.122 Q 142.832 128.007 143.396 128.472 Q 144.38 129.347 146.223 131.526 Q 148.321 134.008 149.372 134.79 Q 150.737 135.805 153.407 136.604 Q 155.164 137.129 155.875 137.569 Q 157.712 138.705 161.892 145.081 Q 163.104 146.931 166.941 148.66 Q 168.402 149.318 165.767 149.509 Q 163.264 149.69 162.078 149.41 Q 160.727 149.09 158.249 147.272 Q 153.923 144.096 150.168 143.789 Q 147.367 143.56 145.918 142.726 Q 145.036 142.218 144.038 140.925 Q 143.031 139.62 142.119 139.002 Q 140.363 137.814 136.844 136.402 Q 135.085 135.696 133.677 135.227 " }
            }
            ShapePath {
                id: _qt_node99_fill
                objectName: "svg_fill_path:path13-7"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 129.678 139.144 Q 130.307 139.911 130.988 140.815 Q 132.35 142.623 132.606 143.307 Q 133.021 144.557 133.527 147.366 Q 134.103 150.564 134.623 151.767 Q 135.297 153.329 137.21 155.355 Q 138.469 156.689 138.864 157.425 Q 139.888 159.328 140.319 166.939 Q 140.444 169.147 142.903 172.563 Q 143.839 173.863 141.461 172.711 Q 139.203 171.617 138.316 170.781 Q 137.306 169.829 136.069 167.015 Q 133.91 162.102 130.812 159.958 Q 128.501 158.359 127.663 156.912 Q 127.153 156.032 126.936 154.413 Q 126.716 152.779 126.234 151.788 Q 125.308 149.881 122.967 146.899 Q 121.797 145.408 120.811 144.298 L 129.678 139.144 " }
            }
            ShapePath {
                id: _qt_node99_stroke
                objectName: "svg_stroke_path:path13-7"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 129.678 139.144 Q 130.307 139.911 130.988 140.815 Q 132.35 142.623 132.606 143.307 Q 133.021 144.557 133.527 147.366 Q 134.103 150.564 134.623 151.767 Q 135.297 153.329 137.21 155.355 Q 138.469 156.689 138.864 157.425 Q 139.888 159.328 140.319 166.939 Q 140.444 169.147 142.903 172.563 Q 143.839 173.863 141.461 172.711 Q 139.203 171.617 138.316 170.781 Q 137.306 169.829 136.069 167.015 Q 133.91 162.102 130.812 159.958 Q 128.501 158.359 127.663 156.912 Q 127.153 156.032 126.936 154.413 Q 126.716 152.779 126.234 151.788 Q 125.308 149.881 122.967 146.899 Q 121.797 145.408 120.811 144.298 " }
            }
            ShapePath {
                id: _qt_node100_fill
                objectName: "svg_fill_path:path14-8"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 99.7421 144.216 Q 99.3927 145.145 98.9503 146.186 Q 98.0656 148.269 97.6011 148.833 Q 96.7258 149.818 94.5465 151.66 Q 92.065 153.758 91.2827 154.81 Q 90.2675 156.175 89.4689 158.845 Q 88.9433 160.602 88.5041 161.312 Q 87.3676 163.15 80.9913 167.329 Q 79.1422 168.541 77.4131 172.379 Q 76.7551 173.839 76.564 171.204 Q 76.3825 168.701 76.6629 167.516 Q 76.9825 166.165 78.8013 163.686 Q 81.9763 159.36 82.2837 155.605 Q 82.513 152.804 83.3471 151.355 Q 83.8546 150.474 85.148 149.476 Q 86.4533 148.468 87.0705 147.556 Q 88.2586 145.8 89.6709 142.282 Q 90.377 140.522 90.8456 139.114 L 99.7421 144.216 " }
            }
            ShapePath {
                id: _qt_node100_stroke
                objectName: "svg_stroke_path:path14-8"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 99.7421 144.216 Q 99.3927 145.145 98.9503 146.186 Q 98.0656 148.269 97.6011 148.833 Q 96.7258 149.818 94.5465 151.66 Q 92.065 153.758 91.2827 154.81 Q 90.2675 156.175 89.4689 158.845 Q 88.9433 160.602 88.5041 161.312 Q 87.3676 163.15 80.9913 167.329 Q 79.1422 168.541 77.4131 172.379 Q 76.7551 173.839 76.564 171.204 Q 76.3825 168.701 76.6629 167.516 Q 76.9825 166.165 78.8013 163.686 Q 81.9763 159.36 82.2837 155.605 Q 82.513 152.804 83.3471 151.355 Q 83.8546 150.474 85.148 149.476 Q 86.4533 148.468 87.0705 147.556 Q 88.2586 145.8 89.6709 142.282 Q 90.377 140.522 90.8456 139.114 " }
            }
            ShapePath {
                id: _qt_node101_fill
                objectName: "svg_fill_path:path15-5"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 86.9286 135.115 Q 86.1616 135.745 85.2578 136.425 Q 83.45 137.787 82.7656 138.044 Q 81.5155 138.458 78.7069 138.964 Q 75.5088 139.54 74.3056 140.06 Q 72.7439 140.734 70.7173 142.647 Q 69.3836 143.906 68.648 144.302 Q 66.7451 145.325 59.1334 145.756 Q 56.926 145.881 53.5097 148.34 Q 52.2096 149.276 53.3615 146.899 Q 54.4558 144.64 55.2915 143.754 Q 56.2438 142.743 59.058 141.507 Q 63.9709 139.347 66.1145 136.249 Q 67.7136 133.938 69.1603 133.101 Q 70.0406 132.591 71.6599 132.373 Q 73.294 132.153 74.2846 131.672 Q 76.1914 130.745 79.1737 128.404 Q 80.6649 127.234 81.7747 126.249 L 86.9286 135.115 " }
            }
            ShapePath {
                id: _qt_node101_stroke
                objectName: "svg_stroke_path:path15-5"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 86.9286 135.115 Q 86.1616 135.745 85.2578 136.425 Q 83.45 137.787 82.7656 138.044 Q 81.5155 138.458 78.7069 138.964 Q 75.5088 139.54 74.3056 140.06 Q 72.7439 140.734 70.7173 142.647 Q 69.3836 143.906 68.648 144.302 Q 66.7451 145.325 59.1334 145.756 Q 56.926 145.881 53.5097 148.34 Q 52.2096 149.276 53.3615 146.899 Q 54.4558 144.64 55.2915 143.754 Q 56.2438 142.743 59.058 141.507 Q 63.9709 139.347 66.1145 136.249 Q 67.7136 133.938 69.1603 133.101 Q 70.0406 132.591 71.6599 132.373 Q 73.294 132.153 74.2846 131.672 Q 76.1914 130.745 79.1737 128.404 Q 80.6649 127.234 81.7747 126.249 " }
            }
            ShapePath {
                id: _qt_node102_fill
                objectName: "svg_fill_path:path16-7"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 81.857 105.179 Q 80.9283 104.83 79.8867 104.388 Q 77.8036 103.503 77.2393 103.038 Q 76.2551 102.163 74.4126 99.9838 Q 72.3146 97.5023 71.263 96.7201 Q 69.8982 95.7048 67.2281 94.9062 Q 65.471 94.3807 64.7607 93.9414 Q 62.9233 92.805 58.7439 86.4287 Q 57.5319 84.5795 53.6942 82.8505 Q 52.2337 82.1925 54.8684 82.0014 Q 57.3715 81.8198 58.5571 82.1003 Q 59.9082 82.4199 62.3864 84.2386 Q 66.7128 87.4137 70.4676 87.7211 Q 73.2687 87.9504 74.7174 88.7844 Q 75.599 89.292 76.5973 90.5854 Q 77.6047 91.8906 78.5169 92.5079 Q 80.2726 93.6959 83.7911 95.1082 Q 85.5504 95.8144 86.9585 96.2829 L 81.857 105.179 " }
            }
            ShapePath {
                id: _qt_node102_stroke
                objectName: "svg_stroke_path:path16-7"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 81.857 105.179 Q 80.9283 104.83 79.8867 104.388 Q 77.8036 103.503 77.2393 103.038 Q 76.2551 102.163 74.4126 99.9838 Q 72.3146 97.5023 71.263 96.7201 Q 69.8982 95.7048 67.2281 94.9062 Q 65.471 94.3807 64.7607 93.9414 Q 62.9233 92.805 58.7439 86.4287 Q 57.5319 84.5795 53.6942 82.8505 Q 52.2337 82.1925 54.8684 82.0014 Q 57.3715 81.8198 58.5571 82.1003 Q 59.9082 82.4199 62.3864 84.2386 Q 66.7128 87.4137 70.4676 87.7211 Q 73.2687 87.9504 74.7174 88.7844 Q 75.599 89.292 76.5973 90.5854 Q 77.6047 91.8906 78.5169 92.5079 Q 80.2726 93.6959 83.7911 95.1082 Q 85.5504 95.8144 86.9585 96.2829 " }
            }
            ShapePath {
                id: _qt_node103_fill
                objectName: "svg_fill_path:path17-4"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 90.9578 92.366 Q 90.3282 91.599 89.6474 90.6951 Q 88.2857 88.8874 88.0293 88.203 Q 87.6145 86.9528 87.1086 84.1442 Q 86.5324 80.9462 86.0128 79.7429 Q 85.3385 78.1813 83.4255 76.1546 Q 82.1665 74.8209 81.771 74.0854 Q 80.748 72.1825 80.3167 64.5708 Q 80.1916 62.3633 77.7326 58.9471 Q 76.7968 57.647 79.174 58.7988 Q 81.4326 59.8931 82.3191 60.7289 Q 83.3294 61.6812 84.5662 64.4954 Q 86.7254 69.4082 89.8235 71.5518 Q 92.1346 73.151 92.9723 74.5977 Q 93.482 75.478 93.6998 77.0973 Q 93.9197 78.7313 94.401 79.722 Q 95.3275 81.6287 97.6684 84.6111 Q 98.8389 86.1023 99.8241 87.2121 L 90.9578 92.366 " }
            }
            ShapePath {
                id: _qt_node103_stroke
                objectName: "svg_stroke_path:path17-4"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 90.9578 92.366 Q 90.3282 91.599 89.6474 90.6951 Q 88.2857 88.8874 88.0293 88.203 Q 87.6145 86.9528 87.1086 84.1442 Q 86.5324 80.9462 86.0128 79.7429 Q 85.3385 78.1813 83.4255 76.1546 Q 82.1665 74.8209 81.771 74.0854 Q 80.748 72.1825 80.3167 64.5708 Q 80.1916 62.3633 77.7326 58.9471 Q 76.7968 57.647 79.174 58.7988 Q 81.4326 59.8931 82.3191 60.7289 Q 83.3294 61.6812 84.5662 64.4954 Q 86.7254 69.4082 89.8235 71.5518 Q 92.1346 73.151 92.9723 74.5977 Q 93.482 75.478 93.6998 77.0973 Q 93.9197 78.7313 94.401 79.722 Q 95.3275 81.6287 97.6684 84.6111 Q 98.8389 86.1023 99.8241 87.2121 " }
            }
        }
        Shape {
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            objectName: "layer4"
            id: _qt_node104
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node104_transform_base_group
                Translate { x: -43.9931; y: -49.4304}
            }
            ShapePath {
                id: _qt_node105_fill
                objectName: "svg_fill_path:path18-1"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 115.483 85.7665 Q 115.225 84.7591 115 83.4408 Q 114.551 80.8042 114.721 79.25 Q 114.801 78.5108 116.55 76.653 Q 117.992 75.1198 119.773 73.6198 Q 121.671 72.0205 122.763 69.6851 Q 123.309 68.5174 123.476 67.6696 L 123.441 71.0031 Q 123.441 74.5781 123.561 76.2151 L 123.574 76.3796 Q 123.956 81.2664 123.307 82.2792 Q 121.957 84.3851 120.975 87.0901 L 120.896 87.307 L 115.483 85.7665 " }
            }
            ShapePath {
                id: _qt_node105_stroke
                objectName: "svg_stroke_path:path18-1"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 115.483 85.7665 Q 115.225 84.7591 115 83.4408 Q 114.551 80.8042 114.721 79.25 Q 114.801 78.5108 116.55 76.653 Q 117.992 75.1198 119.773 73.6198 Q 121.671 72.0205 122.763 69.6851 Q 123.309 68.5174 123.476 67.6696 L 123.441 71.0031 Q 123.441 74.5781 123.561 76.2151 L 123.574 76.3796 Q 123.956 81.2664 123.307 82.2792 Q 121.957 84.3851 120.975 87.0901 L 120.896 87.307 " }
            }
            ShapePath {
                id: _qt_node106_fill
                objectName: "svg_fill_path:path29"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 129.786 92.3671 Q 130.066 91.3654 130.53 90.1114 Q 131.459 87.6035 132.383 86.3422 Q 132.823 85.7423 135.266 85.0076 Q 137.282 84.4012 139.574 83.9923 Q 142.017 83.5563 144.131 82.08 Q 145.188 81.3419 145.756 80.6909 L 144.059 83.5603 Q 142.271 86.6564 141.557 88.1341 L 141.486 88.283 Q 139.373 92.7059 138.304 93.2585 Q 136.083 94.4076 133.879 96.2589 L 133.703 96.4074 L 129.786 92.3671 " }
            }
            ShapePath {
                id: _qt_node106_stroke
                objectName: "svg_stroke_path:path29"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 129.786 92.3671 Q 130.066 91.3654 130.53 90.1114 Q 131.459 87.6035 132.383 86.3422 Q 132.823 85.7423 135.266 85.0076 Q 137.282 84.4012 139.574 83.9923 Q 142.017 83.5563 144.131 82.08 Q 145.188 81.3419 145.756 80.6909 L 144.059 83.5603 Q 142.271 86.6564 141.557 88.1341 L 141.486 88.283 Q 139.373 92.7059 138.304 93.2585 Q 136.083 94.4076 133.879 96.2589 L 133.703 96.4074 " }
            }
            ShapePath {
                id: _qt_node107_fill
                objectName: "svg_fill_path:path30"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 138.871 105.234 Q 139.615 104.507 140.644 103.653 Q 142.703 101.946 144.134 101.316 Q 144.814 101.016 147.297 101.601 Q 149.346 102.084 151.535 102.876 Q 153.87 103.72 156.438 103.498 Q 157.723 103.387 158.54 103.108 L 155.636 104.744 Q 152.54 106.532 151.182 107.454 L 151.046 107.547 Q 147.005 110.321 145.803 110.266 Q 143.305 110.15 140.471 110.652 L 140.243 110.692 L 138.871 105.234 " }
            }
            ShapePath {
                id: _qt_node107_stroke
                objectName: "svg_stroke_path:path30"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 138.871 105.234 Q 139.615 104.507 140.644 103.653 Q 142.703 101.946 144.134 101.316 Q 144.814 101.016 147.297 101.601 Q 149.346 102.084 151.535 102.876 Q 153.87 103.72 156.438 103.498 Q 157.723 103.387 158.54 103.108 L 155.636 104.744 Q 152.54 106.532 151.182 107.454 L 151.046 107.547 Q 147.005 110.321 145.803 110.266 Q 143.305 110.15 140.471 110.652 L 140.243 110.692 " }
            }
            ShapePath {
                id: _qt_node108_fill
                objectName: "svg_fill_path:path31"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 140.306 120.921 Q 141.314 120.662 142.632 120.438 Q 145.269 119.989 146.823 120.158 Q 147.562 120.239 149.42 121.987 Q 150.953 123.43 152.453 125.21 Q 154.052 127.108 156.388 128.201 Q 157.555 128.747 158.403 128.913 L 155.07 128.878 Q 151.495 128.878 149.858 128.999 L 149.693 129.011 Q 144.806 129.393 143.794 128.744 Q 141.688 127.395 138.983 126.412 L 138.766 126.333 L 140.306 120.921 " }
            }
            ShapePath {
                id: _qt_node108_stroke
                objectName: "svg_stroke_path:path31"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 140.306 120.921 Q 141.314 120.662 142.632 120.438 Q 145.269 119.989 146.823 120.158 Q 147.562 120.239 149.42 121.987 Q 150.953 123.43 152.453 125.21 Q 154.052 127.108 156.388 128.201 Q 157.555 128.747 158.403 128.913 L 155.07 128.878 Q 151.495 128.878 149.858 128.999 L 149.693 129.011 Q 144.806 129.393 143.794 128.744 Q 141.688 127.395 138.983 126.412 L 138.766 126.333 " }
            }
            ShapePath {
                id: _qt_node109_fill
                objectName: "svg_fill_path:path32"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 133.706 135.223 Q 134.707 135.503 135.961 135.968 Q 138.469 136.897 139.731 137.821 Q 140.33 138.26 141.065 140.703 Q 141.672 142.719 142.08 145.011 Q 142.516 147.454 143.993 149.568 Q 144.731 150.625 145.382 151.193 L 142.512 149.496 Q 139.416 147.709 137.939 146.994 L 137.79 146.923 Q 133.367 144.81 132.814 143.742 Q 131.665 141.52 129.814 139.317 L 129.665 139.14 L 133.706 135.223 " }
            }
            ShapePath {
                id: _qt_node109_stroke
                objectName: "svg_stroke_path:path32"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 133.706 135.223 Q 134.707 135.503 135.961 135.968 Q 138.469 136.897 139.731 137.821 Q 140.33 138.26 141.065 140.703 Q 141.672 142.719 142.08 145.011 Q 142.516 147.454 143.993 149.568 Q 144.731 150.625 145.382 151.193 L 142.512 149.496 Q 139.416 147.709 137.939 146.994 L 137.79 146.923 Q 133.367 144.81 132.814 143.742 Q 131.665 141.52 129.814 139.317 L 129.665 139.14 " }
            }
            ShapePath {
                id: _qt_node110_fill
                objectName: "svg_fill_path:path33-8"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 120.838 144.309 Q 121.566 145.052 122.42 146.081 Q 124.127 148.14 124.757 149.571 Q 125.057 150.251 124.472 152.734 Q 123.989 154.784 123.197 156.973 Q 122.353 159.307 122.575 161.876 Q 122.686 163.16 122.965 163.977 L 121.329 161.073 Q 119.541 157.977 118.619 156.619 L 118.525 156.483 Q 115.751 152.442 115.807 151.24 Q 115.923 148.742 115.421 145.908 L 115.381 145.681 L 120.838 144.309 " }
            }
            ShapePath {
                id: _qt_node110_stroke
                objectName: "svg_stroke_path:path33-8"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 120.838 144.309 Q 121.566 145.052 122.42 146.081 Q 124.127 148.14 124.757 149.571 Q 125.057 150.251 124.472 152.734 Q 123.989 154.784 123.197 156.973 Q 122.353 159.307 122.575 161.876 Q 122.686 163.16 122.965 163.977 L 121.329 161.073 Q 119.541 157.977 118.619 156.619 L 118.525 156.483 Q 115.751 152.442 115.807 151.24 Q 115.923 148.742 115.421 145.908 L 115.381 145.681 " }
            }
            ShapePath {
                id: _qt_node111_fill
                objectName: "svg_fill_path:path34-5"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 105.152 145.744 Q 105.411 146.751 105.635 148.069 Q 106.084 150.706 105.915 152.26 Q 105.834 152.999 104.086 154.857 Q 102.643 156.39 100.863 157.89 Q 98.9647 159.49 97.8723 161.825 Q 97.3261 162.993 97.1596 163.841 L 97.1944 160.507 Q 97.1946 156.932 97.0743 155.295 L 97.0617 155.131 Q 96.68 150.244 97.3289 149.231 Q 98.6782 147.125 99.6608 144.42 L 99.7397 144.203 L 105.152 145.744 " }
            }
            ShapePath {
                id: _qt_node111_stroke
                objectName: "svg_stroke_path:path34-5"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 105.152 145.744 Q 105.411 146.751 105.635 148.069 Q 106.084 150.706 105.915 152.26 Q 105.834 152.999 104.086 154.857 Q 102.643 156.39 100.863 157.89 Q 98.9647 159.49 97.8723 161.825 Q 97.3261 162.993 97.1596 163.841 L 97.1944 160.507 Q 97.1946 156.932 97.0743 155.295 L 97.0617 155.131 Q 96.68 150.244 97.3289 149.231 Q 98.6782 147.125 99.6608 144.42 L 99.7397 144.203 " }
            }
            ShapePath {
                id: _qt_node112_fill
                objectName: "svg_fill_path:path35-9"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 90.8498 139.143 Q 90.57 140.145 90.1053 141.399 Q 89.176 143.907 88.2521 145.168 Q 87.8127 145.768 85.3698 146.503 Q 83.3537 147.109 81.0619 147.518 Q 78.6184 147.954 76.5047 149.43 Q 75.4478 150.168 74.8797 150.819 L 76.5766 147.95 Q 78.3643 144.854 79.0786 143.376 L 79.1499 143.227 Q 81.2627 138.804 82.3312 138.252 Q 84.5526 137.103 86.756 135.251 L 86.9328 135.103 L 90.8498 139.143 " }
            }
            ShapePath {
                id: _qt_node112_stroke
                objectName: "svg_stroke_path:path35-9"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 90.8498 139.143 Q 90.57 140.145 90.1053 141.399 Q 89.176 143.907 88.2521 145.168 Q 87.8127 145.768 85.3698 146.503 Q 83.3537 147.109 81.0619 147.518 Q 78.6184 147.954 76.5047 149.43 Q 75.4478 150.168 74.8797 150.819 L 76.5766 147.95 Q 78.3643 144.854 79.0786 143.376 L 79.1499 143.227 Q 81.2627 138.804 82.3312 138.252 Q 84.5526 137.103 86.756 135.251 L 86.9328 135.103 " }
            }
            ShapePath {
                id: _qt_node113_fill
                objectName: "svg_fill_path:path36-7"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 81.764 126.276 Q 81.0208 127.003 79.9914 127.857 Q 77.9327 129.564 76.5019 130.195 Q 75.8215 130.494 73.3384 129.909 Q 71.2893 129.426 69.1 128.635 Q 66.7659 127.79 64.1973 128.012 Q 62.9129 128.123 62.0954 128.403 L 64.9997 126.766 Q 68.0959 124.979 69.4534 124.056 L 69.5895 123.963 Q 73.6308 121.189 74.8324 121.244 Q 77.3307 121.36 80.1646 120.858 L 80.392 120.818 L 81.764 126.276 " }
            }
            ShapePath {
                id: _qt_node113_stroke
                objectName: "svg_stroke_path:path36-7"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 81.764 126.276 Q 81.0208 127.003 79.9914 127.857 Q 77.9327 129.564 76.5019 130.195 Q 75.8215 130.494 73.3384 129.909 Q 71.2893 129.426 69.1 128.635 Q 66.7659 127.79 64.1973 128.012 Q 62.9129 128.123 62.0954 128.403 L 64.9997 126.766 Q 68.0959 124.979 69.4534 124.056 L 69.5895 123.963 Q 73.6308 121.189 74.8324 121.244 Q 77.3307 121.36 80.1646 120.858 L 80.392 120.818 " }
            }
            ShapePath {
                id: _qt_node114_fill
                objectName: "svg_fill_path:path37-5"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 80.3292 110.589 Q 79.3217 110.848 78.0034 111.072 Q 75.3669 111.522 73.8127 111.352 Q 73.0734 111.272 71.2156 109.523 Q 69.6825 108.08 68.1824 106.3 Q 66.5831 104.402 64.2478 103.31 Q 63.0801 102.763 62.2322 102.597 L 65.5657 102.632 Q 69.1408 102.632 70.7777 102.512 L 70.9422 102.499 Q 75.829 102.117 76.8419 102.766 Q 78.9477 104.116 81.6526 105.098 L 81.8697 105.177 L 80.3292 110.589 " }
            }
            ShapePath {
                id: _qt_node114_stroke
                objectName: "svg_stroke_path:path37-5"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 80.3292 110.589 Q 79.3217 110.848 78.0034 111.072 Q 75.3669 111.522 73.8127 111.352 Q 73.0734 111.272 71.2156 109.523 Q 69.6825 108.08 68.1824 106.3 Q 66.5831 104.402 64.2478 103.31 Q 63.0801 102.763 62.2322 102.597 L 65.5657 102.632 Q 69.1408 102.632 70.7777 102.512 L 70.9422 102.499 Q 75.829 102.117 76.8419 102.766 Q 78.9477 104.116 81.6526 105.098 L 81.8697 105.177 " }
            }
            ShapePath {
                id: _qt_node115_fill
                objectName: "svg_fill_path:path38"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 86.9297 96.2872 Q 85.928 96.0073 84.6741 95.5426 Q 82.1662 94.6133 80.9049 93.6895 Q 80.305 93.2501 79.5702 90.8071 Q 78.9639 88.791 78.555 86.4992 Q 78.119 84.0557 76.6427 81.942 Q 75.9045 80.8852 75.2535 80.317 L 78.123 82.0139 Q 81.2191 83.8016 82.6968 84.5159 L 82.8456 84.5872 Q 87.2685 86.7001 87.8212 87.7685 Q 88.9702 89.9899 90.8215 92.1933 L 90.9701 92.3702 L 86.9297 96.2872 " }
            }
            ShapePath {
                id: _qt_node115_stroke
                objectName: "svg_stroke_path:path38"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 86.9297 96.2872 Q 85.928 96.0073 84.6741 95.5426 Q 82.1662 94.6133 80.9049 93.6895 Q 80.305 93.2501 79.5702 90.8071 Q 78.9639 88.791 78.555 86.4992 Q 78.119 84.0557 76.6427 81.942 Q 75.9045 80.8852 75.2535 80.317 L 78.123 82.0139 Q 81.2191 83.8016 82.6968 84.5159 L 82.8456 84.5872 Q 87.2685 86.7001 87.8212 87.7685 Q 88.9702 89.9899 90.8215 92.1933 L 90.9701 92.3702 " }
            }
            ShapePath {
                id: _qt_node116_fill
                objectName: "svg_fill_path:path39-3"
                strokeColor: "transparent"
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 99.7971 87.2014 Q 99.0695 86.4581 98.2159 85.4288 Q 96.5086 83.37 95.8783 81.9393 Q 95.5784 81.2588 96.1636 78.7757 Q 96.6465 76.7266 97.4383 74.5374 Q 98.2825 72.2033 98.0608 69.6346 Q 97.95 68.3503 97.6703 67.5327 L 99.3069 70.437 Q 101.094 73.5332 102.017 74.8907 L 102.11 75.0268 Q 104.884 79.068 104.828 80.2697 Q 104.713 82.7681 105.214 85.6019 L 105.255 85.8293 L 99.7971 87.2014 " }
            }
            ShapePath {
                id: _qt_node116_stroke
                objectName: "svg_stroke_path:path39-3"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 99.7971 87.2014 Q 99.0695 86.4581 98.2159 85.4288 Q 96.5086 83.37 95.8783 81.9393 Q 95.5784 81.2588 96.1636 78.7757 Q 96.6465 76.7266 97.4383 74.5374 Q 98.2825 72.2033 98.0608 69.6346 Q 97.95 68.3503 97.6703 67.5327 L 99.3069 70.437 Q 101.094 73.5332 102.017 74.8907 L 102.11 75.0268 Q 104.884 79.068 104.828 80.2697 Q 104.713 82.7681 105.214 85.6019 L 105.255 85.8293 " }
            }
        }
        Item { // Structure node
            objectName: "layer8"
            id: _qt_node117
            transformOrigin: Item.TopLeft
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g223"
                id: _qt_node118
                transformOrigin: Item.TopLeft
                ShapePath {
                    id: _qt_node119_stroke
                    objectName: "svg_stroke_path:path208"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node120_stroke
                    objectName: "svg_stroke_path:path209"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node121_stroke
                    objectName: "svg_stroke_path:path210"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node122_stroke
                    objectName: "svg_stroke_path:path211"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node123_stroke
                    objectName: "svg_stroke_path:path212"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node124_stroke
                    objectName: "svg_stroke_path:path213"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node125_stroke
                    objectName: "svg_stroke_path:path214"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node126_stroke
                    objectName: "svg_stroke_path:path215"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node127_stroke
                    objectName: "svg_stroke_path:path216"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node128_stroke
                    objectName: "svg_stroke_path:path217"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node129_stroke
                    objectName: "svg_stroke_path:path218"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node130_stroke
                    objectName: "svg_stroke_path:path219"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node131_stroke
                    objectName: "svg_stroke_path:path220"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node132_stroke
                    objectName: "svg_stroke_path:path221"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node133_stroke
                    objectName: "svg_stroke_path:path222"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node134_stroke
                    objectName: "svg_stroke_path:path223"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g239"
                id: _qt_node135
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node135_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.866025, 0.5, -0.5, 0.866025, 42.0482, -24.2765)}
                }
                ShapePath {
                    id: _qt_node136_stroke
                    objectName: "svg_stroke_path:path224"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node137_stroke
                    objectName: "svg_stroke_path:path225"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node138_stroke
                    objectName: "svg_stroke_path:path226"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node139_stroke
                    objectName: "svg_stroke_path:path227"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node140_stroke
                    objectName: "svg_stroke_path:path228"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node141_stroke
                    objectName: "svg_stroke_path:path229"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node142_stroke
                    objectName: "svg_stroke_path:path230"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node143_stroke
                    objectName: "svg_stroke_path:path231"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node144_stroke
                    objectName: "svg_stroke_path:path232"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node145_stroke
                    objectName: "svg_stroke_path:path233"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node146_stroke
                    objectName: "svg_stroke_path:path234"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node147_stroke
                    objectName: "svg_stroke_path:path235"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node148_stroke
                    objectName: "svg_stroke_path:path236"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node149_stroke
                    objectName: "svg_stroke_path:path237"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node150_stroke
                    objectName: "svg_stroke_path:path238"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node151_stroke
                    objectName: "svg_stroke_path:path239"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g255"
                id: _qt_node152
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node152_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.5, 0.866025, -0.866025, 0.5, 90.6012, -24.2765)}
                }
                ShapePath {
                    id: _qt_node153_stroke
                    objectName: "svg_stroke_path:path240"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node154_stroke
                    objectName: "svg_stroke_path:path241"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node155_stroke
                    objectName: "svg_stroke_path:path242"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node156_stroke
                    objectName: "svg_stroke_path:path243"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node157_stroke
                    objectName: "svg_stroke_path:path244"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node158_stroke
                    objectName: "svg_stroke_path:path245"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node159_stroke
                    objectName: "svg_stroke_path:path246"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node160_stroke
                    objectName: "svg_stroke_path:path247"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node161_stroke
                    objectName: "svg_stroke_path:path248"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node162_stroke
                    objectName: "svg_stroke_path:path249"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node163_stroke
                    objectName: "svg_stroke_path:path250"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node164_stroke
                    objectName: "svg_stroke_path:path251"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node165_stroke
                    objectName: "svg_stroke_path:path252"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node166_stroke
                    objectName: "svg_stroke_path:path253"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node167_stroke
                    objectName: "svg_stroke_path:path254"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node168_stroke
                    objectName: "svg_stroke_path:path255"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g271"
                id: _qt_node169
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node169_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0, 1, -1, 0, 132.649, 4e-06)}
                }
                ShapePath {
                    id: _qt_node170_stroke
                    objectName: "svg_stroke_path:path256"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node171_stroke
                    objectName: "svg_stroke_path:path257"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node172_stroke
                    objectName: "svg_stroke_path:path258"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node173_stroke
                    objectName: "svg_stroke_path:path259"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node174_stroke
                    objectName: "svg_stroke_path:path260"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node175_stroke
                    objectName: "svg_stroke_path:path261"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node176_stroke
                    objectName: "svg_stroke_path:path262"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node177_stroke
                    objectName: "svg_stroke_path:path263"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node178_stroke
                    objectName: "svg_stroke_path:path264"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node179_stroke
                    objectName: "svg_stroke_path:path265"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node180_stroke
                    objectName: "svg_stroke_path:path266"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node181_stroke
                    objectName: "svg_stroke_path:path267"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node182_stroke
                    objectName: "svg_stroke_path:path268"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node183_stroke
                    objectName: "svg_stroke_path:path269"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node184_stroke
                    objectName: "svg_stroke_path:path270"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node185_stroke
                    objectName: "svg_stroke_path:path271"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g287"
                id: _qt_node186
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node186_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.5, 0.866025, -0.866025, -0.5, 156.926, 42.0482)}
                }
                ShapePath {
                    id: _qt_node187_stroke
                    objectName: "svg_stroke_path:path272"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node188_stroke
                    objectName: "svg_stroke_path:path273"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node189_stroke
                    objectName: "svg_stroke_path:path274"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node190_stroke
                    objectName: "svg_stroke_path:path275"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node191_stroke
                    objectName: "svg_stroke_path:path276"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node192_stroke
                    objectName: "svg_stroke_path:path277"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node193_stroke
                    objectName: "svg_stroke_path:path278"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node194_stroke
                    objectName: "svg_stroke_path:path279"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node195_stroke
                    objectName: "svg_stroke_path:path280"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node196_stroke
                    objectName: "svg_stroke_path:path281"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node197_stroke
                    objectName: "svg_stroke_path:path282"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node198_stroke
                    objectName: "svg_stroke_path:path283"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node199_stroke
                    objectName: "svg_stroke_path:path284"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node200_stroke
                    objectName: "svg_stroke_path:path285"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node201_stroke
                    objectName: "svg_stroke_path:path286"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node202_stroke
                    objectName: "svg_stroke_path:path287"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g303"
                id: _qt_node203
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node203_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.866025, 0.5, -0.5, -0.866025, 156.926, 90.6012)}
                }
                ShapePath {
                    id: _qt_node204_stroke
                    objectName: "svg_stroke_path:path288"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node205_stroke
                    objectName: "svg_stroke_path:path289"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node206_stroke
                    objectName: "svg_stroke_path:path290"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node207_stroke
                    objectName: "svg_stroke_path:path291"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node208_stroke
                    objectName: "svg_stroke_path:path292"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node209_stroke
                    objectName: "svg_stroke_path:path293"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node210_stroke
                    objectName: "svg_stroke_path:path294"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node211_stroke
                    objectName: "svg_stroke_path:path295"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node212_stroke
                    objectName: "svg_stroke_path:path296"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node213_stroke
                    objectName: "svg_stroke_path:path297"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node214_stroke
                    objectName: "svg_stroke_path:path298"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node215_stroke
                    objectName: "svg_stroke_path:path299"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node216_stroke
                    objectName: "svg_stroke_path:path300"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node217_stroke
                    objectName: "svg_stroke_path:path301"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node218_stroke
                    objectName: "svg_stroke_path:path302"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node219_stroke
                    objectName: "svg_stroke_path:path303"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g319"
                id: _qt_node220
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node220_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-1, 0, 0, -1, 132.649, 132.649)}
                }
                ShapePath {
                    id: _qt_node221_stroke
                    objectName: "svg_stroke_path:path304"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node222_stroke
                    objectName: "svg_stroke_path:path305"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node223_stroke
                    objectName: "svg_stroke_path:path306"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node224_stroke
                    objectName: "svg_stroke_path:path307"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node225_stroke
                    objectName: "svg_stroke_path:path308"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node226_stroke
                    objectName: "svg_stroke_path:path309"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node227_stroke
                    objectName: "svg_stroke_path:path310"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node228_stroke
                    objectName: "svg_stroke_path:path311"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node229_stroke
                    objectName: "svg_stroke_path:path312"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node230_stroke
                    objectName: "svg_stroke_path:path313"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node231_stroke
                    objectName: "svg_stroke_path:path314"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node232_stroke
                    objectName: "svg_stroke_path:path315"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node233_stroke
                    objectName: "svg_stroke_path:path316"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node234_stroke
                    objectName: "svg_stroke_path:path317"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node235_stroke
                    objectName: "svg_stroke_path:path318"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node236_stroke
                    objectName: "svg_stroke_path:path319"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g335"
                id: _qt_node237
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node237_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.866025, -0.5, 0.5, -0.866025, 90.6012, 156.926)}
                }
                ShapePath {
                    id: _qt_node238_stroke
                    objectName: "svg_stroke_path:path320"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node239_stroke
                    objectName: "svg_stroke_path:path321"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node240_stroke
                    objectName: "svg_stroke_path:path322"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node241_stroke
                    objectName: "svg_stroke_path:path323"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node242_stroke
                    objectName: "svg_stroke_path:path324"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node243_stroke
                    objectName: "svg_stroke_path:path325"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node244_stroke
                    objectName: "svg_stroke_path:path326"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node245_stroke
                    objectName: "svg_stroke_path:path327"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node246_stroke
                    objectName: "svg_stroke_path:path328"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node247_stroke
                    objectName: "svg_stroke_path:path329"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node248_stroke
                    objectName: "svg_stroke_path:path330"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node249_stroke
                    objectName: "svg_stroke_path:path331"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node250_stroke
                    objectName: "svg_stroke_path:path332"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node251_stroke
                    objectName: "svg_stroke_path:path333"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node252_stroke
                    objectName: "svg_stroke_path:path334"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node253_stroke
                    objectName: "svg_stroke_path:path335"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g351"
                id: _qt_node254
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node254_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.5, -0.866025, 0.866025, -0.5, 42.0482, 156.926)}
                }
                ShapePath {
                    id: _qt_node255_stroke
                    objectName: "svg_stroke_path:path336"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node256_stroke
                    objectName: "svg_stroke_path:path337"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node257_stroke
                    objectName: "svg_stroke_path:path338"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node258_stroke
                    objectName: "svg_stroke_path:path339"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node259_stroke
                    objectName: "svg_stroke_path:path340"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node260_stroke
                    objectName: "svg_stroke_path:path341"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node261_stroke
                    objectName: "svg_stroke_path:path342"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node262_stroke
                    objectName: "svg_stroke_path:path343"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node263_stroke
                    objectName: "svg_stroke_path:path344"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node264_stroke
                    objectName: "svg_stroke_path:path345"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node265_stroke
                    objectName: "svg_stroke_path:path346"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node266_stroke
                    objectName: "svg_stroke_path:path347"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node267_stroke
                    objectName: "svg_stroke_path:path348"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node268_stroke
                    objectName: "svg_stroke_path:path349"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node269_stroke
                    objectName: "svg_stroke_path:path350"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node270_stroke
                    objectName: "svg_stroke_path:path351"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g367"
                id: _qt_node271
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node271_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0, -1, 1, 0, -4e-06, 132.649)}
                }
                ShapePath {
                    id: _qt_node272_stroke
                    objectName: "svg_stroke_path:path352"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node273_stroke
                    objectName: "svg_stroke_path:path353"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node274_stroke
                    objectName: "svg_stroke_path:path354"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node275_stroke
                    objectName: "svg_stroke_path:path355"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node276_stroke
                    objectName: "svg_stroke_path:path356"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node277_stroke
                    objectName: "svg_stroke_path:path357"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node278_stroke
                    objectName: "svg_stroke_path:path358"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node279_stroke
                    objectName: "svg_stroke_path:path359"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node280_stroke
                    objectName: "svg_stroke_path:path360"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node281_stroke
                    objectName: "svg_stroke_path:path361"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node282_stroke
                    objectName: "svg_stroke_path:path362"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node283_stroke
                    objectName: "svg_stroke_path:path363"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node284_stroke
                    objectName: "svg_stroke_path:path364"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node285_stroke
                    objectName: "svg_stroke_path:path365"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node286_stroke
                    objectName: "svg_stroke_path:path366"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node287_stroke
                    objectName: "svg_stroke_path:path367"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g383"
                id: _qt_node288
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node288_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.5, -0.866025, 0.866025, 0.5, -24.2765, 90.6012)}
                }
                ShapePath {
                    id: _qt_node289_stroke
                    objectName: "svg_stroke_path:path368"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node290_stroke
                    objectName: "svg_stroke_path:path369"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node291_stroke
                    objectName: "svg_stroke_path:path370"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node292_stroke
                    objectName: "svg_stroke_path:path371"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node293_stroke
                    objectName: "svg_stroke_path:path372"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node294_stroke
                    objectName: "svg_stroke_path:path373"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node295_stroke
                    objectName: "svg_stroke_path:path374"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node296_stroke
                    objectName: "svg_stroke_path:path375"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node297_stroke
                    objectName: "svg_stroke_path:path376"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node298_stroke
                    objectName: "svg_stroke_path:path377"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node299_stroke
                    objectName: "svg_stroke_path:path378"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node300_stroke
                    objectName: "svg_stroke_path:path379"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node301_stroke
                    objectName: "svg_stroke_path:path380"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node302_stroke
                    objectName: "svg_stroke_path:path381"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node303_stroke
                    objectName: "svg_stroke_path:path382"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node304_stroke
                    objectName: "svg_stroke_path:path383"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
            Shape {
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                objectName: "g399"
                id: _qt_node305
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node305_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.866025, -0.5, 0.5, 0.866025, -24.2765, 42.0482)}
                }
                ShapePath {
                    id: _qt_node306_stroke
                    objectName: "svg_stroke_path:path384"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9505 27.5082 Q 63.3865 28.8178 63.3763 29.6501 Q 63.3686 30.2806 63.7394 31.23 Q 64.2048 32.4219 63.3552 34.7677 " }
                }
                ShapePath {
                    id: _qt_node307_stroke
                    objectName: "svg_stroke_path:path385"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 68.1761 27.5082 Q 68.7401 28.8178 68.7503 29.6501 Q 68.758 30.2806 68.3873 31.23 Q 67.9219 32.4219 68.7714 34.7677 " }
                }
                ShapePath {
                    id: _qt_node308_stroke
                    objectName: "svg_stroke_path:path386"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 66.1128 34.2635 Q 65.5423 32.3793 65.8817 29.5912 Q 66.0769 27.9872 66.7778 25.1113 L 66.9872 24.2417 Q 67.5241 21.9524 67.065 18.9348 Q 67.0182 18.6271 66.8226 17.876 Q 65.5429 12.9624 67.7333 7.9375 " }
                }
                ShapePath {
                    id: _qt_node309_stroke
                    objectName: "svg_stroke_path:path387"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.2937 11.6417 Q 65.1867 11.5755 65.782 11.807 " }
                }
                ShapePath {
                    id: _qt_node310_stroke
                    objectName: "svg_stroke_path:path388"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.9961 12.8984 Q 64.9552 12.8323 65.5836 13.0307 " }
                }
                ShapePath {
                    id: _qt_node311_stroke
                    objectName: "svg_stroke_path:path389"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.6323 14.2875 Q 64.8229 14.2544 65.6497 14.4198 " }
                }
                ShapePath {
                    id: _qt_node312_stroke
                    objectName: "svg_stroke_path:path390"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.7315 15.9742 Q 65.0544 15.9411 65.8812 16.1727 " }
                }
                ShapePath {
                    id: _qt_node313_stroke
                    objectName: "svg_stroke_path:path391"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 63.963 18.3555 Q 64.4798 18.1901 65.2818 18.2066 Q 65.6828 18.2149 65.9805 18.2562 " }
                }
                ShapePath {
                    id: _qt_node314_stroke
                    objectName: "svg_stroke_path:path392"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1945 20.0753 Q 65.319 20.0091 66.1458 20.1745 " }
                }
                ShapePath {
                    id: _qt_node315_stroke
                    objectName: "svg_stroke_path:path393"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.1615 22.225 Q 65.2198 22.1588 66.1458 22.4565 " }
                }
                ShapePath {
                    id: _qt_node316_stroke
                    objectName: "svg_stroke_path:path394"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 64.0622 23.8125 Q 65.0214 23.7133 65.8812 23.9779 " }
                }
                ShapePath {
                    id: _qt_node317_stroke
                    objectName: "svg_stroke_path:path395"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 55.1406 24.0918 L 55.9325 29.0171 L 56.1653 30.4323 Q 56.5429 32.7403 57.1094 33.8764 Q 57.3617 34.3825 57.9012 35.0351 Q 58.171 35.3614 58.3902 35.5865 " }
                }
                ShapePath {
                    id: _qt_node318_stroke
                    objectName: "svg_stroke_path:path396"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.3466 27.9135 Q 53.6567 28.4303 54.4422 28.4716 Q 54.8349 28.4923 55.1656 28.4096 " }
                }
                ShapePath {
                    id: _qt_node319_stroke
                    objectName: "svg_stroke_path:path397"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.8836 29.9641 Q 53.5657 30.2948 54.467 30.3113 Q 54.9176 30.3196 55.2318 30.2617 " }
                }
                ShapePath {
                    id: _qt_node320_stroke
                    objectName: "svg_stroke_path:path398"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 52.6852 32.0146 Q 53.5947 32.3453 54.7026 32.3288 Q 55.2566 32.3205 55.6286 32.2461 " }
                }
                ShapePath {
                    id: _qt_node321_stroke
                    objectName: "svg_stroke_path:path399"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.25
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 53.512 33.8997 Q 54.901 34.0982 56.0586 33.7674 " }
                }
            }
        }
        Shape {
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            objectName: "layer3"
            id: _qt_node322
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node322_transform_base_group
                Translate { x: -43.9931; y: -49.4304}
            }
            ShapePath {
                id: _qt_node323_fill_stroke
                objectName: "svg_path:path1-8"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                miterLimit: 4
                fillColor: sunBackgroundColor
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 140.646 115.755 Q 140.646 128.317 131.763 137.2 Q 122.88 146.083 110.318 146.083 Q 97.7553 146.083 88.8724 137.2 Q 79.9894 128.317 79.9894 115.755 Q 79.9894 103.193 88.8724 94.3097 Q 97.7553 85.4268 110.318 85.4268 Q 122.88 85.4268 131.763 94.3097 Q 140.646 103.193 140.646 115.755 " }
            }
        }
        Shape {
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            objectName: "layer7"
            id: _qt_node324
            transformOrigin: Item.TopLeft
            ShapePath {
                id: _qt_node325_stroke
                objectName: "svg_stroke_path:path184"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 39.1015 67.9132 Q 39.008 72.7775 39.6628 77.1273 " }
            }
            ShapePath {
                id: _qt_node326_stroke
                objectName: "svg_stroke_path:path185"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 40.9724 74.6952 Q 41.3934 79.232 42.7965 83.2077 " }
            }
            ShapePath {
                id: _qt_node327_stroke
                objectName: "svg_stroke_path:path186"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 43.2643 78.9982 Q 44.6206 84.0028 45.7432 86.7156 " }
            }
            ShapePath {
                id: _qt_node328_stroke
                objectName: "svg_stroke_path:path187"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 46.7722 84.5173 Q 48.1286 87.464 50.093 90.2703 " }
            }
            ShapePath {
                id: _qt_node329_stroke
                objectName: "svg_stroke_path:path188"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 51.1688 88.68 Q 53.0396 90.9251 55.3315 93.0766 " }
            }
            ShapePath {
                id: _qt_node330_stroke
                objectName: "svg_stroke_path:path189"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 55.2379 91.4864 Q 57.7169 93.5911 60.2425 94.5733 " }
            }
            ShapePath {
                id: _qt_node331_stroke
                objectName: "svg_stroke_path:path190"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 75.737 39.2245 Q 78.1182 40.4151 80.4995 41.8042 " }
            }
            ShapePath {
                id: _qt_node332_stroke
                objectName: "svg_stroke_path:path191"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 82.3516 42.8625 Q 84.3773 43.648 85.849 45.4505 Q 86.5849 46.3517 86.9156 47.0958 " }
            }
            ShapePath {
                id: _qt_node333_stroke
                objectName: "svg_stroke_path:path192"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 87.3125 48.4849 Q 88.9248 49.4771 89.9666 51.3953 Q 90.4875 52.3544 90.6859 53.1151 " }
            }
            ShapePath {
                id: _qt_node334_stroke
                objectName: "svg_stroke_path:path193"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 91.4797 54.1734 Q 92.8853 55.8271 93.5137 58.2083 Q 93.8279 59.399 93.8609 60.2589 " }
            }
            ShapePath {
                id: _qt_node335_stroke
                objectName: "svg_stroke_path:path194"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 88.3059 56.875 Q 90.4398 56.6411 92.7434 57.2024 Q 93.8951 57.483 94.6201 57.8104 " }
            }
            ShapePath {
                id: _qt_node336_stroke
                objectName: "svg_stroke_path:path195"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 86.8559 50.8881 Q 88.6391 50.7127 90.5217 51.2272 Q 91.463 51.4845 92.0476 51.7768 " }
            }
            ShapePath {
                id: _qt_node337_stroke
                objectName: "svg_stroke_path:path196"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 82.1787 44.4336 Q 83.9034 44.4043 85.786 45.0474 Q 86.7273 45.369 87.3236 45.6964 " }
            }
            ShapePath {
                id: _qt_node338_stroke
                objectName: "svg_stroke_path:path197"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 75.1629 40.2241 Q 78.1095 40.1305 81.1965 40.8321 " }
            }
            ShapePath {
                id: _qt_node339_stroke
                objectName: "svg_stroke_path:path203"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 89.3816 79.653 Q 88.4462 82.6932 87.2301 85.9205 " }
            }
            ShapePath {
                id: _qt_node340_stroke
                objectName: "svg_stroke_path:path204"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 85.6866 84.7512 Q 84.7512 87.5575 83.3012 89.5687 " }
            }
            ShapePath {
                id: _qt_node341_stroke
                objectName: "svg_stroke_path:path205"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 90.0575 81.4586 Q 88.4039 83.1453 86.6841 84.4352 " }
            }
            ShapePath {
                id: _qt_node342_stroke
                objectName: "svg_stroke_path:path206"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 85.8904 85.4935 Q 85.5803 86.6717 84.1334 87.7383 Q 83.4099 88.2716 82.7484 88.5693 " }
            }
            ShapePath {
                id: _qt_node343_stroke
                objectName: "svg_stroke_path:path207"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 63.9961 89.0323 Q 64.2497 88.9478 64.6533 88.7571 Q 65.4879 88.3629 66.2995 88.3405 Q 67.1357 88.3174 67.9315 88.6725 Q 68.3295 88.8501 68.5602 89.0323 " }
            }
            ShapePath {
                id: _qt_node344_stroke
                objectName: "svg_stroke_path:path400"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 66.9727 57.0839 Q 67.7168 57.2699 68.4775 57.9396 Q 68.8578 58.2745 69.0893 58.5721 " }
            }
            ShapePath {
                id: _qt_node345_stroke
                objectName: "svg_stroke_path:path401"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 66.9065 59.0682 Q 67.7664 59.4651 68.494 60.1596 " }
            }
            ShapePath {
                id: _qt_node346_stroke
                objectName: "svg_stroke_path:path402"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.1711 61.3833 Q 67.8326 61.7141 68.3948 62.144 " }
            }
            ShapePath {
                id: _qt_node347_stroke
                objectName: "svg_stroke_path:path403"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 66.9396 62.8716 Q 67.6341 63.1693 67.9979 63.5 " }
            }
            ShapePath {
                id: _qt_node348_stroke
                objectName: "svg_stroke_path:path404"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 66.7411 72.8927 Q 67.1959 73.0581 67.7085 73.4715 Q 67.9648 73.6782 68.1302 73.8518 " }
            }
            ShapePath {
                id: _qt_node349_stroke
                objectName: "svg_stroke_path:path405"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.4688 71.8344 Q 68.4609 72.3635 69.2216 73.0581 " }
            }
            ShapePath {
                id: _qt_node350_stroke
                objectName: "svg_stroke_path:path406"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 66.9396 66.4104 Q 67.4357 67.0057 67.8987 67.6672 " }
            }
            ShapePath {
                id: _qt_node351_stroke
                objectName: "svg_stroke_path:path407"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 66.8073 68.2294 Q 67.0967 68.5395 67.5101 68.9281 L 67.8656 69.2547 " }
            }
            ShapePath {
                id: _qt_node352_stroke
                objectName: "svg_stroke_path:path408"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.0719 69.5523 Q 67.5349 70.0484 68.0971 70.5776 " }
            }
            ShapePath {
                id: _qt_node353_stroke
                objectName: "svg_stroke_path:path409"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.3034 64.4591 Q 67.601 64.7898 67.8656 65.0544 " }
            }
            ShapePath {
                id: _qt_node354_stroke
                objectName: "svg_stroke_path:path412"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.0011 58.0209 Q 68.1237 58.6757 68.6382 59.2603 " }
            }
            ShapePath {
                id: _qt_node355_stroke
                objectName: "svg_stroke_path:path413"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.1181 60.453 Q 67.4981 60.6284 68.0009 60.9091 L 68.4277 61.1546 " }
            }
            ShapePath {
                id: _qt_node356_stroke
                objectName: "svg_stroke_path:path414"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.0245 62.1134 Q 67.7963 62.5344 68.2172 62.7916 " }
            }
            ShapePath {
                id: _qt_node357_stroke
                objectName: "svg_stroke_path:path415"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 66.931 70.5558 Q 67.4425 70.8189 68.2084 71.3568 Q 68.5914 71.6257 68.872 71.842 " }
            }
            ShapePath {
                id: _qt_node358_stroke
                objectName: "svg_stroke_path:path416"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.3987 72.4501 Q 68.1704 72.8476 68.6382 73.3621 " }
            }
        }
        Shape {
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            objectName: "layer2-8"
            id: _qt_node362
            transformOrigin: Item.TopLeft
            ShapePath {
                id: _qt_node363_stroke
                objectName: "svg_stroke_path:path2-1"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 47.31 57.9741 Q 48.2899 59.5535 50.4642 60.0868 Q 51.755 60.4035 54.1385 60.394 Q 56.4908 60.3846 58.9293 59.3421 Q 60.1486 58.8209 60.8974 58.3015 " }
            }
            ShapePath {
                id: _qt_node364_stroke
                objectName: "svg_stroke_path:path8"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 48.5826 59.2025 Q 50.8564 59.833 54.4392 59.9018 Q 56.2306 59.9363 57.5673 59.8446 " }
            }
            ShapePath {
                id: _qt_node365_stroke
                objectName: "svg_stroke_path:path24"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 48.4188 59.5147 Q 48.2224 59.7938 47.9123 60.1865 L 47.6415 60.5234 " }
            }
            ShapePath {
                id: _qt_node366_stroke
                objectName: "svg_stroke_path:path25"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 49.5928 60.11 Q 49.3758 60.4408 49.2642 60.8872 Q 49.2084 61.1105 49.196 61.2676 " }
            }
            ShapePath {
                id: _qt_node367_stroke
                objectName: "svg_stroke_path:path26"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 50.8827 60.5069 Q 50.645 60.8583 50.5664 61.3213 Q 50.5271 61.5528 50.5354 61.7141 " }
            }
            ShapePath {
                id: _qt_node368_stroke
                objectName: "svg_stroke_path:path27"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 52.5694 60.7053 Q 52.373 61.2118 52.3854 61.7203 Q 52.3916 61.9745 52.4371 62.1275 " }
            }
            ShapePath {
                id: _qt_node369_stroke
                objectName: "svg_stroke_path:path28"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 53.7766 60.7384 Q 53.7146 61.0691 53.7476 61.4826 Q 53.7642 61.6893 53.7931 61.8298 " }
            }
            ShapePath {
                id: _qt_node370_stroke
                objectName: "svg_stroke_path:path41-2"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 54.7357 60.7384 Q 54.7191 61.3172 54.901 61.8794 " }
            }
            ShapePath {
                id: _qt_node371_stroke
                objectName: "svg_stroke_path:path46"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 56.1413 60.4904 Q 56.1743 61.1353 56.4555 61.5652 " }
            }
            ShapePath {
                id: _qt_node372_stroke
                objectName: "svg_stroke_path:path52"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 57.4146 60.1762 Q 57.5138 60.8542 57.8445 61.218 " }
            }
            ShapePath {
                id: _qt_node373_stroke
                objectName: "svg_stroke_path:path53"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 58.7871 59.7628 Q 58.9111 60.0728 59.1344 60.4697 Q 59.246 60.6681 59.3328 60.8046 " }
            }
            ShapePath {
                id: _qt_node374_stroke
                objectName: "svg_stroke_path:path54"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 50.7712 55.9395 Q 51.8674 55.1502 53.522 55.0333 Q 54.3493 54.9748 54.9573 55.0742 " }
            }
            ShapePath {
                id: _qt_node375_stroke
                objectName: "svg_stroke_path:path55"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 60.2192 52.034 Q 59.693 52.341 59.2837 52.9198 Q 59.0791 53.2092 58.9797 53.4372 " }
            }
            ShapePath {
                id: _qt_node376_stroke
                objectName: "svg_stroke_path:path56"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 59.3305 51.2857 Q 58.6435 51.7973 58.2518 52.5398 Q 58.056 52.911 57.9975 53.18 " }
            }
            ShapePath {
                id: _qt_node377_stroke
                objectName: "svg_stroke_path:path57"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 57.4362 51.4026 Q 56.9831 51.8996 56.72 52.5778 Q 56.5885 52.9169 56.5475 53.1566 " }
            }
            ShapePath {
                id: _qt_node378_stroke
                objectName: "svg_stroke_path:path58"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 55.9161 51.1688 Q 55.2613 52.0574 55.0508 52.9461 " }
            }
            ShapePath {
                id: _qt_node379_stroke
                objectName: "svg_stroke_path:path59"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 54.4896 51.3325 Q 54.1096 51.5517 53.8523 52.1305 Q 53.7237 52.4199 53.6711 52.6655 " }
            }
            ShapePath {
                id: _qt_node380_stroke
                objectName: "svg_stroke_path:path60"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 53.1332 51.4728 Q 52.797 52.0282 52.8029 52.5661 Q 52.8058 52.835 52.8759 52.9929 " }
            }
            ShapePath {
                id: _qt_node381_stroke
                objectName: "svg_stroke_path:path61"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 52.151 51.8469 Q 52.0048 52.3147 52.0984 52.8642 Q 52.1451 53.139 52.2211 53.3203 " }
            }
            ShapePath {
                id: _qt_node382_stroke
                objectName: "svg_stroke_path:path62"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 51.2389 52.4082 Q 51.1951 52.759 51.3412 53.1916 Q 51.4143 53.408 51.4962 53.5541 " }
            }
            ShapePath {
                id: _qt_node383_stroke
                objectName: "svg_stroke_path:path63"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 50.6075 52.9461 Q 50.6075 53.2238 50.7478 53.5103 Q 50.818 53.6535 50.8881 53.7412 " }
            }
            ShapePath {
                id: _qt_node384_stroke
                objectName: "svg_stroke_path:path64-8"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 59.7748 53.5775 Q 59.6287 53.139 59.2311 52.6011 Q 59.0323 52.3322 58.8628 52.151 " }
            }
            ShapePath {
                id: _qt_node385_stroke
                objectName: "svg_stroke_path:path65-9"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 57.4128 53.18 Q 57.0767 52.5368 56.4628 52.0223 Q 56.1558 51.7651 55.9161 51.6365 " }
            }
            ShapePath {
                id: _qt_node386_stroke
                objectName: "svg_stroke_path:path66-3"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 54.6065 52.8058 Q 54.2996 52.3819 53.7441 52.0136 Q 53.4664 51.8294 53.2501 51.73 " }
            }
            ShapePath {
                id: _qt_node387_stroke
                objectName: "svg_stroke_path:path67-6"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 52.6888 53.1332 Q 52.3234 52.7385 51.8148 52.5924 Q 51.5605 52.5193 51.3792 52.5251 " }
            }
            ShapePath {
                id: _qt_node388_stroke
                objectName: "svg_stroke_path:path68-8"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 51.2623 53.6944 Q 50.9261 53.4021 50.359 53.4255 Q 50.0755 53.4372 49.8591 53.5074 " }
            }
            ShapePath {
                id: _qt_node389_stroke
                objectName: "svg_stroke_path:path69-0"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 46.3044 60.6635 Q 46.6845 61.5697 47.7953 62.4935 Q 48.3507 62.9553 48.8301 63.236 " }
            }
            ShapePath {
                id: _qt_node390_stroke
                objectName: "svg_stroke_path:path70-2"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 59.8216 62.1134 Q 59.12 63.0489 57.8104 63.7738 Q 57.1556 64.1363 56.6411 64.3117 " }
            }
            ShapePath {
                id: _qt_node391_stroke
                objectName: "svg_stroke_path:path71-1"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 62.6401 51.4615 Q 62.2267 50.5106 61.1684 49.535 Q 60.6392 49.0471 60.1927 48.7495 " }
            }
        }
        Item { // Structure node
            objectName: "g133"
            id: _qt_node392
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node392_transform_base_group
                Translate { x: -43.9931; y: -49.4304}
            }
            Shape {
                objectName: "path131-0"
                id: _qt_node393
                transformOrigin: Item.TopLeft
                transform: TransformGroup {
                    id: _qt_node393_transform_base_group
                    Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(1, -0.0172806, 0, 1, 0, 1.69111)}
                }
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node393_stroke
                    objectName: "svg_stroke_path:path131-0"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.65
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.MiterJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 113.807 110.817 Q 112.927 112.079 112.625 113.596 L 112.546 113.992 Q 112.265 115.384 112.25 116.015 Q 112.227 116.959 112.585 118.247 Q 112.992 119.715 113.374 120.475 Q 113.86 121.444 114.664 122.105 Q 115.746 122.994 115.544 124.07 Q 115.464 124.494 115.196 124.704 Q 114.922 124.92 114.609 124.778 Q 113.772 124.398 113.179 124.488 Q 112.804 124.545 112.123 124.939 L 111.923 125.054 Q 111.323 125.393 110.547 125.411 Q 109.7 125.431 109.041 125.038 Q 108.643 124.801 107.775 124.625 Q 106.812 124.431 106.45 124.61 Q 106.214 124.726 105.964 124.498 Q 105.717 124.273 105.613 123.863 Q 105.345 122.804 106.248 122.053 Q 106.793 121.6 107.354 120.576 Q 107.994 119.406 108.258 118.141 Q 108.932 114.903 108.669 113.984 L 108.581 113.659 Q 108.356 112.81 108.143 112.328 Q 107.773 111.494 107.15 110.853 " }
                }
            }
            Shape {
                objectName: "path132-5"
                id: _qt_node394
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node394_fill_stroke
                    objectName: "svg_path:path132-5"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.65
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "#00000000"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                    PathSvg { path: "M 103.612 132.725 Q 104.281 133.261 106.568 134.123 Q 109.133 135.089 110.514 135.109 Q 112.161 135.132 115.856 133.193 Q 120.274 130.875 118.456 131.553 Q 117.999 131.723 117.232 131.738 Q 116.771 131.746 115.762 131.684 Q 114.564 131.609 114.014 131.636 Q 113.564 131.657 112.382 131.903 Q 110.988 132.193 110.466 132.19 Q 109.767 132.185 108.407 131.901 Q 107.375 131.685 106.827 131.629 Q 106.31 131.575 105.31 131.656 Q 103.726 131.784 102.783 131.511 Q 102.101 131.314 102.414 131.701 Q 102.571 131.895 102.864 132.128 L 103.612 132.725 " }
                }
            }
            Shape {
                objectName: "path133-1"
                id: _qt_node395
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                ShapePath {
                    id: _qt_node395_stroke
                    objectName: "svg_stroke_path:path133-1"
                    strokeColor: "#ff000000"
                    strokeWidth: 0.65
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    miterLimit: 4
                    fillColor: "transparent"
                    fillRule: ShapePath.WindingFill
                    pathHints: ShapePath.PathQuadratic
                    PathSvg { path: "M 103.227 132.162 Q 104.02 132.319 105.025 132.625 Q 105.759 132.849 106.271 132.979 Q 108.397 133.517 110.255 133.518 Q 112.155 133.518 114.21 132.984 Q 116.678 132.342 117.434 132.228 " }
                }
            }
        }
        Shape {
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            objectName: "g130"
            id: _qt_node396
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node396_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-1, 0, 0, 1, 132.904, 0)}
            }
            ShapePath {
                id: _qt_node397_stroke
                objectName: "svg_stroke_path:path84-1"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 47.31 57.9741 Q 48.2899 59.5535 50.4642 60.0868 Q 51.755 60.4035 54.1385 60.394 Q 56.4908 60.3846 58.9293 59.3421 Q 60.1486 58.8209 60.8974 58.3015 " }
            }
            ShapePath {
                id: _qt_node398_stroke
                objectName: "svg_stroke_path:path85-0"
                strokeColor: "#ff000000"
                strokeWidth: 0.7
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 48.5826 59.2025 Q 50.8564 59.833 54.4392 59.9018 Q 56.2306 59.9363 57.5673 59.8446 " }
            }
            ShapePath {
                id: _qt_node399_stroke
                objectName: "svg_stroke_path:path104-8"
                strokeColor: "#ff000000"
                strokeWidth: 0.5
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 48.4188 59.5147 Q 48.2224 59.7938 47.9123 60.1865 L 47.6415 60.5234 " }
            }
            ShapePath {
                id: _qt_node400_stroke
                objectName: "svg_stroke_path:path105-5"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 49.5928 60.11 Q 49.3758 60.4408 49.2642 60.8872 Q 49.2084 61.1105 49.196 61.2676 " }
            }
            ShapePath {
                id: _qt_node401_stroke
                objectName: "svg_stroke_path:path106-0"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 50.8827 60.5069 Q 50.645 60.8583 50.5664 61.3213 Q 50.5271 61.5528 50.5354 61.7141 " }
            }
            ShapePath {
                id: _qt_node402_stroke
                objectName: "svg_stroke_path:path107-6"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 52.5694 60.7053 Q 52.373 61.2118 52.3854 61.7203 Q 52.3916 61.9745 52.4371 62.1275 " }
            }
            ShapePath {
                id: _qt_node403_stroke
                objectName: "svg_stroke_path:path108-4"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 53.7766 60.7384 Q 53.7146 61.0691 53.7476 61.4826 Q 53.7642 61.6893 53.7931 61.8298 " }
            }
            ShapePath {
                id: _qt_node404_stroke
                objectName: "svg_stroke_path:path109-6"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 54.7357 60.7384 Q 54.7191 61.3172 54.901 61.8794 " }
            }
            ShapePath {
                id: _qt_node405_stroke
                objectName: "svg_stroke_path:path110-2"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 56.1413 60.4904 Q 56.1743 61.1353 56.4555 61.5652 " }
            }
            ShapePath {
                id: _qt_node406_stroke
                objectName: "svg_stroke_path:path111-5"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 57.4146 60.1762 Q 57.5138 60.8542 57.8445 61.218 " }
            }
            ShapePath {
                id: _qt_node407_stroke
                objectName: "svg_stroke_path:path112-8"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 58.7871 59.7628 Q 58.9111 60.0728 59.1344 60.4697 Q 59.246 60.6681 59.3328 60.8046 " }
            }
            ShapePath {
                id: _qt_node408_stroke
                objectName: "svg_stroke_path:path113-6"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 50.7712 55.9395 Q 51.8674 55.1502 53.522 55.0333 Q 54.3493 54.9748 54.9573 55.0742 " }
            }
            ShapePath {
                id: _qt_node409_stroke
                objectName: "svg_stroke_path:path114-2"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 60.2192 52.034 Q 59.693 52.341 59.2837 52.9198 Q 59.0791 53.2092 58.9797 53.4372 " }
            }
            ShapePath {
                id: _qt_node410_stroke
                objectName: "svg_stroke_path:path115-8"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 59.3305 51.2857 Q 58.6435 51.7973 58.2518 52.5398 Q 58.056 52.911 57.9975 53.18 " }
            }
            ShapePath {
                id: _qt_node411_stroke
                objectName: "svg_stroke_path:path116-4"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 57.4362 51.4026 Q 56.9831 51.8996 56.72 52.5778 Q 56.5885 52.9169 56.5475 53.1566 " }
            }
            ShapePath {
                id: _qt_node412_stroke
                objectName: "svg_stroke_path:path117-7"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 55.9161 51.1688 Q 55.2613 52.0574 55.0508 52.9461 " }
            }
            ShapePath {
                id: _qt_node413_stroke
                objectName: "svg_stroke_path:path118-2"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 54.4896 51.3325 Q 54.1096 51.5517 53.8523 52.1305 Q 53.7237 52.4199 53.6711 52.6655 " }
            }
            ShapePath {
                id: _qt_node414_stroke
                objectName: "svg_stroke_path:path119-4"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 53.1332 51.4728 Q 52.797 52.0282 52.8029 52.5661 Q 52.8058 52.835 52.8759 52.9929 " }
            }
            ShapePath {
                id: _qt_node415_stroke
                objectName: "svg_stroke_path:path120-0"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 52.151 51.8469 Q 52.0048 52.3147 52.0984 52.8642 Q 52.1451 53.139 52.2211 53.3203 " }
            }
            ShapePath {
                id: _qt_node416_stroke
                objectName: "svg_stroke_path:path121-6"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 51.2389 52.4082 Q 51.1951 52.759 51.3412 53.1916 Q 51.4143 53.408 51.4962 53.5541 " }
            }
            ShapePath {
                id: _qt_node417_stroke
                objectName: "svg_stroke_path:path122-2"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 50.6075 52.9461 Q 50.6075 53.2238 50.7478 53.5103 Q 50.818 53.6535 50.8881 53.7412 " }
            }
            ShapePath {
                id: _qt_node418_stroke
                objectName: "svg_stroke_path:path123-9"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 59.7748 53.5775 Q 59.6287 53.139 59.2311 52.6011 Q 59.0323 52.3322 58.8628 52.151 " }
            }
            ShapePath {
                id: _qt_node419_stroke
                objectName: "svg_stroke_path:path124-9"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 57.4128 53.18 Q 57.0767 52.5368 56.4628 52.0223 Q 56.1558 51.7651 55.9161 51.6365 " }
            }
            ShapePath {
                id: _qt_node420_stroke
                objectName: "svg_stroke_path:path125-0"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 54.6065 52.8058 Q 54.2996 52.3819 53.7441 52.0136 Q 53.4664 51.8294 53.2501 51.73 " }
            }
            ShapePath {
                id: _qt_node421_stroke
                objectName: "svg_stroke_path:path126-8"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 52.6888 53.1332 Q 52.3234 52.7385 51.8148 52.5924 Q 51.5605 52.5193 51.3792 52.5251 " }
            }
            ShapePath {
                id: _qt_node422_stroke
                objectName: "svg_stroke_path:path127-1"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 51.2623 53.6944 Q 50.9261 53.4021 50.359 53.4255 Q 50.0755 53.4372 49.8591 53.5074 " }
            }
            ShapePath {
                id: _qt_node423_stroke
                objectName: "svg_stroke_path:path128"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 46.3044 60.6635 Q 46.6845 61.5697 47.7953 62.4935 Q 48.3507 62.9553 48.8301 63.236 " }
            }
            ShapePath {
                id: _qt_node424_stroke
                objectName: "svg_stroke_path:path129"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 59.8216 62.1134 Q 59.12 63.0489 57.8104 63.7738 Q 57.1556 64.1363 56.6411 64.3117 " }
            }
            ShapePath {
                id: _qt_node425_stroke
                objectName: "svg_stroke_path:path130"
                strokeColor: "#ff000000"
                strokeWidth: 0.499999
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 4
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 62.6401 51.4615 Q 62.2267 50.5106 61.1684 49.535 Q 60.6392 49.0471 60.1927 48.7495 " }
            }
        }
    }
}
