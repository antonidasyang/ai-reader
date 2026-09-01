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

    onOpened: compare.loadStored()

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

        // ── the basket ──────────────────────────────────────────────
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            // Where papers come from, in the order a reader would reach for
            // them. The library pane came last and was missing entirely
            // until 1.3.26, which is why a basket of three used to mean
            // opening three PDFs one after another.
            text: compare.count === 0
                  ? qsTr("Nothing selected yet. Right-click any paper in the "
                         + "library pane to add it — or add several at once "
                         + "with “Compare everything shown”. A single "
                         + "statement can also be added from the ⋯ menu beside "
                         + "it in an interpretation.")
                  : qsTr("%1 papers selected.").arg(compare.count)
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: compare.basket
                delegate: Rectangle {
                    required property var modelData
                    height: 26
                    width: chipRow.implicitWidth + 12
                    radius: 13
                    color: Theme.cardBg
                    border.color: Theme.border
                    RowLayout {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 4
                        Label {
                            text: modelData.title || qsTr("(untitled)")
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.maximumWidth: 220
                        }
                        ToolButton {
                            text: "✕"
                            font.pixelSize: 10
                            implicitWidth: 18
                            implicitHeight: 18
                            onClicked: compare.removePaper(modelData.paperId)
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
                onClicked: compare.clearBasket()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.danger
                font.pixelSize: 12
                text: compare.lastError
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
