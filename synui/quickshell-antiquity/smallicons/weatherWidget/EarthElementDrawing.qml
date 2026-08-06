// Generated from SVG file element_earth_artwork.svg
import QtQuick
import QtQuick.VectorImage
import QtQuick.VectorImage.Helpers
import QtQuick.Shapes
import QtQuick.Effects

Item {
    implicitWidth: 437
    implicitHeight: 215
    property string backgroundColor: '#000000'
    component AnimationsInfo : QtObject
    {
        property bool paused: false
        property int loops: 1
        signal restart()
    }
    property AnimationsInfo animations : AnimationsInfo {}
    transform: [
        Scale { xScale: width / 123.458; yScale: height / 60.808 }
    ]
    objectName: "svg1"
    id: _qt_node0
    transformOrigin: Item.TopLeft
    Item { // Structure node
        objectName: "layer17"
        id: _qt_node8
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node8_transform_base_group
            Translate { x: -47.1854; y: -115.787}
        }
        Shape {
            objectName: "path1550"
            id: _qt_node9
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node9_stroke
                objectName: "svg_stroke_path:path1550"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 64.9552 136.525 Q 64.696 136.154 64.2579 136.04 Q 63.8197 135.925 63.4123 136.123 Q 63.0049 136.32 62.8232 136.735 Q 62.6414 137.15 62.7724 137.583 Q 62.8157 137.727 62.8955 137.865 Q 62.9705 137.994 63.0749 138.115 Q 63.2568 138.325 63.5395 138.517 Q 63.7262 138.644 64.1228 138.861 Q 64.4478 139.038 64.604 139.137 Q 64.8935 139.321 65.0897 139.52 Q 65.3232 139.757 65.4325 140.023 Q 65.4926 140.169 65.5131 140.32 Q 65.5345 140.478 65.5105 140.627 Q 65.4573 140.956 65.2198 141.155 Q 65.0582 141.291 64.7382 141.385 L 64.6177 141.42 Q 64.3522 141.494 64.2276 141.552 Q 64.0275 141.646 63.8222 141.832 Q 63.6993 141.943 63.4739 142.191 L 63.4231 142.246 Q 63.1966 142.494 63.0402 142.619 Q 62.8078 142.806 62.574 142.875 Q 62.2441 142.973 61.6972 142.864 L 61.5301 142.829 Q 61.0614 142.729 60.823 142.734 Q 60.58 142.739 60.3904 142.816 Q 60.2849 142.859 60.1974 142.924 Q 60.1053 142.993 60.041 143.079 Q 59.9737 143.17 59.9409 143.276 Q 59.907 143.386 59.9159 143.494 Q 59.925 143.606 59.9799 143.705 Q 60.0361 143.806 60.1266 143.867 Q 60.1732 143.899 60.2383 143.925 Q 60.2787 143.941 60.3579 143.967 L 60.3587 143.967 Q 60.5197 144.019 60.5896 144.066 Q 60.6579 144.111 60.7112 144.182 Q 60.7611 144.248 60.7934 144.33 Q 60.8481 144.468 60.8542 144.661 Q 60.8656 145.022 60.6471 145.719 L 60.6273 145.782 Q 60.5198 146.127 60.4806 146.341 Q 60.4234 146.654 60.4573 146.91 Q 60.4859 147.126 60.6412 147.534 L 60.7238 147.758 Q 60.764 147.874 60.7885 147.971 Q 60.8177 148.086 60.829 148.19 Q 60.8557 148.433 60.788 148.63 Q 60.7478 148.747 60.6716 148.853 Q 60.5985 148.954 60.4988 149.036 Q 60.4039 149.115 60.2875 149.174 Q 60.1775 149.229 60.0524 149.266 Q 59.6853 149.373 59.049 149.312 L 58.8915 149.296 Q 58.3238 149.238 58.0411 149.267 Q 57.9101 149.281 57.792 149.31 Q 57.6648 149.341 57.5532 149.391 Q 57.4345 149.443 57.3343 149.516 Q 57.2289 149.593 57.15 149.688 Q 56.9864 149.885 56.8602 150.304 L 56.8356 150.387 Q 56.7218 150.772 56.6208 150.945 Q 56.4574 151.224 56.1154 151.464 Q 55.9578 151.575 55.6211 151.764 L 55.4881 151.838 Q 55.0945 152.062 54.8893 152.254 Q 54.589 152.534 54.5042 152.863 Q 54.4468 153.085 54.4831 153.382 Q 54.4991 153.513 54.5531 153.77 L 54.579 153.896 Q 54.6421 154.21 54.6371 154.414 Q 54.6299 154.706 54.5042 154.913 Q 54.3295 155.201 53.9202 155.347 Q 53.7405 155.411 53.3647 155.481 L 53.1978 155.513 Q 52.7464 155.603 52.5049 155.764 Q 52.4255 155.817 52.3605 155.88 Q 52.2918 155.947 52.2419 156.022 Q 52.1896 156.101 52.1593 156.188 Q 52.1278 156.278 52.1229 156.369 Q 52.1168 156.483 52.1528 156.613 Q 52.1817 156.717 52.2426 156.845 L 52.3133 156.987 Q 52.4167 157.188 52.4537 157.295 Q 52.6545 157.876 52.2992 158.377 Q 51.9439 158.879 51.3292 158.882 " }
            }
        }
        Shape {
            objectName: "path1551"
            id: _qt_node10
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node10_stroke
                objectName: "svg_stroke_path:path1551"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 60.7103 148.595 Q 63.3781 149.154 65.1069 151.261 L 65.2748 151.471 Q 65.688 151.992 65.9488 152.197 Q 66.2948 152.468 66.7816 152.615 Q 67.1902 152.738 67.7134 152.778 Q 67.996 152.799 68.5633 152.806 Q 69.2538 152.815 69.597 152.851 Q 70.5309 152.95 72.2957 153.613 Q 73.3567 154.011 73.9 154.161 Q 74.2305 154.252 74.4941 154.295 Q 74.8168 154.348 75.099 154.34 Q 75.7811 154.322 76.2386 153.974 Q 76.338 153.898 76.5174 153.724 Q 76.7107 153.536 76.8194 153.456 Q 76.9959 153.327 77.1674 153.288 Q 77.2655 153.265 77.3611 153.272 Q 77.4626 153.279 77.5482 153.319 Q 77.6131 153.349 77.6707 153.399 Q 77.7252 153.447 77.7697 153.508 Q 77.8482 153.617 77.8972 153.772 Q 77.9328 153.885 77.9726 154.12 Q 77.9986 154.273 78.016 154.348 Q 78.187 155.091 78.8103 155.529 Q 79.4336 155.968 80.1903 155.877 Q 80.9471 155.787 81.4499 155.215 Q 81.9529 154.642 81.9448 153.88 " }
            }
        }
        Shape {
            objectName: "path1552"
            id: _qt_node11
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node11_stroke
                objectName: "svg_stroke_path:path1552"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 63.9548 136.045 L 62.9047 135.665 Q 62.5785 135.459 62.2432 135.268 L 61.0699 134.642 L 60.1266 134.144 Q 59.2408 133.657 58.4068 133.085 Q 58.1349 133.064 57.9746 132.844 Q 57.8142 132.623 57.8776 132.358 Q 57.9074 132.233 57.9932 132.113 Q 58.0655 132.011 58.1795 131.91 Q 58.2436 131.853 58.3793 131.749 Q 58.5337 131.63 58.6052 131.564 Q 59.0425 131.16 59.1437 130.538 Q 59.2373 129.962 59.0021 129.381 Q 58.6037 128.398 57.6111 128.024 Q 56.6185 127.649 55.67 128.124 " }
            }
        }
        Shape {
            objectName: "path1554"
            id: _qt_node12
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node12_stroke
                objectName: "svg_stroke_path:path1554"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 58.9359 129.381 L 59.7958 128.587 L 59.9031 128.488 Q 61.024 127.442 61.6479 127 Q 61.9543 126.783 62.5945 126.389 Q 63.1835 126.027 63.4674 125.83 Q 63.9609 125.488 64.3062 125.144 Q 64.7177 124.734 64.9552 124.288 L 65.0257 124.15 Q 65.0979 124.006 65.1384 123.936 Q 65.2543 123.737 65.388 123.631 Q 65.4689 123.567 65.5601 123.533 Q 65.6578 123.496 65.7536 123.5 Q 65.8043 123.502 65.8515 123.515 Q 65.9006 123.529 65.9428 123.553 Q 66.0333 123.605 66.0797 123.693 Q 66.1136 123.757 66.1189 123.836 Q 66.1239 123.91 66.103 123.985 Q 66.0674 124.113 65.9571 124.245 Q 65.9049 124.307 65.7874 124.419 Q 65.6152 124.585 65.5505 124.685 Q 65.3607 124.979 65.4482 125.336 Q 65.5293 125.668 65.8189 125.888 Q 65.9513 125.989 66.1147 126.051 Q 66.2721 126.11 66.4442 126.128 Q 66.6087 126.145 66.7822 126.124 Q 66.947 126.104 67.1129 126.051 Q 67.6335 125.883 68.246 125.325 L 68.546 125.043 Q 69.0012 124.607 69.2547 124.42 Q 69.929 123.924 70.8865 123.697 Q 71.6658 123.511 72.6943 123.494 L 73.3035 123.492 Q 75.1089 123.497 75.9354 123.164 L 76.0446 123.118 Q 76.2701 123.023 76.3889 122.994 Q 76.4505 122.979 76.5082 122.973 Q 76.5708 122.966 76.6291 122.969 Q 76.691 122.973 76.7488 122.988 Q 76.8091 123.003 76.8615 123.031 Q 76.9696 123.089 77.058 123.206 Q 77.0949 123.255 77.1302 123.317 Q 77.1584 123.367 77.1912 123.435 L 77.2282 123.513 Q 77.3543 123.784 77.4568 123.891 Q 77.6652 124.108 78.0302 124.136 Q 78.2678 124.153 78.662 124.069 L 78.7524 124.049 Q 79.1082 123.97 79.2896 123.96 Q 79.6572 123.94 79.9042 124.09 Q 80.1032 124.21 80.2451 124.455 Q 80.3404 124.62 80.4411 124.92 L 80.468 125.001 Q 80.5561 125.267 80.6137 125.395 Q 80.7319 125.657 80.8963 125.809 Q 81.1472 126.042 81.5803 126.107 Q 81.7863 126.138 82.2038 126.136 L 82.3361 126.135 Q 82.8107 126.136 83.0851 126.223 Q 83.5001 126.355 83.6745 126.669 Q 83.8019 126.899 83.8035 127.357 L 83.8036 127.427 Q 83.8034 127.857 83.8729 128.058 Q 83.9323 128.23 84.0569 128.379 Q 84.1756 128.521 84.3375 128.623 Q 84.4894 128.719 84.6745 128.778 Q 84.8458 128.832 85.0392 128.853 Q 85.4273 128.896 86.2024 128.769 L 86.5187 128.72 Q 86.9465 128.659 87.2168 128.696 Q 87.6103 128.75 87.8417 128.984 Q 87.8701 129.013 87.9231 129.075 Q 88.0338 129.205 88.1062 129.249 Q 88.2413 129.331 88.4272 129.308 Q 88.5038 129.299 88.5886 129.271 Q 88.658 129.248 88.7427 129.208 L 88.8636 129.149 Q 88.9815 129.09 89.0422 129.064 Q 89.2205 128.988 89.363 128.984 Q 89.5753 128.98 89.7429 129.11 Q 89.9106 129.241 89.9583 129.447 " }
            }
        }
        Shape {
            objectName: "path1558"
            id: _qt_node13
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node13_stroke
                objectName: "svg_stroke_path:path1558"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 73.5542 153.988 Q 73.0905 154.825 72.4958 155.575 L 72.3704 155.731 Q 71.8478 156.38 71.7021 156.766 Q 71.5396 157.196 71.5815 157.696 Q 71.6208 158.165 71.8307 158.612 Q 72.1834 159.365 73.025 160.073 L 73.124 160.155 Q 73.3373 160.332 73.4358 160.429 Q 73.6572 160.647 73.7526 160.867 Q 73.8734 161.144 73.8315 161.468 Q 73.8122 161.616 73.7578 161.764 Q 73.7058 161.906 73.6242 162.04 Q 73.476 162.285 73.2307 162.505 Q 73.0265 162.689 72.7366 162.871 Q 72.5829 162.968 72.2662 163.146 Q 71.8699 163.37 71.6809 163.496 Q 71.0787 163.898 70.8422 164.372 Q 70.7405 164.576 70.6951 164.812 Q 70.6517 165.038 70.6629 165.276 Q 70.6736 165.501 70.733 165.734 Q 70.7888 165.954 70.8863 166.174 Q 71.0555 166.556 71.3648 166.967 Q 71.5737 167.244 71.9667 167.68 L 72.0114 167.729 Q 72.4611 168.227 72.6476 168.505 Q 72.9862 169.01 73.0443 169.489 Q 73.0776 169.764 73.0148 170.023 Q 72.9482 170.297 72.7852 170.499 Q 72.609 170.718 72.3602 170.815 Q 72.0971 170.917 71.8344 170.855 Q 71.7086 170.825 71.4767 170.708 L 71.3714 170.656 Q 71.211 170.58 71.0541 170.531 Q 70.8852 170.478 70.7215 170.458 Q 70.5476 170.436 70.3806 170.452 Q 70.2054 170.468 70.0484 170.524 Q 69.5815 170.691 69.3318 171.14 Q 69.094 171.568 69.1609 172.074 Q 69.2243 172.554 69.5467 172.946 Q 69.8503 173.315 70.3149 173.517 Q 70.7433 173.702 71.251 173.711 Q 71.7247 173.719 72.2062 173.574 Q 73.0052 173.332 73.8849 172.641 L 73.9377 172.599 Q 74.1549 172.427 74.2742 172.357 Q 74.512 172.217 74.7288 172.213 Q 74.8541 172.21 74.9649 172.253 Q 75.0827 172.3 75.16 172.391 Q 75.2425 172.488 75.2571 172.608 Q 75.2726 172.735 75.2078 172.839 Q 75.1363 172.954 74.9701 173.019 Q 74.9067 173.044 74.7748 173.078 L 74.6786 173.104 Q 74.3569 173.196 74.0131 173.436 Q 73.8074 173.58 73.4233 173.914 L 73.2896 174.03 Q 71.4027 175.651 69.0562 176.477 " }
            }
        }
        Shape {
            objectName: "path1559"
            id: _qt_node14
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node14_fill_stroke
                objectName: "svg_path:path1559"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1560"
            id: _qt_node15
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node15_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.239506, 0.420736, -0.414837, 0.242912, 111.142, 68.5761)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node15_fill_stroke
                objectName: "svg_path:path1560"
                strokeColor: backgroundColor
                strokeWidth: 0.518236
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1561"
            id: _qt_node16
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node16_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.353171, 0.935559, -0.935559, 0.353171, 149.263, 25.7051)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node16_fill_stroke
                objectName: "svg_path:path1561"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1562"
            id: _qt_node17
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node17_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.309037, 0.372664, -0.373767, -0.302315, 124.358, 153.904)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node17_fill_stroke
                objectName: "svg_path:path1562"
                strokeColor: backgroundColor
                strokeWidth: 0.518236
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1563"
            id: _qt_node18
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node18_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.459008, 0.407521, -0.407521, 0.459008, 83.6936, 69.5102)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node18_fill_stroke
                objectName: "svg_path:path1563"
                strokeColor: backgroundColor
                strokeWidth: 0.407293
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1564"
            id: _qt_node19
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node19_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.0615235, 0.290725, -0.289405, -0.0575561, 106.762, 146.28)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node19_fill_stroke
                objectName: "svg_path:path1564"
                strokeColor: backgroundColor
                strokeWidth: 0.844294
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1565"
            id: _qt_node20
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node20_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.344061, 0.58218, -0.58218, -0.344061, 183.254, 164.436)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node20_fill_stroke
                objectName: "svg_path:path1565"
                strokeColor: backgroundColor
                strokeWidth: 0.369687
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1566"
            id: _qt_node21
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node21_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.327349, -0.00532315, 0.00131055, -0.325086, 105.091, 205.546)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node21_fill_stroke
                objectName: "svg_path:path1566"
                strokeColor: backgroundColor
                strokeWidth: 0.76634
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1568"
            id: _qt_node22
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node22_stroke
                objectName: "svg_stroke_path:path1568"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 62.6279 147.005 L 63.7037 145.882 L 67.0245 142.374 L 71.0002 145.649 L 70.205 150.092 " }
            }
        }
        Shape {
            objectName: "path1569"
            id: _qt_node23
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node23_stroke
                objectName: "svg_stroke_path:path1569"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 71.1405 145.461 L 74.2742 143.824 " }
            }
        }
        Shape {
            objectName: "path1570"
            id: _qt_node24
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node24_stroke
                objectName: "svg_stroke_path:path1570"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.0713 142.187 L 70.8131 141.766 L 71.3743 139.241 L 73.6194 139.568 " }
            }
        }
        Shape {
            objectName: "path1580"
            id: _qt_node25
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node25_stroke
                objectName: "svg_stroke_path:path1580"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 66.4104 121.179 L 68.8578 118.599 " }
            }
        }
        Shape {
            objectName: "path1581"
            id: _qt_node26
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node26_stroke
                objectName: "svg_stroke_path:path1581"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 70.1807 118.136 L 71.9005 120.319 " }
            }
        }
        Shape {
            objectName: "path1582"
            id: _qt_node27
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node27_stroke
                objectName: "svg_stroke_path:path1582"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 68.7255 118.599 L 67.9318 115.821 " }
            }
        }
        Shape {
            objectName: "path1583"
            id: _qt_node28
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node28_stroke
                objectName: "svg_stroke_path:path1583"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 68.5932 118.732 L 70.0484 118.136 " }
            }
        }
        Shape {
            objectName: "path1584"
            id: _qt_node29
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node29_stroke
                objectName: "svg_stroke_path:path1584"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 61.1187 134.607 L 63.3016 130.704 " }
            }
        }
    }
    Item { // Structure node
        objectName: "g1605"
        id: _qt_node30
        transformOrigin: Item.TopLeft
        transform: TransformGroup {
            id: _qt_node30_transform_base_group
            Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-1, 0, 0, 1, 170.644, -115.787)}
        }
        Shape {
            objectName: "path1585"
            id: _qt_node31
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node31_stroke
                objectName: "svg_stroke_path:path1585"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 64.9552 136.525 Q 64.696 136.154 64.2579 136.04 Q 63.8197 135.925 63.4123 136.123 Q 63.0049 136.32 62.8232 136.735 Q 62.6414 137.15 62.7724 137.583 Q 62.8157 137.727 62.8955 137.865 Q 62.9705 137.994 63.0749 138.115 Q 63.2568 138.325 63.5395 138.517 Q 63.7262 138.644 64.1228 138.861 Q 64.4478 139.038 64.604 139.137 Q 64.8935 139.321 65.0897 139.52 Q 65.3232 139.757 65.4325 140.023 Q 65.4926 140.169 65.5131 140.32 Q 65.5345 140.478 65.5105 140.627 Q 65.4573 140.956 65.2198 141.155 Q 65.0582 141.291 64.7382 141.385 L 64.6177 141.42 Q 64.3522 141.494 64.2276 141.552 Q 64.0275 141.646 63.8222 141.832 Q 63.6993 141.943 63.4739 142.191 L 63.4231 142.246 Q 63.1966 142.494 63.0402 142.619 Q 62.8078 142.806 62.574 142.875 Q 62.2441 142.973 61.6972 142.864 L 61.5301 142.829 Q 61.0614 142.729 60.823 142.734 Q 60.58 142.739 60.3904 142.816 Q 60.2849 142.859 60.1974 142.924 Q 60.1053 142.993 60.041 143.079 Q 59.9737 143.17 59.9409 143.276 Q 59.907 143.386 59.9159 143.494 Q 59.925 143.606 59.9799 143.705 Q 60.0361 143.806 60.1266 143.867 Q 60.1732 143.899 60.2383 143.925 Q 60.2787 143.941 60.3579 143.967 L 60.3587 143.967 Q 60.5197 144.019 60.5896 144.066 Q 60.6579 144.111 60.7112 144.182 Q 60.7611 144.248 60.7934 144.33 Q 60.8481 144.468 60.8542 144.661 Q 60.8656 145.022 60.6471 145.719 L 60.6273 145.782 Q 60.5198 146.127 60.4806 146.341 Q 60.4234 146.654 60.4573 146.91 Q 60.4859 147.126 60.6412 147.534 L 60.7238 147.758 Q 60.764 147.874 60.7885 147.971 Q 60.8177 148.086 60.829 148.19 Q 60.8557 148.433 60.788 148.63 Q 60.7478 148.747 60.6716 148.853 Q 60.5985 148.954 60.4988 149.036 Q 60.4039 149.115 60.2875 149.174 Q 60.1775 149.229 60.0524 149.266 Q 59.6853 149.373 59.049 149.312 L 58.8915 149.296 Q 58.3238 149.238 58.0411 149.267 Q 57.9101 149.281 57.792 149.31 Q 57.6648 149.341 57.5532 149.391 Q 57.4345 149.443 57.3343 149.516 Q 57.2289 149.593 57.15 149.688 Q 56.9864 149.885 56.8602 150.304 L 56.8356 150.387 Q 56.7218 150.772 56.6208 150.945 Q 56.4574 151.224 56.1154 151.464 Q 55.9578 151.575 55.6211 151.764 L 55.4881 151.838 Q 55.0945 152.062 54.8893 152.254 Q 54.589 152.534 54.5042 152.863 Q 54.4468 153.085 54.4831 153.382 Q 54.4991 153.513 54.5531 153.77 L 54.579 153.896 Q 54.6421 154.21 54.6371 154.414 Q 54.6299 154.706 54.5042 154.913 Q 54.3295 155.201 53.9202 155.347 Q 53.7405 155.411 53.3647 155.481 L 53.1978 155.513 Q 52.7464 155.603 52.5049 155.764 Q 52.4255 155.817 52.3605 155.88 Q 52.2918 155.947 52.2419 156.022 Q 52.1896 156.101 52.1593 156.188 Q 52.1278 156.278 52.1229 156.369 Q 52.1168 156.483 52.1528 156.613 Q 52.1817 156.717 52.2426 156.845 L 52.3133 156.987 Q 52.4167 157.188 52.4537 157.295 Q 52.6545 157.876 52.2992 158.377 Q 51.9439 158.879 51.3292 158.882 " }
            }
        }
        Shape {
            objectName: "path1586"
            id: _qt_node32
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node32_stroke
                objectName: "svg_stroke_path:path1586"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 60.7103 148.595 Q 63.3781 149.154 65.1069 151.261 L 65.2748 151.471 Q 65.688 151.992 65.9488 152.197 Q 66.2948 152.468 66.7816 152.615 Q 67.1902 152.738 67.7134 152.778 Q 67.996 152.799 68.5633 152.806 Q 69.2538 152.815 69.597 152.851 Q 70.5309 152.95 72.2957 153.613 Q 73.3567 154.011 73.9 154.161 Q 74.2305 154.252 74.4941 154.295 Q 74.8168 154.348 75.099 154.34 Q 75.7811 154.322 76.2386 153.974 Q 76.338 153.898 76.5174 153.724 Q 76.7107 153.536 76.8194 153.456 Q 76.9959 153.327 77.1674 153.288 Q 77.2655 153.265 77.3611 153.272 Q 77.4626 153.279 77.5482 153.319 Q 77.6131 153.349 77.6707 153.399 Q 77.7252 153.447 77.7697 153.508 Q 77.8482 153.617 77.8972 153.772 Q 77.9328 153.885 77.9726 154.12 Q 77.9986 154.273 78.016 154.348 Q 78.187 155.091 78.8103 155.529 Q 79.4336 155.968 80.1903 155.877 Q 80.9471 155.787 81.4499 155.215 Q 81.9529 154.642 81.9448 153.88 " }
            }
        }
        Shape {
            objectName: "path1587"
            id: _qt_node33
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node33_stroke
                objectName: "svg_stroke_path:path1587"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 63.9548 136.045 L 62.9047 135.665 Q 62.5785 135.459 62.2432 135.268 L 61.0699 134.642 L 60.1266 134.144 Q 59.2408 133.657 58.4068 133.085 Q 58.1349 133.064 57.9746 132.844 Q 57.8142 132.623 57.8776 132.358 Q 57.9074 132.233 57.9932 132.113 Q 58.0655 132.011 58.1795 131.91 Q 58.2436 131.853 58.3793 131.749 Q 58.5337 131.63 58.6052 131.564 Q 59.0425 131.16 59.1437 130.538 Q 59.2373 129.962 59.0021 129.381 Q 58.6037 128.398 57.6111 128.024 Q 56.6185 127.649 55.67 128.124 " }
            }
        }
        Shape {
            objectName: "path1588"
            id: _qt_node34
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node34_stroke
                objectName: "svg_stroke_path:path1588"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 58.9359 129.381 L 59.7958 128.587 L 59.9031 128.488 Q 61.024 127.442 61.6479 127 Q 61.9543 126.783 62.5945 126.389 Q 63.1835 126.027 63.4674 125.83 Q 63.9609 125.488 64.3062 125.144 Q 64.7177 124.734 64.9552 124.288 L 65.0257 124.15 Q 65.0979 124.006 65.1384 123.936 Q 65.2543 123.737 65.388 123.631 Q 65.4689 123.567 65.5601 123.533 Q 65.6578 123.496 65.7536 123.5 Q 65.8043 123.502 65.8515 123.515 Q 65.9006 123.529 65.9428 123.553 Q 66.0333 123.605 66.0797 123.693 Q 66.1136 123.757 66.1189 123.836 Q 66.1239 123.91 66.103 123.985 Q 66.0674 124.113 65.9571 124.245 Q 65.9049 124.307 65.7874 124.419 Q 65.6152 124.585 65.5505 124.685 Q 65.3607 124.979 65.4482 125.336 Q 65.5293 125.668 65.8189 125.888 Q 65.9513 125.989 66.1147 126.051 Q 66.2721 126.11 66.4442 126.128 Q 66.6087 126.145 66.7822 126.124 Q 66.947 126.104 67.1129 126.051 Q 67.6335 125.883 68.246 125.325 L 68.546 125.043 Q 69.0012 124.607 69.2547 124.42 Q 69.929 123.924 70.8865 123.697 Q 71.6658 123.511 72.6943 123.494 L 73.3035 123.492 Q 75.1089 123.497 75.9354 123.164 L 76.0446 123.118 Q 76.2701 123.023 76.3889 122.994 Q 76.4505 122.979 76.5082 122.973 Q 76.5708 122.966 76.6291 122.969 Q 76.691 122.973 76.7488 122.988 Q 76.8091 123.003 76.8615 123.031 Q 76.9696 123.089 77.058 123.206 Q 77.0949 123.255 77.1302 123.317 Q 77.1584 123.367 77.1912 123.435 L 77.2282 123.513 Q 77.3543 123.784 77.4568 123.891 Q 77.6652 124.108 78.0302 124.136 Q 78.2678 124.153 78.662 124.069 L 78.7524 124.049 Q 79.1082 123.97 79.2896 123.96 Q 79.6572 123.94 79.9042 124.09 Q 80.1032 124.21 80.2451 124.455 Q 80.3404 124.62 80.4411 124.92 L 80.468 125.001 Q 80.5561 125.267 80.6137 125.395 Q 80.7319 125.657 80.8963 125.809 Q 81.1472 126.042 81.5803 126.107 Q 81.7863 126.138 82.2038 126.136 L 82.3361 126.135 Q 82.8107 126.136 83.0851 126.223 Q 83.5001 126.355 83.6745 126.669 Q 83.8019 126.899 83.8035 127.357 L 83.8036 127.427 Q 83.8034 127.857 83.8729 128.058 Q 83.9323 128.23 84.0569 128.379 Q 84.1756 128.521 84.3375 128.623 Q 84.4894 128.719 84.6745 128.778 Q 84.8458 128.832 85.0392 128.853 Q 85.4273 128.896 86.2024 128.769 L 86.5187 128.72 Q 86.9465 128.659 87.2168 128.696 Q 87.6103 128.75 87.8417 128.984 Q 87.8701 129.013 87.9231 129.075 Q 88.0338 129.205 88.1062 129.249 Q 88.2413 129.331 88.4272 129.308 Q 88.5038 129.299 88.5886 129.271 Q 88.658 129.248 88.7427 129.208 L 88.8636 129.149 Q 88.9815 129.09 89.0422 129.064 Q 89.2205 128.988 89.363 128.984 Q 89.5753 128.98 89.7429 129.11 Q 89.9106 129.241 89.9583 129.447 " }
            }
        }
        Shape {
            objectName: "path1589"
            id: _qt_node35
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node35_stroke
                objectName: "svg_stroke_path:path1589"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 73.5542 153.988 Q 73.0905 154.825 72.4958 155.575 L 72.3704 155.731 Q 71.8478 156.38 71.7021 156.766 Q 71.5396 157.196 71.5815 157.696 Q 71.6208 158.165 71.8307 158.612 Q 72.1834 159.365 73.025 160.073 L 73.124 160.155 Q 73.3373 160.332 73.4358 160.429 Q 73.6572 160.647 73.7526 160.867 Q 73.8734 161.144 73.8315 161.468 Q 73.8122 161.616 73.7578 161.764 Q 73.7058 161.906 73.6242 162.04 Q 73.476 162.285 73.2307 162.505 Q 73.0265 162.689 72.7366 162.871 Q 72.5829 162.968 72.2662 163.146 Q 71.8699 163.37 71.6809 163.496 Q 71.0787 163.898 70.8422 164.372 Q 70.7405 164.576 70.6951 164.812 Q 70.6517 165.038 70.6629 165.276 Q 70.6736 165.501 70.733 165.734 Q 70.7888 165.954 70.8863 166.174 Q 71.0555 166.556 71.3648 166.967 Q 71.5737 167.244 71.9667 167.68 L 72.0114 167.729 Q 72.4611 168.227 72.6476 168.505 Q 72.9862 169.01 73.0443 169.489 Q 73.0776 169.764 73.0148 170.023 Q 72.9482 170.297 72.7852 170.499 Q 72.609 170.718 72.3602 170.815 Q 72.0971 170.917 71.8344 170.855 Q 71.7086 170.825 71.4767 170.708 L 71.3714 170.656 Q 71.211 170.58 71.0541 170.531 Q 70.8852 170.478 70.7215 170.458 Q 70.5476 170.436 70.3806 170.452 Q 70.2054 170.468 70.0484 170.524 Q 69.5815 170.691 69.3318 171.14 Q 69.094 171.568 69.1609 172.074 Q 69.2243 172.554 69.5467 172.946 Q 69.8503 173.315 70.3149 173.517 Q 70.7433 173.702 71.251 173.711 Q 71.7247 173.719 72.2062 173.574 Q 73.0052 173.332 73.8849 172.641 L 73.9377 172.599 Q 74.1549 172.427 74.2742 172.357 Q 74.512 172.217 74.7288 172.213 Q 74.8541 172.21 74.9649 172.253 Q 75.0827 172.3 75.16 172.391 Q 75.2425 172.488 75.2571 172.608 Q 75.2726 172.735 75.2078 172.839 Q 75.1363 172.954 74.9701 173.019 Q 74.9067 173.044 74.7748 173.078 L 74.6786 173.104 Q 74.3569 173.196 74.0131 173.436 Q 73.8074 173.58 73.4233 173.914 L 73.2896 174.03 Q 71.4027 175.651 69.0562 176.477 " }
            }
        }
        Shape {
            objectName: "path1590"
            id: _qt_node36
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node36_fill_stroke
                objectName: "svg_path:path1590"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1591"
            id: _qt_node37
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node37_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.239506, 0.420736, -0.414837, 0.242912, 111.142, 68.5761)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node37_fill_stroke
                objectName: "svg_path:path1591"
                strokeColor: backgroundColor
                strokeWidth: 0.518236
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1592"
            id: _qt_node38
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node38_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.353171, 0.935559, -0.935559, 0.353171, 149.263, 25.7051)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node38_fill_stroke
                objectName: "svg_path:path1592"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1593"
            id: _qt_node39
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node39_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.309037, 0.372664, -0.373767, -0.302315, 124.358, 153.904)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node39_fill_stroke
                objectName: "svg_path:path1593"
                strokeColor: backgroundColor
                strokeWidth: 0.518236
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1594"
            id: _qt_node40
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node40_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(0.459008, 0.407521, -0.407521, 0.459008, 83.6936, 69.5102)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node40_fill_stroke
                objectName: "svg_path:path1594"
                strokeColor: backgroundColor
                strokeWidth: 0.407293
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1595"
            id: _qt_node41
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node41_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.0615235, 0.290725, -0.289405, -0.0575561, 106.762, 146.28)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node41_fill_stroke
                objectName: "svg_path:path1595"
                strokeColor: backgroundColor
                strokeWidth: 0.844294
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1596"
            id: _qt_node42
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node42_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.344061, 0.58218, -0.58218, -0.344061, 183.254, 164.436)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node42_fill_stroke
                objectName: "svg_path:path1596"
                strokeColor: backgroundColor
                strokeWidth: 0.369687
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1597"
            id: _qt_node43
            transformOrigin: Item.TopLeft
            transform: TransformGroup {
                id: _qt_node43_transform_base_group
                Matrix4x4 { matrix: PlanarTransform.fromAffineMatrix(-0.327349, -0.00532315, 0.00131055, -0.325086, 105.091, 205.546)}
            }
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node43_fill_stroke
                objectName: "svg_path:path1597"
                strokeColor: backgroundColor
                strokeWidth: 0.76634
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "#00000000"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic | ShapePath.PathNonIntersecting | ShapePath.PathNonOverlappingControlPointTriangles
                PathSvg { path: "M 79.091 131.895 Q 79.091 131.986 77.7405 132.745 L 75.7442 133.837 L 75.4593 133.992 L 75.1841 134.161 L 73.2402 135.344 Q 71.9065 136.134 71.8275 136.088 Q 71.7486 136.043 71.7661 134.494 L 71.8186 132.219 L 71.8275 131.895 L 71.8186 131.571 L 71.7661 129.296 Q 71.7486 127.747 71.8275 127.701 Q 71.9065 127.656 73.2394 128.445 L 75.1832 129.628 L 75.4593 129.798 L 75.7452 129.953 L 77.7414 131.045 Q 79.091 131.804 79.091 131.895 " }
            }
        }
        Shape {
            objectName: "path1598"
            id: _qt_node44
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node44_stroke
                objectName: "svg_stroke_path:path1598"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 62.6279 147.005 L 63.7037 145.882 L 67.0245 142.374 L 71.0002 145.649 L 70.205 150.092 " }
            }
        }
        Shape {
            objectName: "path1599"
            id: _qt_node45
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node45_stroke
                objectName: "svg_stroke_path:path1599"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 71.1405 145.461 L 74.2742 143.824 " }
            }
        }
        Shape {
            objectName: "path1600"
            id: _qt_node46
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node46_stroke
                objectName: "svg_stroke_path:path1600"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 67.0713 142.187 L 70.8131 141.766 L 71.3743 139.241 L 73.6194 139.568 " }
            }
        }
        Shape {
            objectName: "path1601"
            id: _qt_node47
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node47_stroke
                objectName: "svg_stroke_path:path1601"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 66.4104 121.179 L 68.8578 118.599 " }
            }
        }
        Shape {
            objectName: "path1602"
            id: _qt_node48
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node48_stroke
                objectName: "svg_stroke_path:path1602"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 70.1807 118.136 L 71.9005 120.319 " }
            }
        }
        Shape {
            objectName: "path1603"
            id: _qt_node49
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node49_stroke
                objectName: "svg_stroke_path:path1603"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 68.7255 118.599 L 67.9318 115.821 " }
            }
        }
        Shape {
            objectName: "path1604"
            id: _qt_node50
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node50_stroke
                objectName: "svg_stroke_path:path1604"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 68.5932 118.732 L 70.0484 118.136 " }
            }
        }
        Shape {
            objectName: "path1605"
            id: _qt_node51
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            asynchronous: true
            ShapePath {
                id: _qt_node51_stroke
                objectName: "svg_stroke_path:path1605"
                strokeColor: backgroundColor
                strokeWidth: 0.25
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.RoundJoin
                miterLimit: 1
                fillColor: "transparent"
                fillRule: ShapePath.WindingFill
                pathHints: ShapePath.PathQuadratic
                PathSvg { path: "M 61.1187 134.607 L 63.3016 130.704 " }
            }
        }
    }
}
