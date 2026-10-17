/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// The pages of a document: continuous scroll, single page or two pages side
// by side. Only rows near the viewport are instantiated, the rest are exact
// placeholders, so jumping anywhere lands on the right pixel.
Flickable {
    id: view

    // PdfDocument ("pdf") and SearchModel ("search") from C++
    property var document: null
    property var searchModel: null
    property var annotations: null
    readonly property int annotationRevision: annotations ? annotations.revision : 0

    // Points to pixels; 0.75 = 100 % (a 96 dpi screen shows a page at paper size)
    property real renderScale: 1
    // renderScale once zooming has settled: pages are re-rendered at this size
    property real stableScale: 1
    readonly property real minScale: 0.1
    readonly property real maxScale: 8
    readonly property real pointsPerPercent: 0.75 / 100
    readonly property int zoomPercent: Math.round(renderScale / pointsPerPercent)
    // 0 custom, 1 fit width, 2 fit page
    property int zoomMode: 1
    // 0 continuous, 1 single page, 2 two pages
    property int layoutMode: 0
    // 0 normal, 1 night, 2 sepia
    property int readingMode: 0
    // Next click on a page drops a note there
    property bool notePlacement: false

    // The page whose selection is active (one page at a time)
    property Item activeSelection: null
    readonly property string selectedText: activeSelection ? activeSelection.selection.text : ""

    property int singlePage: 0
    readonly property int pageCount: sizes.length
    readonly property int currentPage: {
        if (pageCount === 0)
            return -1
        if (layoutMode === 1)
            return singlePage
        const row = rowAt(contentY + height * 0.3)
        const pages = rowPages[row]
        return pages ? pages[0] : 0
    }

    readonly property real gap: LingmoUI.Units.largeSpacing
    readonly property real margin: LingmoUI.Units.largeSpacing * 2

    // Geometry in points, rebuilt by relayout()
    property var sizes: []
    property var rowPages: []
    property var rowCum: [0]
    property var rowHeights: []
    property var rowWidths: []
    property real maxRowWidth: 0
    property real maxRowHeight: 0
    property int maxRowPages: 1

    signal externalLinkRequested(string url)
    signal annotationActivated(string id)
    signal notePlaced(int page, point position)
    signal contextMenuRequested(Item pageItem, int page, point pagePosition, point viewPosition)

    // ------------------------------------------------------------ geometry

    function pageSize(i) {
        return i >= 0 && i < sizes.length ? sizes[i] : Qt.size(612, 792)
    }

    function rowTop(r) {
        return margin + renderScale * rowCum[r] + gap * r
    }

    function rowWidthPx(r) {
        const pages = rowPages[r]
        return pages ? rowWidths[r] * renderScale + gap * (pages.length - 1) : 0
    }

    function rowLeft(r) {
        return (contentWidth - rowWidthPx(r)) / 2
    }

    function rowAt(y) {
        let lo = 0, hi = rowPages.length - 1
        if (hi < 0)
            return 0
        while (lo < hi) {
            const mid = (lo + hi + 1) >> 1
            if (rowTop(mid) - gap / 2 <= y)
                lo = mid
            else
                hi = mid - 1
        }
        return lo
    }

    function rowOfPage(p) {
        if (layoutMode === 1)
            return 0
        if (layoutMode === 2)
            return Math.floor(p / 2)
        return p
    }

    function reload() {
        const list = []
        const n = document && document.ready ? document.pageCount : 0
        for (let i = 0; i < n; ++i)
            list.push(document.pageSize(i))
        activeSelection = null
        singlePage = 0
        sizes = list
        relayout()
        stableScale = renderScale
    }

    function relayout() {
        const rows = []
        const n = sizes.length
        if (layoutMode === 1) {
            if (n > 0)
                rows.push([Math.max(0, Math.min(singlePage, n - 1))])
        } else if (layoutMode === 2) {
            for (let i = 0; i < n; i += 2)
                rows.push(i + 1 < n ? [i, i + 1] : [i])
        } else {
            for (let i = 0; i < n; ++i)
                rows.push([i])
        }
        const cum = [0], heights = [], widths = []
        let maxW = 0, maxH = 0, maxPages = 1
        for (let r = 0; r < rows.length; ++r) {
            let w = 0, h = 0
            for (const p of rows[r]) {
                w += sizes[p].width
                h = Math.max(h, sizes[p].height)
            }
            heights.push(h)
            widths.push(w)
            cum.push(cum[r] + h)
            maxW = Math.max(maxW, w)
            maxH = Math.max(maxH, h)
            maxPages = Math.max(maxPages, rows[r].length)
        }
        // whole-document maxima so fitting doesn't change from page to page
        if (layoutMode === 1) {
            for (let i = 0; i < n; ++i) {
                maxW = Math.max(maxW, sizes[i].width)
                maxH = Math.max(maxH, sizes[i].height)
            }
        }
        rowHeights = heights
        rowWidths = widths
        maxRowWidth = maxW
        maxRowHeight = maxH
        maxRowPages = maxPages
        rowCum = cum
        rowPages = rows
    }

    contentWidth: Math.max(width, maxRowWidth * renderScale + gap * (maxRowPages - 1) + margin * 2)
    contentHeight: rowPages.length > 0 ? rowTop(rowPages.length) - gap + margin : height

    // ------------------------------------------------------------ navigation

    function clampX(x) { return Math.max(0, Math.min(contentWidth - width, x)) }
    function clampY(y) { return Math.max(0, Math.min(contentHeight - height, y)) }

    function goToPage(page, location) {
        if (pageCount === 0)
            return
        page = Math.max(0, Math.min(pageCount - 1, page))
        if (layoutMode === 1 && singlePage !== page) {
            singlePage = page
            relayout()
        }
        const r = rowOfPage(page)
        let y = rowTop(r) - gap
        if (location && location.y > 0)
            y = rowTop(r) + location.y * renderScale - gap * 2
        contentY = clampY(y)
        if (contentWidth > width)
            contentX = clampX(location && location.x > 0 ? rowLeft(r) + location.x * renderScale - gap * 2
                                                         : (contentWidth - width) / 2)
        forceActiveFocus()
    }

    // Brings a rectangle (in page points) into view, a third from the top
    function showRect(page, rect) {
        if (pageCount === 0)
            return
        if (layoutMode === 1 && singlePage !== page) {
            singlePage = page
            relayout()
        }
        const r = rowOfPage(page)
        const top = rowTop(r) + rect.y * renderScale
        if (top < contentY + gap || top + rect.height * renderScale > contentY + height - gap)
            contentY = clampY(top - height / 3)
        if (contentWidth > width) {
            let left = rowLeft(r)
            const pages = rowPages[r]
            if (pages.length > 1 && page === pages[1])
                left += sizes[pages[0]].width * renderScale + gap
            const x = left + rect.x * renderScale
            if (x < contentX || x + rect.width * renderScale > contentX + width)
                contentX = clampX(x - width / 3)
        }
    }

    function nextPage() {
        const step = layoutMode === 2 ? 2 : 1
        const base = layoutMode === 2 ? currentPage - currentPage % 2 : currentPage
        if (base + step < pageCount)
            goToPage(base + step)
    }

    function previousPage() {
        const step = layoutMode === 2 ? 2 : 1
        const base = layoutMode === 2 ? currentPage - currentPage % 2 : currentPage
        if (layoutMode !== 1 && contentY > rowTop(rowOfPage(base)) + gap)
            goToPage(base)
        else if (base - step >= 0)
            goToPage(base - step)
        else
            goToPage(0)
    }

    function scrollBy(dy) {
        if (layoutMode === 1) {
            if (dy > 0 && contentY >= contentHeight - height - 1 && singlePage < pageCount - 1) {
                goToPage(singlePage + 1)
                return
            }
            if (dy < 0 && contentY <= 0 && singlePage > 0) {
                goToPage(singlePage - 1)
                contentY = clampY(contentHeight)
                return
            }
        }
        contentY = clampY(contentY + dy)
    }

    // ------------------------------------------------------------ zoom

    function fitWidthScale() {
        const avail = width - margin * 2 - gap * (maxRowPages - 1) - (vbar.visible ? vbar.width : 0)
        return maxRowWidth > 0 ? avail / maxRowWidth : 1
    }

    function fitPageScale() {
        const availH = height - margin * 2
        return Math.min(fitWidthScale(), maxRowHeight > 0 ? availH / maxRowHeight : 1)
    }

    // Changes the zoom keeping the document point under (ax, ay) still
    function setScale(s, ax, ay) {
        s = Math.max(minScale, Math.min(maxScale, s))
        if (pageCount === 0) {
            renderScale = s
            stableScale = s
            return
        }
        if (Math.abs(s - renderScale) < 0.0001)
            return
        if (ax === undefined) { ax = width / 2; ay = height / 2 }
        const cy = contentY + ay
        const r = rowAt(cy)
        const offY = (cy - rowTop(r)) / renderScale
        const offX = (contentX + ax - rowLeft(r)) / renderScale
        renderScale = s
        contentY = clampY(rowTop(r) + offY * s - ay)
        contentX = clampX(rowLeft(r) + offX * s - ax)
        settle.restart()
    }

    function applyFit() {
        if (pageCount === 0)
            return
        if (zoomMode === 1)
            setScale(fitWidthScale(), width / 2, 0)
        else if (zoomMode === 2)
            setScale(fitPageScale(), width / 2, 0)
    }

    readonly property var zoomSteps: [10, 25, 33, 50, 67, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400, 500, 800, 1000]

    function zoomIn() {
        zoomMode = 0
        for (const p of zoomSteps) {
            if (p > zoomPercent + 1) {
                setScale(p * pointsPerPercent)
                return
            }
        }
    }

    function zoomOut() {
        zoomMode = 0
        for (let i = zoomSteps.length - 1; i >= 0; --i) {
            if (zoomSteps[i] < zoomPercent - 1) {
                setScale(zoomSteps[i] * pointsPerPercent)
                return
            }
        }
    }

    function setZoomPercent(p) {
        zoomMode = 0
        setScale(p * pointsPerPercent)
    }

    onZoomModeChanged: applyFit()
    onWidthChanged: if (zoomMode !== 0) Qt.callLater(applyFit)
    onHeightChanged: if (zoomMode === 2) Qt.callLater(applyFit)
    onLayoutModeChanged: {
        const page = currentPage
        singlePage = Math.max(0, page)
        relayout()
        applyFit()
        Qt.callLater(goToPage, page)
    }

    Timer {
        id: settle
        interval: 180
        onTriggered: view.stableScale = view.renderScale
    }

    // ------------------------------------------------------------ selection

    function copySelection() {
        if (selectedText.length > 0)
            reader.copyText(selectedText)
    }

    function clearSelection() {
        if (activeSelection)
            activeSelection.selection.clear()
        activeSelection = null
    }

    function selectAllOnPage() {
        const row = rowRepeater.itemAt(rowOfPage(currentPage))
        const item = row ? row.pageItem(currentPage) : null
        if (item)
            item.selection.selectAll()
    }

    function pageContextMenu(pageItem, position) {
        contextMenuRequested(pageItem, pageItem.pageIndex,
                             Qt.point(position.x / renderScale, position.y / renderScale),
                             mapFromItem(pageItem, position.x, position.y))
    }

    function linkActivated(page, location, url) {
        if (page >= 0)
            goToPage(page, location)
        else if (url.length > 0)
            externalLinkRequested(url)
    }

    // ------------------------------------------------------------ input

    clip: true
    focus: true
    boundsBehavior: Flickable.StopAtBounds
    // Mouse drags select text; the wheel, scroll bars and touch scroll
    acceptedButtons: Qt.NoButton
    flickableDirection: Flickable.AutoFlickIfNeeded
    pixelAligned: true

    ScrollBar.vertical: ScrollBar { id: vbar; minimumSize: 0.04 }
    ScrollBar.horizontal: ScrollBar { minimumSize: 0.04 }

    WheelHandler {
        acceptedModifiers: Qt.ControlModifier
        target: null
        onWheel: (event) => {
            view.zoomMode = 0
            const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y * 4
            const p = view.mapFromItem(parent, event.x, event.y)
            view.setScale(view.renderScale * Math.pow(1.0015, delta), p.x, p.y)
        }
    }

    // Single page: the wheel turns the page at the top/bottom edge
    WheelHandler {
        enabled: view.layoutMode === 1
        acceptedModifiers: Qt.NoModifier
        target: null
        property real pending: 0
        onWheel: (event) => {
            const dy = event.pixelDelta.y !== 0 ? -event.pixelDelta.y : -event.angleDelta.y / 120 * 64
            const atEnd = view.contentY >= view.contentHeight - view.height - 1
            const atStart = view.contentY <= 0
            if ((dy > 0 && atEnd) || (dy < 0 && atStart)) {
                pending += dy
                if (Math.abs(pending) >= 96) {
                    view.scrollBy(pending)
                    pending = 0
                }
            } else {
                pending = 0
                view.contentY = view.clampY(view.contentY + dy)
            }
        }
    }

    PinchHandler {
        target: null
        property real startScale: 1
        onActiveChanged: if (active) { startScale = view.renderScale; view.zoomMode = 0 }
        onActiveScaleChanged: {
            if (!active)
                return
            const p = view.mapFromItem(parent, centroid.position.x, centroid.position.y)
            view.setScale(startScale * activeScale, p.x, p.y)
        }
    }

    Keys.onPressed: (event) => {
        const page = view.height * 0.9
        switch (event.key) {
        case Qt.Key_PageDown:
        case Qt.Key_Space:
            if (event.key === Qt.Key_Space && (event.modifiers & Qt.ShiftModifier))
                view.scrollBy(-page)
            else
                view.scrollBy(page)
            break
        case Qt.Key_PageUp:
            view.scrollBy(-page)
            break
        case Qt.Key_Down:
            view.scrollBy(60)
            break
        case Qt.Key_Up:
            view.scrollBy(-60)
            break
        case Qt.Key_Home:
            view.goToPage(0)
            break
        case Qt.Key_End:
            view.goToPage(view.pageCount - 1)
            break
        case Qt.Key_Right:
            if (view.contentWidth > view.width + 1)
                view.contentX = view.clampX(view.contentX + 60)
            else
                view.nextPage()
            break
        case Qt.Key_Left:
            if (view.contentWidth > view.width + 1)
                view.contentX = view.clampX(view.contentX - 60)
            else
                view.previousPage()
            break
        default:
            return
        }
        event.accepted = true
    }

    // ------------------------------------------------------------ rows

    Repeater {
        id: rowRepeater
        model: view.rowPages.length

        delegate: Item {
            id: row
            required property int index
            readonly property Item docView: view
            readonly property var pages: view.rowPages[index] || []
            readonly property bool near: y + height >= view.contentY - view.height
                                         && y <= view.contentY + view.height * 2

            function pageItem(p) {
                return loader.item ? loader.item.pageItem(p) : null
            }

            x: 0
            y: view.rowTop(index)
            width: view.contentWidth
            height: (view.rowHeights[index] || 0) * view.renderScale

            Loader {
                id: loader
                active: row.near
                anchors.horizontalCenter: parent.horizontalCenter
                sourceComponent: Row {
                    spacing: view.gap

                    function pageItem(p) {
                        for (let i = 0; i < pageRepeater.count; ++i) {
                            const it = pageRepeater.itemAt(i)
                            if (it && it.pageIndex === p)
                                return it
                        }
                        return null
                    }

                    Repeater {
                        id: pageRepeater
                        model: row.pages
                        delegate: PageItem {
                            required property int modelData
                            pageIndex: modelData
                            view: row.docView
                        }
                    }
                }
            }
        }
    }
}
