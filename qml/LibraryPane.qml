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

    // The library this pane is showing. A search belongs to the project it
    // was typed in — and to the account that typed it — so when either
    // changes the hits on screen are somebody else's rows and go with it.
    // Bound to the value, not to the signal: `currentChanged` is also
    // emitted whenever the project list is merely re-fetched, and that must
    // not wipe what the user is in the middle of typing.
    readonly property string shownProject: projects.currentId
    onShownProjectChanged: {
        searchField.text = ""
        searchResults = []
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
                    text: qsTr("Check PDFs")
                    enabled: auth.authenticated && projects.canWrite
                             && !fileSync.busy
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Check that every paper's PDF is really in the cloud, and re-upload the ones missing from it")
                    onClicked: fileSync.repairAttachments()
                }
                ToolButton {
                    text: qsTr("+ Add")
                    enabled: auth.authenticated && projects.canWrite
                             && paperController.status === PaperController.Ready
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Add the current paper to this project")
                    onClicked: {
                        const id = libraryModel.addPaper(
                                       paperController.fileName,
                                       paperController.paperId,
                                       paperController.pdfSource)
                        if (id && id.length > 0) {
                            metadata.autoFill(id)
                            // Upload the PDF blob so collaborators can open it.
                            fileSync.uploadPaper(id, paperController.pdfSource)
                        }
                    }
                }
            }
        }

        // Search bar
        AppTextField {
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
                    onClicked: fileSync.openItem(model.itemId, model.localPath)
                    // Match on the paper id first: a paper opened from the
                    // project plays out of the blob cache under a sha256
                    // name, so its path is not the one the library recorded.
                    // Naming paperId/pdfSource is what makes this re-evaluate
                    // when the reader opens a different paper.
                    readonly property bool _isOpen:
                        (paperController.paperId, paperController.pdfSource,
                         (model.paperId && model.paperId.length > 0
                          && paperController.paperId === model.paperId)
                         || paperController.isCurrentFile(model.localPath))
                    background: Rectangle {
                        color: _isOpen  ? Theme.activeRow
                             : hovered  ? Theme.hover
                                        : "transparent"
                    }
                    // §17: whether this paper has been interpreted, right
                    // where the reader is looking. The revision is named so
                    // the invokable below re-runs when the join changes.
                    readonly property string _analysisState:
                        (analysisList.revision,
                         analysisList.stateForPaper(model.paperId))
                    readonly property string _relevance:
                        (analysisList.revision,
                         analysisList.relevanceForPaper(model.paperId))
                    contentItem: ColumnLayout {
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Rectangle {
                                Layout.preferredWidth: 7
                                Layout.preferredHeight: 7
                                radius: 3.5
                                Layout.alignment: Qt.AlignVCenter
                                visible: _analysisState !== "none"
                                color: _analysisState === "failed"
                                       || _analysisState === "insufficient"
                                       ? Theme.danger
                                       : (_relevance === "high" ? Theme.success
                                                                : Theme.accent)
                                ToolTip.visible: dotHover.hovered
                                ToolTip.delay: 400
                                ToolTip.text: _analysisState === "failed"
                                              ? qsTr("Interpreting this failed")
                                              : (_analysisState === "insufficient"
                                                 ? qsTr("Not enough text to interpret")
                                                 : qsTr("Interpreted"))
                                HoverHandler { id: dotHover }
                            }
                            Label {
                                text: model.title
                                color: Theme.text
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
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
                    onClicked: fileSync.openItem(modelData.itemId,
                                                 modelData.localPath)
                    readonly property bool _isOpen:
                        (paperController.paperId, paperController.pdfSource,
                         (modelData.paperId
                          && paperController.paperId === modelData.paperId)
                         || paperController.isCurrentFile(modelData.localPath || ""))
                    background: Rectangle {
                        color: _isOpen  ? Theme.activeRow
                             : hovered  ? Theme.hover
                                        : "transparent"
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
                // What actually happened, in this order: a store that
                // belongs to somebody else is not the same as a signed-out
                // one, and neither is an empty project.
                text: root.searching
                      ? qsTr("No matches.")
                      : (projects.libraryLockReason === "other-account"
                         ? qsTr("This library belongs to a different account. Sign in as that user to open it — papers you sync yourself will appear here.")
                         : (projects.libraryLockReason === "signed-out"
                            || !auth.authenticated
                            ? qsTr("Sign in to use the library.")
                            : (projects.currentId.length === 0
                               ? qsTr("Create or select a project.")
                               : qsTr("No papers yet. Open a PDF, then click + Add."))))
            }
        }

        // File-transfer feedback. Uploads, downloads and the PDF check
        // all report through fileSync.status, which until now had
        // nowhere to appear — a failed upload looked exactly like a
        // successful one.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: fileSync.status.length > 0 ? 26 : 0
            visible: fileSync.status.length > 0
            color: Theme.headerBg
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 6
                BusyIndicator {
                    running: fileSync.busy
                    visible: running
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: fileSync.status
                    color: Theme.dimText
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    ToolTip.visible: hovered && truncated
                    ToolTip.text: fileSync.status
                    HoverHandler { id: statusHover }
                    property bool hovered: statusHover.hovered
                }
            }
        }
    }

    // Re-run search after a sync may have changed the index.
    Connections {
        target: sync
        function onProjectSynced(pid) { if (root.searching) root.runSearch() }
    }
    // Open the resolved local/downloaded PDF (-> Main.qml tabs.openPaper).
    Connections {
        target: fileSync
        function onOpenReady(path) { root.openRequested(path) }
    }

    MetadataDialog { id: metaDlg }
}
