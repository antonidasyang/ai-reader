import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: "#fafafa"

    property var model: null
    property int paperStatus: PaperController.Empty
    property bool syncEnabled: true

    signal pageRequested(int page)
    signal askInChatRequested(string text, int page)

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

    function statusColor(name) {
        switch (name) {
        case "translated":  return "#2e7d32"
        case "translating": return "#1565c0"
        case "queued":      return "#6a1b9a"
        case "failed":      return "#c62828"
        case "skipped":     return "#888888"
        default:            return "#bdbdbd"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "#ececec"

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
                Item { Layout.fillWidth: true }
                Label {
                    visible: translation.busy
                    text: qsTr("translating %1/%2…")
                          .arg(translation.doneCount)
                          .arg(translation.totalCount)
                    color: "#1565c0"
                    font.pixelSize: 11
                }
                Label {
                    visible: !translation.busy && translation.failedCount > 0
                    text: qsTr("%1 failed").arg(translation.failedCount)
                    color: "#c62828"
                    font.pixelSize: 11
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

                onContentYChanged: maybeReportPage()
                onModelChanged: lastReportedPage = -1

                function maybeReportPage() {
                    if (!root.syncEnabled) return
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
                    width: ListView.view ? ListView.view.width : 0
                    color: ctxArea.containsMouse ? "#f0f3ff" : "transparent"
                    implicitHeight: cell.implicitHeight + 16

                    // Captured by the right-click handler so the menu's
                    // actions know which row to operate on, even after the
                    // model mutates (which would otherwise re-bind `index`).
                    readonly property int rowIndex: index

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

                        // Header strip: page · kind · status
                        RowLayout {
                            spacing: 6
                            Layout.fillWidth: true

                            Label {
                                text: qsTr("p.%1 · %2").arg(model.page + 1).arg(model.kindName)
                                font.pixelSize: 10
                                color: "#999"
                            }
                            Item { Layout.fillWidth: true }
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
                                    text: model.translationStatusName
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
                            Layout.fillWidth: true
                            text: model.text
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            color: "#5f6368"
                            font.pixelSize: model.kindName === "heading" ? 14 : 12
                            font.italic: model.kindName === "caption"
                        }

                        // Translation — primary styling
                        Text {
                            visible: model.translation && model.translation.length > 0
                            Layout.fillWidth: true
                            text: model.translation || ""
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            color: "#1d1d1d"
                            font.pixelSize: model.kindName === "heading" ? 16 : 14
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
                            color: "#c62828"
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: "#ececec"
                        visible: index < list.count - 1
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !list.visible
                horizontalAlignment: Text.AlignHCenter
                color: "#888"
                text: root.paperStatus === PaperController.Loading
                      ? qsTr("Extracting paragraphs…")
                      : root.paperStatus === PaperController.Error
                        ? qsTr("No paragraphs (load failed).")
                        : qsTr("Open a PDF to see extracted text.")
            }
        }
    }
}
