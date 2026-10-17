/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

LingmoUI.Window {
    id: rootWindow

    width: 1120
    height: 780
    minimumWidth: 760
    minimumHeight: 560
    visible: true
    title: hasDocument ? documentTitle + " — " + qsTr("Documents") : qsTr("Documents")

    background.opacity: LingmoUI.Theme.darkMode ? 0.88 : 0.97
    header.height: LingmoUI.Units.windowHeaderHeight
    contentTopMargin: 0

    LingmoUI.WindowBlur {
        view: rootWindow
        geometry: Qt.rect(rootWindow.x, rootWindow.y, rootWindow.width, rootWindow.height)
        windowRadius: rootWindow.windowRadius
        enabled: true
    }

    // ------------------------------------------------------------ state

    property string filePath: ""
    property string password: ""
    property int pendingPage: -1
    property bool passwordWanted: false
    property string errorText: ""
    property string infoText: ""
    readonly property var annotationStore: annotations
    readonly property bool hasDocument: filePath.length > 0 && pdf.ready && pdf.path === filePath
    readonly property bool loading: false
    readonly property bool searching: searchBar.visible
    readonly property bool sidebarShown: hasDocument && settings.sidebarVisible
    readonly property string documentTitle: {
        const t = pdf.title ? pdf.title.trim() : ""
        return t.length > 0 && t.toLowerCase() !== "untitled" ? t : reader.fileName(filePath)
    }

    Settings {
        id: settings
        category: "View"
        property bool sidebarVisible: true
        property int sidebarTab: 0
        property int layoutMode: 0
        property int readingMode: 0
        property int zoomMode: 1
        property real customScale: 1
        property alias windowWidth: rootWindow.width
        property alias windowHeight: rootWindow.height
    }

    Binding {
        target: search
        property: "searchString"
        value: rootWindow.searching ? searchField.text : ""
    }

    Connections {
        target: search
        function onCurrentResultChanged() { rootWindow.showCurrentResult() }
        function onCountChanged() {
            // First match at or after the page being read
            if (search.count > 0 && search.currentResult < 0 && search.searchString.length > 0)
                Qt.callLater(rootWindow.selectFirstResult)
        }
    }

    // ------------------------------------------------------------ actions

    function openFile(path, page) {
        path = reader.toLocalPath(path)
        if (path.length === 0)
            return
        if (!reader.exists(path)) {
            errorText = qsTr("“%1” could not be found.").arg(reader.fileName(path))
            recentFiles.remove(path)
            return
        }
        if (!reader.isSupported(path)) {
            errorText = qsTr("“%1” is not a PDF document.").arg(reader.fileName(path))
            return
        }
        saveProgress()
        closeSearch()
        errorText = ""
        password = ""
        passwordWanted = false
        pendingPage = page !== undefined ? page : -1
        if (path === filePath) {
            if (hasDocument && pendingPage >= 0)
                documentView.goToPage(pendingPage)
            return
        }
        documentView.notePlacement = false
        filePath = path
        pdf.load(path)
        if (pdf.ready) {
            documentReady()
        } else if (pdf.locked) {
            passwordWanted = true
            passwordDialog.ask(false)
        } else {
            errorText = qsTr("“%1” could not be opened: %2").arg(reader.fileName(path)).arg(pdf.error)
            filePath = ""
            pdf.close()
        }
    }

    function closeDocument() {
        saveProgress()
        closeSearch()
        documentView.notePlacement = false
        filePath = ""
        password = ""
        pdf.close()
        annotations.documentPath = ""
        documentView.reload()
    }

    function documentReady() {
        passwordWanted = false
        passwordDialog.finish()
        annotations.documentPath = filePath
        documentView.layoutMode = settings.layoutMode
        documentView.reload()
        if (settings.zoomMode === 0)
            documentView.setScale(settings.customScale)
        else
            documentView.applyFit()
        const last = recentFiles.lastPage(filePath)
        const page = pendingPage >= 0 ? pendingPage : Math.max(0, last)
        pendingPage = -1
        recentFiles.add(filePath, pdf.pageCount)
        Qt.callLater(documentView.goToPage, Math.min(page, pdf.pageCount - 1))
        if (last > 0 && page === last)
            infoText = qsTr("Continuing from page %1.").arg(page + 1)
    }

    function saveProgress() {
        if (hasDocument && documentView.currentPage >= 0)
            recentFiles.setLastPage(filePath, documentView.currentPage, pdf.pageCount)
    }

    function openSearch() {
        if (!hasDocument)
            return
        searchBar.visible = true
        if (documentView.selectedText.length > 0 && documentView.selectedText.length < 80 && documentView.selectedText.indexOf("\n") < 0)
            searchField.text = documentView.selectedText
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    function closeSearch() {
        searchBar.visible = false
        search.currentResult = -1
        documentView.forceActiveFocus()
    }

    function selectFirstResult() {
        if (search.count === 0 || search.currentResult >= 0)
            return
        // the model has roles page, indexOnPage, location...
        search.currentResult = search.firstResultFrom(Math.max(0, documentView.currentPage))
    }

    function findNext() {
        if (search.count === 0)
            return
        search.currentResult = (search.currentResult + 1) % search.count
    }

    function findPrevious() {
        if (search.count === 0)
            return
        search.currentResult = (search.currentResult - 1 + search.count) % search.count
    }

    function showCurrentResult() {
        if (search.currentResult < 0)
            return
        documentView.showRect(search.currentPage, search.currentRect)
    }

    function highlightSelection(withNote) {
        const pageItem = documentView.activeSelection
        if (!pageItem)
            return
        const id = annotations.addHighlight(pageItem.pageIndex, pageItem.selection.geometry, pageItem.selection.text)
        documentView.clearSelection()
        if (id.length > 0 && withNote)
            noteDialog.edit(id)
    }

    function askAssistant() {
        if (hasDocument && reader.askAssistant(qsTr("Summarize the document %1").arg(filePath)))
            infoText = qsTr("Lingmo Assistant is reading the document.")
    }

    function printDocument() {
        if (hasDocument)
            // after the menu has closed: the print dialog runs its own event loop
            Qt.callLater(() => reader.print(documentView.currentPage, documentTitle))
    }

    function goToAnnotation(id) {
        const a = annotations.get(id)
        if (!a.page && a.page !== 0)
            return
        documentView.goToPage(a.page, Qt.point(0, Math.max(1, a.top - 40)))
    }

    onClosing: saveProgress()

    Timer {
        // keep the reading position even if the app is killed
        id: progressTimer
        interval: 1500
        onTriggered: rootWindow.saveProgress()
    }
    Connections {
        target: documentView
        function onCurrentPageChanged() { if (rootWindow.hasDocument) progressTimer.restart() }
        function onZoomModeChanged() { settings.zoomMode = documentView.zoomMode }
        function onStableScaleChanged() { if (documentView.zoomMode === 0) settings.customScale = documentView.stableScale }
    }
    Connections {
        target: annotations
        function onSaveFailed(message) { rootWindow.errorText = qsTr("Notes could not be saved: %1").arg(message) }
    }

    Component.onCompleted: {
        documentView.zoomMode = settings.zoomMode
        documentView.readingMode = settings.readingMode
        if (initialFile.length > 0)
            openFile(initialFile, initialPage >= 0 ? initialPage : undefined)
        reader.refreshAssistant()
    }

    // ------------------------------------------------------------ layout

    RowLayout {
        anchors.fill: parent
        spacing: 0

        SidePanel {
            id: sidePanel
            visible: rootWindow.sidebarShown
            Layout.fillHeight: true
            Layout.preferredWidth: implicitWidth
            document: pdf
            docView: documentView
            searchModel: search
            searching: rootWindow.searching
            tab: settings.sidebarTab
            onTabChanged: settings.sidebarTab = tab
            onAnnotationActivated: (id) => rootWindow.goToAnnotation(id)
            onAnnotationEditRequested: (id) => noteDialog.edit(id)
        }

        Rectangle {
            visible: sidePanel.visible
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: LingmoUI.Theme.dividerColor
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ------------------------------------------------ toolbar
            // In the window's header strip: the strip only takes the presses
            // the toolbar leaves (window drag, double click)
            Item {
                id: toolbar
                visible: rootWindow.hasDocument
                Layout.fillWidth: true
                Layout.preferredHeight: rootWindow.header.height

                RowLayout {
                    anchors.fill: parent
                    // Without the sidebar the toolbar shares the strip with the window buttons
                    anchors.leftMargin: rootWindow.sidebarShown ? LingmoUI.Units.largeSpacing
                                                                : LingmoUI.Units.windowControlsInset
                    anchors.rightMargin: LingmoUI.Units.largeSpacing
                    spacing: LingmoUI.Units.smallSpacing

                    LingmoUI.IconButton {
                        iconName: "sidebar-show-symbolic"
                        tipText: settings.sidebarVisible ? qsTr("Hide sidebar (F9)") : qsTr("Show sidebar (F9)")
                        checkable: true
                        checked: settings.sidebarVisible
                        onClicked: settings.sidebarVisible = !settings.sidebarVisible
                    }

                    LingmoUI.PageHeader {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 60
                        large: false
                        title: rootWindow.documentTitle
                    }

                    // page navigation
                    LingmoUI.IconButton {
                        iconName: "go-previous"
                        tipText: qsTr("Previous page")
                        enabled: documentView.currentPage > 0
                        onClicked: documentView.previousPage()
                    }
                    TextField {
                        id: pageField
                        Layout.preferredWidth: 46
                        Layout.preferredHeight: LingmoUI.Units.smallControlHeight
                        horizontalAlignment: Text.AlignHCenter
                        text: documentView.currentPage >= 0 ? String(documentView.currentPage + 1) : ""
                        validator: IntValidator { bottom: 1; top: Math.max(1, pdf.pageCount) }
                        selectByMouse: true
                        onAccepted: { documentView.goToPage(parseInt(text) - 1); documentView.forceActiveFocus() }
                        onActiveFocusChanged: if (activeFocus) selectAll()
                        Accessible.name: qsTr("Page number")
                    }
                    Label {
                        text: qsTr("of %1").arg(pdf.pageCount)
                        color: LingmoUI.Theme.secondaryTextColor
                        font.pointSize: LingmoUI.Units.fontBody
                    }
                    LingmoUI.IconButton {
                        iconName: "go-next"
                        tipText: qsTr("Next page")
                        enabled: documentView.currentPage < pdf.pageCount - 1
                        onClicked: documentView.nextPage()
                    }

                    Item { Layout.preferredWidth: LingmoUI.Units.smallSpacing }

                    // zoom
                    LingmoUI.IconButton {
                        iconName: "zoom-out-symbolic"
                        tipText: qsTr("Zoom out (Ctrl+−)")
                        onClicked: documentView.zoomOut()
                    }
                    LingmoUI.ActionButton {
                        id: zoomButton
                        text: documentView.zoomMode === 1 ? qsTr("Fit width")
                            : documentView.zoomMode === 2 ? qsTr("Fit page")
                            : documentView.zoomPercent + "%"
                        tipText: qsTr("Zoom")
                        Layout.preferredWidth: Math.max(implicitWidth, 72)
                        onClicked: zoomMenu.popup(zoomButton, 0, zoomButton.height + 4)
                    }
                    LingmoUI.IconButton {
                        iconName: "zoom-in-symbolic"
                        tipText: qsTr("Zoom in (Ctrl++)")
                        onClicked: documentView.zoomIn()
                    }

                    Item { Layout.preferredWidth: LingmoUI.Units.smallSpacing }

                    LingmoUI.SegmentedControl {
                        id: layoutSwitch
                        model: [
                            { iconName: "view-pages-continuous" },
                            { iconName: "view-pages-single" },
                            { iconName: "view-pages-facing" }
                        ]
                        currentIndex: settings.layoutMode
                        onActivated: (index) => {
                            settings.layoutMode = index
                            documentView.layoutMode = index
                        }
                        ToolTip.visible: layoutHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Continuous · Single page · Two pages")
                        HoverHandler { id: layoutHover }
                    }

                    LingmoUI.IconButton {
                        iconName: "edit-find"
                        tipText: qsTr("Search (Ctrl+F)")
                        checkable: true
                        checked: rootWindow.searching
                        onClicked: rootWindow.searching ? rootWindow.closeSearch() : rootWindow.openSearch()
                    }
                    LingmoUI.IconButton {
                        id: menuButton
                        iconName: "open-menu-symbolic"
                        tipText: qsTr("Menu")
                        onClicked: mainMenu.popup(menuButton, menuButton.width - mainMenu.width, menuButton.height + 4)
                    }
                }
            }

            // ---------------------------------------------------- content
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // No document: just "Open", in the header strip
                LingmoUI.IconButton {
                    z: 1
                    visible: !rootWindow.hasDocument
                    anchors.right: parent.right
                    anchors.rightMargin: LingmoUI.Units.largeSpacing
                    anchors.top: parent.top
                    anchors.topMargin: (rootWindow.header.height - height) / 2
                    iconName: "document-open"
                    tipText: qsTr("Open document (Ctrl+O)")
                    onClicked: openDialog.open()
                }

                StackLayout {
                    anchors.fill: parent
                    currentIndex: rootWindow.filePath.length > 0 ? 1 : 0

                    WelcomePage {
                        onOpenRequested: openDialog.open()
                        onFileChosen: (path) => rootWindow.openFile(path)
                    }

                    Item {
                        DocumentView {
                            id: documentView
                            anchors.fill: parent
                            visible: rootWindow.hasDocument
                            document: pdf
                            searchModel: search
                            annotations: rootWindow.annotationStore
                            onExternalLinkRequested: (url) => linkDialog.ask(url)
                            onAnnotationActivated: (id) => noteDialog.edit(id)
                            onNotePlaced: (page, position) => {
                                documentView.notePlacement = false
                                noteDialog.create(page, position)
                            }
                            onContextMenuRequested: (pageItem, page, pagePosition, viewPosition) => {
                                pageMenu.pageIndex = page
                                pageMenu.pagePosition = pagePosition
                                pageMenu.pageItem = pageItem
                                pageMenu.popup()
                            }
                            onReadingModeChanged: settings.readingMode = readingMode
                        }

                        LingmoUI.Spinner {
                            anchors.centerIn: parent
                            running: rootWindow.loading
                            visible: running
                            size: LingmoUI.Units.iconSizes.medium
                        }

                        // Page number while scrolling
                        LingmoUI.Card {
                            id: pagePill
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: LingmoUI.Units.largeSpacing * 2
                            width: pillLabel.implicitWidth + LingmoUI.Units.largeSpacing * 2
                            height: pillLabel.implicitHeight + LingmoUI.Units.smallSpacing * 2
                            radius: height / 2
                            elevation: 2
                            opacity: documentView.moving && rootWindow.hasDocument ? 1 : 0
                            visible: opacity > 0
                            Behavior on opacity { NumberAnimation { duration: LingmoUI.Units.longDuration } }
                            Label {
                                id: pillLabel
                                anchors.centerIn: parent
                                text: qsTr("Page %1 of %2").arg(documentView.currentPage + 1).arg(pdf.pageCount)
                                font.pointSize: LingmoUI.Units.fontCaption
                            }
                        }

                        // Floating search bar
                        LingmoUI.Card {
                            id: searchBar
                            visible: false
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.topMargin: LingmoUI.Units.smallSpacing
                            anchors.rightMargin: LingmoUI.Units.largeSpacing * 2
                            width: Math.min(parent.width - LingmoUI.Units.largeSpacing * 4, 420)
                            height: searchRow.implicitHeight + LingmoUI.Units.smallSpacing * 2
                            elevation: 2

                            RowLayout {
                                id: searchRow
                                anchors.fill: parent
                                anchors.leftMargin: LingmoUI.Units.smallSpacing
                                anchors.rightMargin: LingmoUI.Units.smallSpacing
                                spacing: LingmoUI.Units.smallSpacing

                                TextField {
                                    id: searchField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Search in document")
                                    selectByMouse: true
                                    Keys.onReturnPressed: (event) => (event.modifiers & Qt.ShiftModifier) ? rootWindow.findPrevious() : rootWindow.findNext()
                                    Keys.onEnterPressed: (event) => (event.modifiers & Qt.ShiftModifier) ? rootWindow.findPrevious() : rootWindow.findNext()
                                    Keys.onEscapePressed: rootWindow.closeSearch()
                                    onTextChanged: search.currentResult = -1
                                }
                                Label {
                                    visible: searchField.text.length > 0
                                    text: search.count === 0 ? qsTr("No matches")
                                        : qsTr("%1 of %2").arg(search.currentResult + 1).arg(search.count)
                                    color: LingmoUI.Theme.secondaryTextColor
                                    font.pointSize: LingmoUI.Units.fontCaption
                                }
                                LingmoUI.IconButton {
                                    iconName: "go-up"
                                    tipText: qsTr("Previous match (Shift+Enter)")
                                    enabled: search.count > 0
                                    onClicked: rootWindow.findPrevious()
                                }
                                LingmoUI.IconButton {
                                    iconName: "go-down"
                                    tipText: qsTr("Next match (Enter)")
                                    enabled: search.count > 0
                                    onClicked: rootWindow.findNext()
                                }
                                LingmoUI.IconButton {
                                    iconName: "window-close"
                                    tipText: qsTr("Close search (Esc)")
                                    onClicked: rootWindow.closeSearch()
                                }
                            }
                        }
                    }
                }

                // Messages
                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: LingmoUI.Units.largeSpacing * 2
                    spacing: LingmoUI.Units.smallSpacing

                    LingmoUI.Banner {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 640
                        Layout.alignment: Qt.AlignHCenter
                        type: LingmoUI.Banner.Info
                        visible: documentView.notePlacement
                        title: qsTr("Add a note")
                        text: qsTr("Click on the page where the note should go.")
                        closable: false
                        LingmoUI.ActionButton {
                            text: qsTr("Cancel")
                            onClicked: documentView.notePlacement = false
                        }
                    }

                    LingmoUI.Banner {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 640
                        Layout.alignment: Qt.AlignHCenter
                        type: LingmoUI.Banner.Info
                        text: rootWindow.infoText
                        onClosed: rootWindow.infoText = ""
                    }

                    LingmoUI.Banner {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 640
                        Layout.alignment: Qt.AlignHCenter
                        type: LingmoUI.Banner.Error
                        text: rootWindow.errorText
                        onClosed: rootWindow.errorText = ""
                    }
                }

                Timer {
                    interval: 4000
                    running: rootWindow.infoText.length > 0
                    onTriggered: rootWindow.infoText = ""
                }
            }
        }
    }

    // ------------------------------------------------------------ drag & drop

    DropArea {
        id: dropArea
        anchors.fill: parent
        property bool acceptable: false
        onEntered: (drag) => {
            acceptable = drag.hasUrls && drag.urls.length > 0 && reader.isSupported(reader.toLocalPath(drag.urls[0]))
            drag.accepted = acceptable
        }
        onExited: acceptable = false
        onDropped: (drop) => {
            acceptable = false
            if (drop.hasUrls && drop.urls.length > 0) {
                rootWindow.openFile(drop.urls[0])
                drop.accept()
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.largeSpacing
            visible: dropArea.containsDrag && dropArea.acceptable
            radius: LingmoUI.Theme.bigRadius
            color: Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                           LingmoUI.Theme.highlightColor.b, 0.12)
            border.width: 2
            border.color: LingmoUI.Theme.highlightColor

            LingmoUI.EmptyState {
                anchors.centerIn: parent
                iconName: "document-open"
                title: qsTr("Drop to open")
            }
        }
    }

    // ------------------------------------------------------------ dialogs & menus

    FileDialog {
        id: openDialog
        title: qsTr("Open document")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("PDF documents (*.pdf *.PDF)"), qsTr("All files (*)")]
        currentFolder: rootWindow.filePath.length > 0
                       ? reader.toUrl(rootWindow.filePath.substring(0, rootWindow.filePath.lastIndexOf("/")))
                       : StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: rootWindow.openFile(selectedFile)
    }

    PasswordDialog {
        id: passwordDialog
        fileName: reader.fileName(rootWindow.filePath)
        onSubmitted: (text) => {
            if (pdf.unlock(text)) {
                rootWindow.password = text
                rootWindow.documentReady()
            } else {
                passwordDialog.ask(true)
            }
        }
        onCanceled: {
            rootWindow.passwordWanted = false
            rootWindow.filePath = ""
            pdf.close()
        }
    }

    LinkDialog {
        id: linkDialog
    }

    NoteDialog {
        id: noteDialog
    }

    Menu {
        id: zoomMenu
        MenuItem { text: qsTr("Fit width"); checkable: true; checked: documentView.zoomMode === 1; onTriggered: documentView.zoomMode = 1 }
        MenuItem { text: qsTr("Fit page"); checkable: true; checked: documentView.zoomMode === 2; onTriggered: documentView.zoomMode = 2 }
        MenuSeparator {}
        Instantiator {
            model: [50, 75, 100, 125, 150, 200, 300, 400]
            delegate: MenuItem {
                required property int modelData
                text: modelData + "%"
                checkable: true
                checked: documentView.zoomMode === 0 && documentView.zoomPercent === modelData
                onTriggered: documentView.setZoomPercent(modelData)
            }
            onObjectAdded: (index, object) => zoomMenu.insertItem(3 + index, object)
            onObjectRemoved: (index, object) => zoomMenu.removeItem(object)
        }
    }

    Menu {
        id: mainMenu
        width: 260

        MenuItem { text: qsTr("Open…"); onTriggered: openDialog.open() }
        Menu {
            id: openWithMenu
            title: qsTr("Open with")
            enabled: rootWindow.hasDocument && openWithRepeater.count > 0
            property var apps: rootWindow.hasDocument ? reader.openWithApplications(rootWindow.filePath) : []
            Instantiator {
                id: openWithRepeater
                model: openWithMenu.apps
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name
                    icon.name: modelData.icon
                    onTriggered: reader.openWith(modelData.id, rootWindow.filePath)
                }
                onObjectAdded: (index, object) => openWithMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => openWithMenu.removeItem(object)
            }
        }
        MenuItem { text: qsTr("Print…"); enabled: rootWindow.hasDocument; onTriggered: rootWindow.printDocument() }
        MenuItem {
            text: qsTr("Ask the Assistant")
            visible: reader.assistantAvailable
            height: visible ? implicitHeight : 0
            enabled: rootWindow.hasDocument
            onTriggered: rootWindow.askAssistant()
        }
        MenuSeparator {}
        Menu {
            title: qsTr("Reading mode")
            enabled: rootWindow.hasDocument
            MenuItem { text: qsTr("Normal"); checkable: true; checked: documentView.readingMode === 0; onTriggered: documentView.readingMode = 0 }
            MenuItem { text: qsTr("Night"); checkable: true; checked: documentView.readingMode === 1; onTriggered: documentView.readingMode = 1 }
            MenuItem { text: qsTr("Sepia"); checkable: true; checked: documentView.readingMode === 2; onTriggered: documentView.readingMode = 2 }
        }
        MenuItem {
            text: qsTr("Add note…")
            enabled: rootWindow.hasDocument
            onTriggered: documentView.notePlacement = true
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Close document"); enabled: rootWindow.hasDocument; onTriggered: rootWindow.closeDocument() }
    }

    Menu {
        id: pageMenu
        property int pageIndex: 0
        property point pagePosition
        property Item pageItem: null

        MenuItem {
            text: qsTr("Copy")
            enabled: documentView.selectedText.length > 0
            onTriggered: documentView.copySelection()
        }
        MenuItem {
            text: qsTr("Highlight")
            enabled: documentView.selectedText.length > 0
            onTriggered: rootWindow.highlightSelection(false)
        }
        MenuItem {
            text: qsTr("Highlight and add note…")
            enabled: documentView.selectedText.length > 0
            onTriggered: rootWindow.highlightSelection(true)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Add note here…")
            onTriggered: noteDialog.create(pageMenu.pageIndex, pageMenu.pagePosition)
        }
        MenuItem {
            text: qsTr("Select all on this page")
            onTriggered: if (pageMenu.pageItem) pageMenu.pageItem.selection.selectAll()
        }
        MenuItem {
            text: qsTr("Search for selection")
            enabled: documentView.selectedText.length > 0 && documentView.selectedText.length < 80
            onTriggered: rootWindow.openSearch()
        }
    }

    // ------------------------------------------------------------ shortcuts

    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+P"; enabled: rootWindow.hasDocument; onActivated: rootWindow.printDocument() }
    Shortcut { sequence: "Ctrl+F"; enabled: rootWindow.hasDocument; onActivated: rootWindow.openSearch() }
    Shortcut { sequence: "F3"; enabled: rootWindow.searching; onActivated: rootWindow.findNext() }
    Shortcut { sequence: "Shift+F3"; enabled: rootWindow.searching; onActivated: rootWindow.findPrevious() }
    Shortcut { sequence: "Ctrl+W"; enabled: rootWindow.hasDocument; onActivated: rootWindow.closeDocument() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: rootWindow.close() }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; enabled: rootWindow.hasDocument; onActivated: documentView.zoomIn() }
    Shortcut { sequence: "Ctrl+-"; enabled: rootWindow.hasDocument; onActivated: documentView.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; enabled: rootWindow.hasDocument; onActivated: documentView.zoomMode = 1 }
    Shortcut { sequence: "F9"; enabled: rootWindow.hasDocument; onActivated: settings.sidebarVisible = !settings.sidebarVisible }
    Shortcut { sequence: "Ctrl+H"; enabled: documentView.selectedText.length > 0; onActivated: rootWindow.highlightSelection(false) }
    Shortcut {
        sequence: "Ctrl+C"
        enabled: documentView.selectedText.length > 0 && !searchField.activeFocus && !pageField.activeFocus
        onActivated: documentView.copySelection()
    }
    Shortcut {
        sequence: "Ctrl+G"
        enabled: rootWindow.hasDocument
        onActivated: { pageField.forceActiveFocus(); pageField.selectAll() }
    }
    Shortcut {
        sequence: "Escape"
        enabled: rootWindow.hasDocument && !searchField.activeFocus
        onActivated: {
            if (documentView.notePlacement)
                documentView.notePlacement = false
            else if (rootWindow.searching)
                rootWindow.closeSearch()
            else
                documentView.clearSelection()
        }
    }
}
