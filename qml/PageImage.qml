/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// One rendered page on its paper, tinted for the reading mode
// (0 normal, 1 night, 2 sepia). Shared by the pages and the thumbnails.
Item {
    id: control

    // the PdfDocument ("pdf"); its generation changes when another file opens
    property var document: null
    property int page: 0
    // Width in device pixels the page is rendered at (kept stable while zooming)
    property real renderWidth: width
    property int readingMode: 0
    readonly property alias status: image.status

    // Night: page white turns into this grey, black ink into a soft white
    readonly property color nightPaper: Qt.rgba(0.13, 0.13, 0.14, 1)
    readonly property color nightInk: Qt.rgba(0.86, 0.86, 0.87, 1)
    readonly property color sepiaPaper: Qt.rgba(0.976, 0.937, 0.855, 1)
    readonly property color sepiaInk: Qt.rgba(0.29, 0.21, 0.13, 1)
    readonly property color paperColor: readingMode === 1 ? nightPaper : readingMode === 2 ? sepiaPaper : "white"

    Rectangle {
        anchors.fill: parent
        color: control.paperColor
        Behavior on color { ColorAnimation { duration: LingmoUI.Units.longDuration } }
    }

    Image {
        id: image
        anchors.fill: parent
        asynchronous: true
        retainWhileLoading: true
        cache: false
        smooth: true
        fillMode: Image.Stretch
        // Rendered by Poppler in a worker thread (PageImageProvider)
        source: control.document && control.document.ready
                ? control.document.imageSource(control.page, Math.min(8192, Math.max(16, Math.round(control.renderWidth))))
                : ""

        layer.enabled: control.readingMode !== 0
        layer.effect: ShaderEffect {
            property real mode: control.readingMode
            property color paper: control.readingMode === 1 ? control.nightPaper : control.sepiaPaper
            property color ink: control.readingMode === 1 ? control.nightInk : control.sepiaInk
            fragmentShader: "qrc:/shaders/readingmode.frag.qsb"
        }
    }
}
