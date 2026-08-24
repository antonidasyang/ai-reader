import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Putting several papers side by side (§10).
//
// The comparability warnings sit above the table on purpose. A grid of numbers
// implies that the numbers mean the same thing, and across papers they usually
// do not — different task, different data, different metric. The point of this
// window is to compare honestly, which sometimes means saying these cannot be
// ranked at all.
Dialog {
    id: root
    title: qsTr("Compare papers")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(900, Overlay.overlay ? Overlay.overlay.width - 60 : 900)
    height: Math.min(660, Overlay.overlay ? Overlay.overlay.height - 60 : 660)
    padding: 14
    standardButtons: Dialog.NoButton

    readonly property int btnH: 30
    readonly property var res: compare.result
    readonly property var papers: res && res.papers ? res.papers : []

    palette.window: Theme.paneBg
    palette.windowText: Theme.text
    palette.base: Theme.fieldBg
    palette.text: Theme.text
    palette.button: Theme.buttonBg
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    palette.placeholderText: Theme.dimText
    background: Rectangle {
        color: Theme.paneBg
        border.color: Theme.border
        radius: 6
    }

    onOpened: compare.loadStored()

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

        // ── the basket ──────────────────────────────────────────────
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            text: compare.count === 0
                  ? qsTr("Nothing selected yet. Add papers from the ⋯ menu next "
                         + "to any statement in an interpretation, or from the "
                         + "Interpret pane.")
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
            Button {
                text: compare.busy ? qsTr("Cancel")
                                   : (compare.hasResult ? qsTr("Compare again")
                                                        : qsTr("Compare"))
                enabled: compare.busy || compare.canRun
                Layout.preferredHeight: root.btnH
                onClicked: compare.busy ? compare.cancel() : compare.compare()
            }
            BusyIndicator {
                running: compare.busy
                visible: compare.busy
                implicitWidth: 18
                implicitHeight: 18
            }
            Button {
                text: qsTr("Clear")
                enabled: compare.count > 0 && !compare.busy
                Layout.preferredHeight: root.btnH
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
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Close")
                Layout.preferredHeight: root.btnH
                onClicked: root.close()
            }
        }
    }
}
