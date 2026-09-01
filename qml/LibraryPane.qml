import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// The current project's papers: the bibliography, the full-text search, and
// — since 1.3.26 — the worklist §7 used to keep in a dialog of its own.
//
// There were two lists of the same papers before this: this pane, and a
// "Interpret the library" window with its own filters, its own row menu and
// its own idea of what was selected. Neither could act on what the other was
// showing, so a filtered set could be marked but not read, and a starred
// paper had nowhere to go. One list solves that by construction: what the
// filters show is what the batch buttons act on, and every per-paper action
// — open it, interpret it, close-read it, star it, compare it, set it aside
// — is on the row itself.
//
// The model is AnalysisListModel, not LibraryModel: it carries the same
// bibliographic fields plus what has been made of each paper. LibraryModel
// is still what writes (adding a paper, removing one).
Rectangle {
    id: root
    color: Theme.paneBg

    signal openRequested(string path)

    property var searchResults: []
    readonly property bool searching: searchField.text.trim().length > 0
    // Bumped by every basket change, so a row's ✓ re-evaluates: contains()
    // is an invokable and notifies nothing on its own.
    property int compareRevision: 0

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

    function stateLabel(s) {
        switch (s) {
        case "done":         return qsTr("interpreted")
        case "queued":       return qsTr("queued")
        case "running":      return qsTr("working…")
        case "failed":       return qsTr("failed")
        case "insufficient": return qsTr("not enough text")
        default:             return qsTr("not read")
        }
    }
    function stateColor(s) {
        switch (s) {
        case "done":         return Theme.success
        case "running":      return Theme.accent
        case "failed":       return Theme.danger
        case "insufficient": return Theme.danger
        default:             return Theme.dimText
        }
    }
    function relevanceLabel(code) {
        switch (code) {
        case "high":    return qsTr("high")
        case "medium":  return qsTr("medium")
        case "low":     return qsTr("low")
        case "unclear": return qsTr("unclear")
        default:        return ""
        }
    }
    function adviceLabel(code) {
        switch (code) {
        case "read_full":               return qsTr("read in full")
        case "read_method_experiments": return qsTr("method + experiments")
        case "background":              return qsTr("background")
        case "low_relevance":           return qsTr("low relevance")
        case "insufficient":            return qsTr("needs a human")
        default:                        return ""
        }
    }

    // Put every paper the filters are showing into the comparison basket.
    function addShownToCompare() {
        const papers = analysisList.visiblePapers()
        for (let i = 0; i < papers.length; ++i)
            compare.add(papers[i].paperId, papers[i].title)
        root.compareRevision++
        compareRequested()
    }
    signal compareRequested()

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
                    // The filtered count only when it differs, so the usual
                    // case still reads as a plain paper count.
                    text: analysisList.count === analysisList.totalPapers
                          ? qsTr("Library (%1)").arg(analysisList.totalPapers)
                          : qsTr("Library (%1 of %2)").arg(analysisList.count)
                                                      .arg(analysisList.totalPapers)
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

        // ── filters ────────────────────────────────────────────────────
        // A Flow, not a row: this pane is often 280px wide and three combos
        // do not fit across it. Each one names its own "no filter" value so
        // it needs no label beside it.
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 6
            spacing: 4
            visible: !root.searching && analysisList.totalPapers > 0
            AppComboBox {
                width: 132
                model: [qsTr("all papers"), qsTr("not read"), qsTr("interpreted"),
                        qsTr("failed"), qsTr("starred"), qsTr("set aside")]
                property var codes: ["", "none", "done", "failed", "toRead",
                                     "excluded"]
                onActivated: function(i) {
                    analysisList.filterState = codes[i]
                    // "Set aside" is the one view where hiding them makes no
                    // sense.
                    analysisList.hideExcluded = codes[i] !== "excluded"
                }
            }
            AppComboBox {
                width: 124
                model: [qsTr("any relevance"), qsTr("high"), qsTr("medium"),
                        qsTr("low"), qsTr("unclear")]
                property var codes: ["", "high", "medium", "low", "unclear"]
                onActivated: function(i) { analysisList.filterRelevance = codes[i] }
            }
            AppComboBox {
                width: 168
                model: [qsTr("any advice"), qsTr("read in full"),
                        qsTr("method + experiments"), qsTr("background"),
                        qsTr("low relevance"), qsTr("needs a human")]
                property var codes: ["", "read_full", "read_method_experiments",
                                     "background", "low_relevance", "insufficient"]
                onActivated: function(i) { analysisList.filterAdvice = codes[i] }
            }
        }

        // ── what the project adds up to ────────────────────────────────
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 4
            visible: !root.searching && analysisList.totalPapers > 0
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: 11
            text: qsTr("%1 interpreted · %2 not read · %3 close-read · %4 starred")
                  .arg(analysisList.interpretedCount)
                  .arg(analysisList.pendingCount)
                  .arg(analysisList.deepDoneCount)
                  .arg(analysisList.toReadCount)
                  + (analysisList.failedCount > 0
                     ? qsTr(" · %1 failed").arg(analysisList.failedCount) : "")
                  + (analysisList.excludedCount > 0
                     ? qsTr(" · %1 set aside").arg(analysisList.excludedCount) : "")
        }

        // ── progress ───────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 4
            spacing: 2
            visible: batchAnalysis.busy || batchAnalysis.status.length > 0
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                ProgressBar {
                    Layout.fillWidth: true
                    visible: batchAnalysis.busy
                    from: 0
                    to: Math.max(1, batchAnalysis.total)
                    value: batchAnalysis.done + batchAnalysis.failed
                           + batchAnalysis.skipped
                }
                Label {
                    visible: batchAnalysis.busy
                    color: Theme.dimText
                    font.pixelSize: 11
                    text: (batchAnalysis.done + batchAnalysis.failed
                           + batchAnalysis.skipped) + "/" + batchAnalysis.total
                }
                ToolButton {
                    text: "✕"
                    visible: batchAnalysis.busy
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Stop the run")
                    onClicked: batchAnalysis.cancel()
                }
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.dimText
                font.pixelSize: 11
                text: batchAnalysis.status
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 4

            // ── the papers ──
            ListView {
                id: libraryList
                anchors.fill: parent
                clip: true
                model: analysisList
                visible: !root.searching && analysisList.count > 0
                ScrollBar.vertical: ScrollBar { active: true }
                // Page/Home/End walk the list once the pane has been clicked.
                Keys.onPressed: (event) => ScrollKeys.handle(event, libraryList)

                delegate: Rectangle {
                    id: row
                    required property string itemId
                    required property string paperId
                    required property string title
                    required property string creators
                    required property string year
                    required property string publication
                    required property string localPath
                    required property string analysisState
                    required property string deepState
                    required property string error
                    required property string oneLiner
                    required property string relevance
                    required property string advice
                    required property string authorEmail
                    required property bool mine
                    required property bool toRead
                    required property bool excluded

                    width: ListView.view ? ListView.view.width : 0
                    height: rowCol.implicitHeight + 14

                    // Match on the paper id first: a paper opened from the
                    // project plays out of the blob cache under a sha256
                    // name, so its path is not the one the library recorded.
                    // Naming paperId/pdfSource is what makes this re-evaluate
                    // when the reader opens a different paper.
                    readonly property bool _isOpen:
                        (paperController.paperId, paperController.pdfSource,
                         (row.paperId && row.paperId.length > 0
                          && paperController.paperId === row.paperId)
                         || paperController.isCurrentFile(row.localPath))
                    readonly property bool _inCompare:
                        (root.compareRevision, compare.count,
                         row.paperId.length > 0 && compare.contains(row.paperId))

                    color: _isOpen   ? Theme.activeRow
                         : rowHover.hovered ? Theme.hover
                         : excluded  ? Theme.paneBg
                                     : "transparent"
                    HoverHandler { id: rowHover }
                    TapHandler {
                        onTapped: fileSync.openItem(row.itemId, row.localPath)
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            ctxMenu.openFor(row.itemId, row.paperId, row.title,
                                            row.localPath, row.analysisState,
                                            row.deepState, row.toRead,
                                            row.excluded)
                        }
                    }

                    ColumnLayout {
                        id: rowCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 1

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Rectangle {
                                Layout.preferredWidth: 7
                                Layout.preferredHeight: 7
                                radius: 3.5
                                Layout.alignment: Qt.AlignVCenter
                                visible: analysisState !== "none"
                                color: analysisState === "failed"
                                       || analysisState === "insufficient"
                                       ? Theme.danger
                                       : (relevance === "high" ? Theme.success
                                                               : Theme.accent)
                                ToolTip.visible: dotHover.hovered
                                ToolTip.delay: 400
                                ToolTip.text: root.stateLabel(analysisState)
                                HoverHandler { id: dotHover }
                            }
                            Label {
                                text: row.title
                                color: excluded ? Theme.dimText : Theme.text
                                font.bold: true
                                font.strikeout: excluded
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                // In the basket, so the reader can see what a
                                // comparison would be built from without
                                // opening it.
                                visible: row._inCompare
                                text: "⇄"
                                color: Theme.accent
                                font.pixelSize: 12
                            }
                            ToolButton {
                                implicitWidth: 24
                                implicitHeight: 24
                                text: toRead ? "★" : "☆"
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                // What the star is *for*: it used to be a
                                // marker with nothing downstream of it.
                                ToolTip.text: toRead
                                    ? qsTr("Starred for a close read — “Close-read the starred” below reads these")
                                    : qsTr("Star this for a close read")
                                onClicked: analysisList.setToRead(row.itemId,
                                                                  !toRead)
                            }
                        }
                        Label {
                            text: [row.creators, row.year, row.publication]
                                  .filter(function(s) { return s && s.length > 0 })
                                  .join("  ·  ")
                            visible: text.length > 0
                            color: Theme.dimText
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: analysisState !== "none"
                                     || deepState !== "none"
                            Label {
                                visible: analysisState !== "done"
                                text: root.stateLabel(analysisState)
                                color: root.stateColor(analysisState)
                                font.pixelSize: 11
                            }
                            Label {
                                visible: relevance.length > 0
                                text: root.relevanceLabel(relevance)
                                color: relevance === "high" ? Theme.success
                                                            : Theme.dimText
                                font.pixelSize: 11
                            }
                            Label {
                                visible: advice.length > 0
                                text: root.adviceLabel(advice)
                                color: Theme.accent
                                font.pixelSize: 11
                            }
                            Label {
                                visible: deepState !== "none"
                                text: deepState === "done"
                                      ? qsTr("close-read")
                                      : qsTr("close read, part done")
                                color: deepState === "done" ? Theme.success
                                                            : Theme.dimText
                                font.pixelSize: 11
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: oneLiner.length > 0
                            elide: Text.ElideRight
                            color: Theme.bodyText
                            font.pixelSize: 11
                            text: oneLiner
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: analysisState === "failed" && error.length > 0
                            wrapMode: Text.Wrap
                            color: Theme.danger
                            font.pixelSize: 11
                            text: row.error
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !mine && authorEmail.length > 0
                            color: Theme.dimText
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            text: qsTr("interpreted by %1").arg(row.authorEmail)
                        }
                    }
                }

                // Everything that can be done to one paper, in one place.
                // The batch dialog had three of these actions, this pane's
                // old menu had two, and neither had the other's.
                //
                // It copies the row's values rather than holding the row.
                // A ListView recycles delegates, and this list reloads
                // itself whenever the store changes -- which a batch makes
                // it do every few seconds -- so a menu holding the delegate
                // would, if that happened while it was open, be pointing at
                // whatever paper the item had been reused for. "Remove from
                // library" is not an action to get wrong that way.
                Menu {
                    id: ctxMenu
                    objectName: "libraryRowMenu"
                    property string itemId: ""
                    property string paperId: ""
                    property string rowTitle: ""
                    property string localPath: ""
                    property string analysisState: "none"
                    property string deepState: "none"
                    property bool toRead: false
                    property bool excluded: false
                    readonly property bool inCompare:
                        (root.compareRevision, compare.count,
                         paperId.length > 0 && compare.contains(paperId))

                    function openFor(id, pid, t, path, state, deep, star, aside) {
                        itemId = id; paperId = pid; rowTitle = t
                        localPath = path; analysisState = state
                        deepState = deep; toRead = star; excluded = aside
                        popup()
                    }
                    MenuItem {
                        text: qsTr("Open")
                        onTriggered: fileSync.openItem(ctxMenu.itemId,
                                                       ctxMenu.localPath)
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: ctxMenu.analysisState === "done"
                              ? qsTr("Interpret again") : qsTr("Interpret")
                        enabled: batchAnalysis.canRun && !batchAnalysis.busy
                        onTriggered: batchAnalysis.startItems([ctxMenu.itemId],
                                                              true)
                    }
                    MenuItem {
                        text: ctxMenu.deepState === "done"
                              ? qsTr("Close-read again") : qsTr("Close-read")
                        enabled: batchAnalysis.canRun && !batchAnalysis.busy
                        onTriggered: batchAnalysis.startDeepItems(
                                         [ctxMenu.itemId],
                                         ctxMenu.deepState === "done")
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: ctxMenu.toRead ? qsTr("Remove the star")
                                             : qsTr("Star for a close read")
                        onTriggered: analysisList.setToRead(ctxMenu.itemId,
                                                            !ctxMenu.toRead)
                    }
                    MenuItem {
                        text: ctxMenu.excluded ? qsTr("Bring back")
                                               : qsTr("Set aside")
                        onTriggered: analysisList.setExcluded(ctxMenu.itemId,
                                                              !ctxMenu.excluded)
                    }
                    MenuItem {
                        // The comparison used to be reachable only from a ⋯
                        // menu inside an open paper's interpretation, so
                        // building a basket of three meant opening three PDFs.
                        text: ctxMenu.inCompare ? qsTr("Remove from the comparison")
                                                : qsTr("Add to the comparison")
                        enabled: ctxMenu.paperId.length > 0
                        onTriggered: {
                            if (ctxMenu.inCompare)
                                compare.removePaper(ctxMenu.paperId)
                            else
                                compare.add(ctxMenu.paperId, ctxMenu.rowTitle)
                            root.compareRevision++
                        }
                    }
                    MenuSeparator {}
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
                                        : analysisList.count === 0
                // What actually happened, in this order: a store that
                // belongs to somebody else is not the same as a signed-out
                // one, an empty project is neither, and a project with
                // papers in it that the filters are hiding is none of them.
                text: root.searching
                      ? qsTr("No matches.")
                      : (projects.libraryLockReason === "other-account"
                         ? qsTr("This library belongs to a different account. Sign in as that user to open it — papers you sync yourself will appear here.")
                         : (projects.libraryLockReason === "signed-out"
                            || !auth.authenticated
                            ? qsTr("Sign in to use the library.")
                            : (projects.currentId.length === 0
                               ? qsTr("Create or select a project.")
                               : (analysisList.totalPapers > 0
                                  ? qsTr("Nothing matches these filters.")
                                  : qsTr("No papers yet. Open a PDF, then click + Add.")))))
            }
        }

        // ── what to do with the whole set ──────────────────────────────
        // The primary button is whatever the project needs next; everything
        // else is one menu, because a pane this narrow cannot hold five
        // buttons and a reader does not need them all at once.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 4
            Layout.bottomMargin: 6
            spacing: 4
            visible: !root.searching && analysisList.totalPapers > 0
            AppButton {
                Layout.fillWidth: true
                primary: true
                text: analysisList.pendingCount > 0
                      ? qsTr("Interpret the %1 unread").arg(analysisList.pendingCount)
                      : (analysisList.deepPendingCount > 0
                         ? qsTr("Close-read the %1 starred").arg(analysisList.deepPendingCount)
                         : qsTr("Everything is interpreted"))
                enabled: batchAnalysis.canRun && !batchAnalysis.busy
                         && (analysisList.pendingCount > 0
                             || analysisList.deepPendingCount > 0)
                onClicked: {
                    if (analysisList.pendingCount > 0)
                        batchAnalysis.startPending()
                    else
                        batchAnalysis.startDeepItems(analysisList.toReadItemIds(),
                                                     false)
                }
            }
            AppButton {
                text: qsTr("More…")
                onClicked: bulkMenu.popup(0, -bulkMenu.height)
                Menu {
                    id: bulkMenu
                    objectName: "libraryBulkMenu"
                    MenuItem {
                        text: qsTr("Interpret what the filters show (%1)")
                              .arg(analysisList.count)
                        enabled: batchAnalysis.canRun && !batchAnalysis.busy
                                 && analysisList.count > 0
                        onTriggered: batchAnalysis.startItems(
                                         analysisList.visibleItemIds(), false)
                    }
                    MenuItem {
                        text: qsTr("Retry the %1 that failed").arg(batchAnalysis.failed)
                        height: visible ? implicitHeight : 0
                        visible: batchAnalysis.failed > 0
                        enabled: !batchAnalysis.busy
                        onTriggered: batchAnalysis.retryFailed()
                    }
                    MenuSeparator {}
                    MenuItem {
                        // The star's outlet. Starring used to be a note to
                        // self with nothing that could act on it.
                        text: qsTr("Close-read the %1 starred")
                              .arg(analysisList.deepPendingCount)
                        enabled: batchAnalysis.canRun && !batchAnalysis.busy
                                 && analysisList.deepPendingCount > 0
                        onTriggered: batchAnalysis.startDeepItems(
                                         analysisList.toReadItemIds(), false)
                    }
                    MenuItem {
                        text: qsTr("Close-read what the filters show (%1)")
                              .arg(analysisList.deepPendingAmong(
                                       analysisList.visibleItemIds()).length)
                        enabled: batchAnalysis.canRun && !batchAnalysis.busy
                                 && analysisList.count > 0
                        onTriggered: batchAnalysis.startDeepItems(
                                         analysisList.visibleItemIds(), false)
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Star everything shown")
                        enabled: analysisList.count > 0
                        onTriggered: analysisList.applyToRead(
                                         analysisList.visibleItemIds(), true)
                    }
                    MenuItem {
                        text: qsTr("Unstar everything shown")
                        enabled: analysisList.count > 0
                        onTriggered: analysisList.applyToRead(
                                         analysisList.visibleItemIds(), false)
                    }
                    MenuItem {
                        text: qsTr("Set everything shown aside")
                        enabled: analysisList.count > 0
                        onTriggered: analysisList.applyExcluded(
                                         analysisList.visibleItemIds(), true)
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Compare everything shown (%1)")
                              .arg(analysisList.count)
                        enabled: analysisList.count > 1
                        onTriggered: root.addShownToCompare()
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Stop the run")
                        height: visible ? implicitHeight : 0
                        visible: batchAnalysis.busy
                        onTriggered: batchAnalysis.cancel()
                    }
                    MenuItem {
                        text: qsTr("Check PDFs are in the cloud")
                        enabled: auth.authenticated && projects.canWrite
                                 && !fileSync.busy
                        onTriggered: fileSync.repairAttachments()
                    }
                }
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
