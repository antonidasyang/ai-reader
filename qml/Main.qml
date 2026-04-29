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
        onRejected: paperController.clear()
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open PDF")
        nameFilters: ["PDF files (*.pdf)", "All files (*)"]
        onAccepted: paperController.openPdf(selectedFile)
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
                text: qsTr("Close")
                enabled: paperController.status !== PaperController.Empty
                onClicked: paperController.clear()
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
                      ? qsTr("%1 pages · %2 blocks")
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
                        paperController.openPdf(drop.urls[i])
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

            // ── Far left: TOC sidebar ──────────────────────────────────
            TocSidebar {
                id: tocSidebar
                SplitView.preferredWidth: 220
                SplitView.minimumWidth: 0
                onSectionClicked: function(blockId, page) {
                    blockList.showPage(page)
                    pdfView.goToPage(page)
                }
            }

            // ── Middle: PDF reader ─────────────────────────────────────
            Item {
                SplitView.preferredWidth: split.width * 0.45
                SplitView.minimumWidth: 280
                // Without clip the PdfMultiPageView (a Flickable) can paint
                // pages past the pane's right/left edges when the user
                // shrinks the splitter and scrolls horizontally — the
                // overflow draws over the TOC and BlockList panes.
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

            // ── Right: extracted blocks / translations ─────────────────
            BlockList {
                id: blockList
                SplitView.fillWidth: true
                SplitView.minimumWidth: 240
                model: paperController.blocks
                paperStatus: paperController.status
            }

            // ── Interpretation pane (toggleable) ───────────────────────
            SummaryPane {
                id: summaryPane
                visible: false
                SplitView.preferredWidth: 360
                SplitView.minimumWidth: 240
            }

            // ── Far right: chat pane (toggleable) ──────────────────────
            ChatPane {
                id: chatPane
                visible: false
                SplitView.preferredWidth: 360
                SplitView.minimumWidth: 240
            }

            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.pressed ? "#5b8def"
                       : SplitHandle.hovered ? "#bbbbbb" : "#dddddd"
            }
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
