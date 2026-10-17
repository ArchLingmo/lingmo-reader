/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Left panel: page thumbnails, the outline (bookmarks), the user's notes and,
// while searching, the list of matches.
Rectangle {
    id: panel

    property var document: null      // PdfDocument ("pdf")
    property Item docView
    property var searchModel: null   // SearchModel ("search")
    property bool searching: false
    // 0 pages, 1 outline, 2 notes
    property int tab: 0

    signal annotationActivated(string id)
    signal annotationEditRequested(string id)

    implicitWidth: 264
    color: LingmoUI.Theme.sideBarColor
    Behavior on color { ColorAnimation { duration: LingmoUI.Units.longDuration } }

    function escapeHtml(s) {
        return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The window's close / minimize / zoom buttons sit in this strip
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: LingmoUI.Units.windowHeaderHeight
        }

        LingmoUI.SegmentedControl {
            visible: !panel.searching
            fillWidth: true
            model: [qsTr("Pages"), qsTr("Outline"), qsTr("Notes")]
            currentIndex: panel.tab
            onActivated: (index) => panel.tab = index
            Layout.fillWidth: true
            Layout.leftMargin: LingmoUI.Units.largeSpacing
            Layout.rightMargin: LingmoUI.Units.largeSpacing
            Layout.bottomMargin: LingmoUI.Units.smallSpacing
        }

        RowLayout {
            visible: panel.searching
            Layout.fillWidth: true
            Layout.leftMargin: LingmoUI.Units.largeSpacing + LingmoUI.Units.smallSpacing
            Layout.rightMargin: LingmoUI.Units.largeSpacing
            Layout.preferredHeight: LingmoUI.Units.smallControlHeight
            Layout.bottomMargin: LingmoUI.Units.smallSpacing

            Label {
                text: qsTr("Search results")
                font.pointSize: LingmoUI.Units.fontBody
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            LingmoUI.Badge {
                text: panel.searchModel ? String(panel.searchModel.count) : ""
                subtle: true
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: panel.searching ? 3 : panel.tab

            // ---------------------------------------------------- thumbnails
            ListView {
                id: thumbs
                clip: true
                model: panel.document && panel.document.ready ? panel.document.pageCount : 0
                spacing: LingmoUI.Units.largeSpacing
                topMargin: LingmoUI.Units.smallSpacing
                bottomMargin: LingmoUI.Units.largeSpacing
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                cacheBuffer: 400
                ScrollBar.vertical: ScrollBar {}

                readonly property int currentPage: panel.docView ? panel.docView.currentPage : 0
                onCurrentPageChanged: if (currentPage >= 0 && count > 0) positionViewAtIndex(currentPage, ListView.Contain)

                LingmoUI.WheelHandler { target: thumbs }

                delegate: Item {
                    id: thumb
                    required property int index
                    readonly property size pts: panel.docView ? panel.docView.pageSize(index) : Qt.size(612, 792)
                    readonly property bool current: panel.docView && (panel.docView.currentPage === index
                                                    || (panel.docView.layoutMode === 2 && Math.floor(panel.docView.currentPage / 2) === Math.floor(index / 2)))
                    readonly property real thumbWidth: Math.min(132, thumbs.width - LingmoUI.Units.largeSpacing * 4)
                    width: ListView.view.width
                    height: frame.height + label.implicitHeight + LingmoUI.Units.smallSpacing

                    Rectangle {
                        id: frame
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: thumb.thumbWidth + 8
                        height: thumb.thumbWidth * thumb.pts.height / Math.max(1, thumb.pts.width) + 8
                        radius: LingmoUI.Theme.smallRadius
                        color: thumb.current ? LingmoUI.Theme.highlightColor
                             : thumbHover.hovered ? LingmoUI.Theme.hoverColor : "transparent"
                        Behavior on color { ColorAnimation { duration: LingmoUI.Units.veryShortDuration } }

                        LingmoUI.Elevation {
                            anchors.fill: pageThumb
                            radius: 1
                            level: 1
                        }
                        PageImage {
                            id: pageThumb
                            anchors.centerIn: parent
                            width: thumb.thumbWidth
                            height: parent.height - 8
                            document: panel.document
                            page: thumb.index
                            renderWidth: thumb.thumbWidth * Screen.devicePixelRatio
                            readingMode: panel.docView ? panel.docView.readingMode : 0
                        }
                    }

                    Label {
                        id: label
                        anchors.top: frame.bottom
                        anchors.topMargin: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: panel.document && panel.document.ready ? panel.document.pageLabel(thumb.index) : ""
                        font.pointSize: LingmoUI.Units.fontCaption
                        font.weight: thumb.current ? Font.DemiBold : Font.Normal
                        color: thumb.current ? LingmoUI.Theme.textColor : LingmoUI.Theme.secondaryTextColor
                    }

                    HoverHandler {
                        id: thumbHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: panel.docView.goToPage(thumb.index)
                    }
                }
            }

            // ---------------------------------------------------- outline
            Item {
                ListView {
                    id: outline
                    anchors.fill: parent
                    anchors.leftMargin: LingmoUI.Units.smallSpacing
                    anchors.rightMargin: LingmoUI.Units.smallSpacing
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    // Flattened outline; collapsed entries hide their descendants
                    property var collapsed: ({})
                    readonly property var entries: panel.document && panel.document.ready ? panel.document.outline : []
                    onEntriesChanged: collapsed = ({})
                    model: {
                        const rows = []
                        let hideBelow = -1
                        for (let i = 0; i < entries.length; ++i) {
                            const e = entries[i]
                            if (hideBelow >= 0 && e.depth > hideBelow)
                                continue
                            hideBelow = -1
                            rows.push({ index: i, title: e.title, depth: e.depth, page: e.page, top: e.top,
                                        hasChildren: e.hasChildren, expanded: !collapsed[i] })
                            if (e.hasChildren && collapsed[i])
                                hideBelow = e.depth
                        }
                        return rows
                    }

                    function toggle(i) {
                        const c = Object.assign({}, collapsed)
                        c[i] = !c[i]
                        collapsed = c
                    }

                    LingmoUI.WheelHandler { target: outline }

                    // The entry the reader is in: last one starting at or before the current page
                    readonly property int currentEntry: {
                        if (!panel.docView)
                            return -1
                        const cur = panel.docView.currentPage
                        let best = -1
                        for (let i = 0; i < entries.length; ++i)
                            if (entries[i].page >= 0 && entries[i].page <= cur)
                                best = i
                        return best
                    }

                    delegate: Item {
                        id: node
                        required property var modelData
                        readonly property int depth: modelData.depth
                        readonly property int page: modelData.page
                        readonly property bool hasChildren: modelData.hasChildren
                        readonly property bool expanded: modelData.expanded
                        readonly property bool isCurrent: modelData.index === outline.currentEntry

                        width: ListView.view.width
                        implicitHeight: Math.max(LingmoUI.Units.smallControlHeight + 2, titleText.implicitHeight + 10)

                        Rectangle {
                            anchors.fill: parent
                            anchors.topMargin: 1
                            anchors.bottomMargin: 1
                            radius: LingmoUI.Theme.smallRadius
                            color: node.isCurrent ? Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                                                            LingmoUI.Theme.highlightColor.b, LingmoUI.Theme.darkMode ? 0.3 : 0.15)
                                 : nodeHover.hovered ? LingmoUI.Theme.hoverColor : "transparent"
                            Behavior on color { ColorAnimation { duration: LingmoUI.Units.veryShortDuration } }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: LingmoUI.Units.smallSpacing + node.depth * LingmoUI.Units.largeSpacing
                            anchors.rightMargin: LingmoUI.Units.smallSpacing
                            spacing: 2

                            // Expand arrow, drawn so it doesn't depend on the icon theme
                            Item {
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                opacity: node.hasChildren ? 0.7 : 0
                                Canvas {
                                    id: arrow
                                    anchors.centerIn: parent
                                    width: 8
                                    height: 8
                                    rotation: node.expanded ? 90 : 0
                                    Behavior on rotation { NumberAnimation { duration: LingmoUI.Units.shortDuration } }
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.reset()
                                        ctx.strokeStyle = LingmoUI.Theme.textColor
                                        ctx.lineWidth = 1.5
                                        ctx.beginPath()
                                        ctx.moveTo(2, 0.5)
                                        ctx.lineTo(6, 4)
                                        ctx.lineTo(2, 7.5)
                                        ctx.stroke()
                                    }
                                    Connections {
                                        target: LingmoUI.Theme
                                        function onDarkModeChanged() { arrow.requestPaint() }
                                    }
                                }
                                TapHandler {
                                    enabled: node.hasChildren
                                    onTapped: outline.toggle(node.modelData.index)
                                }
                            }

                            Label {
                                id: titleText
                                text: node.modelData.title
                                elide: Text.ElideRight
                                font.pointSize: LingmoUI.Units.fontBody
                                font.weight: node.depth === 0 ? Font.DemiBold : Font.Normal
                                Layout.fillWidth: true
                            }

                            Label {
                                text: node.page >= 0 ? String(node.page + 1) : ""
                                font.pointSize: LingmoUI.Units.fontCaption
                                color: LingmoUI.Theme.secondaryTextColor
                            }
                        }

                        HoverHandler {
                            id: nodeHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: if (node.page >= 0) panel.docView.goToPage(node.page, Qt.point(0, node.modelData.top))
                        }
                    }
                }

                LingmoUI.EmptyState {
                    anchors.centerIn: parent
                    width: parent.width - LingmoUI.Units.largeSpacing * 2
                    visible: outline.count === 0
                    iconName: "view-list-tree"
                    iconSize: LingmoUI.Units.iconSizes.large
                    title: qsTr("No outline")
                    text: qsTr("This document has no table of contents.")
                }
            }

            // ---------------------------------------------------- notes
            Item {
                ListView {
                    id: notes
                    anchors.fill: parent
                    clip: true
                    model: annotations
                    spacing: LingmoUI.Units.smallSpacing
                    leftMargin: LingmoUI.Units.largeSpacing
                    rightMargin: LingmoUI.Units.largeSpacing
                    topMargin: LingmoUI.Units.smallSpacing
                    bottomMargin: LingmoUI.Units.largeSpacing
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    LingmoUI.WheelHandler { target: notes }

                    delegate: LingmoUI.Card {
                        id: noteCard
                        required property string annotationId
                        required property int page
                        required property string kind
                        required property string quote
                        required property string note
                        required property string colorName

                        width: ListView.view.width - LingmoUI.Units.largeSpacing * 2
                        implicitHeight: noteColumn.implicitHeight + LingmoUI.Units.largeSpacing * 1.5
                        clickable: true
                        padding: LingmoUI.Units.largeSpacing
                        onClicked: panel.annotationActivated(annotationId)
                        onDoubleClicked: panel.annotationEditRequested(annotationId)

                        ColumnLayout {
                            id: noteColumn
                            anchors.fill: parent
                            anchors.margins: noteCard.padding
                            anchors.topMargin: LingmoUI.Units.largeSpacing * 0.75
                            anchors.bottomMargin: LingmoUI.Units.largeSpacing * 0.75
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: LingmoUI.Units.smallSpacing

                                Rectangle {
                                    Layout.preferredWidth: 10
                                    Layout.preferredHeight: 10
                                    radius: kindIsNote ? 2 : 5
                                    readonly property bool kindIsNote: noteCard.kind === "note"
                                    color: noteCard.colorName === "green" ? LingmoUI.Theme.greenColor
                                         : noteCard.colorName === "blue" ? LingmoUI.Theme.blueColor
                                         : noteCard.colorName === "pink" ? LingmoUI.Theme.pinkColor
                                         : noteCard.colorName === "purple" ? LingmoUI.Theme.purpleColor
                                         : LingmoUI.Theme.orangeColor
                                }
                                Label {
                                    text: (noteCard.kind === "note" ? qsTr("Note") : qsTr("Highlight"))
                                          + " · " + qsTr("Page %1").arg(noteCard.page + 1)
                                    font.pointSize: LingmoUI.Units.fontCaption
                                    color: LingmoUI.Theme.secondaryTextColor
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                LingmoUI.IconButton {
                                    size: 22
                                    iconSize: 14
                                    iconName: "document-edit"
                                    tipText: qsTr("Edit note")
                                    opacity: noteCard.hovered ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: LingmoUI.Units.shortDuration } }
                                    onClicked: panel.annotationEditRequested(noteCard.annotationId)
                                }
                                LingmoUI.IconButton {
                                    size: 22
                                    iconSize: 14
                                    iconName: "edit-delete"
                                    tipText: qsTr("Delete")
                                    opacity: noteCard.hovered ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: LingmoUI.Units.shortDuration } }
                                    onClicked: annotations.remove(noteCard.annotationId)
                                }
                            }

                            Label {
                                visible: noteCard.quote.length > 0
                                text: "“" + noteCard.quote + "”"
                                font.pointSize: LingmoUI.Units.fontBody
                                font.italic: true
                                color: LingmoUI.Theme.secondaryTextColor
                                wrapMode: Text.Wrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                visible: noteCard.note.length > 0
                                text: noteCard.note
                                font.pointSize: LingmoUI.Units.fontBody
                                wrapMode: Text.Wrap
                                maximumLineCount: 6
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                LingmoUI.EmptyState {
                    anchors.centerIn: parent
                    width: parent.width - LingmoUI.Units.largeSpacing * 2
                    visible: notes.count === 0
                    iconName: "note"
                    iconSize: LingmoUI.Units.iconSizes.large
                    title: qsTr("No notes yet")
                    text: qsTr("Select text and choose Highlight, or right-click a page to add a note.")
                }
            }

            // ---------------------------------------------------- search results
            Item {
                ListView {
                    id: results
                    anchors.fill: parent
                    clip: true
                    model: panel.searching ? panel.searchModel : null
                    spacing: 2
                    leftMargin: LingmoUI.Units.smallSpacing
                    rightMargin: LingmoUI.Units.smallSpacing
                    bottomMargin: LingmoUI.Units.largeSpacing
                    boundsBehavior: Flickable.StopAtBounds
                    currentIndex: panel.searchModel ? panel.searchModel.currentResult : -1
                    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                    ScrollBar.vertical: ScrollBar {}

                    LingmoUI.WheelHandler { target: results }

                    delegate: Item {
                        id: hit
                        required property int index
                        required property int page
                        required property string contextBefore
                        required property string contextAfter
                        required property string match
                        readonly property bool current: panel.searchModel.currentResult === index

                        width: ListView.view.width - LingmoUI.Units.smallSpacing * 2
                        implicitHeight: hitColumn.implicitHeight + LingmoUI.Units.smallSpacing * 2

                        Rectangle {
                            anchors.fill: parent
                            radius: LingmoUI.Theme.smallRadius
                            color: hit.current ? LingmoUI.Theme.highlightColor
                                 : hitHover.hovered ? LingmoUI.Theme.hoverColor : "transparent"
                            Behavior on color { ColorAnimation { duration: LingmoUI.Units.veryShortDuration } }
                        }

                        ColumnLayout {
                            id: hitColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: LingmoUI.Units.smallSpacing * 1.5
                            anchors.rightMargin: LingmoUI.Units.smallSpacing * 1.5
                            spacing: 1

                            Label {
                                text: qsTr("Page %1").arg(hit.page + 1)
                                font.pointSize: LingmoUI.Units.fontCaption
                                color: hit.current ? LingmoUI.Theme.highlightedTextColor : LingmoUI.Theme.secondaryTextColor
                            }
                            Label {
                                text: panel.escapeHtml(hit.contextBefore) + "<b>"
                                      + panel.escapeHtml(hit.match) + "</b>"
                                      + panel.escapeHtml(hit.contextAfter)
                                textFormat: Text.StyledText
                                font.pointSize: LingmoUI.Units.fontBody
                                color: hit.current ? LingmoUI.Theme.highlightedTextColor : LingmoUI.Theme.textColor
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        HoverHandler {
                            id: hitHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: panel.searchModel.currentResult = hit.index
                        }
                    }
                }

                LingmoUI.EmptyState {
                    anchors.centerIn: parent
                    width: parent.width - LingmoUI.Units.largeSpacing * 2
                    visible: results.count === 0
                    iconName: "edit-find"
                    iconSize: LingmoUI.Units.iconSizes.large
                    title: panel.searchModel && panel.searchModel.searchString.length > 0
                           ? qsTr("No matches") : qsTr("Search the document")
                    text: panel.searchModel && panel.searchModel.searchString.length > 0
                          ? qsTr("Nothing found for “%1”.").arg(panel.searchModel.searchString)
                          : qsTr("Type a word or phrase in the search field.")
                }
            }
        }
    }
}
