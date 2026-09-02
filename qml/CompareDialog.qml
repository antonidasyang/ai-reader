import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Putting several papers side by side (§10).
//
// The comparability warnings sit above the table on purpose. A grid of numbers
// implies that the numbers mean the same thing, and across papers they usually
// do not — different task, different data, different metric. The point of this
// window is to compare honestly, which sometimes means saying these cannot be
// ranked at all.
AppDialog {
    id: root
    title: qsTr("Compare papers")
    width: Math.min(900, Overlay.overlay ? Overlay.overlay.width - 60 : 900)
    height: Math.min(660, Overlay.overlay ? Overlay.overlay.height - 60 : 660)
    standardButtons: Dialog.NoButton

    readonly property var res: compare.result
    readonly property var papers: res && res.papers ? res.papers : []
    // Bumped by every basket change made from this window, so the boxes
    // and chips re-read contains(): it is an invokable and notifies nothing.
    property int pickRevision: 0
    // Library item ids of the picked papers that have no interpretation
    // yet -- what "Interpret the N not yet read" sends to the batch. A
    // paper the quick read gave up on is not among them: running it again
    // would give the same answer.
    readonly property var missingItemIds: {
        compare.count; root.pickRevision; analysisList.revision
        const out = []
        const basket = compare.basket
        for (let i = 0; i < basket.length; ++i) {
            const state = analysisList.stateForPaper(basket[i].paperId)
            if (state === "done" || state === "insufficient"
                || state === "running" || state === "queued")
                continue
            const id = libraryModel.findByPaperId(basket[i].paperId)
            if (id.length > 0)
                out.push(id)
        }
        return out
    }

    onOpened: compare.loadStored()

    // How long the comparison has been running, for the line beside the
    // button. It is one model call that can take a couple of minutes, and a
    // spinner alone reads as "nothing is happening".
    property int elapsedS: 0
    Timer {
        running: compare.busy && root.visible
        interval: 1000
        repeat: true
        onRunningChanged: if (running) root.elapsedS = 0
        onTriggered: root.elapsedS++
    }
    function clock(s) {
        const m = Math.floor(s / 60)
        const r = s % 60
        return m > 0 ? m + ":" + (r < 10 ? "0" : "") + r : r + " s"
    }
    function sizeText(bytes) {
        return bytes < 1024 ? bytes + " B" : Math.round(bytes / 1024) + " KB"
    }
    // What the status line says while the comparison runs.
    readonly property string progressText: {
        if (!compare.busy)
            return ""
        if (compare.queued)
            return qsTr("Waiting for a free slot behind the other model calls… %1")
                       .arg(root.clock(root.elapsedS))
        if (compare.receivedBytes > 0)
            return qsTr("Comparing %1 papers — the model is writing, %2 so far · %3")
                       .arg(compare.count).arg(root.sizeText(compare.receivedBytes))
                       .arg(root.clock(root.elapsedS))
        return qsTr("Comparing %1 papers — waiting for the model's first words · %2")
                   .arg(compare.count).arg(root.clock(root.elapsedS))
    }

    FileDialog {
        id: exportCompareDialog
        title: qsTr("Export comparison")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTr("Markdown files (*.md)"), qsTr("All files (*)")]
        onAccepted: exporter.save(exporter.comparisonMarkdown(), selectedFile)
    }

    function dimensionLabel(code) {
        switch (code) {
        case "research_problem": return qsTr("Research problem")
        case "hypothesis":       return qsTr("Hypothesis")
        case "method":           return qsTr("Method")
        case "inputs_outputs":   return qsTr("Inputs / outputs")
        case "data":             return qsTr("Data / environment")
        case "baselines":        return qsTr("Baselines")
        case "metrics":          return qsTr("Metrics")
        case "results":          return qsTr("Main results")
        case "contributions":    return qsTr("Contributions")
        case "limitations":      return qsTr("Limitations")
        case "reproducibility":  return qsTr("Reproducibility")
        default:                 return code || ""
        }
    }
    function titleOf(paperId) {
        for (let i = 0; i < papers.length; ++i)
            if (papers[i].paperId === paperId)
                return papers[i].title || qsTr("(untitled)")
        return paperId
    }
    function cellText(row, paperId) {
        const cells = row.cells || []
        for (let i = 0; i < cells.length; ++i)
            if (cells[i].paperId === paperId)
                return cells[i].text || ""
        return "—"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // Page/Home/End walk the comparison table.
        focus: true
        Keys.onPressed: (event) => ScrollKeys.handle(event, tableFlick)

        // ── pick the papers ─────────────────────────────────────────
        // Chosen here, in the window that compares them. They used to be
        // reachable only from elsewhere -- the library pane's menu, a
        // statement's ⋯ menu -- and a window that said "nothing selected"
        // without offering anywhere to select was a wall: the reader was
        // told to choose and could not see where.
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            text: libraryModel.count === 0
                  ? qsTr("This project has no papers yet. Add some to the "
                         + "library first.")
                  : qsTr("Tick at least two papers. Each needs an "
                         + "interpretation: the comparison is built from "
                         + "those, not from the PDFs.")
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(200, pickList.contentHeight + 2)
            visible: libraryModel.count > 0
            color: Theme.fieldBg
            border.color: Theme.border
            radius: 4
            clip: true
            ListView {
                id: pickList
                anchors.fill: parent
                anchors.margins: 1
                model: libraryModel
                clip: true
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    id: pickRow
                    required property string itemId
                    required property string paperId
                    required property string title
                    width: ListView.view ? ListView.view.width : 0
                    spacing: 4
                    // Named so the invokables re-run: neither notifies on
                    // its own.
                    readonly property string _state:
                        (analysisList.revision,
                         analysisList.stateForPaper(pickRow.paperId))
                    readonly property bool _picked:
                        (compare.count, root.pickRevision,
                         pickRow.paperId.length > 0
                         && compare.contains(pickRow.paperId))
                    CheckBox {
                        Layout.fillWidth: true
                        text: pickRow.title
                        enabled: pickRow.paperId.length > 0 && !compare.busy
                        // Bound, not assigned: a click writes `checked`
                        // itself, and a plain binding would not survive
                        // that -- Clear would then leave the box ticked.
                        Binding on checked {
                            value: pickRow._picked
                            restoreMode: Binding.RestoreNone
                        }
                        onToggled: {
                            if (checked)
                                compare.add(pickRow.paperId, pickRow.title)
                            else
                                compare.removePaper(pickRow.paperId)
                            root.pickRevision++
                        }
                    }
                    Label {
                        Layout.rightMargin: 8
                        visible: pickRow._state !== "done"
                        color: pickRow._state === "insufficient" ? Theme.danger
                                                                 : Theme.dimText
                        font.pixelSize: 11
                        text: pickRow._state === "insufficient"
                              ? qsTr("not enough text to interpret")
                              : (pickRow._state === "running"
                                 || pickRow._state === "queued")
                                ? qsTr("interpreting…")
                                : qsTr("not interpreted yet")
                    }
                }
            }
        }

        // Papers in the basket that are not in this project's library: a
        // statement added from an open paper that was never added to the
        // project. The list above cannot show them, so they get a chip
        // with its own ✕, or there would be no way to take one out short
        // of clearing everything.
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: compare.basket
                delegate: Rectangle {
                    required property var modelData
                    visible: (root.pickRevision,
                              libraryModel.findByPaperId(modelData.paperId)
                                  .length === 0)
                    height: visible ? 26 : 0
                    width: visible ? chipRow.implicitWidth + 12 : 0
                    radius: 13
                    color: Theme.cardBg
                    border.color: Theme.border
                    RowLayout {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 4
                        Label {
                            text: (modelData.title || qsTr("(untitled)"))
                                  + qsTr(" (not in the library)")
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.maximumWidth: 260
                        }
                        ToolButton {
                            text: "✕"
                            font.pixelSize: 10
                            implicitWidth: 18
                            implicitHeight: 18
                            onClicked: {
                                compare.removePaper(modelData.paperId)
                                root.pickRevision++
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            AppButton {
                primary: true
                text: compare.busy ? qsTr("Cancel")
                                   : (compare.hasResult ? qsTr("Compare again")
                                                        : qsTr("Compare"))
                enabled: compare.busy || compare.canRun
                onClicked: compare.busy ? compare.cancel() : compare.compare()
            }
            BusyIndicator {
                running: compare.busy
                visible: compare.busy
                implicitWidth: 18
                implicitHeight: 18
            }
            AppButton {
                text: qsTr("Clear")
                enabled: compare.count > 0 && !compare.busy
                onClicked: {
                    compare.clearBasket()
                    root.pickRevision++
                }
            }
            AppButton {
                // A ticked paper with no interpretation is not a dead end:
                // the thing it is missing is one click away.
                visible: root.missingItemIds.length > 0
                text: qsTr("Interpret the %1 not yet read")
                      .arg(root.missingItemIds.length)
                enabled: batchAnalysis.canRun && !batchAnalysis.busy
                onClicked: batchAnalysis.startItems(root.missingItemIds, false)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: compare.lastError.length > 0 && !compare.busy
                       ? Theme.danger : Theme.dimText
                font.pixelSize: 12
                // While it runs: what it is doing and for how long. Before:
                // the error from last time, or the reason the button is
                // dead, or how many are picked.
                text: compare.busy
                      ? root.progressText
                      : compare.lastError.length > 0
                        ? compare.lastError
                        : (compare.count === 0
                           ? ""
                           : compare.count === 1
                             ? qsTr("1 paper picked — one more to compare")
                             : compare.blocker.length > 0
                               ? qsTr("%1 papers picked — %2")
                                     .arg(compare.count).arg(compare.blocker)
                               : qsTr("%1 papers picked").arg(compare.count))
            }
        }

        // ── comparability first ─────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: warnCol.implicitHeight + 12
            visible: !!(root.res && root.res.comparability
                        && root.res.comparability.length > 0)
            color: Theme.cardBg
            radius: 4
            ColumnLayout {
                id: warnCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 2
                Label {
                    text: qsTr("Read the table with these in mind")
                    color: Theme.heading
                    font.bold: true
                }
                Repeater {
                    model: root.res && root.res.comparability
                           ? root.res.comparability : []
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: modelData.severity === "blocking" ? Theme.danger
                                                                 : Theme.bodyText
                        font.pixelSize: 12
                        text: (modelData.severity === "blocking"
                               ? qsTr("Not directly comparable: ")
                               : qsTr("Careful: ")) + (modelData.issue || "")
                    }
                }
            }
        }

        // ── the table ───────────────────────────────────────────────
        // One block per dimension, each listing the papers under it. A true
        // grid would need horizontal scrolling as soon as there are more than
        // three papers, and the cells are sentences, not numbers.
        Flickable {
            id: tableFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !!(root.res && root.res.rows && root.res.rows.length > 0)
            contentWidth: width
            contentHeight: tableCol.implicitHeight
            clip: true
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: tableCol
                width: parent.width
                spacing: 10

                Repeater {
                    model: root.res && root.res.rows ? root.res.rows : []
                    delegate: ColumnLayout {
                        id: dimBlock
                        required property var modelData
                        // Named so the inner delegate can still reach it: its
                        // own `modelData` (the paper) shadows this one.
                        readonly property var rowData: modelData
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: root.dimensionLabel(dimBlock.rowData.dimension)
                            color: Theme.heading
                            font.bold: true
                            font.pixelSize: 13
                        }
                        Repeater {
                            model: root.papers
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 6
                                Label {
                                    Layout.preferredWidth: 150
                                    Layout.alignment: Qt.AlignTop
                                    text: modelData.title || qsTr("(untitled)")
                                    color: Theme.dimText
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    color: Theme.text
                                    font.pixelSize: 12
                                    text: root.cellText(dimBlock.rowData,
                                                        modelData.paperId)
                                }
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: !!(root.res && root.res.ranking)
                    wrapMode: Text.Wrap
                    color: Theme.bodyText
                    font.pixelSize: 12
                    text: root.res && root.res.ranking ? root.res.ranking : ""
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    visible: !!(root.res && root.res.takeaways
                                && root.res.takeaways.length > 0)
                    Label {
                        text: qsTr("What to take from this")
                        color: Theme.heading
                        font.bold: true
                        font.pixelSize: 13
                    }
                    Repeater {
                        model: root.res && root.res.takeaways
                               ? root.res.takeaways : []
                        delegate: ClaimBlock {
                            required property var modelData
                            Layout.fillWidth: true
                            claim: modelData
                            fs: 13
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8
                    wrapMode: Text.Wrap
                    color: Theme.dimText
                    font.pixelSize: 11
                    text: compare.resultAuthorEmail.length > 0
                          ? qsTr("Compared from each paper's own interpretation, "
                                 + "not from the PDFs. Generated by %1.")
                            .arg(compare.resultAuthorEmail)
                          : qsTr("Compared from each paper's own interpretation, "
                                 + "not from the PDFs.")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            AppButton {
                text: qsTr("Export as Markdown…")
                visible: compare.hasResult
                onClicked: exportCompareDialog.open()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }
}
