import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Dialog {
    id: dialog
    title: qsTr("Paper summary")
    modal: true
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    width: 760
    height: 640

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            BusyIndicator {
                running: summary.status === SummaryService.Generating
                visible: running
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            Label {
                text: {
                    switch (summary.status) {
                    case SummaryService.Generating:
                        return qsTr("Generating…")
                    case SummaryService.Done:
                        return qsTr("Done.")
                    case SummaryService.Failed:
                        return qsTr("Failed: %1").arg(summary.lastError)
                    default:
                        return qsTr("Ready.")
                    }
                }
                color: summary.status === SummaryService.Failed ? "#c62828" : "#555"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Button {
                text: qsTr("Cancel")
                visible: summary.status === SummaryService.Generating
                onClicked: summary.cancel()
            }
            Button {
                text: summary.text.length > 0 ? qsTr("Regenerate") : qsTr("Generate")
                enabled: summary.status !== SummaryService.Generating
                         && settings.isConfigured
                         && paperController.status === PaperController.Ready
                onClicked: summary.generate()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#fafafa"
            border.color: "#e0e0e0"

            ScrollView {
                id: summaryScroll
                anchors.fill: parent
                anchors.margins: 1
                clip: true

                TextArea {
                    id: body
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.MarkdownText
                    background: null
                    text: summary.text.length > 0
                          ? summary.text
                          : (summary.status === SummaryService.Generating
                             ? qsTr("Sending paper to model…")
                             : qsTr("Click *Generate* to interpret this paper."))
                    color: "#1d1d1d"
                    font.pixelSize: 13
                    leftPadding: 16
                    rightPadding: 16
                    topPadding: 12
                    bottomPadding: 12

                    // Follow the bottom while the model is streaming so
                    // the user sees fresh tokens without scrolling.
                    onContentHeightChanged: {
                        if (summary.status === SummaryService.Generating)
                            Qt.callLater(scrollToEnd)
                    }
                }

                function scrollToEnd() {
                    const f = summaryScroll.contentItem
                    if (f) f.contentY = Math.max(0, f.contentHeight - f.height)
                }
            }
        }
    }

    onClosed: {
        if (summary.status === SummaryService.Generating)
            summary.cancel()
    }
}
