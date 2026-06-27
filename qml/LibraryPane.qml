import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// The current project's bibliographic items (synced library). Bound to the
// libraryModel / projects / sync / auth context properties.
Rectangle {
    id: root
    color: Theme.paneBg

    signal openRequested(string path)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.headerBg
            clip: true
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 6
                Label {
                    text: qsTr("Library (%1)").arg(libraryModel.count)
                    font.bold: true
                    color: Theme.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                BusyIndicator {
                    running: sync.syncing
                    visible: sync.syncing
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                ToolButton {
                    text: qsTr("+ Add")
                    enabled: auth.authenticated && projects.canWrite
                             && paperController.status === PaperController.Ready
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Add the current paper to this project")
                    onClicked: {
                        const id = libraryModel.addCurrentPaper(
                                       paperController.fileName,
                                       paperController.paperId,
                                       paperController.pdfSource)
                        // Try to auto-complete bibliographic fields from the
                        // PDF's DOI/arXiv id (non-blocking; manual fill remains).
                        if (id && id.length > 0)
                            metadata.autoFill(id)
                    }
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
                model: libraryModel
                visible: libraryModel.count > 0
                ScrollBar.vertical: ScrollBar { active: true }

                delegate: ItemDelegate {
                    width: ListView.view ? ListView.view.width : 0
                    height: 52
                    onClicked: if (model.localPath && model.localPath.length > 0)
                                   root.openRequested(model.localPath)

                    background: Rectangle {
                        color: hovered ? Theme.hover : "transparent"
                    }
                    contentItem: ColumnLayout {
                        spacing: 2
                        Label {
                            text: model.title
                            color: Theme.text
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: [model.creators, model.year, model.publication]
                                  .filter(function(s) { return s && s.length > 0 })
                                  .join("  ·  ")
                            color: Theme.dimText
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            ctxMenu.itemId = model.itemId
                            ctxMenu.popup()
                        }
                    }
                }

                Menu {
                    id: ctxMenu
                    property string itemId: ""
                    MenuItem {
                        text: qsTr("Edit metadata")
                        onTriggered: metaDlg.openFor(ctxMenu.itemId)
                    }
                    MenuItem {
                        text: qsTr("Remove from library")
                        enabled: projects.canWrite
                        onTriggered: libraryModel.removeItem(ctxMenu.itemId)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: libraryModel.count === 0
                color: Theme.dimText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                width: parent.width - 32
                text: !auth.authenticated
                      ? qsTr("Sign in to use the library.")
                      : (projects.currentId.length === 0
                         ? qsTr("Create or select a project.")
                         : qsTr("No papers yet. Open a PDF, then click + Add."))
            }
        }
    }

    MetadataDialog { id: metaDlg }
}
