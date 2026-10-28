/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// One page of the document view: the rendered page plus search hits,
// highlights, notes, links and text selection on top.
Item {
    id: pageRoot

    required property int pageIndex
    required property Item view

    readonly property size pointSize: view.pageSize(pageIndex)
    readonly property real pageScale: view.renderScale
    readonly property alias selection: selection

    width: pointSize.width * view.renderScale
    height: pointSize.height * view.renderScale

    function highlightColor(name) {
        const c = name === "green" ? LingmoUI.Theme.greenColor
                : name === "blue" ? LingmoUI.Theme.blueColor
                : name === "pink" ? LingmoUI.Theme.pinkColor
                : name === "purple" ? LingmoUI.Theme.purpleColor
                : LingmoUI.Theme.orangeColor
        return Qt.rgba(c.r, c.g, c.b, view.readingMode === 1 ? 0.45 : 0.35)
    }

    LingmoUI.Elevation {
        anchors.fill: parent
        level: 1
        radius: 2
    }

    PageImage {
        id: pageImage
        anchors.fill: parent
        document: pageRoot.view.document
        page: pageRoot.pageIndex
        renderWidth: pageRoot.pointSize.width * pageRoot.view.stableScale * Screen.devicePixelRatio
        readingMode: pageRoot.view.readingMode
    }

    // ---- search hits
    Shape {
        id: searchShape
        anchors.fill: parent
        visible: pageRoot.view.searchModel.searchString.length > 0
        ShapePath {
            strokeWidth: -1
            fillColor: Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                               LingmoUI.Theme.highlightColor.b, 0.28)
            scale: Qt.size(pageRoot.pageScale, pageRoot.pageScale)
            PathMultiline {
                id: searchPaths
            }
        }
        function refresh() {
            searchPaths.paths = pageRoot.view.searchModel.polygonsOnPage(pageRoot.pageIndex)
        }
        Component.onCompleted: refresh()
        Connections {
            target: pageRoot.view.searchModel
            function onRevisionChanged() { searchShape.refresh() }
        }
    }

    Shape {
        anchors.fill: parent
        visible: pageRoot.view.searchModel.searchString.length > 0
                 && pageRoot.view.searchModel.currentPage === pageRoot.pageIndex
                 && pageRoot.view.searchModel.currentResult >= 0
        ShapePath {
            strokeWidth: 2
            strokeColor: LingmoUI.Theme.orangeColor
            fillColor: Qt.rgba(LingmoUI.Theme.orangeColor.r, LingmoUI.Theme.orangeColor.g,
                               LingmoUI.Theme.orangeColor.b, 0.30)
            scale: Qt.size(pageRoot.pageScale, pageRoot.pageScale)
            PathMultiline {
                paths: pageRoot.view.searchModel.currentPolygons
            }
        }
    }

    // ---- read aloud ("Ouvir"): the sentence softly, the word being spoken strongly
    readonly property bool readingHere: readAloud.active && readAloud.highlightEnabled
    Shape {
        id: readSentence
        anchors.fill: parent
        visible: pageRoot.readingHere
        readonly property var polygons: {
            readAloud.highlightRevision   // dependency
            return pageRoot.readingHere ? readAloud.sentencePolygons(pageRoot.pageIndex) : []
        }
        ShapePath {
            strokeWidth: -1
            fillColor: Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                               LingmoUI.Theme.highlightColor.b, pageRoot.view.readingMode === 1 ? 0.24 : 0.14)
            scale: Qt.size(pageRoot.pageScale, pageRoot.pageScale)
            PathMultiline { paths: readSentence.polygons }
        }
    }
    Shape {
        id: readWord
        anchors.fill: parent
        visible: pageRoot.readingHere
        readonly property var polygons: {
            readAloud.highlightRevision
            return pageRoot.readingHere ? readAloud.wordPolygons(pageRoot.pageIndex) : []
        }
        ShapePath {
            strokeWidth: 1
            strokeColor: Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                                 LingmoUI.Theme.highlightColor.b, 0.85)
            fillColor: Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                               LingmoUI.Theme.highlightColor.b, pageRoot.view.readingMode === 1 ? 0.55 : 0.38)
            scale: Qt.size(pageRoot.pageScale, pageRoot.pageScale)
            PathMultiline { paths: readWord.polygons }
        }
    }

    // ---- highlights and notes from the sidecar
    readonly property var pageAnnotations: {
        pageRoot.view.annotationRevision   // dependency
        return pageRoot.view.annotations ? pageRoot.view.annotations.annotationsOnPage(pageIndex) : []
    }

    Repeater {
        model: pageRoot.pageAnnotations.filter(a => a.kind === "highlight")
        delegate: Shape {
            id: hl
            required property var modelData
            anchors.fill: parent
            containsMode: Shape.FillContains
            ShapePath {
                strokeWidth: -1
                fillColor: pageRoot.highlightColor(hl.modelData.colorName)
                scale: Qt.size(pageRoot.pageScale, pageRoot.pageScale)
                PathMultiline { paths: hl.modelData.polygons }
            }
            HoverHandler {
                id: hlHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: pageRoot.view.annotationActivated(hl.modelData.annotationId)
            }
            ToolTip.visible: hlHover.hovered && hl.modelData.note.length > 0
            ToolTip.text: hl.modelData.note
            ToolTip.delay: 400
        }
    }

    // ---- links (internal jumps and web links), from Poppler
    Repeater {
        model: pageRoot.view.document && pageRoot.view.document.ready
               ? pageRoot.view.document.links(pageRoot.pageIndex) : []
        delegate: Item {
            id: link
            required property var modelData
            x: modelData.x * pageRoot.pageScale
            y: modelData.y * pageRoot.pageScale
            width: modelData.width * pageRoot.pageScale
            height: modelData.height * pageRoot.pageScale

            HoverHandler {
                id: linkHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: pageRoot.view.linkActivated(link.modelData.page, Qt.point(0, link.modelData.top),
                                                      link.modelData.url)
            }
            ToolTip.visible: linkHover.hovered
            ToolTip.delay: 700
            ToolTip.text: modelData.page >= 0 ? qsTr("Go to page %1").arg(modelData.page + 1) : modelData.url
        }
    }

    // ---- text selection (drag with the mouse)
    DragHandler {
        id: selectDrag
        acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
        acceptedButtons: Qt.LeftButton
        target: null
        enabled: !pageRoot.view.notePlacement
        cursorShape: Qt.IBeamCursor
        onCentroidChanged: if (active) selection.update()
    }
    TapHandler {
        id: clickTap
        acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
        acceptedButtons: Qt.LeftButton
        onTapped: (eventPoint, button) => {
            const s = pageRoot.pageScale
            if (tapCount === 2 && !pageRoot.view.notePlacement) {
                selection.apply(pageRoot.view.document.selectWord(pageRoot.pageIndex,
                                                              Qt.point(eventPoint.position.x / s, eventPoint.position.y / s)))
                return
            }
            // while reading aloud: a click reads from that sentence (learning mode: translates the word)
            if (readAloud.active && tapCount === 1 && !pageRoot.view.notePlacement) {
                const pt = Qt.point(eventPoint.position.x / s, eventPoint.position.y / s)
                selection.clear()
                if (readAloud.learning)
                    readAloud.translateWord(pageRoot.pageIndex, pt)
                else
                    readAloud.startAt(pageRoot.pageIndex, pt)
                pageRoot.view.forceActiveFocus()
                return
            }
            selection.clear()
            if (pageRoot.view.notePlacement)
                pageRoot.view.notePlaced(pageRoot.pageIndex,
                                         Qt.point(eventPoint.position.x / pageRoot.pageScale,
                                                  eventPoint.position.y / pageRoot.pageScale))
            pageRoot.view.forceActiveFocus()
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint) => pageRoot.view.pageContextMenu(pageRoot, eventPoint.position)
    }
    TapHandler {
        acceptedDevices: PointerDevice.TouchScreen
        onLongPressed: pageRoot.view.pageContextMenu(pageRoot, point.position)
    }
    HoverHandler {
        cursorShape: pageRoot.view.notePlacement ? Qt.CrossCursor
                   : readAloud.active ? Qt.PointingHandCursor : Qt.IBeamCursor
    }

    // Text selection of this page: { text, geometry } from the Poppler text layer
    QtObject {
        id: selection
        property string text: ""
        property var geometry: []

        function apply(result) {
            text = result.text || ""
            geometry = result.polygons !== undefined ? result.polygons : []
            if (text.length > 0)
                pageRoot.view.activeSelection = pageRoot
            else if (pageRoot.view.activeSelection === pageRoot)
                pageRoot.view.activeSelection = null
        }
        function clear() {
            text = ""
            geometry = []
            if (pageRoot.view.activeSelection === pageRoot)
                pageRoot.view.activeSelection = null
        }
        function selectAll() {
            apply(pageRoot.view.document.selectAll(pageRoot.pageIndex))
        }
        function update() {
            const s = pageRoot.pageScale
            const from = selectDrag.centroid.pressPosition
            const to = selectDrag.centroid.position
            apply(pageRoot.view.document.selection(pageRoot.pageIndex, Qt.point(from.x / s, from.y / s),
                                                  Qt.point(to.x / s, to.y / s)))
        }
    }

    Connections {
        target: pageRoot.view
        function onActiveSelectionChanged() {
            if (pageRoot.view.activeSelection !== pageRoot && selection.text.length > 0)
                selection.clear()
        }
    }

    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: -1
            fillColor: Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                               LingmoUI.Theme.highlightColor.b, 0.32)
            scale: Qt.size(pageRoot.pageScale, pageRoot.pageScale)
            PathMultiline { paths: selection.geometry }
        }
    }

    // Note markers sit above the selection so they stay clickable
    Repeater {
        model: pageRoot.pageAnnotations.filter(a => a.kind === "note")
        delegate: Rectangle {
            id: marker
            required property var modelData
            readonly property real markerSize: Math.max(18, 22 * Math.min(1.4, pageRoot.pageScale))
            x: modelData.position.x * pageRoot.pageScale - width / 2
            y: modelData.position.y * pageRoot.pageScale - height / 2
            width: markerSize
            height: markerSize
            radius: LingmoUI.Theme.tinyRadius
            color: markerHover.hovered ? Qt.lighter(LingmoUI.Theme.orangeColor, 1.1) : LingmoUI.Theme.orangeColor
            border.width: 1
            border.color: Qt.darker(LingmoUI.Theme.orangeColor, 1.25)

            LingmoUI.Elevation {
                z: -1
                anchors.fill: parent
                radius: parent.radius
                level: 1
            }
            // Folded "sticky note" lines, drawn so they don't depend on the icon theme
            Column {
                anchors.centerIn: parent
                spacing: Math.max(2, marker.height / 9)
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        width: marker.width * (index === 2 ? 0.32 : 0.5)
                        height: Math.max(1.5, marker.height / 12)
                        radius: height / 2
                        color: LingmoUI.Theme.highlightedTextColor
                    }
                }
            }
            HoverHandler {
                id: markerHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: pageRoot.view.annotationActivated(marker.modelData.annotationId)
            }
            ToolTip.visible: markerHover.hovered
            ToolTip.text: modelData.note
            ToolTip.delay: 300
        }
    }
}
