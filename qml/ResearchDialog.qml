import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Everything that looks at the whole project at once (§8–§15).
//
// Seven questions, one tab each: how the papers group, what the field's map
// looks like, where the papers agree and where they fight, how the work moved
// over time, what this collection does not cover, what openings are left, and
// what to do on Monday morning. Each one is generated on its own and stored on
// its own, so a thin answer can be rewritten without paying for the other six.
//
// None of this reads a PDF. Every tab is built out of the per-paper
// interpretations, which is why the empty states say so and why the stale
// notice matters: interpret another paper and everything derived from the set
// is one paper out of date.
//
// The category tab is the one the reader can edit. §8.3 is explicit that the
// classification belongs to them, not to the model — renaming, merging,
// locking and confirming are all here, and a locked category survives the next
// regeneration untouched.
AppDialog {
    id: root
    title: qsTr("Research this library")
    width: Math.min(880, Overlay.overlay ? Overlay.overlay.width - 60 : 880)
    height: Math.min(660, Overlay.overlay ? Overlay.overlay.height - 60 : 660)
    standardButtons: Dialog.NoButton

    // Main.qml opens the paper.
    FileDialog {
        id: exportReportDialog
        title: qsTr("Export research report")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTr("Markdown files (*.md)"), qsTr("All files (*)")]
        onAccepted: exporter.save(exporter.projectMarkdown(), selectedFile)
    }

    // §8.3's other half: a category that turned out to be two categories.
    function beginSplit(categoryId, categoryName) {
        splitDialog.catId = categoryId
        splitDialog.papers = research.categoryPapers(categoryId)
        splitDialog.picked = []
        splitDialog.newName = qsTr("%1 (part)").arg(categoryName)
        splitDialog.open()
    }

    AppDialog {
        id: splitDialog
        title: qsTr("Split a category")
        width: 460
        standardButtons: Dialog.NoButton

        property string catId: ""
        property var papers: []
        property var picked: []
        property string newName: ""

        function toggle(paperId, on) {
            let next = splitDialog.picked.filter(function(p) { return p !== paperId })
            if (on)
                next.push(paperId)
            splitDialog.picked = next
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            // Page/Home/End walk the paper list below.
            focus: true
            Keys.onPressed: (event) => ScrollKeys.handle(event, splitScroll.contentItem)

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.dimText
                text: qsTr("The papers you tick move into a new category "
                           + "beside this one. It becomes yours, so "
                           + "regenerating the category system leaves it "
                           + "alone.")
            }
            AppTextField {
                id: splitName
                Layout.fillWidth: true
                text: splitDialog.newName
                placeholderText: qsTr("Name for the new category")
                onTextChanged: splitDialog.newName = text
            }
            ScrollView {
                id: splitScroll
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 2
                    Repeater {
                        model: splitDialog.papers
                        delegate: CheckBox {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.title
                            onToggled: splitDialog.toggle(modelData.paperId, checked)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Cancel")
                    onClicked: splitDialog.close()
                }
                AppButton {
                    primary: true
                    text: qsTr("Split")
                    enabled: splitDialog.picked.length > 0
                             && splitDialog.newName.trim().length > 0
                             && splitDialog.picked.length < splitDialog.papers.length
                    onClicked: {
                        research.splitCategory(splitDialog.catId,
                                               splitDialog.newName.trim(),
                                               splitDialog.picked)
                        splitDialog.close()
                    }
                }
            }
        }
    }

    signal paperActivated(string paperId)
    // A follow-up question about a project-wide statement still goes to the
    // chat, against whatever paper is open.
    signal askAiRequested(string text)

    readonly property int fs: 13

    // result(), has(), history() and friends are plain calls with no change
    // notification of their own, so nothing that reads one would ever
    // re-evaluate. Every such binding goes through a helper below and passes
    // `rev`, which the Connections block bumps — that read is what ties the
    // binding to the service's signals.
    property int rev: 0

    onOpened: root.rev = root.rev + 1

    // ── reading the service ─────────────────────────────────────────
    // `rev` is unused in the bodies on purpose; see the note above.
    function resultOf(kind, rev) {
        const r = research.result(kind)
        return r ? r : ({})
    }
    function hasResult(kind, rev) {
        return research.has(kind) === true
    }
    function isStale(kind, rev) {
        return research.isStale(kind) === true
    }
    function historyOf(kind, rev) {
        const h = research.history(kind)
        return h ? h : []
    }
    function unclassified(rev) {
        const u = research.unclassifiedPapers()
        return u ? u : []
    }
    function paperName(paperId, rev) {
        return research.paperTitle(paperId)
    }
    function stampLine(kind, rev) {
        const who = research.authorOf(kind)
        const when = root.stamp(research.updatedAtOf(kind))
        const n = research.paperCountOf(kind)
        if (who.length === 0)
            return qsTr("%1 papers · %2").arg(n).arg(when)
        return qsTr("generated by %1 · %2 papers · %3").arg(who).arg(n).arg(when)
    }

    // ── reading a result safely ─────────────────────────────────────
    // Every field of every answer is optional: the model can leave a list out,
    // and a restored old version can predate a field entirely.
    function listAt(obj, key) {
        const v = obj ? obj[key] : null
        return v && v.length !== undefined ? v : []
    }
    function textAt(obj, key) {
        const v = obj ? obj[key] : null
        return (v === undefined || v === null) ? "" : String(v)
    }
    function joinList(items) {
        const src = items ? items : []
        const out = []
        for (let i = 0; i < src.length; ++i) {
            if (src[i] === undefined || src[i] === null)
                continue
            const s = String(src[i])
            if (s.length > 0)
                out.push(s)
        }
        return out.join(" · ")
    }
    function stamp(iso) {
        if (!iso || iso.length === 0)
            return ""
        const d = new Date(iso)
        return isNaN(d.getTime()) ? iso
                                  : d.toLocaleString(Qt.locale(), Locale.ShortFormat)
    }
    // A library-level statement wearing the shape ClaimBlock expects. There is
    // no per-passage evidence at this scale — the provenance is the list of
    // papers shown beside it.
    function asClaim(g) {
        return { text: g && g.claim ? String(g.claim) : "" }
    }
    // Everything in the same dimension except the one the reader is holding.
    function otherCategories(list, exceptId) {
        const src = list ? list : []
        const out = []
        for (let i = 0; i < src.length; ++i) {
            const c = src[i]
            if (c && String(c.id) !== exceptId)
                out.push(c)
        }
        return out
    }

    // ── labels ──────────────────────────────────────────────────────
    function dimensionLabel(code) {
        switch (code) {
        case "research_problem":  return qsTr("Research problem")
        case "scenario":          return qsTr("Application scenario")
        case "paper_type":        return qsTr("Kind of paper")
        case "method_route":      return qsTr("Method route")
        case "input_type":        return qsTr("Kind of input")
        case "task_type":         return qsTr("Task type")
        case "dataset":           return qsTr("Dataset / environment")
        case "metric":            return qsTr("Evaluation metric")
        case "contribution_type": return qsTr("Kind of contribution")
        case "main_limitation":   return qsTr("Main limitation")
        case "relevance":         return qsTr("Relevance to this project")
        default:                  return code || ""
        }
    }
    function conflictLabel(code) {
        switch (code) {
        case "real":        return qsTr("a real disagreement")
        case "apparent":    return qsTr("only looks like one")
        case "undecidable": return qsTr("cannot be settled here")
        default:            return qsTr("unclassified")
        }
    }
    function conflictColor(code) {
        // No "warning" role exists; anything short of a real disagreement is
        // said quietly.
        return code === "real" ? Theme.danger : Theme.dimText
    }
    function conflictHint(code) {
        switch (code) {
        case "real":
            return qsTr("The papers were doing comparable things and still "
                        + "disagree — one of them is wrong, or the effect does "
                        + "not hold as widely as claimed.")
        case "apparent":
            return qsTr("Different task, data, or setup. The numbers look "
                        + "opposed but the papers are not actually answering "
                        + "the same question.")
        case "undecidable":
            return qsTr("Nothing in these interpretations settles it. It needs "
                        + "the papers themselves, or a new experiment.")
        default:
            return qsTr("The model did not say what kind of conflict this is.")
        }
    }
    function levelLabel(code) {
        switch (code) {
        case "low":    return qsTr("low")
        case "medium": return qsTr("medium")
        case "high":   return qsTr("high")
        default:       return code || ""
        }
    }
    function difficultyColor(code) {
        switch (code) {
        case "low":    return Theme.success
        case "medium": return Theme.accent
        case "high":   return Theme.danger
        default:       return Theme.dimText
        }
    }
    function confidenceColor(code) {
        switch (code) {
        case "high":   return Theme.success
        case "medium": return Theme.accent
        default:       return Theme.dimText
        }
    }
    function gapTypeLabel(code) {
        switch (code) {
        case "paper_left":       return qsTr("a question this paper left open")
        case "library_gap":      return qsTr("nothing in this library covers it")
        case "unverified_field": return qsTr("unverified — needs a literature search")
        default:                 return code || ""
        }
    }
    function gapTypeColor(code) {
        switch (code) {
        case "paper_left":       return Theme.accent
        case "library_gap":      return Theme.heading
        case "unverified_field": return Theme.danger
        default:                 return Theme.dimText
        }
    }
    function gapTypeHint(code) {
        switch (code) {
        case "paper_left":
            return qsTr("The paper itself says this is unresolved.")
        case "library_gap":
            return qsTr("No paper in this project covers it — which is not the "
                        + "same as no paper existing.")
        case "unverified_field":
            return qsTr("Nobody has checked the literature for this yet. Search "
                        + "before believing it is open.")
        default:
            return ""
        }
    }

    // Seven tabs, seven scrolling pages: the scroll keys belong to the one
    // on screen, so they page that instead of a fixed tab.
    function currentFlickable() {
        const views = [taxScroll, mapScroll, conScroll, evoScroll,
                       covScroll, oppScroll, actScroll]
        const view = views[tabBar.currentIndex]
        return view ? view.contentItem : null
    }

    // ── the seven results ───────────────────────────────────────────
    readonly property var taxRes:  root.resultOf("taxonomy", root.rev)
    readonly property var mapRes:  root.resultOf("map", root.rev)
    readonly property var conRes:  root.resultOf("consensus", root.rev)
    readonly property var evoRes:  root.resultOf("evolution", root.rev)
    readonly property var covRes:  root.resultOf("coverage", root.rev)
    readonly property var oppRes:  root.resultOf("opportunities", root.rev)
    readonly property var actRes:  root.resultOf("actions", root.rev)

    // ── small shared pieces ─────────────────────────────────────────
    component Chip: Rectangle {
        id: chip
        property string label: ""
        property color tint: Theme.dimText
        property string hint: ""
        implicitWidth: chipText.implicitWidth + 12
        implicitHeight: root.fs + 6
        radius: height / 2
        color: Qt.alpha(chip.tint, Theme.dark ? 0.22 : 0.13)
        border.color: Qt.alpha(chip.tint, 0.5)
        Label {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.tint
            font.pixelSize: root.fs - 2
        }
        ToolTip.visible: chipHover.hovered && chip.hint.length > 0
        ToolTip.text: chip.hint
        ToolTip.delay: 400
        HoverHandler { id: chipHover }
    }

    component GroupTitle: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.heading
        font.bold: true
        font.pixelSize: root.fs
    }

    // A paper named by a library analysis. Ids never reach the reader — the
    // title does, and clicking it opens the paper.
    component PaperChip: Rectangle {
        id: pc
        property string paperId: ""
        property int maxWidth: 260
        readonly property string paperName: root.paperName(pc.paperId, root.rev)
        implicitHeight: root.fs + 9
        implicitWidth: Math.min(pc.maxWidth, pcText.implicitWidth + 16)
        radius: height / 2
        color: pcHover.hovered ? Theme.hover : Theme.cardBg
        border.color: pcHover.hovered ? Theme.accent : Theme.border
        Label {
            id: pcText
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: Theme.accent
            font.pixelSize: root.fs - 2
            text: pc.paperName
        }
        HoverHandler {
            id: pcHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler { onTapped: root.paperActivated(pc.paperId) }
        ToolTip.visible: pcHover.hovered
        ToolTip.delay: 500
        ToolTip.text: qsTr("Open “%1”").arg(pc.paperName)
    }

    component PaperChips: Flow {
        id: pcs
        property var ids: []
        Layout.fillWidth: true
        spacing: 4
        visible: !!(pcs.ids && pcs.ids.length > 0)
        Repeater {
            model: pcs.ids ? pcs.ids : []
            delegate: PaperChip {
                required property var modelData
                paperId: modelData ? String(modelData) : ""
            }
        }
    }

    component FieldLine: RowLayout {
        id: fl
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: 6
        visible: !!(fl.value && fl.value.length > 0)
        Label {
            Layout.preferredWidth: 120
            Layout.alignment: Qt.AlignTop
            wrapMode: Text.Wrap
            text: fl.label
            color: Theme.dimText
            font.pixelSize: root.fs - 1
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: fl.value
            color: Theme.text
            font.pixelSize: root.fs - 1
        }
    }

    // A short list said on one line — datasets, metrics, baselines. Long
    // enough to matter, short enough that bullets would be noise.
    component MiniList: RowLayout {
        id: ml
        property string label: ""
        property var items: []
        Layout.fillWidth: true
        spacing: 6
        visible: !!(ml.items && ml.items.length > 0)
        Label {
            Layout.preferredWidth: 120
            Layout.alignment: Qt.AlignTop
            wrapMode: Text.Wrap
            text: ml.label
            color: Theme.dimText
            font.pixelSize: root.fs - 1
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: root.joinList(ml.items)
            color: Theme.bodyText
            font.pixelSize: root.fs - 1
        }
    }

    component BulletList: ColumnLayout {
        id: bl
        property string heading: ""
        property var items: []
        Layout.fillWidth: true
        spacing: 1
        visible: !!(bl.items && bl.items.length > 0)
        Label {
            Layout.fillWidth: true
            visible: !!(bl.heading.length > 0)
            wrapMode: Text.Wrap
            text: bl.heading
            color: Theme.dimText
            font.pixelSize: root.fs - 1
        }
        Repeater {
            model: bl.items ? bl.items : []
            delegate: Label {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 8
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: root.fs - 1
                text: "• " + (modelData ? String(modelData) : "")
            }
        }
    }

    // A labelled run of library-level statements. Each one goes through
    // ClaimBlock, so a claim looks the same here as it does in a single
    // paper's interpretation.
    component ClaimGroup: ColumnLayout {
        id: cg
        property string heading: ""
        property string hint: ""
        property var items: []
        Layout.fillWidth: true
        spacing: 4
        visible: !!(cg.items && cg.items.length > 0)
        GroupTitle { text: cg.heading }
        Label {
            Layout.fillWidth: true
            visible: !!(cg.hint.length > 0)
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: root.fs - 2
            text: cg.hint
        }
        Repeater {
            model: cg.items ? cg.items : []
            delegate: ColumnLayout {
                id: gi
                required property var modelData
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                spacing: 2
                ClaimBlock {
                    Layout.fillWidth: true
                    claim: root.asClaim(gi.modelData)
                    fs: root.fs
                    // Nothing here is about one paper, so the two actions
                    // that need one are hidden rather than left inert.
                    allowNotes: false
                    allowCompare: false
                    onAskAiRequested: (text) => root.askAiRequested(text)
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    visible: !!(root.textAt(gi.modelData, "note").length > 0)
                    wrapMode: Text.Wrap
                    color: Theme.bodyText
                    font.pixelSize: root.fs - 2
                    text: root.textAt(gi.modelData, "note")
                }
                PaperChips {
                    Layout.leftMargin: 10
                    ids: root.listAt(gi.modelData, "paperIds")
                }
            }
        }
    }

    // A conflict is two lists of papers, not one, and the kind of conflict is
    // the whole point: §11 asks for the difference between papers that really
    // disagree and papers that were never comparable.
    component ConflictGroup: ColumnLayout {
        id: cfg
        property string heading: ""
        property var items: []
        Layout.fillWidth: true
        spacing: 4
        visible: !!(cfg.items && cfg.items.length > 0)
        GroupTitle { text: cfg.heading }
        Repeater {
            model: cfg.items ? cfg.items : []
            delegate: ColumnLayout {
                id: ci
                required property var modelData
                readonly property string conflictKind: root.textAt(ci.modelData, "kind")
                Layout.fillWidth: true
                Layout.bottomMargin: 6
                spacing: 2
                ClaimBlock {
                    Layout.fillWidth: true
                    claim: root.asClaim(ci.modelData)
                    fs: root.fs
                    allowNotes: false
                    allowCompare: false
                    onAskAiRequested: (text) => root.askAiRequested(text)
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    spacing: 4
                    Chip {
                        label: root.conflictLabel(ci.conflictKind)
                        tint: root.conflictColor(ci.conflictKind)
                        hint: root.conflictHint(ci.conflictKind)
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    visible: !!(root.textAt(ci.modelData, "note").length > 0)
                    wrapMode: Text.Wrap
                    color: Theme.bodyText
                    font.pixelSize: root.fs - 2
                    text: root.textAt(ci.modelData, "note")
                }
                Label {
                    Layout.leftMargin: 10
                    visible: !!(root.listAt(ci.modelData, "paperIds").length > 0)
                    color: Theme.dimText
                    font.pixelSize: root.fs - 2
                    text: qsTr("One side")
                }
                PaperChips {
                    Layout.leftMargin: 10
                    ids: root.listAt(ci.modelData, "paperIds")
                }
                Label {
                    Layout.leftMargin: 10
                    visible: !!(root.listAt(ci.modelData, "opposingPaperIds").length > 0)
                    color: Theme.dimText
                    font.pixelSize: root.fs - 2
                    text: qsTr("The other")
                }
                PaperChips {
                    Layout.leftMargin: 10
                    ids: root.listAt(ci.modelData, "opposingPaperIds")
                }
            }
        }
    }

    // A labelled run of "here is a thing, here is why, here are the papers" —
    // the shape §13 and §15 both use, under different field names.
    component NoteGroup: ColumnLayout {
        id: ng
        property string heading: ""
        property string hint: ""
        property var items: []
        property string titleKey: "topic"
        property string noteKey: "note"
        Layout.fillWidth: true
        spacing: 3
        visible: !!(ng.items && ng.items.length > 0)
        GroupTitle { text: ng.heading }
        Label {
            Layout.fillWidth: true
            visible: !!(ng.hint.length > 0)
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: root.fs - 2
            text: ng.hint
        }
        Repeater {
            model: ng.items ? ng.items : []
            delegate: ColumnLayout {
                id: ni
                required property var modelData
                Layout.fillWidth: true
                Layout.bottomMargin: 3
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.text
                    font.pixelSize: root.fs
                    text: "• " + root.textAt(ni.modelData, ng.titleKey)
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    visible: !!(root.textAt(ni.modelData, ng.noteKey).length > 0)
                    wrapMode: Text.Wrap
                    color: Theme.bodyText
                    font.pixelSize: root.fs - 1
                    text: root.textAt(ni.modelData, ng.noteKey)
                }
                PaperChips {
                    Layout.leftMargin: 12
                    ids: root.listAt(ni.modelData, "paperIds")
                }
            }
        }
    }

    // ── the strip every tab wears ───────────────────────────────────
    component TabHeader: ColumnLayout {
        id: th
        property string kind: ""
        Layout.fillWidth: true
        spacing: 3

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.heading
                font.bold: true
                font.pixelSize: root.fs + 2
                text: research.titleOf(th.kind)
            }
            BusyIndicator {
                running: research.runningKind === th.kind
                visible: running
                implicitWidth: 18
                implicitHeight: 18
            }
            AppButton {
                primary: true
                enabled: research.runningKind === th.kind || research.canRun
                text: research.runningKind === th.kind
                      ? qsTr("Cancel")
                      : (root.hasResult(th.kind, root.rev) ? qsTr("Regenerate")
                                                           : qsTr("Generate"))
                onClicked: research.runningKind === th.kind
                           ? research.cancel()
                           : research.generate(th.kind)
            }
            // §16: regenerating is never a silent loss — the versions before
            // this one are still here.
            ToolButton {
                text: "⋯"
                enabled: root.historyOf(th.kind, root.rev).length > 0
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Earlier versions")
                onClicked: histMenu.popup()
                Menu {
                    id: histMenu
                    Instantiator {
                        model: root.historyOf(th.kind, root.rev)
                        delegate: MenuItem {
                            required property var modelData
                            required property int index
                            text: qsTr("Restore the version from %1 by %2")
                                  .arg(root.stamp(modelData && modelData.generatedAt
                                                  ? modelData.generatedAt : ""))
                                  .arg(modelData && modelData.generatedByEmail
                                       ? modelData.generatedByEmail
                                       : qsTr("someone"))
                            onTriggered: research.restoreVersion(th.kind, index)
                        }
                        onObjectAdded: (i, obj) => histMenu.insertItem(i, obj)
                        onObjectRemoved: (i, obj) => histMenu.removeItem(obj)
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !!(research.lastError.length > 0
                        && research.runningKind.length === 0)
            wrapMode: Text.Wrap
            color: Theme.danger
            font.pixelSize: root.fs - 1
            text: research.lastError
        }
        Label {
            Layout.fillWidth: true
            visible: !!root.isStale(th.kind, root.rev)
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: root.fs - 1
            text: qsTr("Papers have been interpreted since this was written — "
                       + "generate it again to take them in.")
        }
        Label {
            Layout.fillWidth: true
            visible: !!root.hasResult(th.kind, root.rev)
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: root.fs - 2
            text: root.stampLine(th.kind, root.rev)
        }
    }

    // What this tab answers, said before there is anything to show.
    component EmptyState: ColumnLayout {
        id: es
        property string kind: ""
        property string what: ""
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 12
        spacing: 8
        visible: !root.hasResult(es.kind, root.rev)
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: root.fs
            text: es.what
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: root.fs - 1
            text: qsTr("It is written from the papers' interpretations, never "
                       + "from the PDFs — %1 interpreted so far.")
                  .arg(research.digestCount)
        }
        AppButton {
            enabled: research.canRun
            text: qsTr("Generate")
            onClicked: research.generate(es.kind)
        }
        Item { Layout.fillHeight: true }
    }

    // ── §8.3: one category, and everything the reader can do to it ──
    component CategoryRow: Rectangle {
        id: cr
        property var category: null
        property var siblings: []
        property bool expanded: false
        property bool renaming: false

        readonly property string catId: root.textAt(cr.category, "id")
        readonly property string catName: root.textAt(cr.category, "name")
        readonly property var catPapers: root.listAt(cr.category, "paperIds")
        readonly property bool locked: !!(cr.category && cr.category.locked === true)
        readonly property bool confirmed: !!(cr.category
                                             && cr.category.confirmed === true)
        readonly property bool mine: !!(cr.category
                                        && cr.category.source === "user")

        function beginRename() {
            renameField.text = cr.catName
            cr.renaming = true
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        function commitRename() {
            const next = renameField.text.trim()
            cr.renaming = false
            if (next.length > 0 && next !== cr.catName)
                research.renameCategory(cr.catId, next)
        }

        Layout.fillWidth: true
        implicitHeight: crCol.implicitHeight + 8
        radius: 4
        color: cr.locked ? Theme.cardBg : "transparent"
        border.color: cr.confirmed ? Qt.alpha(Theme.success, 0.5) : "transparent"

        ColumnLayout {
            id: crCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 2

            RowLayout {
                Layout.fillWidth: true
                spacing: 3

                ToolButton {
                    implicitWidth: 20
                    implicitHeight: 20
                    font.pixelSize: 10
                    text: cr.expanded ? "▾" : "▸"
                    onClicked: cr.expanded = !cr.expanded
                }
                Label {
                    Layout.fillWidth: true
                    visible: !cr.renaming
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: root.fs
                    text: cr.catName
                    TapHandler {
                        onDoubleTapped: cr.beginRename()
                    }
                }
                AppTextField {
                    id: renameField
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.fs + 12
                    visible: cr.renaming
                    font.pixelSize: root.fs
                    onAccepted: cr.commitRename()
                    onActiveFocusChanged: {
                        if (!activeFocus && cr.renaming)
                            cr.commitRename()
                    }
                    Keys.onEscapePressed: cr.renaming = false
                }
                Label {
                    color: Theme.dimText
                    font.pixelSize: root.fs - 2
                    text: qsTr("%1 papers").arg(cr.catPapers.length)
                }
                ToolButton {
                    implicitWidth: 22
                    implicitHeight: 22
                    font.pixelSize: 11
                    text: "✎"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Rename (or double-click the name)")
                    onClicked: cr.beginRename()
                }
                ToolButton {
                    implicitWidth: 22
                    implicitHeight: 22
                    font.pixelSize: 11
                    text: cr.locked ? "🔒" : "🔓"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: cr.locked
                                  ? qsTr("Locked — this one is yours, and the "
                                         + "next regeneration will leave it "
                                         + "exactly as it is.")
                                  : qsTr("Lock this category so regenerating "
                                         + "cannot change it.")
                    onClicked: research.setCategoryLocked(cr.catId, !cr.locked)
                }
                ToolButton {
                    implicitWidth: 22
                    implicitHeight: 22
                    font.pixelSize: 11
                    text: cr.confirmed ? "✔" : "○"
                    palette.buttonText: cr.confirmed ? Theme.success : Theme.dimText
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: cr.confirmed
                                  ? qsTr("You have confirmed this category.")
                                  : qsTr("Confirm this category — new papers "
                                         + "are placed against the confirmed "
                                         + "ones.")
                    onClicked: research.setCategoryConfirmed(cr.catId, !cr.confirmed)
                }
                ToolButton {
                    implicitWidth: 22
                    implicitHeight: 22
                    font.pixelSize: 11
                    text: "⇄"
                    enabled: root.otherCategories(cr.siblings, cr.catId).length > 0
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Merge this category into another one")
                    onClicked: mergeMenu.popup()
                    Menu {
                        id: mergeMenu
                        title: qsTr("Merge into…")
                        Instantiator {
                            model: root.otherCategories(cr.siblings, cr.catId)
                            delegate: MenuItem {
                                required property var modelData
                                text: qsTr("Merge into “%1”")
                                      .arg(root.textAt(modelData, "name"))
                                onTriggered: research.mergeCategories(
                                                 root.textAt(modelData, "id"),
                                                 cr.catId)
                            }
                            onObjectAdded: (i, obj) => mergeMenu.insertItem(i, obj)
                            onObjectRemoved: (i, obj) => mergeMenu.removeItem(obj)
                        }
                    }
                }
                ToolButton {
                    implicitWidth: 22
                    implicitHeight: 22
                    font.pixelSize: 11
                    text: "⑂"
                    enabled: cr.catPapers.length > 1
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Split some of these papers into a "
                                       + "category of their own")
                    onClicked: root.beginSplit(cr.catId, cr.catName)
                }
                ToolButton {
                    implicitWidth: 22
                    implicitHeight: 22
                    font.pixelSize: 11
                    text: "🗑"
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Delete this category (the papers stay)")
                    onClicked: research.removeCategory(cr.catId)
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 22
                visible: !!(root.textAt(cr.category, "description").length > 0)
                wrapMode: Text.Wrap
                color: Theme.bodyText
                font.pixelSize: root.fs - 2
                text: root.textAt(cr.category, "description")
            }
            Label {
                Layout.leftMargin: 22
                visible: cr.mine
                color: Theme.dimText
                font.pixelSize: root.fs - 3
                text: qsTr("yours")
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 22
                spacing: 1
                visible: cr.expanded
                Repeater {
                    model: cr.catPapers
                    delegate: RowLayout {
                        id: pr
                        required property var modelData
                        readonly property string pid: pr.modelData
                                                      ? String(pr.modelData) : ""
                        Layout.fillWidth: true
                        spacing: 4
                        PaperChip {
                            paperId: pr.pid
                            maxWidth: 380
                        }
                        Item { Layout.fillWidth: true }
                        ToolButton {
                            implicitWidth: 18
                            implicitHeight: 18
                            font.pixelSize: 10
                            text: "✕"
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            ToolTip.text: qsTr("Take this paper out of the category")
                            onClicked: research.assignPaper(pr.pid, cr.catId, false)
                        }
                    }
                }
                Label {
                    visible: !!(cr.catPapers.length === 0)
                    color: Theme.dimText
                    font.pixelSize: root.fs - 2
                    text: qsTr("Nothing in here yet.")
                }
            }
        }
    }

    // ── the dialog ──────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // Page/Home/End walk whichever tab is showing.
        focus: true
        Keys.onPressed: (event) => ScrollKeys.handle(event, root.currentFlickable())

        // Nothing here notifies on its own; this is what makes the bindings
        // above re-read after a generation lands or a category is edited.
        Connections {
            target: research
            function onResultChanged(kind) { root.rev = root.rev + 1 }
            function onStateChanged() { root.rev = root.rev + 1 }
        }

        AppTabBar {
            id: tabBar
            Layout.fillWidth: true
            AppTabButton { text: qsTr("Categories") }
            AppTabButton { text: qsTr("Map") }
            AppTabButton { text: qsTr("Consensus") }
            AppTabButton { text: qsTr("Timeline") }
            AppTabButton { text: qsTr("Coverage") }
            AppTabButton { text: qsTr("Openings") }
            AppTabButton { text: qsTr("Next steps") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            // ── §8 categories ───────────────────────────────────────
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    TabHeader { kind: "taxonomy" }

                    EmptyState {
                        kind: "taxonomy"
                        what: qsTr("Which papers belong together — by problem, "
                                   + "scenario, method route, data, metric, "
                                   + "contribution and more, with one paper "
                                   + "free to sit in several categories at "
                                   + "once. The categories are yours to rename, "
                                   + "merge, lock and confirm.")
                    }

                    ScrollView {
                        id: taxScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !!root.hasResult("taxonomy", root.rev)
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: parent.width
                            spacing: 10

                            // §8.4: papers the category system has never seen.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                visible: !!(root.unclassified(root.rev).length > 0)
                                AppButton {
                                    enabled: research.canRun
                                    text: qsTr("Place %1 new papers")
                                          .arg(root.unclassified(root.rev).length)
                                    onClicked: research.classifyNewPapers()
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    color: Theme.dimText
                                    font.pixelSize: root.fs - 2
                                    text: qsTr("They are placed against the "
                                               + "categories you already have — "
                                               + "the system is not redrawn.")
                                }
                            }

                            Repeater {
                                model: root.listAt(root.taxRes, "dimensions")
                                delegate: ColumnLayout {
                                    id: dimBlock
                                    required property var modelData
                                    readonly property var dim: dimBlock.modelData
                                    readonly property var cats: root.listAt(dimBlock.dim,
                                                                            "categories")
                                    Layout.fillWidth: true
                                    Layout.bottomMargin: 6
                                    spacing: 2

                                    GroupTitle {
                                        text: root.dimensionLabel(
                                                  root.textAt(dimBlock.dim, "dimension"))
                                    }

                                    Repeater {
                                        model: dimBlock.cats
                                        delegate: CategoryRow {
                                            required property var modelData
                                            category: modelData
                                            siblings: dimBlock.cats
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 6
                                        spacing: 4
                                        AppTextField {
                                            id: newCatField
                                            Layout.preferredWidth: 220
                                            Layout.preferredHeight: root.fs + 12
                                            font.pixelSize: root.fs - 1
                                            placeholderText: qsTr("a category of your own…")
                                            onAccepted: {
                                                const n = text.trim()
                                                if (n.length === 0)
                                                    return
                                                research.addCategory(
                                                    root.textAt(dimBlock.dim,
                                                                "dimension"), n)
                                                text = ""
                                            }
                                        }
                                        AppButton {
                                            enabled: newCatField.text.trim().length > 0
                                            text: qsTr("+ new category")
                                            onClicked: {
                                                research.addCategory(
                                                    root.textAt(dimBlock.dim,
                                                                "dimension"),
                                                    newCatField.text.trim())
                                                newCatField.text = ""
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }

                            // §8.4: the ones the model could not place.
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.bottomMargin: 8
                                spacing: 3
                                visible: !!(root.listAt(root.taxRes,
                                                        "ambiguous").length > 0)
                                GroupTitle { text: qsTr("These need a human") }
                                Repeater {
                                    model: root.listAt(root.taxRes, "ambiguous")
                                    delegate: ColumnLayout {
                                        id: amb
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 1
                                        PaperChip {
                                            paperId: root.textAt(amb.modelData, "paperId")
                                            maxWidth: 420
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 10
                                            visible: !!(root.textAt(amb.modelData,
                                                                    "note").length > 0)
                                            wrapMode: Text.Wrap
                                            color: Theme.bodyText
                                            font.pixelSize: root.fs - 2
                                            text: root.textAt(amb.modelData, "note")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── §9 research map ─────────────────────────────────────
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    TabHeader { kind: "map" }

                    EmptyState {
                        kind: "map"
                        what: qsTr("The questions this library is asking, the "
                                   + "different routes people take to each one, "
                                   + "and what those routes were tried on and "
                                   + "what they left unsolved.")
                    }

                    ScrollView {
                        id: mapScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !!root.hasResult("map", root.rev)
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: parent.width
                            spacing: 10

                            Repeater {
                                model: root.listAt(root.mapRes, "questions")
                                delegate: ColumnLayout {
                                    id: qBlock
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.bottomMargin: 6
                                    spacing: 4

                                    GroupTitle {
                                        text: root.textAt(qBlock.modelData, "question")
                                    }

                                    Repeater {
                                        model: root.listAt(qBlock.modelData, "routes")
                                        delegate: ColumnLayout {
                                            id: rBlock
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 12
                                            Layout.bottomMargin: 4
                                            spacing: 2
                                            Label {
                                                Layout.fillWidth: true
                                                wrapMode: Text.Wrap
                                                color: Theme.text
                                                font.pixelSize: root.fs
                                                font.bold: true
                                                text: root.textAt(rBlock.modelData,
                                                                  "route")
                                            }
                                            PaperChips {
                                                ids: root.listAt(rBlock.modelData,
                                                                 "paperIds")
                                            }
                                            MiniList {
                                                label: qsTr("Tried on")
                                                items: root.listAt(rBlock.modelData,
                                                                   "datasets")
                                            }
                                            MiniList {
                                                label: qsTr("Measured by")
                                                items: root.listAt(rBlock.modelData,
                                                                   "metrics")
                                            }
                                            MiniList {
                                                label: qsTr("What came out")
                                                items: root.listAt(rBlock.modelData,
                                                                   "results")
                                            }
                                            MiniList {
                                                label: qsTr("Still wrong with it")
                                                items: root.listAt(rBlock.modelData,
                                                                   "limitations")
                                            }
                                        }
                                    }
                                }
                            }

                            Item { Layout.preferredHeight: 4 }
                        }
                    }
                }
            }

            // ── §11 consensus and conflict ──────────────────────────
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    TabHeader { kind: "consensus" }

                    EmptyState {
                        kind: "consensus"
                        what: qsTr("What several papers agree on, what rests on "
                                   + "a single paper, what gets repeated "
                                   + "without anyone checking it, and where the "
                                   + "papers actually contradict each other.")
                    }

                    ScrollView {
                        id: conScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !!root.hasResult("consensus", root.rev)
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: parent.width
                            spacing: 10

                            ClaimGroup {
                                heading: qsTr("Several papers agree")
                                hint: qsTr("Independently supported by more than "
                                           + "one paper in this library.")
                                items: root.listAt(root.conRes, "agreed")
                            }
                            ClaimGroup {
                                heading: qsTr("Only one paper says this")
                                hint: qsTr("True or not, it has not been "
                                           + "reproduced by anything else here.")
                                items: root.listAt(root.conRes, "singleSource")
                            }
                            ClaimGroup {
                                heading: qsTr("Repeated, never checked")
                                hint: qsTr("Several papers assert it, but they "
                                           + "cite each other rather than "
                                           + "evidence.")
                                items: root.listAt(root.conRes, "repeatedUnverified")
                            }
                            ConflictGroup {
                                heading: qsTr("They disagree")
                                items: root.listAt(root.conRes, "conflicts")
                            }

                            Item { Layout.preferredHeight: 4 }
                        }
                    }
                }
            }

            // ── §12 how the field moved ─────────────────────────────
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    TabHeader { kind: "evolution" }

                    EmptyState {
                        kind: "evolution"
                        what: qsTr("How the problems, the methods, the data and "
                                   + "the way results are judged changed over "
                                   + "time — with the turning points, and the "
                                   + "questions that have been open the whole "
                                   + "way through.")
                    }

                    // The dating comes out of the interpretations, so whatever
                    // the model wants to warn about goes above the timeline
                    // rather than under it.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: caveatText.implicitHeight + 14
                        visible: !!(root.hasResult("evolution", root.rev)
                                    && root.textAt(root.evoRes, "caveat").length > 0)
                        color: Theme.cardBg
                        radius: 4
                        Label {
                            id: caveatText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            wrapMode: Text.Wrap
                            color: Theme.danger
                            font.pixelSize: root.fs - 1
                            text: root.textAt(root.evoRes, "caveat")
                        }
                    }

                    ScrollView {
                        id: evoScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !!root.hasResult("evolution", root.rev)
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: root.listAt(root.evoRes, "periods")
                                delegate: RowLayout {
                                    id: period
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8

                                    // the rail
                                    Item {
                                        Layout.preferredWidth: 14
                                        Layout.fillHeight: true
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: 2
                                            color: Theme.divider
                                        }
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            y: 5
                                            width: 9
                                            height: 9
                                            radius: 4.5
                                            color: Theme.accent
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.bottomMargin: 8
                                        spacing: 2

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Label {
                                                wrapMode: Text.Wrap
                                                color: Theme.heading
                                                font.bold: true
                                                font.pixelSize: root.fs + 1
                                                text: root.textAt(period.modelData,
                                                                  "label")
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                color: Theme.dimText
                                                font.pixelSize: root.fs - 1
                                                text: root.textAt(period.modelData,
                                                                  "years")
                                            }
                                        }

                                        BulletList {
                                            heading: qsTr("Problems")
                                            items: root.listAt(period.modelData,
                                                               "problems")
                                        }
                                        BulletList {
                                            heading: qsTr("Methods")
                                            items: root.listAt(period.modelData,
                                                               "methods")
                                        }
                                        BulletList {
                                            heading: qsTr("Data")
                                            items: root.listAt(period.modelData, "data")
                                        }
                                        BulletList {
                                            heading: qsTr("How it was judged")
                                            items: root.listAt(period.modelData,
                                                               "evaluation")
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            visible: !!(root.listAt(
                                                            period.modelData,
                                                            "representativePaperIds").length > 0)
                                            color: Theme.dimText
                                            font.pixelSize: root.fs - 1
                                            text: qsTr("Representative papers")
                                        }
                                        PaperChips {
                                            Layout.leftMargin: 8
                                            ids: root.listAt(period.modelData,
                                                             "representativePaperIds")
                                        }
                                        FieldLine {
                                            label: qsTr("Turning point")
                                            value: root.textAt(period.modelData,
                                                               "turningPoint")
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.bottomMargin: 8
                                spacing: 2
                                visible: !!(root.listAt(root.evoRes,
                                                        "longstanding").length > 0)
                                GroupTitle {
                                    text: qsTr("Open the whole way through")
                                }
                                BulletList {
                                    items: root.listAt(root.evoRes, "longstanding")
                                }
                            }
                        }
                    }
                }
            }

            // ── §13 coverage ────────────────────────────────────────
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    TabHeader { kind: "coverage" }

                    // §13 mandates this warning, so it is pinned above the
                    // scroll area and never scrolls away: a hole in this
                    // library is not a hole in the field.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: covDisclaimer.implicitHeight + 14
                        color: Theme.cardBg
                        radius: 4
                        Label {
                            id: covDisclaimer
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            wrapMode: Text.Wrap
                            color: Theme.danger
                            font.pixelSize: root.fs - 1
                            text: root.textAt(root.covRes, "disclaimer").length > 0
                                  ? root.textAt(root.covRes, "disclaimer")
                                  : qsTr("This looks only at the papers in this "
                                         + "project. Not covered here does not "
                                         + "mean it does not exist in the field.")
                        }
                    }

                    EmptyState {
                        kind: "coverage"
                        what: qsTr("Which topics this library covers properly, "
                                   + "which rest on one thin paper, which "
                                   + "conclusions have weak evidence behind "
                                   + "them, and what kind of work is missing "
                                   + "from the collection entirely.")
                    }

                    ScrollView {
                        id: covScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !!root.hasResult("coverage", root.rev)
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: parent.width
                            spacing: 10

                            NoteGroup {
                                heading: qsTr("Well covered")
                                items: root.listAt(root.covRes, "wellCovered")
                            }
                            NoteGroup {
                                heading: qsTr("Thin — only a paper or two")
                                items: root.listAt(root.covRes, "thin")
                            }
                            NoteGroup {
                                heading: qsTr("Weak evidence")
                                hint: qsTr("Stated confidently, supported "
                                           + "lightly.")
                                items: root.listAt(root.covRes, "weakEvidence")
                            }
                            NoteGroup {
                                heading: qsTr("No fair comparison")
                                items: root.listAt(root.covRes, "missingComparisons")
                            }
                            NoteGroup {
                                heading: qsTr("Never tried in the real world")
                                items: root.listAt(root.covRes, "noRealWorldValidation")
                            }
                            NoteGroup {
                                heading: qsTr("No shared metric")
                                hint: qsTr("Everyone measures something "
                                           + "different, so the numbers cannot "
                                           + "be lined up.")
                                items: root.listAt(root.covRes, "noSharedMetric")
                            }
                            NoteGroup {
                                heading: qsTr("Kinds of work missing here")
                                items: root.listAt(root.covRes, "missingTypes")
                            }

                            Item { Layout.preferredHeight: 4 }
                        }
                    }
                }
            }

            // ── §14 candidate openings ──────────────────────────────
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    TabHeader { kind: "opportunities" }

                    // Same rule as coverage: an opening this library sees is a
                    // lead, not a finding about the field.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: oppDisclaimer.implicitHeight + 14
                        color: Theme.cardBg
                        radius: 4
                        Label {
                            id: oppDisclaimer
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            wrapMode: Text.Wrap
                            color: Theme.danger
                            font.pixelSize: root.fs - 1
                            text: root.textAt(root.oppRes, "disclaimer").length > 0
                                  ? root.textAt(root.oppRes, "disclaimer")
                                  : qsTr("These are leads to check, not proven "
                                         + "gaps. Nothing here replaces a "
                                         + "literature search.")
                        }
                    }

                    EmptyState {
                        kind: "opportunities"
                        what: qsTr("Questions worth taking further, built out of "
                                   + "what the papers admit they did not solve, "
                                   + "where they contradict each other, and "
                                   + "what they assumed without testing — each "
                                   + "with a smallest experiment that would "
                                   + "settle it.")
                    }

                    ScrollView {
                        id: oppScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !!root.hasResult("opportunities", root.rev)
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: root.listAt(root.oppRes, "opportunities")
                                delegate: AppSectionCard {
                                    id: opp
                                    required property var modelData
                                    implicitHeight: oppCol.implicitHeight + 16

                                    ColumnLayout {
                                        id: oppCol
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 3

                                        Label {
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            color: Theme.heading
                                            font.bold: true
                                            font.pixelSize: root.fs + 1
                                            text: root.textAt(opp.modelData, "question")
                                        }
                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 4
                                            Chip {
                                                visible: !!(root.textAt(opp.modelData,
                                                                        "difficulty").length > 0)
                                                label: qsTr("difficulty: %1")
                                                       .arg(root.levelLabel(
                                                                root.textAt(opp.modelData,
                                                                            "difficulty")))
                                                tint: root.difficultyColor(
                                                          root.textAt(opp.modelData,
                                                                      "difficulty"))
                                            }
                                            Chip {
                                                visible: !!(root.textAt(opp.modelData,
                                                                        "confidence").length > 0)
                                                label: qsTr("confidence: %1")
                                                       .arg(root.levelLabel(
                                                                root.textAt(opp.modelData,
                                                                            "confidence")))
                                                tint: root.confidenceColor(
                                                          root.textAt(opp.modelData,
                                                                      "confidence"))
                                                hint: qsTr("How sure the model is "
                                                           + "that this opening is "
                                                           + "real.")
                                            }
                                            Chip {
                                                visible: !!(root.textAt(opp.modelData,
                                                                        "gapType").length > 0)
                                                label: root.gapTypeLabel(
                                                           root.textAt(opp.modelData,
                                                                       "gapType"))
                                                tint: root.gapTypeColor(
                                                          root.textAt(opp.modelData,
                                                                      "gapType"))
                                                hint: root.gapTypeHint(
                                                          root.textAt(opp.modelData,
                                                                      "gapType"))
                                            }
                                        }

                                        FieldLine {
                                            label: qsTr("What is missing")
                                            value: root.textAt(opp.modelData, "gap")
                                        }
                                        FieldLine {
                                            label: qsTr("How to attack it")
                                            value: root.textAt(opp.modelData, "approach")
                                        }
                                        FieldLine {
                                            label: qsTr("Smallest experiment")
                                            value: root.textAt(opp.modelData,
                                                               "minimalExperiment")
                                        }
                                        MiniList {
                                            label: qsTr("Baselines")
                                            items: root.listAt(opp.modelData, "baselines")
                                        }
                                        MiniList {
                                            label: qsTr("Data and metrics")
                                            items: root.listAt(opp.modelData,
                                                               "dataAndMetrics")
                                        }
                                        FieldLine {
                                            label: qsTr("What it would add")
                                            value: root.textAt(opp.modelData,
                                                               "contribution")
                                        }
                                        MiniList {
                                            label: qsTr("Risks")
                                            items: root.listAt(opp.modelData, "risks")
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            visible: !!(root.listAt(opp.modelData,
                                                                    "sourcePaperIds").length > 0)
                                            color: Theme.dimText
                                            font.pixelSize: root.fs - 1
                                            text: qsTr("Came from")
                                        }
                                        PaperChips {
                                            ids: root.listAt(opp.modelData,
                                                             "sourcePaperIds")
                                        }
                                    }
                                }
                            }

                            Item { Layout.preferredHeight: 4 }
                        }
                    }
                }
            }

            // ── §15 what to do next ─────────────────────────────────
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    TabHeader { kind: "actions" }

                    EmptyState {
                        kind: "actions"
                        what: qsTr("The next moves this library suggests: what "
                                   + "to read closely, what to go and search "
                                   + "for, what is worth reproducing or "
                                   + "comparing, what small experiment to run, "
                                   + "and what to take to your advisor.")
                    }

                    ScrollView {
                        id: actScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !!root.hasResult("actions", root.rev)
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: parent.width
                            spacing: 10

                            NoteGroup {
                                heading: qsTr("Read these next")
                                titleKey: "what"
                                noteKey: "why"
                                items: root.listAt(root.actRes, "readNext")
                            }
                            NoteGroup {
                                heading: qsTr("Go and search for")
                                titleKey: "what"
                                noteKey: "why"
                                items: root.listAt(root.actRes, "searchFor")
                            }
                            NoteGroup {
                                heading: qsTr("Worth reproducing")
                                titleKey: "what"
                                noteKey: "why"
                                items: root.listAt(root.actRes, "reproduce")
                            }
                            NoteGroup {
                                heading: qsTr("Worth comparing head to head")
                                titleKey: "what"
                                noteKey: "why"
                                items: root.listAt(root.actRes, "compare")
                            }
                            NoteGroup {
                                heading: qsTr("Small experiments you could run now")
                                titleKey: "what"
                                noteKey: "why"
                                items: root.listAt(root.actRes, "smallExperiments")
                            }
                            NoteGroup {
                                heading: qsTr("Ask your advisor")
                                titleKey: "what"
                                noteKey: "why"
                                items: root.listAt(root.actRes, "advisorQuestions")
                            }

                            Item { Layout.preferredHeight: 4 }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.dimText
                font.pixelSize: 11
                text: qsTr("Every tab here is written from the papers' "
                           + "interpretations, never from the PDFs, and is "
                           + "shared with everyone in the project.")
            }
            AppButton {
                text: qsTr("Export report…")
                onClicked: exportReportDialog.open()
            }
            AppButton {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }
}
