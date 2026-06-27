import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// The current project's bibliographic items (synced library) + full-text
// search. Bound to libraryModel / search / projects / sync / auth.
Rectangle {
    id: root
    color: Theme.paneBg

    signal openRequested(string path)

    property var searchResults: []
    readonly property bool searching: searchField.text.trim().length > 0

    function runSearch() {
        searchResults = searching ? search.search(searchField.text) : []
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
                        if (id && id.length > 0)
                            metadata.autoFill(id)
                    }
                }
            }
        }

        // Search bar
        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: search.available
                             ? qsTr("Search the project library…")
                             : qsTr("Search unavailable (no FTS5)")
            enabled: search.available && projects.currentId.length > 0
            onTextChanged: searchDebounce.restart()
        }
        Timer {
            id: searchDebounce
            interval: 250
            onTriggered: root.runSearch()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ── Full library ──
            ListView {
                id: libraryList
                anchors.fill: parent
                clip: true
                model: libraryModel
                visible: !root.searching && libraryModel.count > 0
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

            // ── Search results ──
            ListView {
                id: searchList
                anchors.fill: parent
                clip: true
                model: root.searchResults
                visible: root.searching
                ScrollBar.vertical: ScrollBar { active: true }
                delegate: ItemDelegate {
                    width: ListView.view ? ListView.view.width : 0
                    height: 54
                    onClicked: {
                        if (modelData.localPath && modelData.localPath.length > 0)
                            root.openRequested(modelData.localPath)
                        else
                            metaDlg.openFor(modelData.itemId)
                    }
                    background: Rectangle {
                        color: hovered ? Theme.hover : "transparent"
                    }
                    contentItem: ColumnLayout {
                        spacing: 2
                        Label {
                            text: modelData.title || qsTr("(untitled)")
                            color: Theme.text
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: modelData.snippet || ""
                            color: Theme.dimText
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.dimText
                visible: root.searching ? root.searchResults.length === 0
                                        : libraryModel.count === 0
                text: root.searching
                      ? qsTr("No matches.")
                      : (!auth.authenticated
                         ? qsTr("Sign in to use the library.")
                         : (projects.currentId.length === 0
                            ? qsTr("Create or select a project.")
                            : qsTr("No papers yet. Open a PDF, then click + Add.")))
            }
        }
    }

    // Re-run search after a sync may have changed the index.
    Connections {
        target: sync
        function onProjectSynced(pid) { if (root.searching) root.runSearch() }
    }

    MetadataDialog { id: metaDlg }
}
