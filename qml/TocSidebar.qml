import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: Theme.paneBg

    // Page/Home/End walk the section list.
    focus: true
    Keys.onPressed: (event) => ScrollKeys.handle(event, list)

    signal sectionClicked(int blockId, int page)

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
                    Layout.minimumWidth: 0
                    text: list.count > 0
                          ? qsTr("TOC (%1)").arg(list.count)
                          : qsTr("TOC")
                    font.bold: true
                    elide: Text.ElideRight
                }
                // Where the visible TOC came from. Without this the
                // rebuild button below is a blind trade: the paper's
                // own structure is usually the better outline, and
                // replacing it costs an LLM request.
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: list.count > 0 && text !== ""
                    text: toc.source === TocService.Structural
                          ? qsTr("from paper structure")
                          : toc.source === TocService.Llm ? qsTr("by AI") : ""
                    color: Theme.dimText
                    font.pixelSize: Math.max(8, settings.tocFontSize - 2)
                    elide: Text.ElideRight
                }
                BusyIndicator {
                    running: toc.status === TocService.Generating
                    visible: running
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                ToolButton {
                    text: list.count > 0 ? qsTr("Rebuild with AI")
                                         : qsTr("Build with AI")
                    enabled: paperController.status === PaperController.Ready
                             && settings.isConfigured
                             && toc.status !== TocService.Generating
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: list.count > 0
                        ? qsTr("Discard this table of contents and have the AI model build a new one (one request).")
                        : qsTr("Have the AI model read the paper and build a table of contents (one request).")
                    onClicked: toc.generate()
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
                model: toc.sections
                visible: count > 0

                ScrollBar.vertical: ScrollBar { active: true }

                delegate: ItemDelegate {
                    width: ListView.view ? ListView.view.width : 0
                    height: 28

                    contentItem: RowLayout {
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter

                        Item {
                            implicitWidth: model.indent
                            Layout.preferredWidth: model.indent
                        }
                        Label {
                            Layout.fillWidth: true
                            text: model.title
                            elide: Text.ElideRight
                            font.bold: model.level === 1
                            // Level-1 sections render +1 px so the
                            // visual hierarchy survives a custom base
                            // size from Settings.
                            font.pixelSize: settings.tocFontSize
                                          + (model.level === 1 ? 1 : 0)
                            color: model.level === 1 ? Theme.heading : Theme.text
                        }
                        Label {
                            text: qsTr("p.%1").arg(model.startPage + 1)
                            color: Theme.dimText
                            // Page badge stays small relative to the
                            // section title (-2 px from the body).
                            font.pixelSize: Math.max(8, settings.tocFontSize - 2)
                            Layout.rightMargin: 4
                        }
                    }

                    onClicked: root.sectionClicked(model.startBlockId, model.startPage)
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !list.visible
                horizontalAlignment: Text.AlignHCenter
                color: Theme.dimText
                wrapMode: Text.Wrap
                width: parent.width - 32
                text: {
                    switch (toc.status) {
                    case TocService.Generating: return qsTr("Generating TOC…")
                    case TocService.Failed:     return qsTr("Failed: %1").arg(toc.lastError)
                    default:
                        if (paperController.status !== PaperController.Ready)
                            return qsTr("Open a PDF first.")
                        // GROBID fills this in by itself for papers it
                        // can parse, so an empty pane means it didn't
                        // (service off/unreachable, or not a paper) —
                        // say so instead of implying the button is the
                        // normal route.
                        return paperController.extracting
                            ? qsTr("Reading the paper's structure…")
                            : qsTr("No table of contents in this document's structure. Use Build with AI to have the model create one.")
                    }
                }
            }
        }
    }
}
