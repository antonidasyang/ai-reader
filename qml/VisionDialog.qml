import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

AppDialog {
    id: dialog
    title: vision.page >= 0
           ? qsTr("Page %1 — Vision read").arg(vision.page + 1)
           : qsTr("Vision read")
    standardButtons: Dialog.Close
    width: Math.min(760, parent ? parent.width - 48 : 760)
    height: Math.min(620, parent ? parent.height - 48 : 620)

    // ── Content ─────────────────────────────────────────────────────
    contentItem: ColumnLayout {
        spacing: Theme.spaceM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceS

            BusyIndicator {
                running: vision.status === VisionService.Rendering
                         || vision.status === VisionService.Generating
                visible: running
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            Label {
                Layout.fillWidth: true
                color: vision.status === VisionService.Failed ? Theme.danger : Theme.dimText
                font.pixelSize: 12
                elide: Text.ElideRight
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
            AppButton {
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

        // Result card — readable Markdown output on a quiet surface.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusM
            color: Theme.cardBg
            border.width: 1
            border.color: Theme.divider
            clip: true

            ScrollView {
                id: visionScroll
                anchors.fill: parent
                anchors.margins: 1
                clip: true

                TextArea {
                    id: textArea
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.MarkdownText
                    background: null
                    color: Theme.text
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.onAccent
                    font.pixelSize: 13
                    leftPadding: Theme.spaceL
                    rightPadding: Theme.spaceL
                    topPadding: Theme.spaceM
                    bottomPadding: Theme.spaceM
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
}
