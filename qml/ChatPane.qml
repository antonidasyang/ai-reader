import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Rectangle {
    id: root
    color: "#fafafa"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ──────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "#ececec"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 6

                Label {
                    text: chat.messages.rowCount() > 0
                          ? qsTr("Chat (%1)").arg(chat.messages.rowCount())
                          : qsTr("Chat")
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    running: chat.busy
                    visible: running
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                ToolButton {
                    text: qsTr("Clear")
                    enabled: !chat.busy && chat.messages.rowCount() > 0
                    onClicked: chat.clear()
                }
            }
        }

        // ── Message list ────────────────────────────────────────────────
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: chat.messages
            ScrollBar.vertical: ScrollBar { active: true }

            // Auto-scroll to the bottom as new content streams in.
            onCountChanged: positionViewAtEnd()

            delegate: Item {
                width: ListView.view ? ListView.view.width : 0
                implicitHeight: bubble.implicitHeight + 12

                Rectangle {
                    id: bubble
                    anchors {
                        left: model.role === "user" ? undefined : parent.left
                        right: model.role === "user" ? parent.right : undefined
                        leftMargin: model.role === "user" ? 0 : 10
                        rightMargin: model.role === "user" ? 10 : 0
                        top: parent.top
                    }
                    width: Math.min(parent.width - 20,
                                    Math.max(content.implicitWidth + 16, 80))
                    color: model.role === "user" ? "#dee5ff" : "#ffffff"
                    border.color: model.status === 2 /*Failed*/ ? "#c62828" : "#dddddd"
                    border.width: 1
                    radius: 6

                    ColumnLayout {
                        id: content
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        Label {
                            text: model.role === "user" ? qsTr("You")
                                                        : qsTr("Assistant")
                            font.pixelSize: 10
                            color: "#888"
                        }
                        Label {
                            Layout.fillWidth: true
                            text: model.content.length > 0
                                  ? model.content
                                  : (model.status === 1 /*Streaming*/
                                     ? qsTr("…")
                                     : "")
                            wrapMode: Text.Wrap
                            color: "#1d1d1d"
                            textFormat: Text.PlainText
                        }
                        Label {
                            visible: model.status === 2 /*Failed*/
                            text: qsTr("Error: %1").arg(model.error)
                            color: "#c62828"
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                color: "#888"
                wrapMode: Text.Wrap
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                text: paperController.status === PaperController.Ready
                      ? qsTr("Ask a question about this paper.")
                      : qsTr("Open a PDF first.")
            }
        }

        // ── Input area ──────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: input.implicitHeight + 20
            color: "#f0f0f0"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(120, Math.max(36, input.implicitHeight + 8))
                    clip: true
                    TextArea {
                        id: input
                        wrapMode: TextEdit.Wrap
                        placeholderText: paperController.status === PaperController.Ready
                                         ? qsTr("Ask about the paper…  (Ctrl+Enter to send)")
                                         : qsTr("Open a PDF first.")
                        enabled: paperController.status === PaperController.Ready
                                 && settings.isConfigured
                        Keys.onPressed: function(event) {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                && (event.modifiers & Qt.ControlModifier)) {
                                sendCurrent()
                                event.accepted = true
                            }
                        }
                    }
                }
                Button {
                    text: chat.busy ? qsTr("Stop") : qsTr("Send")
                    enabled: paperController.status === PaperController.Ready
                             && settings.isConfigured
                             && (chat.busy || input.text.trim().length > 0)
                    onClicked: {
                        if (chat.busy) chat.cancel()
                        else sendCurrent()
                    }
                }
            }
        }
    }

    function sendCurrent() {
        const t = input.text.trim()
        if (t.length === 0) return
        chat.sendMessage(t)
        input.clear()
    }
}
