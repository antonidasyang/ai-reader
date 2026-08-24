import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Interpreting the whole project (§7).
//
// The library as a worklist: what has been read, what is queued, what failed
// and why, and — once the interpretations exist — the two filters that make a
// hundred papers tractable, relevance and reading advice. Whatever the filters
// are showing is what the batch buttons act on, so "everything the model
// called low-relevance" can be excluded in one go, and "everything worth a
// close read" can be marked for one.
AppDialog {
    id: root
    title: qsTr("Interpret the library")
    width: Math.min(820, Overlay.overlay ? Overlay.overlay.width - 80 : 820)
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 60 : 640)
    standardButtons: Dialog.NoButton

    onOpened: analysisList.reload()

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
        case "queued":       return Theme.dimText
        case "failed":       return Theme.danger
        case "insufficient": return Theme.danger
        default:             return Theme.dimText
        }
    }
    function relevanceLabel(code) {
        switch (code) {
        case "high":   return qsTr("high")
        case "medium": return qsTr("medium")
        case "low":    return qsTr("low")
        case "unclear":return qsTr("unclear")
        default:       return ""
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // ── summary ─────────────────────────────────────────────────
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            text: qsTr("%1 papers · %2 interpreted · %3 not read · %4 failed · "
                       + "%5 marked to read closely · %6 set aside")
                  .arg(analysisList.totalPapers)
                  .arg(analysisList.interpretedCount)
                  .arg(analysisList.pendingCount)
                  .arg(analysisList.failedCount)
                  .arg(analysisList.toReadCount)
                  .arg(analysisList.excludedCount)
        }

        // ── filters ─────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: qsTr("Show"); color: Theme.dimText }
            AppComboBox {
                id: stateFilter
                Layout.preferredWidth: 150
                model: [qsTr("everything"), qsTr("not read"), qsTr("interpreted"),
                        qsTr("failed"), qsTr("marked to read"), qsTr("set aside")]
                property var codes: ["", "none", "done", "failed", "toRead", "excluded"]
                onActivated: function(i) {
                    analysisList.filterState = codes[i]
                    // "Set aside" is the one view where hiding them makes no
                    // sense.
                    analysisList.hideExcluded = codes[i] !== "excluded"
                }
            }
            Label { text: qsTr("relevance"); color: Theme.dimText }
            AppComboBox {
                id: relFilter
                Layout.preferredWidth: 120
                model: [qsTr("any"), qsTr("high"), qsTr("medium"), qsTr("low"),
                        qsTr("unclear")]
                property var codes: ["", "high", "medium", "low", "unclear"]
                onActivated: function(i) { analysisList.filterRelevance = codes[i] }
            }
            Label { text: qsTr("advice"); color: Theme.dimText }
            AppComboBox {
                id: adviceFilter
                Layout.preferredWidth: 180
                model: [qsTr("any"), qsTr("read in full"),
                        qsTr("method + experiments"), qsTr("background"),
                        qsTr("low relevance"), qsTr("needs a human")]
                property var codes: ["", "read_full", "read_method_experiments",
                                     "background", "low_relevance", "insufficient"]
                onActivated: function(i) { analysisList.filterAdvice = codes[i] }
            }
            Item { Layout.fillWidth: true }
        }

        // ── progress ────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            visible: batchAnalysis.busy || batchAnalysis.status.length > 0
            spacing: 8
            ProgressBar {
                Layout.preferredWidth: 180
                visible: batchAnalysis.busy
                from: 0
                to: Math.max(1, batchAnalysis.total)
                value: batchAnalysis.done + batchAnalysis.failed
                       + batchAnalysis.skipped
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.dimText
                text: batchAnalysis.busy
                      ? qsTr("%1 of %2 · %3 running · %4 failed — %5")
                        .arg(batchAnalysis.done + batchAnalysis.failed
                             + batchAnalysis.skipped)
                        .arg(batchAnalysis.total)
                        .arg(batchAnalysis.running)
                        .arg(batchAnalysis.failed)
                        .arg(batchAnalysis.status)
                      : batchAnalysis.status
            }
            AppButton {
                text: qsTr("Stop")
                visible: batchAnalysis.busy
                onClicked: batchAnalysis.cancel()
            }
        }

        // ── the papers ──────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.fieldBg
            border.color: Theme.border
            radius: 4
            clip: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 1
                model: analysisList
                clip: true
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    required property string itemId
                    required property string title
                    required property string analysisState
                    required property string error
                    required property string oneLiner
                    required property string relevance
                    required property string advice
                    required property string authorEmail
                    required property bool mine
                    required property bool toRead
                    required property bool excluded

                    width: ListView.view.width
                    height: rowCol.implicitHeight + 12
                    color: excluded ? Theme.paneBg : "transparent"

                    ColumnLayout {
                        id: rowCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: excluded ? Theme.dimText : Theme.text
                                font.strikeout: excluded
                                text: title
                            }
                            Label {
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
                            ToolButton {
                                text: toRead ? "★" : "☆"
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: qsTr("Mark for a close read")
                                onClicked: analysisList.setToRead(itemId, !toRead)
                            }
                            ToolButton {
                                text: excluded ? "↩" : "✕"
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: excluded ? qsTr("Bring back")
                                                       : qsTr("Set aside")
                                onClicked: analysisList.setExcluded(itemId, !excluded)
                            }
                            ToolButton {
                                text: "▶"
                                enabled: batchAnalysis.canRun
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: analysisState === "done"
                                              ? qsTr("Interpret again")
                                              : qsTr("Interpret this one")
                                onClicked: batchAnalysis.startItems([itemId], true)
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: oneLiner.length > 0
                            wrapMode: Text.Wrap
                            color: Theme.bodyText
                            font.pixelSize: 12
                            text: oneLiner
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !mine && authorEmail.length > 0
                            color: Theme.dimText
                            font.pixelSize: 11
                            text: qsTr("interpreted by %1").arg(authorEmail)
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: analysisState === "failed" && error.length > 0
                            wrapMode: Text.Wrap
                            color: Theme.danger
                            font.pixelSize: 11
                            text: error
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.dimText
                text: analysisList.totalPapers === 0
                      ? qsTr("This project has no papers yet. Add PDFs to the "
                             + "library first.")
                      : qsTr("Nothing matches these filters.")
            }
        }

        // ── actions ─────────────────────────────────────────────────
        Flow {
            Layout.fillWidth: true
            spacing: 6
            AppButton {
                primary: true
                text: qsTr("Interpret the %1 unread").arg(analysisList.pendingCount)
                enabled: batchAnalysis.canRun && analysisList.pendingCount > 0
                         && !batchAnalysis.busy
                onClicked: batchAnalysis.startPending()
            }
            AppButton {
                text: qsTr("Interpret what's shown")
                enabled: batchAnalysis.canRun && analysisList.count > 0
                         && !batchAnalysis.busy
                onClicked: batchAnalysis.startItems(analysisList.visibleItemIds(), false)
            }
            AppButton {
                text: qsTr("Retry the %1 that failed").arg(batchAnalysis.failed)
                visible: batchAnalysis.failed > 0
                enabled: !batchAnalysis.busy
                onClicked: batchAnalysis.retryFailed()
            }
            AppButton {
                text: qsTr("Mark all shown to read closely")
                enabled: analysisList.count > 0
                onClicked: analysisList.applyToRead(analysisList.visibleItemIds(), true)
            }
            AppButton {
                text: qsTr("Set all shown aside")
                enabled: analysisList.count > 0
                onClicked: analysisList.applyExcluded(analysisList.visibleItemIds(), true)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.dimText
                font.pixelSize: 11
                text: batchAnalysis.busy
                      ? qsTr("Closing this window does not stop the run — the "
                             + "toolbar keeps the count, and Stop is the only "
                             + "thing that ends it.")
                      : qsTr("Papers already interpreted — by you or by a "
                             + "collaborator — are skipped, so a project pays "
                             + "for each paper once.")
            }
            AppButton {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }
}
