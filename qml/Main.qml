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

    SummaryDialog {
        id: summaryDialog
        anchors.centerIn: Overlay.overlay
    }

    VisionDialog {
        id: visionDialog
        anchors.centerIn: Overlay.overlay
    }

    Connections {
        target: paperController
        function onPasswordRequired() { passwordDialog.open() }
        function onStatusChanged() {
            if (paperController.status === PaperController.Error) {
                errorBanner.text = paperController.errorString
                errorBanner.visible = true
            } else if (paperController.status === PaperController.Ready) {
                errorBanner.visible = false
            }
        }
    }

    Connections {
        target: translation
        function onLastErrorChanged() {
            if (translation.lastError && translation.lastError.length > 0) {
                errorBanner.text = qsTr("Translation: %1").arg(translation.lastError)
                errorBanner.visible = true
            }
        }
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
                enabled: paperController.status === PaperController.Ready
                         && settings.isConfigured
                onClicked: {
                    summaryDialog.open()
                    if (summary.text.length === 0
                        && summary.status !== SummaryService.Generating)
                        summary.generate()
                }
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

                PdfMultiPageView {
                    id: pdfView
                    anchors.fill: parent
                    document: pdfDoc
                    visible: pdfDoc.status === PdfDocument.Ready
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
                onClicked: errorBanner.visible = false
            }
        }
    }
}
