import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: Theme.paneBg

    property var model: null
    property int paperStatus: PaperController.Empty
    property bool syncEnabled: true
    // True while the user drags a splitter handle. Every paragraph in
    // view re-wraps its text when this pane's width changes — measured at
    // 3.2 ms — which is too much to pay on every mouse move. The rows keep
    // re-wrapping during the drag (they should follow the handle), just at
    // a capped rate, and land exactly right when the handle is released.
    property bool resizing: false
    onResizingChanged: if (!resizing) { reflow.stop(); list.layoutWidth = list.width }

    signal pageRequested(int page)
    signal askInChatRequested(string text, int page)
    signal translateBlockRequested(int row)
    signal segmentRequested()

    function showPage(page) {
        if (!root.model)
            return
        const idx = root.model.firstRowOnPage(page)
        if (idx < 0)
            return
        root.syncEnabled = false
        list.positionViewAtIndex(idx, ListView.Beginning)
        Qt.callLater(function() { root.syncEnabled = true })
    }

    // Badge tint + label color per translation status. Routed through
    // Theme so each color has a light/dark pair — the old fixed Material
    // 800 values (green/blue/purple/red) sank into the dark background.
    function statusColor(name) {
        switch (name) {
        case "translated":  return Theme.success
        case "translating": return Theme.accent
        // Purple has no shared Theme role; inline light/dark pair.
        case "queued":      return Theme.dark ? "#ce93d8" : "#6a1b9a"
        case "failed":      return Theme.danger
        case "skipped":     return Theme.dimText
        default:            return Theme.dimText
        }
    }

    // Display labels for the model's internal enum-name strings. The
    // raw names stay untranslated in the model roles (they drive the
    // colors and menu logic above); only the visible text is mapped.
    function kindLabel(name) {
        switch (name) {
        case "heading":   return qsTr("heading")
        case "caption":   return qsTr("caption")
        case "list":      return qsTr("list item")
        case "equation":  return qsTr("equation")
        case "paragraph": return qsTr("paragraph")
        default:          return name
        }
    }
    function statusLabel(name) {
        switch (name) {
        case "translated":  return qsTr("translated")
        case "translating": return qsTr("translating")
        case "queued":      return qsTr("queued")
        case "failed":      return qsTr("failed")
        case "skipped":     return qsTr("skipped")
        default:            return name
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.headerBg

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                Label {
                    text: list.count > 0
                          ? qsTr("Paragraphs (%1)").arg(list.count)
                          : qsTr("Paragraphs")
                    font.bold: true
                }
                // Standing label for paragraphs that came from the project.
                // The notice below is a one-off "this just happened"; this
                // stays for as long as the paragraphs are somebody else's.
                Rectangle {
                    visible: paperSync.blocksOriginLabel.length > 0
                    radius: 6
                    color: Theme.accent
                    opacity: 0.18
                    implicitWidth: blocksOriginLabel.implicitWidth + 12
                    implicitHeight: blocksOriginLabel.implicitHeight + 4
                    Label {
                        id: blocksOriginLabel
                        anchors.centerIn: parent
                        text: qsTr("split by %1").arg(paperSync.blocksOriginLabel)
                        font.pixelSize: 10
                        color: Theme.accent
                    }
                    HoverHandler { id: originHover }
                    ToolTip.visible: originHover.hovered
                    ToolTip.text: qsTr("These paragraphs came from the project. "
                                       + "Re-segmenting makes them yours.")
                }

                Item { Layout.fillWidth: true }
                Label {
                    visible: translation.busy
                    text: qsTr("translating %1/%2…")
                          .arg(translation.doneCount)
                          .arg(translation.totalCount)
                    color: Theme.accent
                    font.pixelSize: 11
                }
                Label {
                    visible: !translation.busy && translation.failedCount > 0
                    text: qsTr("%1 failed").arg(translation.failedCount)
                    color: Theme.danger
                    font.pixelSize: 11
                }
            }
        }

        // What the project just handed this paper — a segmentation from
        // another machine of yours, or paragraphs a collaborator already
        // translated. Says so once, then gets out of the way.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? noticeRow.implicitHeight + 12 : 0
            visible: paperSync.notice.length > 0
            color: Theme.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.04)

            RowLayout {
                id: noticeRow
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 6
                anchors.topMargin: 6
                anchors.bottomMargin: 6
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 11
                    color: Theme.dimText
                    text: paperSync.notice
                }
                ToolButton {
                    text: "\u00d7"
                    flat: true
                    implicitWidth: 22
                    implicitHeight: 22
                    onClicked: paperSync.dismissNotice()
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: root.model
                spacing: 8
                visible: count > 0

                property int lastReportedPage: -1
                // The width the delegates wrap to. Tracks the pane
                // exactly, except during a splitter drag, where it is
                // resampled on a timer (see root.resizing).
                property real layoutWidth: width
                onWidthChanged: {
                    if (!root.resizing) { layoutWidth = width; return }
                    if (!reflow.running) reflow.start()
                }
                Timer {
                    id: reflow
                    interval: 32   // ~30 re-wraps a second while dragging
                    onTriggered: list.layoutWidth = list.width
                }

                onContentYChanged: maybeReportPage()
                onModelChanged: lastReportedPage = -1

                function maybeReportPage() {
                    if (!root.syncEnabled) return
                    // A relayout is not the user scrolling — reporting a
                    // page here would jump the PDF pane mid-drag.
                    if (root.resizing) return
                    if (count === 0) return
                    const idx = list.indexAt(list.width / 2, list.contentY + 1)
                    if (idx < 0) return
                    const page = root.model.pageOfRow(idx)
                    if (page < 0 || page === lastReportedPage) return
                    lastReportedPage = page
                    root.pageRequested(page)
                }

                ScrollBar.vertical: ScrollBar { active: true }

                delegate: Rectangle {
                    id: blockDelegate
                    width: list.layoutWidth
                    color: ctxArea.containsMouse ? Theme.hover : "transparent"
                    implicitHeight: cell.implicitHeight + 16

                    // Captured by the right-click handler so the menu's
                    // actions know which row to operate on, even after the
                    // model mutates (which would otherwise re-bind `index`).
                    readonly property int rowIndex: index

                    // Visibility logic — properties of the delegate
                    // root so visibility bindings inside `cell` can
                    // reference them via `blockDelegate.x`. The
                    // per-paragraph toggles (model.sourceVisible /
                    // model.translationVisible) are user-driven and
                    // take precedence. _showTrans guards the
                    // translation block until something is actually
                    // translated; _showSrc falls back to showing the
                    // source when both halves of an un-translated
                    // paragraph are hidden, so the row never collapses
                    // to just the header line.
                    readonly property bool _hasTranslation:
                        model.translation && model.translation.length > 0
                    readonly property bool _showTrans:
                        model.translationVisible && _hasTranslation
                    readonly property bool _showSrc:
                        model.sourceVisible || !_showTrans

                    // Right-click → context menu. We map the click position
                    // into the source TextEdit and remember the character
                    // offset so "Split here" knows where to cut. Left-click
                    // is left to the TextEdit (selection still works).
                    MouseArea {
                        id: ctxArea
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        hoverEnabled: true
                        onPressed: function(mouse) {
                            if (mouse.button !== Qt.RightButton) return
                            const local = mapToItem(sourceText, mouse.x, mouse.y)
                            const off = sourceText.positionAt(local.x, local.y)
                            ctxMenu.cursorOffset =
                                (off > 0 && off < model.text.length) ? off : -1
                            ctxMenu.popup()
                        }
                    }

                    Menu {
                        id: ctxMenu
                        // Character offset inside the source text where the
                        // user right-clicked. -1 means the click was on the
                        // header strip / outside the text or at a boundary
                        // where splitting wouldn't produce two halves.
                        property int cursorOffset: -1

                        MenuItem {
                            text: qsTr("Ask AI about this")
                            onTriggered: root.askInChatRequested(model.text, model.page)
                        }
                        MenuItem {
                            // Translate just this paragraph. Disabled
                            // while it's already in flight, when no LLM
                            // is configured, or while the row's text
                            // would be skipped (very short / numeric).
                            text: qsTr("Translate this paragraph")
                            enabled: settings.isConfigured
                                     && model.translationStatusName !== "translating"
                                     && model.translationStatusName !== "queued"
                            onTriggered: root.translateBlockRequested(blockDelegate.rowIndex)
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Split here")
                            enabled: ctxMenu.cursorOffset > 0
                            onTriggered: root.model.splitBlock(blockDelegate.rowIndex,
                                                               ctxMenu.cursorOffset)
                        }
                        MenuItem {
                            text: qsTr("Merge with previous")
                            enabled: blockDelegate.rowIndex > 0
                            onTriggered: root.model.mergeWithNext(blockDelegate.rowIndex - 1)
                        }
                        MenuItem {
                            text: qsTr("Merge with next")
                            enabled: blockDelegate.rowIndex < list.count - 1
                            onTriggered: root.model.mergeWithNext(blockDelegate.rowIndex)
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Delete paragraph")
                            onTriggered: root.model.removeBlock(blockDelegate.rowIndex)
                        }
                    }

                    ColumnLayout {
                        id: cell
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 6
                        anchors.bottomMargin: 8
                        spacing: 4

                        // Header strip: page · kind · per-paragraph
                        // visibility chevrons · status badge.
                        RowLayout {
                            spacing: 6
                            Layout.fillWidth: true

                            Label {
                                text: qsTr("p.%1 · %2").arg(model.page + 1)
                                                       .arg(root.kindLabel(model.kindName))
                                font.pixelSize: 10
                                color: Theme.dimText
                            }

                            // Show the chevrons only when there's a
                            // translation to compare against — for an
                            // untranslated paragraph there's nothing
                            // useful to toggle. ▲ expanded → click
                            // hides; ▼ collapsed → click reveals.
                            ToolButton {
                                visible: blockDelegate._hasTranslation
                                text: (model.sourceVisible ? "▲ " : "▼ ") + qsTr("Src")
                                flat: true
                                font.pixelSize: 10
                                padding: 2
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: model.sourceVisible
                                              ? qsTr("Hide source text")
                                              : qsTr("Show source text")
                                onClicked: model.sourceVisible = !model.sourceVisible
                            }
                            ToolButton {
                                visible: blockDelegate._hasTranslation
                                text: (model.translationVisible ? "▲ " : "▼ ") + qsTr("Trans")
                                flat: true
                                font.pixelSize: 10
                                padding: 2
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: model.translationVisible
                                              ? qsTr("Hide translation")
                                              : qsTr("Show translation")
                                onClicked: model.translationVisible = !model.translationVisible
                            }

                            Item { Layout.fillWidth: true }
                            // Translations are adopted per paragraph, so the
                            // attribution has to be per paragraph too — one
                            // page can mix your own with a collaborator's.
                            Rectangle {
                                visible: model.translationOrigin.length > 0
                                radius: 6
                                color: Theme.dimText
                                opacity: 0.16
                                implicitWidth: originBadge.implicitWidth + 12
                                implicitHeight: originBadge.implicitHeight + 4
                                Label {
                                    id: originBadge
                                    anchors.centerIn: parent
                                    text: qsTr("from %1").arg(model.translationOrigin)
                                    font.pixelSize: 10
                                    color: Theme.dimText
                                }
                                HoverHandler { id: rowOriginHover }
                                ToolTip.visible: rowOriginHover.hovered
                                ToolTip.text: qsTr("Translated by %1, shared through "
                                                   + "the project. Translating this "
                                                   + "paragraph yourself replaces it.")
                                              .arg(model.translationOrigin)
                            }
                            Rectangle {
                                visible: model.translationStatusName !== "idle"
                                radius: 6
                                color: root.statusColor(model.translationStatusName)
                                opacity: 0.18
                                implicitWidth: statusLabel.implicitWidth + 12
                                implicitHeight: statusLabel.implicitHeight + 4
                                Label {
                                    id: statusLabel
                                    anchors.centerIn: parent
                                    text: root.statusLabel(model.translationStatusName)
                                    font.pixelSize: 10
                                    color: root.statusColor(model.translationStatusName)
                                }
                            }
                        }

                        // Source text (English) — read-only TextEdit so the
                        // user can position a cursor for "Split here" and
                        // also select / copy passages.
                        TextEdit {
                            id: sourceText
                            visible: blockDelegate._showSrc
                            Layout.fillWidth: true
                            text: model.text
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            color: Theme.bodyText
                            // Headings get +2 px so they remain
                            // visually above the body even when the
                            // user scales paragraphFontSize from
                            // Settings.
                            font.pixelSize: settings.paragraphFontSize
                                          + (model.kindName === "heading" ? 2 : 0)
                            font.italic: model.kindName === "caption"
                        }

                        // Translation — primary styling. Renders at
                        // +2 px from the source so the translated
                        // line is the dominant element; headings
                        // stack another +2 px on top.
                        Text {
                            visible: blockDelegate._showTrans
                            Layout.fillWidth: true
                            text: model.translation || ""
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: settings.paragraphFontSize + 2
                                          + (model.kindName === "heading" ? 2 : 0)
                            font.bold: model.kindName === "heading"
                        }

                        // Failure detail
                        Text {
                            visible: model.translationStatusName === "failed"
                                     && model.translationError
                                     && model.translationError.length > 0
                            Layout.fillWidth: true
                            text: model.translationError || ""
                            wrapMode: Text.Wrap
                            color: Theme.danger
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.divider
                        visible: index < list.count - 1
                    }
                }
            }

            // Empty state. With auto-segmentation off (the default) an
            // opened paper lands here with nothing to show, so the state
            // carries the action that fills it rather than just a label.
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: !list.visible
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.dimText
                    text: paperController.extracting
                          ? qsTr("Segmenting…")
                          : root.paperStatus === PaperController.Loading
                            ? qsTr("Extracting paragraphs…")
                            : root.paperStatus === PaperController.Error
                              ? qsTr("No paragraphs (load failed).")
                              : root.paperStatus === PaperController.Ready
                                ? qsTr("This paper hasn't been split into paragraphs yet.")
                                : qsTr("Open a PDF to see extracted text.")
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.paperStatus === PaperController.Ready
                             && !paperController.extracting
                    text: qsTr("Segment paragraphs")
                    onClicked: root.segmentRequested()
                }
            }
        }
    }
}
