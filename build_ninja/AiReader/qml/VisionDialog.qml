import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Dialog {
    id: dialog
    title: vision.page >= 0
           ? qsTr("Page %1 — Vision read").arg(vision.page + 1)
           : qsTr("Vision read")
    modal: true
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    width: 760
    height: 620

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            BusyIndicator {
                running: vision.status === VisionService.Rendering
                         || vision.status === VisionService.Generating
                visible: running
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            Label {
                Layout.fillWidth: true
                color: vision.status === VisionService.Failed ? "#c62828" : "#555"
                font.pixelSize: 11
                text: {
                    switch (vision.status) {
                    case VisionService.Rendering:  return qsTr("Rendering page…")
                    case VisionService.Generating: return qsTr("Reading with vision…")
                    case VisionService.Failed:     return qsTr("Failed: %1").arg(vision.lastError)
                    case VisionService.Done:       return qsTr("Done.")
                    default: return ""
                    }
                }
            }
            Button {
                text: vision.status === VisionService.Generating
                      || vision.status === VisionService.Rendering
                      ? qsTr("Cancel")
                      : qsTr("Re-run")
                enabled: paperController.status === PaperController.Ready
                         && settings.isConfigured
                onClicked: {
                    if (vision.status === VisionService.Generating
                        || vision.status === VisionService.Rendering) {
                        vision.cancel()
                    } else {
                        vision.readPage(vision.page >= 0
                                        ? vision.page : pdfView.currentPage)
                    }
                }
            }
        }

        ScrollView {
            id: visionScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: textArea
                readOnly: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.MarkdownText
                text: vision.text.length > 0
                      ? vision.text
                      : qsTr("Click 'Read page (vision)' to send the current "
                             + "page image to the LLM.")

                // Follow the bottom while the vision model streams so
                // newly-arrived tokens stay visible.
                onContentHeightChanged: {
                    if (vision.status === VisionService.Generating)
                        Qt.callLater(visionScroll.scrollToEnd)
                }
            }

            function scrollToEnd() {
                const f = visionScroll.contentItem
                if (f) f.contentY = Math.max(0, f.contentHeight - f.height)
            }
        }
    }
}
