import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The interpretation of the open paper: the quick read (§2 + §4), the close
// reading in nine parts (§3, in DeepReadView), and the reader's own notes.
//
// Everything here is rendered from a structured result, never from model
// prose: each statement shows where it came from (the authors, an
// experiment, or the model's own reading), and the little page chips next
// to it are citations that have been checked against the paragraph they
// name. Clicking one jumps the PDF and the paragraph list to it. A
// citation that did not check out is shown greyed with a warning rather
// than quietly dropped -- the reader should see that the model reached for
// something that is not there.
Rectangle {
    id: root
    color: Theme.paneBg

    // page is 1-based (as shown to the reader); blockId identifies the
    // paragraph, which is what actually gets scrolled to.
    signal evidenceRequested(int page, int blockId)
    signal askAiRequested(string text)

    // 0 = the quick read, 1 = the close reading, 2 = the reader's own notes.
    property int mode: 0

    // True while a splitter handle is being dragged. Re-wrapping a whole
    // interpretation on every mouse move is expensive, so the content holds
    // the width it had and catches up on a timer (the same trick the
    // paragraph and summary panes use).
    property bool resizing: false
    property real layoutWidth: width
    onResizingChanged: if (!resizing) {
        reflow.stop()
        root.layoutWidth = root.width
    }
    onWidthChanged: {
        if (!root.resizing) { root.layoutWidth = root.width; return }
        if (!reflow.running) reflow.start()
    }
    Timer {
        id: reflow
        interval: 32
        onTriggered: root.layoutWidth = root.width
    }

    // Home / End / PageUp / PageDown scroll whichever tab is showing, once
    // the pane has been clicked into.
    function currentFlickable() {
        const view = root.mode === 0 ? quickScroll
                   : root.mode === 1 ? deepScroll : notesScroll
        return view ? view.contentItem : null
    }
    Keys.onPressed: (event) => ScrollKeys.handle(event, root.currentFlickable())
    TapHandler { onTapped: root.forceActiveFocus() }

    readonly property var d: analysis.quick
    readonly property int fs: settings.summaryFontSize
    readonly property var meta: d && d.meta ? d.meta : null

    function adviceLabel(code) {
        switch (code) {
        case "read_full":               return qsTr("Read the whole paper")
        case "read_method_experiments": return qsTr("Read method + experiments")
        case "background":              return qsTr("Background / related work")
        case "low_relevance":           return qsTr("Low relevance")
        default:                        return qsTr("Not enough information")
        }
    }
    function relevanceLabel(code) {
        switch (code) {
        case "high":   return qsTr("highly relevant")
        case "medium": return qsTr("somewhat relevant")
        case "low":    return qsTr("barely relevant")
        default:       return qsTr("relevance unclear")
        }
    }

    // The digest as an ordered list of sections, so one Repeater renders
    // them all and adding a section later is a one-line change.
    readonly property var sectionList: {
        const x = root.d
        if (!x || !x.problem)
            return []
        return [
            {"title": qsTr("The problem"),   "claims": [x.problem]},
            {"title": qsTr("Why it matters"), "claims": [x.importance]},
            {"title": qsTr("What they did"),  "claims": [x.method]},
            {"title": qsTr("Main results"),   "claims": x.results || []},
            {"title": qsTr("Contributions"),  "claims": x.contributions || []},
            {"title": qsTr("Limitations"),    "claims": x.limitations || []}
        ]
    }

    // ── small shared pieces ─────────────────────────────────────────
    component Pill: Rectangle {
        id: pillRoot
        property string label: ""
        property color tint: Theme.dimText
        property string hint: ""
        implicitWidth: pillText.implicitWidth + 12
        implicitHeight: root.fs + 6
        radius: height / 2
        color: Qt.alpha(pillRoot.tint, Theme.dark ? 0.22 : 0.13)
        border.color: Qt.alpha(pillRoot.tint, 0.5)
        Label {
            id: pillText
            anchors.centerIn: parent
            text: pillRoot.label
            color: pillRoot.tint
            font.pixelSize: root.fs - 2
        }
        ToolTip.visible: pillHover.hovered && pillRoot.hint.length > 0
        ToolTip.text: pillRoot.hint
        ToolTip.delay: 400
        HoverHandler { id: pillHover }
    }

    // Its own save dialog: ids are file-scoped, so a pane cannot reach one
    // declared in the window.
    FileDialog {
        id: exportDialog
        title: qsTr("Export interpretation")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTr("Markdown files (*.md)"), qsTr("All files (*)")]
        onAccepted: exporter.save(exporter.paperMarkdown(analysis.paperId),
                                  selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── header ──────────────────────────────────────────────────
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
                    text: qsTr("Interpretation")
                    font.bold: true
                    color: Theme.text
                    elide: Text.ElideRight
                }
                BusyIndicator {
                    running: analysis.status === AnalysisService.Running
                    visible: running
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                ToolButton {
                    text: analysis.status === AnalysisService.Running
                          ? qsTr("Cancel")
                          : (analysis.hasQuick ? qsTr("Regenerate")
                                               : qsTr("Interpret"))
                    enabled: analysis.status === AnalysisService.Running
                             || analysis.canRun
                    onClicked: {
                        if (analysis.status === AnalysisService.Running)
                            analysis.cancel()
                        else
                            analysis.generateQuick(true)
                    }
                }
                ToolButton {
                    text: "⋯"
                    onClicked: paneMenu.popup()
                    Menu {
                        id: paneMenu
                        MenuItem {
                            text: qsTr("Discard this interpretation")
                            enabled: analysis.hasQuick && analysis.quickIsMine
                            onTriggered: analysis.discardQuick()
                        }
                        MenuItem {
                            // §16: a regenerate is recoverable. The menu
                            // re-reads the history each time it opens, so it
                            // does not need a change signal of its own.
                            text: qsTr("Restore the previous version")
                            enabled: root.mode === 1
                                     ? analysis.deepHistory().length > 0
                                     : analysis.quickHistory().length > 0
                            onTriggered: {
                                if (root.mode === 1)
                                    analysis.restoreDeep(0)
                                else
                                    analysis.restoreQuick(0)
                            }
                        }
                        MenuItem {
                            text: qsTr("Export this paper as Markdown…")
                            enabled: analysis.hasQuick || analysis.hasDeep
                            onTriggered: exportDialog.open()
                        }
                    }
                }
            }
        }

        // ── notices ─────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: noticeCol.implicitHeight + 10
            // Spelled out rather than derived from the children: `children`
            // is not a JS array, so there is nothing to fold over.
            visible: !!(analysis.status === AnalysisService.Failed
                        || analysis.quickStale
                        || (analysis.hasQuick && !analysis.quickIsMine)
                        || (analysis.hasQuick && !analysis.quickSaved)
                        || (root.d && root.d.insufficient === true)
                        || (root.meta && root.meta.truncated === true))
            color: Theme.cardBg
            ColumnLayout {
                id: noticeCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    visible: analysis.status === AnalysisService.Failed
                    wrapMode: Text.Wrap
                    color: Theme.danger
                    font.pixelSize: root.fs - 1
                    text: analysis.lastError
                }
                Label {
                    Layout.fillWidth: true
                    visible: analysis.quickStale
                    wrapMode: Text.Wrap
                    color: Theme.danger
                    font.pixelSize: root.fs - 1
                    text: qsTr("The paper, the research profile or the model has "
                               + "changed since this was written — it may be out of date.")
                }
                Label {
                    Layout.fillWidth: true
                    visible: analysis.hasQuick && !analysis.quickIsMine
                    wrapMode: Text.Wrap
                    color: Theme.dimText
                    font.pixelSize: root.fs - 1
                    text: qsTr("Written by %1. Regenerate to make your own.")
                          .arg(analysis.quickAuthorEmail)
                }
                Label {
                    Layout.fillWidth: true
                    visible: analysis.hasQuick && !analysis.quickSaved
                    wrapMode: Text.Wrap
                    color: Theme.dimText
                    font.pixelSize: root.fs - 1
                    text: qsTr("Not saved: sign in and pick a project you can "
                               + "write to, and interpretations are kept and shared.")
                }
                Label {
                    Layout.fillWidth: true
                    visible: !!(root.d && root.d.insufficient === true)
                    wrapMode: Text.Wrap
                    color: Theme.danger
                    font.pixelSize: root.fs - 1
                    text: qsTr("Not enough usable text: %1")
                          .arg(root.d && root.d.insufficientReason
                               ? root.d.insufficientReason : "")
                }
                Label {
                    Layout.fillWidth: true
                    visible: !!(root.meta && root.meta.truncated === true)
                    wrapMode: Text.Wrap
                    color: Theme.dimText
                    font.pixelSize: root.fs - 1
                    text: qsTr("The paper was too long to send in full — only "
                               + "the first %1 of %2 paragraphs were read.")
                          .arg(root.meta ? root.meta.blocksIncluded : 0)
                          .arg(root.meta ? root.meta.blocksTotal : 0)
                }
            }
        }

        // ── what to look at ─────────────────────────────────────────
        AppTabBar {
            id: modeBar
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 8
            currentIndex: root.mode
            onCurrentIndexChanged: root.mode = currentIndex
            AppTabButton {
                text: qsTr("Quick")
            }
            AppTabButton {
                text: analysis.deepDone > 0
                      ? qsTr("Close read (%1/%2)").arg(analysis.deepDone)
                                                  .arg(analysis.deepTotal)
                      : qsTr("Close read")
            }
            AppTabButton {
                text: analysis.notes.length > 0
                      ? qsTr("Notes (%1)").arg(analysis.notes.length)
                      : qsTr("Notes")
            }
        }

        // ── empty state ─────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            visible: root.mode === 0 && !analysis.hasQuick
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.dimText
                font.pixelSize: root.fs
                text: qsTr("A quick interpretation says what the paper is about, "
                           + "how relevant it is to this project, what to read "
                           + "first, and where each statement comes from — with "
                           + "every citation checked against the paper itself.")
            }
            Label {
                Layout.fillWidth: true
                visible: !profile.hasProfile
                wrapMode: Text.Wrap
                color: Theme.dimText
                font.pixelSize: root.fs - 1
                text: qsTr("Tip: fill in the project's research profile first "
                           + "(toolbar → Profile). Relevance and reading advice "
                           + "are judged against it.")
            }
            AppButton {
                text: qsTr("Interpret this paper")
                primary: true
                enabled: analysis.canRun
                onClicked: analysis.generateQuick(true)
            }
            Item { Layout.fillHeight: true }
        }

        // ── the interpretation ──────────────────────────────────────
        ScrollView {
            id: quickScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.mode === 0 && analysis.hasQuick
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: root.layoutWidth
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    wrapMode: Text.Wrap
                    font.pixelSize: root.fs + 2
                    font.bold: true
                    color: Theme.text
                    text: root.d && root.d.oneLiner ? root.d.oneLiner : ""
                }

                // advice + relevance
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 6
                    Pill {
                        label: root.adviceLabel(root.d && root.d.advice
                                                ? root.d.advice.code : "")
                        tint: Theme.accent
                        hint: root.d && root.d.advice ? root.d.advice.reason : ""
                    }
                    Pill {
                        label: root.relevanceLabel(root.d && root.d.relevance
                                                   ? root.d.relevance.level : "")
                        tint: {
                            const lv = root.d && root.d.relevance
                                     ? root.d.relevance.level : ""
                            return lv === "high" ? Theme.success
                                 : lv === "low" ? Theme.dimText : Theme.accent
                        }
                        hint: root.d && root.d.relevance ? root.d.relevance.reason : ""
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    wrapMode: Text.Wrap
                    color: Theme.bodyText
                    font.pixelSize: root.fs
                    visible: text.length > 0
                    text: root.d && root.d.relevance ? (root.d.relevance.reason || "") : ""
                }

                // where to start reading
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 2
                    visible: !!(root.d && root.d.priority && root.d.priority.length > 0)
                    Label {
                        text: qsTr("Read first")
                        color: Theme.heading
                        font.bold: true
                        font.pixelSize: root.fs
                    }
                    Repeater {
                        model: root.d && root.d.priority ? root.d.priority : []
                        delegate: ItemDelegate {
                            required property var modelData
                            Layout.fillWidth: true
                            padding: 4
                            contentItem: ColumnLayout {
                                spacing: 0
                                Label {
                                    Layout.fillWidth: true
                                    text: "→ " + (modelData.what || "")
                                    color: Theme.accent
                                    font.pixelSize: root.fs
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.why || ""
                                    color: Theme.dimText
                                    font.pixelSize: root.fs - 1
                                    wrapMode: Text.Wrap
                                }
                            }
                            onClicked: root.evidenceRequested(0, modelData.blockId || -1)
                        }
                    }
                }

                // the sections
                Repeater {
                    model: root.sectionList
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 4
                        visible: !!(modelData.claims && modelData.claims.length > 0)
                        Label {
                            text: modelData.title
                            color: Theme.heading
                            font.bold: true
                            font.pixelSize: root.fs
                        }
                        Repeater {
                            model: modelData.claims
                            delegate: ClaimBlock {
                                required property var modelData
                                Layout.fillWidth: true
                                claim: modelData
                                fs: root.fs
                                onEvidenceRequested: function(page, blockId) {
                                    root.evidenceRequested(page, blockId)
                                }
                                onAskAiRequested: function(text) {
                                    root.askAiRequested(text)
                                }
                                onNoteRequested: function(text) {
                                    analysis.saveNote(text, "quick")
                                }
                            }
                        }
                    }
                }

                // facets — the labels the project-wide analyses group on
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 4
                    visible: !!(root.d && root.d.facets)
                    Label {
                        text: qsTr("At a glance")
                        color: Theme.heading
                        font.bold: true
                        font.pixelSize: root.fs
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: {
                                const f = root.d ? root.d.facets : null
                                if (!f) return []
                                let out = []
                                if (f.methodRoute) out.push(f.methodRoute)
                                if (f.paperType) out.push(f.paperType)
                                if (f.scenario) out.push(f.scenario)
                                const lists = [f.datasets, f.metrics, f.baselines]
                                for (const l of lists)
                                    if (l) for (const v of l) if (v) out.push(v)
                                return out
                            }
                            delegate: Pill {
                                required property var modelData
                                label: modelData
                                tint: Theme.dimText
                            }
                        }
                    }
                }

                // provenance footer
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.bottomMargin: 14
                    wrapMode: Text.Wrap
                    color: Theme.dimText
                    font.pixelSize: root.fs - 2
                    visible: root.meta !== null
                    text: {
                        if (!root.meta) return ""
                        let parts = [qsTr("%1 of %2 citations check out")
                                     .arg(root.meta.evidenceVerified)
                                     .arg(root.meta.evidenceTotal)]
                        if (root.meta.claimsDemoted > 0)
                            parts.push(qsTr("%1 statement(s) demoted to AI reading")
                                       .arg(root.meta.claimsDemoted))
                        if (analysis.quickModel)
                            parts.push(analysis.quickModel)
                        return parts.join(" · ")
                    }
                }
            }
        }

        // ── the close reading ───────────────────────────────────────
        ScrollView {
            id: deepScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.mode === 1
            clip: true
            contentWidth: availableWidth

            DeepReadView {
                width: root.layoutWidth
                fs: root.fs
                onEvidenceRequested: function(page, blockId) {
                    root.evidenceRequested(page, blockId)
                }
                onAskAiRequested: function(text) { root.askAiRequested(text) }
            }
        }

        // ── the reader's own notes ──────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            visible: root.mode === 2
            spacing: 8

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.dimText
                font.pixelSize: root.fs - 1
                text: qsTr("Your own notes on this paper. They are kept in their "
                           + "own place, so regenerating an interpretation never "
                           + "touches them.")
            }

            ScrollView {
                id: notesScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: root.layoutWidth
                    spacing: 6
                    Repeater {
                        model: analysis.notes
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            implicitHeight: noteRow.implicitHeight + 12
                            color: Theme.cardBg
                            radius: 4
                            RowLayout {
                                id: noteRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 8
                                anchors.rightMargin: 4
                                spacing: 6
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    color: Theme.text
                                    font.pixelSize: root.fs
                                    text: modelData.text || ""
                                }
                                ToolButton {
                                    text: "✕"
                                    font.pixelSize: root.fs - 2
                                    onClicked: analysis.removeNote(index)
                                }
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: analysis.notes.length === 0
                        wrapMode: Text.Wrap
                        color: Theme.dimText
                        font.pixelSize: root.fs
                        text: qsTr("Nothing yet. Any statement in an "
                                   + "interpretation can be saved here from its "
                                   + "⋯ menu.")
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                AppTextField {
                    id: noteInput
                    Layout.fillWidth: true
                    placeholderText: qsTr("Write a note…")
                    onAccepted: {
                        analysis.saveNote(text, "")
                        text = ""
                    }
                }
                AppButton {
                    text: qsTr("Add")
                    primary: true
                    enabled: noteInput.text.trim().length > 0
                    onClicked: {
                        analysis.saveNote(noteInput.text, "")
                        noteInput.text = ""
                    }
                }
            }
        }
    }
}
