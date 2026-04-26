import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: "#fafafa"

    property var model: null
    property int paperStatus: PaperController.Empty

    // Suppresses pageRequested while the list is being scrolled
    // programmatically (e.g. in response to PDF navigation).
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
        // Re-enable on the next event-loop pass, after the resulting
        // contentY changes have already fired.
        Qt.callLater(function() { root.syncEnabled = true })
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
                          ? qsTr("Extracted blocks (%1)").arg(list.count)
                          : qsTr("Extracted blocks")
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: root.paperStatus === PaperController.Loading
                    text: qsTr("extracting…")
                    color: "#888"
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
                spacing: 6
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
                    implicitHeight: cell.implicitHeight + 14

                    ColumnLayout {
                        id: cell
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 6
                        anchors.bottomMargin: 8
                        spacing: 2

                        Label {
                            text: qsTr("p.%1 · %2").arg(model.page + 1).arg(model.kindName)
                            font.pixelSize: 10
                            color: "#999"
                        }
                        Text {
                            Layout.fillWidth: true
                            text: model.text
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            color: "#1d1d1d"
                            font.pixelSize: model.kindName === "heading" ? 16 : 13
                            font.bold:    model.kindName === "heading"
                            font.italic:  model.kindName === "caption"
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
