import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Pdf
import AiReader

ApplicationWindow {
    id: window
    width: 1400
    height: 900
    visible: true
    title: paperController.fileName.length > 0
           ? "AI Reader — " + paperController.fileName
           : "AI Reader"

    // Zoom limits picked to match the rest of the Qt PDF demos: below
    // 25 % the text becomes unreadable, above 500 % we burn memory on
    // raster pages the user could see by scrolling instead. Step is
    // 1.2× per click so seven clicks span the full range.
    readonly property real _zoomMin: 0.25
    readonly property real _zoomMax: 5.0
    readonly property real _zoomStep: 1.2
    function _setZoom(s) {
        pdfView.renderScale = Math.max(_zoomMin, Math.min(_zoomMax, s))
    }
    function zoomIn()    { _setZoom(pdfView.renderScale * _zoomStep) }
    function zoomOut()   { _setZoom(pdfView.renderScale / _zoomStep) }
    function resetZoom() { _setZoom(1.0) }

    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        enabled: pdfDoc.status === PdfDocument.Ready
        onActivated: window.zoomIn()
    }
    Shortcut {
        sequence: StandardKey.ZoomOut
        enabled: pdfDoc.status === PdfDocument.Ready
        onActivated: window.zoomOut()
    }
    Shortcut {
        sequence: "Ctrl+0"
        enabled: pdfDoc.status === PdfDocument.Ready
        onActivated: window.resetZoom()
    }

    PdfDocument {
        id: pdfDoc
        source: paperController.pdfSource
        password: paperController.pdfPassword
        // Password handling lives on PaperController; suppress duplicate dialogs.
    }

    PasswordDialog {
        id: passwordDialog
        anchors.centerIn: Overlay.overlay
        promptText: qsTr("\"%1\" is encrypted. Enter the password:").arg(paperController.fileName)
        onAccepted: paperController.setPassword(password)
        // User chose not to unlock this paper — drop it from the tab
        // list rather than leaving an inert "stuck on password" tab.
        onRejected: {
            if (tabs.activeIndex >= 0)
                tabs.closePaper(tabs.activeIndex)
            else
                paperController.clear()
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open PDF")
        nameFilters: ["PDF files (*.pdf)", "All files (*)"]
        onAccepted: tabs.openPaper(selectedFile)
    }

    FileDialog {
        id: exportTextDialog
        title: qsTr("Export extracted text")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: ["Text files (*.txt)", "All files (*)"]
        onAccepted: {
            const ok = paperController.exportExtractedText(selectedFile)
            if (!ok) {
                errorBanner.text = qsTr("Failed to write extracted text.")
                errorBanner.visible = true
            }
        }
    }

    FolderDialog {
        id: openFolderDialog
        title: qsTr("Open folder")
        currentFolder: library.currentFolder.length > 0
                       ? Qt.url("file://" + library.currentFolder)
                       : ""
        onAccepted: {
            library.openFolder(selectedFolder)
            folderPane.visible = true
        }
    }

    SettingsDialog {
        id: settingsDialog
        anchors.centerIn: Overlay.overlay
    }

    VisionDialog {
        id: visionDialog
        anchors.centerIn: Overlay.overlay
    }

    PromptsDialog {
        id: promptsDialog
        anchors.centerIn: Overlay.overlay
    }

    WelcomeWizard {
        id: welcomeWizard
        anchors.centerIn: Overlay.overlay
    }

    function showError(prefix, message) {
        if (!message || message.length === 0) return
        errorBanner.text = prefix && prefix.length > 0
                           ? qsTr("%1: %2").arg(prefix).arg(message)
                           : message
        errorBanner.visible = true
        bannerHideTimer.restart()
    }

    Connections {
        target: paperController
        function onPasswordRequired() { passwordDialog.open() }
        function onStatusChanged() {
            if (paperController.status === PaperController.Error) {
                showError(qsTr("PDF"), paperController.errorString)
            } else if (paperController.status === PaperController.Ready) {
                errorBanner.visible = false
                bannerHideTimer.stop()
            }
        }
    }

    Connections {
        target: translation
        function onLastErrorChanged() { showError(qsTr("Translation"), translation.lastError) }
    }
    Connections {
        target: summary
        function onStatusChanged() {
            if (summary.status === SummaryService.Failed)
                showError(qsTr("Summary"), summary.lastError)
        }
    }
    Connections {
        target: toc
        function onStatusChanged() {
            if (toc.status === TocService.Failed)
                showError(qsTr("TOC"), toc.lastError)
        }
    }
    Connections {
        target: vision
        function onStatusChanged() {
            if (vision.status === VisionService.Failed)
                showError(qsTr("Vision"), vision.lastError)
        }
    }
    Connections {
        target: chat
        function onLastErrorChanged() { showError(qsTr("Chat"), chat.lastError) }
    }

    // ── Bidirectional scroll sync ─────────────────────────────────────
    // Two re-entrancy guards prevent a feedback loop:
    //   • blockList.syncEnabled is dropped when we drive the list from PDF.
    //   • _suppressPdfSync is set when we drive the PDF from the list.
    QtObject {
        id: scrollSync
        property bool suppressPdfSync: false
        property int  lastShownPage: -1
    }

    // ── Pane reorder helpers ──────────────────────────────────────────
    // The DockGrip in each pane drags the pane to a new slot in the
    // SplitView; on release we serialize the order to QSettings so it
    // survives across launches. Pane identity is tracked by objectName.
    function persistPaneOrder() {
        const arr = []
        for (let i = 0; i < split.count; ++i) {
            const it = split.itemAt(i)
            if (it && it.objectName && it.objectName.length > 0)
                arr.push(it.objectName)
        }
        layoutSettings.setPaneOrder(arr.join(","))
    }

    function applySavedPaneOrder() {
        const csv = layoutSettings.paneOrder()
        if (!csv || csv.length === 0) return
        const desired = csv.split(",")
        // Walk each desired position and pull the matching pane (by
        // objectName) into that slot. Skip ids that aren't present
        // any longer (e.g., a pane was removed in a new build).
        for (let dst = 0; dst < desired.length; ++dst) {
            const id = desired[dst]
            for (let i = dst; i < split.count; ++i) {
                const it = split.itemAt(i)
                if (it && it.objectName === id) {
                    if (i !== dst) {
                        const item = split.takeItem(i)
                        split.insertItem(dst, item)
                    }
                    break
                }
            }
        }
    }

    Component.onCompleted: {
        applySavedPaneOrder()
        // First-run tour: defer past the initial render so the wizard
        // pops over a finished UI rather than over an empty window.
        if (!layoutSettings.wizardSeen())
            Qt.callLater(function() { welcomeWizard.open() })
    }

    // PDF → block list. Watch pdfView.currentPage via a side-effect binding.
    Item {
        property int observedPage: pdfView.currentPage
        onObservedPageChanged: {
            if (scrollSync.suppressPdfSync) return
            if (observedPage === scrollSync.lastShownPage) return
            scrollSync.lastShownPage = observedPage
            blockList.showPage(observedPage)
        }
    }

    // Block list → PDF.
    Connections {
        target: blockList
        function onPageRequested(page) {
            if (page < 0) return
            if (page === pdfView.currentPage) return
            scrollSync.suppressPdfSync = true
            scrollSync.lastShownPage = page
            pdfView.goToPage(page)
            Qt.callLater(function() { scrollSync.suppressPdfSync = false })
        }
        function onAskInChatRequested(text, page) {
            chatPane.visible = true
            chatPane.prefillInput(text, page + 1)
        }
        function onTranslateBlockRequested(row) {
            translation.translateBlock(row)
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            ToolButton {
                text: qsTr("Open…")
                onClicked: fileDialog.open()
            }
            ToolButton {
                text: qsTr("Open folder…")
                onClicked: openFolderDialog.open()
            }
            ToolButton {
                text: qsTr("Export text…")
                enabled: paperController.status === PaperController.Ready
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Save the raw PDF text + per-line bboxes + detected paragraphs to a .txt file")
                onClicked: exportTextDialog.open()
            }
            ToolButton {
                text: qsTr("Re-extract")
                enabled: paperController.status === PaperController.Ready
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Discard manual paragraph edits and re-run automatic extraction")
                onClicked: paperController.rebuildBlocks()
            }
            ToolSeparator {}
            ToolButton {
                text: translation.busy ? qsTr("Cancel") : qsTr("Translate")
                enabled: paperController.status === PaperController.Ready
                         && (translation.busy || settings.isConfigured)
                onClicked: translation.busy ? translation.cancel()
                                            : translation.translateAll()
            }
            ToolButton {
                text: qsTr("Retry failed")
                visible: !translation.busy && translation.failedCount > 0
                onClicked: translation.retryFailed()
            }
            ToolButton {
                text: qsTr("Interpret")
                checkable: true
                checked: summaryPane.visible
                onClicked: summaryPane.visible = !summaryPane.visible
            }
            ToolButton {
                text: qsTr("Read page (vision)")
                enabled: paperController.status === PaperController.Ready
                         && settings.isConfigured
                         && vision.status !== VisionService.Generating
                         && vision.status !== VisionService.Rendering
                onClicked: {
                    visionDialog.open()
                    vision.readPage(pdfView.currentPage)
                }
            }
            ToolSeparator {}
            ToolButton {
                text: "−"
                enabled: pdfDoc.status === PdfDocument.Ready
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Zoom out")
                onClicked: window.zoomOut()
            }
            ToolButton {
                // Doubles as a "current zoom" readout and a reset button —
                // saves a slot vs. a separate label + button.
                text: pdfDoc.status === PdfDocument.Ready
                      ? Math.round(pdfView.renderScale * 100) + "%"
                      : "—"
                enabled: pdfDoc.status === PdfDocument.Ready
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Reset zoom")
                onClicked: window.resetZoom()
            }
            ToolButton {
                text: "+"
                enabled: pdfDoc.status === PdfDocument.Ready
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Zoom in")
                onClicked: window.zoomIn()
            }
            ToolSeparator {}
            ToolButton {
                text: qsTr("Folder")
                checkable: true
                checked: folderPane.visible
                onClicked: folderPane.visible = !folderPane.visible
            }
            ToolButton {
                text: qsTr("TOC")
                checkable: true
                checked: tocSidebar.visible
                onClicked: tocSidebar.visible = !tocSidebar.visible
            }
            ToolButton {
                text: qsTr("Chat")
                checkable: true
                checked: chatPane.visible
                onClicked: chatPane.visible = !chatPane.visible
            }
            ToolButton {
                text: qsTr("Quote → Chat")
                visible: paperController.currentSelection.length > 0
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Quote the highlighted PDF text into the chat input")
                onClicked: {
                    chatPane.visible = true
                    chatPane.prefillInput(paperController.currentSelection,
                                          paperController.currentSelectionPage + 1)
                }
            }
            Label {
                text: pdfDoc.status === PdfDocument.Ready
                      ? qsTr("%1 pages · %2 paragraphs")
                            .arg(pdfDoc.pageCount)
                            .arg(paperController.blockCount)
                      : ""
                color: "#555"
                Layout.leftMargin: 8
            }
            Item { Layout.fillWidth: true }
            Label {
                text: settings.isConfigured
                      ? qsTr("%1 · %2").arg(settings.provider).arg(settings.model)
                      : qsTr("LLM not configured")
                color: settings.isConfigured ? "#3949AB" : "#c62828"
                font.pixelSize: 11
                Layout.rightMargin: 8
            }
            ToolButton {
                text: qsTr("Prompts…")
                onClicked: promptsDialog.open()
            }
            ToolButton {
                text: qsTr("Settings…")
                onClicked: settingsDialog.open()
            }
            ToolButton {
                text: "?"
                font.pixelSize: 16
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Show getting-started tour")
                onClicked: welcomeWizard.open()
            }
        }
    }

    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length > 0) {
                for (let i = 0; i < drop.urls.length; ++i) {
                    const u = drop.urls[i].toString()
                    if (u.toLowerCase().endsWith(".pdf")) {
                        tabs.openPaper(drop.urls[i])
                        drop.accepted = true
                        return
                    }
                }
                errorBanner.text = qsTr("Dropped file is not a PDF.")
                errorBanner.visible = true
            }
        }

        SplitView {
            id: split
            anchors.fill: parent
            orientation: Qt.Horizontal

            // ── Far left: folder browser (toggleable) ──────────────────
            // Visible by default if the user previously had a folder
            // open; auto-hidden otherwise so first-launch isn't crowded.
            FolderPane {
                id: folderPane
                objectName: "folder"
                visible: library.currentFolder.length > 0
                SplitView.preferredWidth: 240
                SplitView.minimumWidth: 0
                onPdfChosen: function(path) { tabs.openPaper(path) }

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: folderPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── TOC sidebar ────────────────────────────────────────────
            TocSidebar {
                id: tocSidebar
                objectName: "toc"
                SplitView.preferredWidth: 220
                SplitView.minimumWidth: 0
                onSectionClicked: function(blockId, page) {
                    blockList.showPage(page)
                    pdfView.goToPage(page)
                }

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: tocSidebar
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Middle: PDF reader ─────────────────────────────────────
            Item {
                id: pdfPane
                objectName: "pdf"
                SplitView.preferredWidth: split.width * 0.45
                SplitView.minimumWidth: 280
                // Without clip the PdfMultiPageView (a Flickable) can paint
                // pages past the pane's right/left edges when the user
                // shrinks the splitter and scrolls horizontally — the
                // overflow draws over the TOC and BlockList panes.
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // ── VS Code-style tab strip ─────────────────────────
                    // Hidden when no papers are open so the empty-state
                    // hero card stays the dominant element on first run.
                    Rectangle {
                        id: pdfTabBar
                        Layout.fillWidth: true
                        Layout.preferredHeight: tabs.count > 0 ? 30 : 0
                        visible: tabs.count > 0
                        color: "#2d2d30"

                        Flickable {
                            anchors.left: parent.left
                            anchors.leftMargin: 28   // space for the DockGrip
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            contentWidth: tabRow.width
                            contentHeight: height
                            clip: true
                            interactive: contentWidth > width

                            Row {
                                id: tabRow
                                spacing: 1
                                height: parent.height

                                Repeater {
                                    model: tabs.count
                                    delegate: Rectangle {
                                        id: tabDelegate
                                        readonly property bool isActive: index === tabs.activeIndex
                                        height: parent.height
                                        // Cap tabs at ~220px so a long paper name
                                        // doesn't push the others off-screen.
                                        width: Math.min(220,
                                                        Math.max(80,
                                                                 nameLabel.implicitWidth + 44))
                                        color: isActive ? "#1e1f22"
                                                        : (tabHover.containsMouse ? "#3e3e42" : "#252526")

                                        // Active-tab accent stripe (top edge).
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            height: 2
                                            color: tabDelegate.isActive ? "#5b8def" : "transparent"
                                        }

                                        Label {
                                            id: nameLabel
                                            anchors.left: parent.left
                                            anchors.leftMargin: 10
                                            anchors.right: closeBtn.left
                                            anchors.rightMargin: 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: tabs.nameAt(index)
                                            color: tabDelegate.isActive ? "white" : "#c8c8c8"
                                            elide: Text.ElideMiddle
                                            font.pixelSize: 12
                                        }

                                        // Click anywhere on the tab body
                                        // (other than the × button) to
                                        // activate. Middle-click closes,
                                        // matching VS Code.
                                        MouseArea {
                                            id: tabHover
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            anchors.right: closeBtn.left
                                            hoverEnabled: true
                                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                            onClicked: function(mouse) {
                                                if (mouse.button === Qt.LeftButton)
                                                    tabs.activatePaper(index)
                                                else
                                                    tabs.closePaper(index)
                                            }
                                        }

                                        Rectangle {
                                            id: closeBtn
                                            anchors.right: parent.right
                                            anchors.rightMargin: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 18
                                            height: 18
                                            radius: 3
                                            color: closeArea.containsMouse ? "#c0392b" : "transparent"
                                            Text {
                                                anchors.centerIn: parent
                                                text: "×"
                                                color: closeArea.containsMouse ? "white"
                                                                                : (tabDelegate.isActive ? "#bbb" : "#888")
                                                font.pixelSize: 14
                                            }
                                            MouseArea {
                                                id: closeArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: tabs.closePaper(index)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        PdfMultiPageView {
                            id: pdfView
                            anchors.fill: parent
                            document: pdfDoc
                            visible: pdfDoc.status === PdfDocument.Ready
                            // Mirror the user's PDF selection into the controller so
                            // the chat tool `get_user_selection` can read it.
                            onSelectedTextChanged: paperController.setCurrentSelection(
                                selectedText, currentPage)

                            // Wheel router as a *child* of pdfView so it sits in
                            // the event chain ABOVE the inner Flickable but
                            // INSIDE the pdfView item, which means a non-accepted
                            // wheel cleanly bubbles up to pdfView's own scroll
                            // handling. acceptedButtons=NoButton keeps clicks
                            // and drags (text selection) flowing to pdfView
                            // untouched; only Ctrl+wheel is consumed.
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                z: 1
                                onWheel: function(wheel) {
                                    if (wheel.modifiers & Qt.ControlModifier) {
                                        if (wheel.angleDelta.y > 0)
                                            window.zoomIn()
                                        else if (wheel.angleDelta.y < 0)
                                            window.zoomOut()
                                        wheel.accepted = true
                                    } else {
                                        wheel.accepted = false
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            visible: paperController.status === PaperController.Empty
                                     || (paperController.status === PaperController.Error
                                         && paperController.pdfSource.toString().length === 0)
                            color: "#1e1f22"
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 12
                                Label {
                                    text: qsTr("Drag a PDF here, or click Open…")
                                    color: "#bbbbbb"
                                    font.pixelSize: 18
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Label {
                                    text: qsTr("AI Reader — milestone 3.2 (TOC sidebar)")
                                    color: "#666666"
                                    font.pixelSize: 12
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: paperController.status === PaperController.Loading
                                     || pdfDoc.status === PdfDocument.Loading
                            visible: running
                        }
                    }
                }

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 6
                    pane: pdfPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Right: extracted blocks / translations ─────────────────
            BlockList {
                id: blockList
                objectName: "blocks"
                SplitView.fillWidth: true
                SplitView.minimumWidth: 240
                model: paperController.blocks
                paperStatus: paperController.status

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: blockList
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Interpretation pane (toggleable) ───────────────────────
            SummaryPane {
                id: summaryPane
                objectName: "summary"
                visible: false
                SplitView.preferredWidth: 360
                SplitView.minimumWidth: 240

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: summaryPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Far right: chat pane (toggleable) ──────────────────────
            ChatPane {
                id: chatPane
                objectName: "chat"
                visible: false
                SplitView.preferredWidth: 360
                SplitView.minimumWidth: 240

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: chatPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.pressed ? "#5b8def"
                       : SplitHandle.hovered ? "#bbbbbb" : "#dddddd"
            }
        }

        // Floating insertion marker shown by DockGrip during a drag.
        // Sibling of `split` so the absolute-positioned x/y from the
        // grip's mapToItem(parent, ...) line up correctly.
        Rectangle {
            id: dropMarker
            visible: false
            width: 3
            color: "#5b8def"
            opacity: 0.85
            z: 1000
        }

        Rectangle {
            anchors.fill: parent
            visible: dropArea.containsDrag
            color: "#332b6cff"
            border.color: "#5b8def"
            border.width: 2
            Label {
                anchors.centerIn: parent
                text: qsTr("Drop PDF to open")
                color: "white"
                font.pixelSize: 20
            }
        }
    }

    Rectangle {
        id: errorBanner
        property alias text: errorLabel.text
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: visible ? 36 : 0
        visible: false
        color: "#5a1f1f"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            Label {
                id: errorLabel
                color: "white"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            ToolButton {
                text: "✕"
                onClicked: {
                    errorBanner.visible = false
                    bannerHideTimer.stop()
                }
            }
        }
    }

    // Auto-dismiss the banner so a transient failure doesn't sit there
    // forever once the user has seen it. Restarted by showError().
    Timer {
        id: bannerHideTimer
        interval: 10000
        onTriggered: errorBanner.visible = false
    }
}
