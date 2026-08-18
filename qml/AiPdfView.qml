// Forked from Qt's PdfMultiPageView.qml (QtQuick.Pdf), which is
// explicitly designed to be copied and customized (see its qdoc).
// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
//
// Changes vs upstream: the per-page PdfSelection/DragHandler selection
// (single-page, no snapping) is replaced by one viewport-level
// MouseArea driving PdfSelectionModel (C++): cross-page drag, word /
// paragraph snapping on double / triple click, edge auto-scroll,
// I-beam & pointing-hand cursors, right-click copy menu. Link
// delegates are replaced by PdfSelectionModel.linkAt hit tests.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Pdf
import QtQuick.Shapes

Item {
    id: root

    required property PdfDocument document

    // PdfSelectionModel (context property pdfSelection) — injected so
    // this file stays free of app-global names.
    property var selectionModel: null
    // Off while the hand/pan tool is active: the pan overlay above
    // takes the drags and the I-beam should not show.
    property bool selectionEnabled: true

    readonly property string selectedText:
        selectionModel ? selectionModel.text : ""

    function selectAll() {
        if (root.selectionModel)
            root.selectionModel.selectAllOnPage(pageNavigator.currentPage)
    }

    function copySelectionToClipboard() {
        if (root.selectionModel)
            root.selectionModel.copyToClipboard()
    }

    // -------------------------------- page navigation

    property alias currentPage: pageNavigator.currentPage
    property alias backEnabled: pageNavigator.backAvailable
    property alias forwardEnabled: pageNavigator.forwardAvailable
    function back() { pageNavigator.back() }
    function forward() { pageNavigator.forward() }

    function goToPage(page) {
        if (page === pageNavigator.currentPage)
            return
        goToLocation(page, Qt.point(-1, -1), 0)
    }

    function goToLocation(page, location, zoom) {
        if (tableView.rows === 0) {
            // save this request for later
            tableView.pendingRow = page
            tableView.pendingLocation = location
            tableView.pendingZoom = zoom
            return
        }
        if (zoom > 0) {
            pageNavigator.jumping = true // don't call pageNavigator.update() because we will jump() instead
            root.renderScale = zoom
            pageNavigator.jumping = false
        }
        pageNavigator.jump(page, location, zoom) // actually jump
    }

    property int currentPageRenderingStatus: Image.Null

    // -------------------------------- page scaling

    property real renderScale: 1
    property real pageRotation: 0
    function resetScale() { root.renderScale = 1 }

    function scaleToWidth(width, height) {
        root.renderScale = width / (tableView.rot90 ? tableView.firstPagePointSize.height : tableView.firstPagePointSize.width)
    }

    function scaleToPage(width, height) {
        const windowAspect = width / height
        const pageAspect = tableView.firstPagePointSize.width / tableView.firstPagePointSize.height
        if (tableView.rot90) {
            if (windowAspect > pageAspect) {
                root.renderScale = height / tableView.firstPagePointSize.width
            } else {
                root.renderScale = width / tableView.firstPagePointSize.height
            }
        } else {
            if (windowAspect > pageAspect) {
                root.renderScale = height / tableView.firstPagePointSize.height
            } else {
                root.renderScale = width / tableView.firstPagePointSize.width
            }
        }
    }

    // -------------------------------- text search

    property alias searchModel: searchModel
    property alias searchString: searchModel.searchString
    function searchBack() { --searchModel.currentResult }
    function searchForward() { ++searchModel.currentResult }

    // ── Page-size cache ─────────────────────────────────────────────
    // Layout (rowHeightProvider) reads page sizes constantly while
    // scrolling, and every QPdfDocument.pagePointSize() call takes
    // QtPdf's global PDFium lock — stalling the GUI thread behind the
    // render/build workers. Query once per document instead.
    property var _pageSizes: []
    function pageSizeAt(i) {
        const a = root._pageSizes
        return i >= 0 && i < a.length ? a[i] : Qt.size(1, 1)
    }
    function _rebuildPageSizes() {
        const ready = root.document
                      && root.document.status === PdfDocument.Ready
        const n = ready ? root.document.pageCount : 0
        const a = new Array(n)
        for (let i = 0; i < n; ++i)
            a[i] = root.document.pagePointSize(i)
        root._pageSizes = a
        tableView.forceLayout()
    }
    Connections {
        target: root.document
        function onStatusChanged() { root._rebuildPageSizes() }
    }
    Component.onCompleted: _rebuildPageSizes()

    // Rendered width of the widest page at the current zoom — the app
    // uses it to clamp horizontal panning to the page edges.
    readonly property real pageDisplayWidth:
        (root.document ? root.document.maxPageWidth : 0) * root.renderScale

    LoggingCategory {
        id: lcMPV
        name: "qt.pdf.multipageview"
    }

    PdfStyle { id: style }
    TableView {
        id: tableView
        property bool debug: false
        property real minScale: 0.1
        property real maxScale: 10
        property point jumpLocationMargin: Qt.point(10, 10)  // px away from viewport edges
        anchors.fill: parent
        anchors.leftMargin: 2
        model: root.document ? root.document.pageCount : 0
        rowSpacing: 6
        property real rotationNorm: Math.round((360 + (root.pageRotation % 360)) % 360)
        property bool rot90: rotationNorm == 90 || rotationNorm == 270
        onRot90Changed: forceLayout()
        onHeightChanged: forceLayout()
        onWidthChanged: forceLayout()
        property size firstPagePointSize: root.pageSizeAt(0)
        property real pageHolderWidth: Math.max(root.width, ((rot90 ? root.document?.maxPageHeight : root.document?.maxPageWidth) ?? 0) * root.renderScale)
        columnWidthProvider: function(col) { return root.document ? pageHolderWidth + vscroll.width + 2 : 0 }
        rowHeightProvider: function(row) { const s = root.pageSizeAt(row); return (rot90 ? s.width : s.height) * root.renderScale }

        // delayed-jump feature in case the user called goToPage() or goToLocation() too early
        property int pendingRow: -1
        property point pendingLocation
        property real pendingZoom: -1
        onRowsChanged: {
            if (rows > 0 && tableView.pendingRow >= 0) {
                root.goToLocation(tableView.pendingRow, tableView.pendingLocation, tableView.pendingZoom)
                tableView.pendingRow = -1
                tableView.pendingLocation = Qt.point(-1, -1)
                tableView.pendingZoom = -1
            }
        }

        delegate: Rectangle {
            id: pageHolder
            required property int index
            color: tableView.debug ? "beige" : "transparent"
            // The selection MouseArea reaches in for coordinate mapping.
            readonly property Item paperItem: paper
            Rectangle {
                id: paper
                width: image.width
                height: image.height
                rotation: root.pageRotation
                anchors.centerIn: pinch.active ? undefined : parent
                property size pagePointSize: root.pageSizeAt(pageHolder.index)
                property real pageScale: image.paintedWidth / pagePointSize.width
                PdfPageImage {
                    id: image
                    document: root.document
                    currentFrame: pageHolder.index
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    width: paper.pagePointSize.width * root.renderScale
                    height: paper.pagePointSize.height * root.renderScale
                    property real renderScale: root.renderScale
                    property real oldRenderScale: 1
                    onRenderScaleChanged: {
                        image.sourceSize.width = paper.pagePointSize.width * renderScale * Screen.devicePixelRatio
                        image.sourceSize.height = 0
                        paper.scale = 1
                        searchHighlights.update()
                    }
                    onStatusChanged: {
                        if (pageHolder.index === pageNavigator.currentPage)
                            root.currentPageRenderingStatus = status
                    }
                }
                Shape {
                    anchors.fill: parent
                    visible: image.status === Image.Ready
                    onVisibleChanged: searchHighlights.update()
                    ShapePath {
                        strokeWidth: -1
                        fillColor: style.pageSearchResultsColor
                        scale: Qt.size(paper.pageScale, paper.pageScale)
                        PathMultiline {
                            id: searchHighlights
                            function update() {
                                // paths could be a binding, but we need to be able to "kick" it sometimes
                                paths = searchModel.boundingPolygonsOnPage(pageHolder.index)
                            }
                        }
                    }
                    Connections {
                        target: searchModel
                        // whenever the highlights on the _current_ page change, they actually need to change on _all_ pages
                        // (usually because the search string has changed)
                        function onCurrentPageBoundingPolygonsChanged() { searchHighlights.update() }
                    }
                    ShapePath {
                        strokeWidth: -1
                        fillColor: style.selectionColor
                        scale: Qt.size(paper.pageScale, paper.pageScale)
                        PathMultiline {
                            id: selectionHighlight
                            function update() {
                                paths = root.selectionModel
                                      ? root.selectionModel.polygonsOnPage(pageHolder.index)
                                      : []
                            }
                        }
                    }
                    Connections {
                        target: root.selectionModel
                        function onSelectionChanged() { selectionHighlight.update() }
                    }
                    // Pages instantiated mid-drag (edge auto-scroll across
                    // pages) must show the already-active selection.
                    Component.onCompleted: selectionHighlight.update()
                }
                Shape {
                    anchors.fill: parent
                    visible: image.status === Image.Ready && searchModel.currentPage === pageHolder.index
                    ShapePath {
                        strokeWidth: style.currentSearchResultStrokeWidth
                        strokeColor: style.currentSearchResultStrokeColor
                        fillColor: "transparent"
                        scale: Qt.size(paper.pageScale, paper.pageScale)
                        PathMultiline {
                            paths: searchModel.currentResultBoundingPolygons
                        }
                    }
                }
                PinchHandler {
                    id: pinch
                    minimumScale: tableView.minScale / root.renderScale
                    maximumScale: Math.max(1, tableView.maxScale / root.renderScale)
                    minimumRotation: root.pageRotation
                    maximumRotation: root.pageRotation
                    onActiveChanged:
                        if (active) {
                            paper.z = 10
                        } else {
                            paper.z = 0
                            const centroidInPoints = Qt.point(pinch.centroid.position.x / root.renderScale,
                                                            pinch.centroid.position.y / root.renderScale)
                            const centroidInFlickable = tableView.mapFromItem(paper, pinch.centroid.position.x, pinch.centroid.position.y)
                            const newSourceWidth = image.sourceSize.width * paper.scale
                            const ratio = newSourceWidth / image.sourceSize.width
                            if (ratio > 1.1 || ratio < 0.9) {
                                const centroidOnPage = Qt.point(centroidInPoints.x * root.renderScale * ratio, centroidInPoints.y * root.renderScale * ratio)
                                paper.scale = 1
                                pinch.persistentScale = 1
                                paper.x = 0
                                paper.y = 0
                                root.renderScale *= ratio
                                tableView.forceLayout()
                                if (tableView.rotationNorm == 0) {
                                    tableView.contentX = pageHolder.x + tableView.originX + centroidOnPage.x - centroidInFlickable.x
                                    tableView.contentY = pageHolder.y + tableView.originY + centroidOnPage.y - centroidInFlickable.y
                                } else if (tableView.rotationNorm == 90) {
                                    tableView.contentX = pageHolder.x + tableView.originX + image.height - centroidOnPage.y - centroidInFlickable.x
                                    tableView.contentY = pageHolder.y + tableView.originY + centroidOnPage.x - centroidInFlickable.y
                                } else if (tableView.rotationNorm == 180) {
                                    tableView.contentX = pageHolder.x + tableView.originX + image.width - centroidOnPage.x - centroidInFlickable.x
                                    tableView.contentY = pageHolder.y + tableView.originY + image.height - centroidOnPage.y - centroidInFlickable.y
                                } else if (tableView.rotationNorm == 270) {
                                    tableView.contentX = pageHolder.x + tableView.originX + centroidOnPage.y - centroidInFlickable.x
                                    tableView.contentY = pageHolder.y + tableView.originY + image.width - centroidOnPage.x - centroidInFlickable.y
                                }
                                tableView.returnToBounds()
                            }
                        }
                    grabPermissions: PointerHandler.CanTakeOverFromAnything
                }
                TapHandler {
                    acceptedDevices: PointerDevice.TouchScreen
                    onTapped: {
                        if (root.selectionModel)
                            root.selectionModel.clear()
                    }
                }
            }
        }
        ScrollBar.vertical: ScrollBar {
            id: vscroll
            property bool moved: false
            onPositionChanged: moved = true
            onPressedChanged: if (pressed) {
                // When the user starts scrolling, push the location where we came from so the user can go "back" there
                const cell = tableView.cellAtPos(root.width / 2, root.height / 2)
                const currentItem = tableView.itemAtCell(cell)
                const currentLocation = currentItem
                                      ? Qt.point((tableView.contentX - currentItem.x + tableView.jumpLocationMargin.x) / root.renderScale,
                                                 (tableView.contentY - currentItem.y + tableView.jumpLocationMargin.y) / root.renderScale)
                                      : Qt.point(0, 0) // maybe the delegate wasn't loaded yet
                pageNavigator.jump(cell.y, currentLocation, root.renderScale)
            }
            onActiveChanged: if (!active ) {
                // When the scrollbar stops moving, tell navstack where we are, so as to update currentPage etc.
                const cell = tableView.cellAtPos(root.width / 2, root.height / 2)
                const currentItem = tableView.itemAtCell(cell)
                const currentLocation = currentItem
                                      ? Qt.point((tableView.contentX - currentItem.x + tableView.jumpLocationMargin.x) / root.renderScale,
                                                 (tableView.contentY - currentItem.y + tableView.jumpLocationMargin.y) / root.renderScale)
                                      : Qt.point(0, 0) // maybe the delegate wasn't loaded yet
                pageNavigator.update(cell.y, currentLocation, root.renderScale)
            }
        }
        ScrollBar.horizontal: ScrollBar { id: hscroll }
    }

    // ── Selection interaction layer ─────────────────────────────────
    // One MouseArea over the whole viewport, so a drag can flow across
    // page boundaries. Wheel events are untouched (no onWheel) — the
    // app's wheel router above keeps working.
    MouseArea {
        id: selArea
        anchors.fill: parent
        // Leave the scrollbars clickable: this layer sits above the
        // TableView, which the attached ScrollBars are children of.
        anchors.rightMargin: vscroll.visible ? vscroll.width : 0
        anchors.bottomMargin: hscroll.visible ? hscroll.height : 0
        z: 1
        visible: root.selectionEnabled && !!root.selectionModel
        // NOTE: nothing may be stacked above this item over the page
        // area — an overlapping MouseArea (even acceptedButtons:
        // NoButton) can block hover delivery and kill the I-beam.
        // The app keeps its wheel router at z 0 for that reason.
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true

        property int clickCount: 1
        property double lastPressTime: 0
        property real lastPressX: -1e6
        property real lastPressY: -1e6
        property bool selecting: false
        property var pressLink: null
        property bool dragMoved: false
        property real lastMouseX: 0
        property real lastMouseY: 0
        property bool overText: false
        property bool overLink: false

        cursorShape: overLink && !selecting ? Qt.PointingHandCursor
                   : (overText || selecting) ? Qt.IBeamCursor
                                             : Qt.ArrowCursor

        // Viewport point → { page, pos (page points) }, or null.
        // NB: cellAtPosition wants CONTENT-item coordinates (the
        // deprecated cellAtPos was the viewport-coordinate variant);
        // passing viewport coords silently resolves everything to the
        // rows near the origin — i.e. selection stuck on page 1.
        function pageHit(x, y) {
            const tp = selArea.mapToItem(tableView.contentItem, x, y)
            const cell = tableView.cellAtPosition(tp.x, tp.y, true)
            if (cell.y < 0)
                return null
            const item = tableView.itemAtCell(cell)
            if (!item || !item.paperItem)
                return null
            const paper = item.paperItem
            const pp = selArea.mapToItem(paper, x, y)
            const s = paper.pageScale
            if (!(s > 0))
                return null
            return { page: cell.y, pos: Qt.point(pp.x / s, pp.y / s) }
        }

        function extendAt(x, y) {
            const cx = Math.max(0, Math.min(x, selArea.width - 1))
            const cy = Math.max(0, Math.min(y, selArea.height - 1))
            const h = pageHit(cx, cy)
            if (h)
                root.selectionModel.extendTo(h.page, h.pos)
        }

        onPressed: function(mouse) {
            const h = pageHit(mouse.x, mouse.y)
            if (mouse.button === Qt.RightButton) {
                ctxMenu.pageIdx = h ? h.page : pageNavigator.currentPage
                ctxMenu.popup()
                return
            }
            const now = Date.now()
            const dx = mouse.x - lastPressX
            const dy = mouse.y - lastPressY
            clickCount = (now - lastPressTime < 450 && dx * dx + dy * dy < 25)
                       ? clickCount + 1 : 1
            lastPressTime = now
            lastPressX = mouse.x
            lastPressY = mouse.y
            dragMoved = false
            pressLink = null
            if (!h) {
                root.selectionModel.clear()
                return
            }
            const link = root.selectionModel.linkAt(h.page, h.pos, true)
            if (link.found && clickCount === 1) {
                // Click follows the link; selection is untouched until
                // we know it wasn't a drag.
                pressLink = link
                return
            }
            selecting = true
            root.selectionModel.beginAt(h.page, h.pos, clickCount)
        }

        onPositionChanged: function(mouse) {
            lastMouseX = mouse.x
            lastMouseY = mouse.y
            if (pressed && (selecting || pressLink)) {
                if (Math.abs(mouse.x - lastPressX) > 4
                        || Math.abs(mouse.y - lastPressY) > 4)
                    dragMoved = true
                if (!selecting)
                    return
                // Edge auto-scroll, browser-style: speed grows with
                // distance past the edge (or into the margin band).
                const m = 34
                let vy = 0
                if (mouse.y < m)
                    vy = -Math.min(28, (m - mouse.y) * 0.45)
                else if (mouse.y > selArea.height - m)
                    vy = Math.min(28, (mouse.y - (selArea.height - m)) * 0.45)
                autoScroll.vy = vy
                autoScroll.running = vy !== 0
                extendAt(mouse.x, mouse.y)
                return
            }
            // Hover: drive the cursor.
            const h = pageHit(mouse.x, mouse.y)
            overText = h && root.selectionModel
                ? root.selectionModel.overText(h.page, h.pos) : false
            overLink = h && root.selectionModel
                ? root.selectionModel.linkAt(h.page, h.pos).found : false
        }

        onReleased: function(mouse) {
            autoScroll.running = false
            if (pressLink && pressLink.found && !dragMoved) {
                root.selectionModel.clear()
                if (pressLink.page >= 0)
                    root.goToLocation(pressLink.page, pressLink.location,
                                      pressLink.zoom)
                else
                    Qt.openUrlExternally(pressLink.url)
            }
            pressLink = null
            selecting = false
        }

        onCanceled: {
            autoScroll.running = false
            pressLink = null
            selecting = false
        }

        Timer {
            id: autoScroll
            interval: 16
            repeat: true
            property real vy: 0
            onTriggered: {
                const minY = tableView.originY
                const maxY = Math.max(minY,
                    tableView.originY + tableView.contentHeight - tableView.height)
                tableView.contentY =
                    Math.max(minY, Math.min(maxY, tableView.contentY + vy))
                selArea.extendAt(selArea.lastMouseX, selArea.lastMouseY)
            }
        }

        Menu {
            id: ctxMenu
            property int pageIdx: 0
            MenuItem {
                text: qsTr("Copy")
                enabled: root.selectionModel && root.selectionModel.hasSelection
                onTriggered: root.selectionModel.copyToClipboard()
            }
            MenuItem {
                text: qsTr("Select All on Page")
                onTriggered: root.selectionModel.selectAllOnPage(ctxMenu.pageIdx)
            }
        }
    }

    onRenderScaleChanged: {
        // if pageNavigator.jumped changes the scale, don't turn around and update the stack again;
        // and don't force layout either, because positionViewAtCell() will do that
        if (pageNavigator.jumping)
            return
        // page size changed: TableView needs to redo layout to avoid overlapping delegates or gaps between them
        tableView.forceLayout()
        const cell = tableView.cellAtPos(root.width / 2, root.height / 2)
        const currentItem = tableView.itemAtCell(cell)
        if (currentItem) {
            const currentLocation = Qt.point((tableView.contentX - currentItem.x + tableView.jumpLocationMargin.x) / root.renderScale,
                                             (tableView.contentY - currentItem.y + tableView.jumpLocationMargin.y) / root.renderScale)
            pageNavigator.update(cell.y, currentLocation, renderScale)
        }
    }
    PdfPageNavigator {
        id: pageNavigator
        property bool jumping: false
        property int previousPage: 0
        onJumped: function(current) {
            jumping = true
            if (current.zoom > 0)
                root.renderScale = current.zoom
            const pageSize = root.pageSizeAt(current.page)
            if (current.location.y < 0) {
                // invalid to indicate that a specific location was not needed,
                // so attempt to position the new page just as the current page is
                const previousPageDelegate = tableView.itemAtCell(0, previousPage)
                const currentYOffset = previousPageDelegate
                                     ? tableView.contentY - previousPageDelegate.y
                                     : 0
                tableView.positionViewAtRow(current.page, Qt.AlignTop, currentYOffset)
            } else if (current.rectangles.length > 0) {
                // jump to a search result and position the covered area within the viewport
                pageSize.width *= root.renderScale
                pageSize.height *= root.renderScale
                const rectPts = current.rectangles[0]
                const rectPx = Qt.rect(rectPts.x * root.renderScale - tableView.jumpLocationMargin.x,
                                       rectPts.y * root.renderScale - tableView.jumpLocationMargin.y,
                                       rectPts.width * root.renderScale + tableView.jumpLocationMargin.x * 2,
                                       rectPts.height * root.renderScale + tableView.jumpLocationMargin.y * 2)
                tableView.positionViewAtCell(0, current.page, TableView.Contain, Qt.point(0, 0), rectPx)
            } else {
                // jump to a page and position the given location relative to the top-left corner of the viewport
                pageSize.width *= root.renderScale
                pageSize.height *= root.renderScale
                const rectPx = Qt.rect(current.location.x * root.renderScale - tableView.jumpLocationMargin.x,
                                       current.location.y * root.renderScale - tableView.jumpLocationMargin.y,
                                       tableView.jumpLocationMargin.x * 2, tableView.jumpLocationMargin.y * 2)
                tableView.positionViewAtCell(0, current.page, TableView.AlignLeft | TableView.AlignTop, Qt.point(0, 0), rectPx)
            }
            jumping = false
            previousPage = current.page
        }

        property url documentSource: root.document.source
        onDocumentSourceChanged: {
            pageNavigator.clear()
            root.resetScale()
            tableView.contentX = 0
            tableView.contentY = 0
        }
    }
    PdfSearchModel {
        id: searchModel
        document: root.document === undefined ? null : root.document
        onCurrentResultChanged: pageNavigator.jump(currentResultLink)
    }
}
