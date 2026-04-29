import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: "#f7f7fa"

    signal sectionClicked(int blockId, int page)

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
                anchors.rightMargin: 8
                spacing: 6

                Label {
                    text: list.count > 0
                          ? qsTr("TOC (%1)").arg(list.count)
                          : qsTr("TOC")
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    running: toc.status === TocService.Generating
                    visible: running
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                ToolButton {
                    text: list.count > 0 ? qsTr("Refresh") : qsTr("Generate")
                    enabled: paperController.status === PaperController.Ready
                             && settings.isConfigured
                             && toc.status !== TocService.Generating
                    onClicked: toc.generate()
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
                model: toc.sections
                visible: count > 0

                ScrollBar.vertical: ScrollBar { active: true }

                delegate: ItemDelegate {
                    width: ListView.view ? ListView.view.width : 0
                    height: 28

                    contentItem: RowLayout {
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter

                        Item {
                            implicitWidth: model.indent
                            Layout.preferredWidth: model.indent
                        }
                        Label {
                            Layout.fillWidth: true
                            text: model.title
                            elide: Text.ElideRight
                            font.bold: model.level === 1
                            font.pixelSize: model.level === 1 ? 13 : 12
                            color: model.level === 1 ? "#1a237e" : "#333"
                        }
                        Label {
                            text: qsTr("p.%1").arg(model.startPage + 1)
                            color: "#999"
                            font.pixelSize: 10
                            Layout.rightMargin: 4
                        }
                    }

                    onClicked: root.sectionClicked(model.startBlockId, model.startPage)
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !list.visible
                horizontalAlignment: Text.AlignHCenter
                color: "#888"
                wrapMode: Text.Wrap
                width: parent.width - 32
                text: {
                    switch (toc.status) {
                    case TocService.Generating: return qsTr("Generating TOC…")
                    case TocService.Failed:     return qsTr("Failed: %1").arg(toc.lastError)
                    default:
                        return paperController.status === PaperController.Ready
                            ? qsTr("Click Generate to build the table of contents.")
                            : qsTr("Open a PDF first.")
                    }
                }
            }
        }
    }
}
