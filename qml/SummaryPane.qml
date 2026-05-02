import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: "#fafafa"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "#ececec"
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Interpretation")
                    font.bold: true
                    elide: Text.ElideRight
                }
                BusyIndicator {
                    running: summary.status === SummaryService.Generating
                    visible: running
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                ToolButton {
                    text: summary.status === SummaryService.Generating
                          ? qsTr("Cancel")
                          : (summary.text.length > 0
                             ? qsTr("Regenerate") : qsTr("Generate"))
                    enabled: summary.status === SummaryService.Generating
                             || (settings.isConfigured
                                 && paperController.status === PaperController.Ready)
                    onClicked: {
                        if (summary.status === SummaryService.Generating)
                            summary.cancel()
                        else
                            summary.generate()
                    }
                }
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
                             : (summary.status === SummaryService.Failed
                                ? qsTr("Failed: %1").arg(summary.lastError)
                                : qsTr("Click Generate to interpret this paper.")))
                    color: "#1d1d1d"
                    font.pixelSize: settings.summaryFontSize
                    leftPadding: 16
                    rightPadding: 16
                    topPadding: 12
                    bottomPadding: 12

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
}
