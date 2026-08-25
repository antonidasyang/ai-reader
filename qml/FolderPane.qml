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

    // Where the paper on screen came from, when it is not being played from
    // the file it was imported from. A paper opened out of the project plays
    // from the blob cache under a sha256 name, and without this the row for
    // that very paper would not highlight. Set by Main.qml; one lookup per
    // paper change, not per row.
    property string openPaperPath: ""

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
    // Change the map without announcing it, and hand back what that did
    // to the count, so a caller can announce a whole batch in one go.
    function applyOne(path, on) {
        if (!path || on === (root.selectedPaths[path] === true))
            return 0
        if (on)
            root.selectedPaths[path] = true
        else
            delete root.selectedPaths[path]
        return on ? 1 : -1
    }
    function setSelected(path, on) {
        root.selectedCount += root.applyOne(path, on)
    }
    // Ticking a folder used to bump selectedCount once per PDF, and every
    // bump re-evaluates the tick state of every visible row, each of
    // which re-reads its own whole list — quadratic, and a freeze of its
    // own on a folder with thousands of PDFs. Mutate everything first,
    // notify once at the end.
    function selectPaths(paths, on) {
        if (!paths || paths.length === 0)
            return
        let delta = 0
        for (let i = 0; i < paths.length; ++i)
            delta += root.applyOne(paths[i], on)
        if (delta !== 0)
            root.selectedCount += delta
    }
    function clearSelection() {
        root.selectedPaths = ({})
        root.selectedCount = 0
    }
    function selectedList() {
        return Object.keys(root.selectedPaths)
    }
    // Tick state of a whole folder: checked when every PDF under it is
    // selected, partially checked when only some are. Reading
    // selectedCount first is what makes callers re-evaluate.
    function groupState(paths) {
        const notifier = root.selectedCount
        if (notifier < 0 || !paths || paths.length === 0)
            return Qt.Unchecked
        // Bail out at the first disagreement rather than counting the
        // whole list: on a folder of thousands this runs per row.
        let any = false
        let all = true
        for (let i = 0; i < paths.length; ++i) {
            if (root.selectedPaths[paths[i]] === true)
                any = true
            else
                all = false
            if (any && !all)
                return Qt.PartiallyChecked
        }
        return all ? Qt.Checked : Qt.Unchecked
    }

    // The playing paper's own path, resolved once per paper change. The
    // row highlight used to call paperController.isCurrentFile() per row,
    // which canonicalises both sides — two filesystem round-trips for
    // every visible row, and they are not free on a share.
    readonly property string activeFilePath:
        library.localFile(paperController.pdfSource)
    readonly property string activeFileName: paperController.fileName

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
                // The button is disabled while the folder is still being
                // counted, and a disabled control gets no hover events —
                // so the tooltip that explains why hangs off this wrapper,
                // which stays enabled.
                Item {
                    visible: !importer.busy
                    implicitWidth: allButton.implicitWidth
                    implicitHeight: allButton.implicitHeight
                    Layout.preferredWidth: allButton.implicitWidth

                    // Counted in the background; -1 means the walk has not
                    // landed yet. A truncated walk saw only part of the
                    // tree, so "All" would silently mean "some".
                    readonly property int pdfCount:
                        (library.scanRevision,
                         library.pdfCountUnder(library.rootIndex()))
                    readonly property bool truncated:
                        (library.scanRevision,
                         library.scanTruncated(library.rootIndex()))

                    HoverHandler { id: allHover }
                    ToolTip.visible: allHover.hovered
                    ToolTip.delay: 400
                    ToolTip.text:
                        truncated
                        ? qsTr("More than %1 PDFs in this folder — too many to select in one go. Pick a subfolder instead.")
                              .arg(library.scanLimit)
                        : pdfCount < 0
                          ? qsTr("Still counting the PDFs in this folder…")
                          : qsTr("Select every PDF in this folder, subfolders included")

                    ToolButton {
                        id: allButton
                        anchors.centerIn: parent
                        text: qsTr("All")
                        enabled: parent.pdfCount > 0 && !parent.truncated
                        onClicked: root.selectPaths(
                            library.pdfsUnderCached(library.rootIndex()), true)
                    }
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
                    readonly property string _name: delegateRoot.model.display
                    readonly property bool _isActiveFile:
                        !_isDir
                        // Both sides are the model's own path strings for
                        // anything opened from this pane, so the plain
                        // compare answers almost every row for free.
                        && (_path === root.activeFilePath
                            || (root.openPaperPath.length > 0
                                && root.openPaperPath === _path)
                            // Only a row whose file name matches can still
                            // turn out to be the same file through a link
                            // or an alias, and that is at most one row on
                            // screen — the expensive canonical compare is
                            // worth it there and nowhere else.
                            || (root.activeFileName.length > 0
                                && _name === root.activeFileName
                                && paperController.isCurrentFile(_path)))

                    // Every PDF this row stands for: the file itself, or
                    // everything under the folder — including the parts
                    // of it the user never expanded, which have no rows.
                    //
                    // A folder row must never call library.pdfsUnder():
                    // that walks the subtree on this thread and is exactly
                    // what froze the pane. It reads the background walk's
                    // cache instead — -1 until that walk lands, so the row
                    // paints straight away and settles afterwards, and
                    // library.scanRevision is in the binding so it does.
                    // A file row stays synchronous: one QFileInfo.
                    readonly property int _pdfCount:
                        _isDir ? (library.scanRevision,
                                  library.pdfCountUnder(_modelIndex))
                               : _pdfs.length
                    // The walk gave up part-way, so this listing is a
                    // subset of the subtree — countable, but not something
                    // to tick "all" of.
                    readonly property bool _truncated:
                        _isDir && (library.scanRevision,
                                   library.scanTruncated(_modelIndex))
                    readonly property var _pdfs:
                        !_isDir  ? library.pdfsUnder(_modelIndex)
                        : _truncated ? []
                                     : (library.scanRevision,
                                        library.pdfsUnderCached(_modelIndex))
                    readonly property int _tickState:
                        _isDir ? root.groupState(_pdfs)
                               : (root.isSelected(_path) ? Qt.Checked
                                                         : Qt.Unchecked)

                    // Tick box for the batch import. A folder's box ticks
                    // everything underneath it and shows a partial mark
                    // while only some of it is selected.
                    contentItem: RowLayout {
                        spacing: 4
                        // Wrapped because the box of a truncated folder is
                        // disabled, and a disabled control receives no
                        // hover events — the tooltip saying why has to
                        // hang off something that is still enabled.
                        Item {
                            implicitWidth: 20
                            implicitHeight: 20
                            Layout.preferredWidth: 20
                            // Nothing to tick until the count is known.
                            visible: delegateRoot._pdfCount > 0

                            HoverHandler { id: tickHover }
                            ToolTip.visible: tickHover.hovered
                                             && delegateRoot._truncated
                            ToolTip.delay: 400
                            ToolTip.text:
                                qsTr("More than %1 PDFs under this folder — too many to tick in one go. Open a subfolder to pick from it.")
                                    .arg(library.scanLimit)

                            CheckBox {
                                id: tickBox
                                anchors.fill: parent
                                // A subset listing must not offer to tick
                                // the whole subtree.
                                enabled: !delegateRoot._truncated
                                padding: 0
                                tristate: delegateRoot._isDir
                                checkState: delegateRoot._tickState
                                onClicked: {
                                    // The control already cycled its own
                                    // checkState; _tickState still holds
                                    // what the selection said before the
                                    // click.
                                    const on = delegateRoot._tickState !== Qt.Checked
                                    root.selectPaths(delegateRoot._pdfs, on)
                                    // That click also dropped the binding
                                    // above; restore it so the All / None
                                    // buttons and sibling rows still
                                    // drive it.
                                    tickBox.checkState = Qt.binding(
                                        () => delegateRoot._tickState)
                                }
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

                    // All three act on the row's cached listing. Asking
                    // the filesystem here instead would put the freeze
                    // back one click further along: a right-click on a
                    // folder nobody has counted yet.
                    Menu {
                        id: rowMenu
                        MenuItem {
                            text: !delegateRoot._isDir
                                  ? qsTr("Select this PDF")
                                  : delegateRoot._truncated
                                    ? qsTr("Too many PDFs here to select at once")
                                    : delegateRoot._pdfCount < 0
                                      ? qsTr("Counting PDFs…")
                                      : qsTr("Select all PDFs here")
                            enabled: delegateRoot._pdfs.length > 0
                            onTriggered: root.selectPaths(delegateRoot._pdfs, true)
                        }
                        MenuItem {
                            text: delegateRoot._isDir
                                  ? qsTr("Deselect all PDFs here")
                                  : qsTr("Deselect this PDF")
                            enabled: delegateRoot._pdfs.length > 0
                            onTriggered: root.selectPaths(delegateRoot._pdfs, false)
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Add to library")
                            enabled: root.canImport && !importer.busy
                                     && delegateRoot._pdfs.length > 0
                            onTriggered: importer.importFiles(delegateRoot._pdfs)
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
