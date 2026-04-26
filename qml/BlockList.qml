import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: "#fafafa"

    property var model: null
    property int paperStatus: PaperController.Empty
    property bool syncEnabled: true

    signal pageRequested(int page)

    function showPage(page) {
        if (!root.model)
            return
        const idx = root.model.firstRowOnPage(page)
        if (idx < 0)
            return
        root.syncEnabled = false
        list.positionViewAtIndex(idx, ListView.Beginning)
        Qt.callLater(function() { root.syncEnabled = true })
    }

    function statusColor(name) {
        switch (name) {
        case "translated":  return "#2e7d32"
        case "translating": return "#1565c0"
        case "queued":      return "#6a1b9a"
        case "failed":      return "#c62828"
        case "skipped":     return "#888888"
        default:            return "#bdbdbd"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "#ececec"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                Label {
                    text: list.count > 0
                          ? qsTr("Blocks (%1)").arg(list.count)
                          : qsTr("Blocks")
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: translation.busy
                    text: qsTr("translating %1/%2…")
                          .arg(translation.doneCount)
                          .arg(translation.totalCount)
                    color: "#1565c0"
                    font.pixelSize: 11
                }
                Label {
                    visible: !translation.busy && translation.failedCount > 0
                    text: qsTr("%1 failed").arg(translation.failedCount)
                    color: "#c62828"
                    font.pixelSize: 11
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: root.model
                spacing: 8
                visible: count > 0

                property int lastReportedPage: -1

                onContentYChanged: maybeReportPage()
                onModelChanged: lastReportedPage = -1

                function maybeReportPage() {
                    if (!root.syncEnabled) return
                    if (count === 0) return
                    const idx = list.indexAt(list.width / 2, list.contentY + 1)
                    if (idx < 0) return
                    const page = root.model.pageOfRow(idx)
                    if (page < 0 || page === lastReportedPage) return
                    lastReportedPage = page
                    root.pageRequested(page)
                }

                ScrollBar.vertical: ScrollBar { active: true }

                delegate: Rectangle {
                    width: ListView.view ? ListView.view.width : 0
                    color: "transparent"
                    implicitHeight: cell.implicitHeight + 16

                    ColumnLayout {
                        id: cell
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 6
                        anchors.bottomMargin: 8
                        spacing: 4

                        // Header strip: page · kind · status
                        RowLayout {
                            spacing: 6
                            Layout.fillWidth: true

                            Label {
                                text: qsTr("p.%1 · %2").arg(model.page + 1).arg(model.kindName)
                                font.pixelSize: 10
                                color: "#999"
                            }
                            Item { Layout.fillWidth: true }
                            Rectangle {
                                visible: model.translationStatusName !== "idle"
                                radius: 6
                                color: root.statusColor(model.translationStatusName)
                                opacity: 0.18
                                implicitWidth: statusLabel.implicitWidth + 12
                                implicitHeight: statusLabel.implicitHeight + 4
                                Label {
                                    id: statusLabel
                                    anchors.centerIn: parent
                                    text: model.translationStatusName
                                    font.pixelSize: 10
                                    color: root.statusColor(model.translationStatusName)
                                }
                            }
                        }

                        // Source text (English) — secondary styling
                        Text {
                            Layout.fillWidth: true
                            text: model.text
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            color: "#5f6368"
                            font.pixelSize: model.kindName === "heading" ? 14 : 12
                            font.italic: model.kindName === "caption"
                        }

                        // Translation — primary styling
                        Text {
                            visible: model.translation && model.translation.length > 0
                            Layout.fillWidth: true
                            text: model.translation || ""
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            color: "#1d1d1d"
                            font.pixelSize: model.kindName === "heading" ? 16 : 14
                            font.bold: model.kindName === "heading"
                        }

                        // Failure detail
                        Text {
                            visible: model.translationStatusName === "failed"
                                     && model.translationError
                                     && model.translationError.length > 0
                            Layout.fillWidth: true
                            text: model.translationError || ""
                            wrapMode: Text.Wrap
                            color: "#c62828"
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: "#ececec"
                        visible: index < list.count - 1
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !list.visible
                horizontalAlignment: Text.AlignHCenter
                color: "#888"
                text: root.paperStatus === PaperController.Loading
                      ? qsTr("Extracting blocks…")
                      : root.paperStatus === PaperController.Error
                        ? qsTr("No blocks (load failed).")
                        : qsTr("Open a PDF to see extracted text.")
            }
        }
    }
}
