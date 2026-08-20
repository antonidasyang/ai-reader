import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: Theme.paneBg

    // Emitted when the user picks a PDF in the tree. Wired to
    // PaperController.openPdf in Main.qml so a single click loads it.
    signal pdfChosen(url path)

    // ── Batch selection ─────────────────────────────────────────────
    // Ticked PDFs, as a path → true map. `selectedCount` is the change
    // notifier: a plain JS object mutates without notifying, so every
    // binding that depends on the set reads the count first (see
    // isSelected() below, whose result is only re-evaluated because the
    // count was touched).
    property var selectedPaths: ({})
    property int selectedCount: 0

    function isSelected(path) {
        return root.selectedCount >= 0 && root.selectedPaths[path] === true
    }
    function setSelected(path, on) {
        if (!path || on === (root.selectedPaths[path] === true))
            return
        if (on)
            root.selectedPaths[path] = true
        else
            delete root.selectedPaths[path]
        root.selectedCount += on ? 1 : -1
    }
    function selectPaths(paths, on) {
        for (let i = 0; i < paths.length; ++i)
            root.setSelected(paths[i], on)
    }
    function clearSelection() {
        root.selectedPaths = ({})
        root.selectedCount = 0
    }
    function selectedList() {
        return Object.keys(root.selectedPaths)
    }

    // The import lands in the cloud project library, so it needs the
    // same rights the library pane's "+ Add" needs.
    readonly property bool canImport: auth.authenticated && projects.canWrite

    Connections {
        target: importer
        function onFinished(added, skipped, failed) { root.clearSelection() }
    }

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
            color: Theme.headerBg
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
            color: Theme.dimText
            font.pixelSize: 10
            leftPadding: 12
            rightPadding: 12
            topPadding: 4
            bottomPadding: 4
        }

        // ── Batch import strip ──────────────────────────────────────
        // Tick PDFs in the tree and add them to the project library in
        // one go, without opening any of them.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 34 : 0
            visible: library.currentFolder.length > 0
            color: Theme.headerBg

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    color: importer.busy ? Theme.accent : Theme.dimText
                    text: importer.busy
                          ? qsTr("%1 (%2/%3)").arg(importer.status)
                                              .arg(importer.done)
                                              .arg(importer.total)
                          : root.selectedCount > 0
                            ? qsTr("%1 selected").arg(root.selectedCount)
                            : qsTr("Tick PDFs to add them to the library")
                }
                ToolButton {
                    text: qsTr("All")
                    visible: !importer.busy
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Select every PDF in this folder, subfolders included")
                    onClicked: root.selectPaths(library.pdfsUnder(library.rootIndex()), true)
                }
                ToolButton {
                    text: qsTr("None")
                    visible: !importer.busy && root.selectedCount > 0
                    onClicked: root.clearSelection()
                }
                ToolButton {
                    text: importer.busy ? qsTr("Cancel")
                                        : qsTr("Add %1 →").arg(root.selectedCount)
                    enabled: importer.busy
                             || (root.selectedCount > 0 && root.canImport)
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: root.canImport
                        ? qsTr("Add the ticked PDFs to the current project's library and upload them")
                        : qsTr("Sign in and pick a project you can write to")
                    onClicked: importer.busy ? importer.cancel()
                                             : importer.importFiles(root.selectedList())
                }
            }
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
                // Recycled delegates were showing a sibling's name on the
                // wrong folder row (a stale text binding survived reuse);
                // disabling reuse keeps each row's label correct.
                reuseItems: false
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

                    // Cache the model index so the active-row check and
                    // the click handler don't each rebuild it.
                    readonly property var _modelIndex:
                        tree.index(row, column)
                    readonly property bool _isDir: library.isDir(_modelIndex)
                    readonly property string _path: library.filePath(_modelIndex)
                    readonly property bool _isActiveFile:
                        !_isDir
                        && library.fileUrl(_modelIndex).toString()
                           === paperController.pdfSource.toString()

                    // Tick box for the batch import. Only files carry
                    // one; a folder's PDFs are ticked from its context
                    // menu, since the tree has no rows for the parts of
                    // it the user never expanded.
                    contentItem: RowLayout {
                        spacing: 4
                        CheckBox {
                            id: tickBox
                            visible: !delegateRoot._isDir
                            implicitWidth: 20
                            implicitHeight: 20
                            padding: 0
                            checked: root.isSelected(delegateRoot._path)
                            onToggled: {
                                root.setSelected(delegateRoot._path, checked)
                                // Clicking assigned `checked` directly, which
                                // drops the binding above; restore it so the
                                // All / None buttons still drive this box.
                                tickBox.checked = Qt.binding(
                                    () => root.isSelected(delegateRoot._path))
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            // Same source the stock contentItem uses —
                            // TreeViewDelegate.text is not the row label.
                            text: delegateRoot.model.display
                            elide: Text.ElideRight
                            color: Theme.text
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    // Right-click: bulk actions scoped to the row. Only
                    // the right button is taken here, so the left-click
                    // open/expand behaviour above is untouched.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onPressed: rowMenu.popup()
                    }

                    Menu {
                        id: rowMenu
                        MenuItem {
                            text: delegateRoot._isDir
                                  ? qsTr("Select all PDFs here")
                                  : qsTr("Select this PDF")
                            onTriggered: root.selectPaths(
                                library.pdfsUnder(delegateRoot._modelIndex), true)
                        }
                        MenuItem {
                            text: delegateRoot._isDir
                                  ? qsTr("Deselect all PDFs here")
                                  : qsTr("Deselect this PDF")
                            onTriggered: root.selectPaths(
                                library.pdfsUnder(delegateRoot._modelIndex), false)
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Add to library")
                            enabled: root.canImport && !importer.busy
                            onTriggered: importer.importFiles(
                                library.pdfsUnder(delegateRoot._modelIndex))
                        }
                    }

                    // Replace the Fusion default — which paints
                    // alternating row backgrounds via palette.alternateBase
                    // — with a single uniform background plus a soft hover
                    // tint and a stronger highlight on the active PDF.
                    background: Rectangle {
                        color: delegateRoot._isActiveFile ? Theme.activeRow
                             : delegateRoot.hovered       ? Theme.hover
                                                          : "transparent"
                    }

                    onClicked: {
                        if (library.isDir(_modelIndex))
                            tree.toggleExpanded(row)
                        else
                            root.pdfChosen(library.fileUrl(_modelIndex))
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: library.currentFolder.length === 0
                color: Theme.dimText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                width: parent.width - 32
                text: qsTr("Click \"Open…\" to choose a folder of PDFs.")
            }
        }
    }
}
