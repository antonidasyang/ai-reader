import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: "#f7f7fa"

    // Emitted when the user picks a PDF in the tree. Wired to
    // PaperController.openPdf in Main.qml so a single click loads it.
    signal pdfChosen(url path)

    FolderDialog {
        id: folderDialog
        title: qsTr("Open folder")
        // Start the picker in the previously chosen folder, if any, so
        // re-opening a sibling is one click instead of three.
        currentFolder: library.currentFolder.length > 0
                       ? Qt.url("file://" + library.currentFolder)
                       : ""
        onAccepted: library.openFolder(selectedFolder)
    }

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
                    text: qsTr("Folder")
                    font.bold: true
                    elide: Text.ElideRight
                }
                ToolButton {
                    text: qsTr("Open…")
                    onClicked: folderDialog.open()
                }
                ToolButton {
                    text: qsTr("Close")
                    enabled: library.currentFolder.length > 0
                    onClicked: library.close()
                }
            }
        }

        // Path strip — let the user see what folder is open without
        // hovering the (single) ToolButton tooltip.
        Label {
            Layout.fillWidth: true
            visible: library.currentFolder.length > 0
            text: library.currentFolder
            elide: Text.ElideMiddle
            color: "#666"
            font.pixelSize: 10
            leftPadding: 12
            rightPadding: 12
            topPadding: 4
            bottomPadding: 4
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Re-binding rootIndex from a Connections handler dodges the
            // chicken-and-egg of QFileSystemModel exposing an empty root
            // before setRootPath() resolves.
            TreeView {
                id: tree
                anchors.fill: parent
                clip: true
                visible: library.currentFolder.length > 0
                model: library.model
                rootIndex: library.rootIndex()

                Connections {
                    target: library
                    function onCurrentFolderChanged() {
                        tree.rootIndex = library.rootIndex()
                    }
                }

                ScrollBar.vertical: ScrollBar { active: true }

                delegate: TreeViewDelegate {
                    id: delegateRoot
                    // TreeViewDelegate inherits ItemDelegate's clicked()
                    // signal. Folder click toggles expansion, file click
                    // opens it. Matches VSCode's Explorer pane.
                    onClicked: {
                        const idx = tree.index(row, column)
                        if (library.isDir(idx))
                            tree.toggleExpanded(row)
                        else
                            root.pdfChosen(library.fileUrl(idx))
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: library.currentFolder.length === 0
                color: "#888"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                width: parent.width - 32
                text: qsTr("Click \"Open…\" to choose a folder of PDFs.")
            }
        }
    }
}
