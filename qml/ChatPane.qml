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

        // ── Session strip ────────────────────────────────────────────────
        // VS Code style row of session tabs. Each tab shows the session
        // name and a × close button; clicking the body activates the
        // session, double-click renames inline, + at the right adds a new
        // empty session.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "#e4e4e4"

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Flickable {
                    id: tabFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: tabRow.implicitWidth
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Row {
                        id: tabRow
                        height: tabFlick.height
                        spacing: 0

                        Repeater {
                            model: chat.sessions
                            delegate: Rectangle {
                                id: tab
                                height: tabRow.height
                                width: Math.min(180, Math.max(90, label.implicitWidth + 44))
                                color: model.isActive ? "#fafafa" : "transparent"
                                border.color: model.isActive ? "#d0d0d0" : "transparent"
                                border.width: 1

                                // Active accent strip.
                                Rectangle {
                                    visible: model.isActive
                                    width: parent.width
                                    height: 2
                                    color: "#4c8bf5"
                                    anchors.bottom: parent.bottom
                                }

                                MouseArea {
                                    id: tabMouse
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                    hoverEnabled: true
                                    onClicked: function(mouse) {
                                        if (mouse.button === Qt.MiddleButton) {
                                            chat.deleteSession(model.sessionId)
                                        } else {
                                            chat.activateSession(model.sessionId)
                                        }
                                    }
                                    onDoubleClicked: function(mouse) {
                                        if (mouse.button !== Qt.LeftButton) return
                                        renameField.text = model.sessionName
                                        renameField.visible = true
                                        label.visible = false
                                        renameField.forceActiveFocus()
                                        renameField.selectAll()
                                    }
                                }

                                Label {
                                    id: label
                                    anchors.left: parent.left
                                    anchors.right: closeBtn.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 4
                                    elide: Text.ElideRight
                                    text: model.sessionName
                                    color: model.isActive ? "#1d1d1d" : "#555"
                                    font.pixelSize: 12
                                }

                                TextField {
                                    id: renameField
                                    anchors.left: parent.left
                                    anchors.right: closeBtn.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 4
                                    visible: false
                                    selectByMouse: true
                                    font.pixelSize: 12
                                    background: Rectangle {
                                        color: "#ffffff"
                                        border.color: "#c0c0c0"
                                        radius: 2
                                    }
                                    onAccepted: {
                                        chat.renameSession(model.sessionId, text.trim())
                                        visible = false
                                        label.visible = true
                                    }
                                    Keys.onEscapePressed: {
                                        visible = false
                                        label.visible = true
                                    }
                                    onActiveFocusChanged: {
                                        if (!activeFocus && visible) {
                                            chat.renameSession(model.sessionId, text.trim())
                                            visible = false
                                            label.visible = true
                                        }
                                    }
                                }

                                ToolButton {
                                    id: closeBtn
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.rightMargin: 2
                                    width: 18
                                    height: 18
                                    flat: true
                                    padding: 0
                                    text: "×"
                                    font.pixelSize: 14
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 400
                                    ToolTip.text: qsTr("Close session")
                                    onClicked: chat.deleteSession(model.sessionId)
                                }

                                // Slim divider between non-active tabs to
                                // keep the strip readable.
                                Rectangle {
                                    visible: !model.isActive
                                    width: 1
                                    height: parent.height - 8
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: "#cfcfcf"
                                }
                            }
                        }
                    }
                }

                ToolButton {
                    Layout.preferredHeight: 26
                    Layout.preferredWidth: 28
                    Layout.alignment: Qt.AlignVCenter
                    Layout.rightMargin: 4
                    text: "+"
                    font.pixelSize: 16
                    flat: true
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("New session")
                    onClicked: chat.newSession()
                }
            }
        }

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
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Clear messages in the current session")
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
            // onCountChanged covers new bubbles; onContentHeightChanged
            // covers the active assistant bubble growing chunk-by-chunk
            // (count stays the same, contentHeight grows).
            onCountChanged: positionViewAtEnd()
            onContentHeightChanged: if (chat.busy) positionViewAtEnd()

            delegate: Item {
                width: ListView.view ? ListView.view.width : 0
                implicitHeight: bubble.height + 12

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
                                    Math.max(bubbleContent.implicitWidth + 16, 120))
                    // Drive height from the inner column's implicit size —
                    // Rectangle does not auto-size to children, so without
                    // this binding the bubble collapses to ~0 px and the
                    // TextEdit's caret/I-beam shows through as a stray cross.
                    height: bubbleContent.implicitHeight + 16
                    color: model.role === "user" ? "#dee5ff" : "#ffffff"
                    border.color: model.status === 2 /*Failed*/ ? "#c62828" : "#dddddd"
                    border.width: 1
                    radius: 6

                    ColumnLayout {
                        id: bubbleContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 4

                        Label {
                            text: model.role === "user" ? qsTr("You")
                                                        : qsTr("Assistant")
                            font.pixelSize: 10
                            color: "#888"
                        }
                        TextEdit {
                            id: bodyText
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: "#1d1d1d"
                            // Assistant replies route through cmark-gfm for
                            // GFM tables / strikethrough / footnotes /
                            // task lists. During streaming we use Qt's
                            // built-in MarkdownText to skip re-parsing the
                            // doc on every chunk; once the turn is Done we
                            // swap to RichText with the cmark-rendered
                            // HTML for the richer output. User messages
                            // stay verbatim.
                            textFormat: model.role !== "assistant"
                                        ? TextEdit.PlainText
                                        : (model.status === 1 /*Streaming*/
                                           ? TextEdit.MarkdownText
                                           : TextEdit.RichText)
                            text: model.content.length === 0
                                  ? (model.status === 1 ? "..." : " ")
                                  : (model.role === "assistant"
                                     && model.status !== 1
                                     ? markdown.toHtml(model.content)
                                     : model.content)
                            // Hide the blinking text caret — this is a
                            // read-only view, not an editable field.
                            cursorVisible: false
                            activeFocusOnPress: false
                            // Pointer stays as the default arrow except when
                            // the user is dragging to select text.
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                cursorShape: Qt.IBeamCursor
                            }
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
            // Track the (capped) row height — NOT input.implicitHeight,
            // which is unbounded and would push the input/Send button
            // off the visible pane when a long block is prefilled.
            Layout.preferredHeight: inputRow.rowHeight + 12
            color: "#f0f0f0"

            RowLayout {
                id: inputRow
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6

                // Fixed input row height — both the text area and the
                // square Send button bind to this. Long content (manually
                // typed or prefilled via right-click → Ask AI) scrolls
                // inside the ScrollView; the row itself never grows.
                property int rowHeight: 36

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: inputRow.rowHeight
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
                    // Glyphs instead of words: ➤ for send, ■ for stop.
                    text: chat.busy ? "■" : "➤"
                    font.pixelSize: 16
                    Layout.preferredWidth: inputRow.rowHeight
                    Layout.preferredHeight: inputRow.rowHeight
                    enabled: paperController.status === PaperController.Ready
                             && settings.isConfigured
                             && (chat.busy || input.text.trim().length > 0)
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: chat.busy ? qsTr("Stop") : qsTr("Send")
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

    // Called by the BlockList right-click → "Ask AI about this". Prefills
    // the input with the block text quoted as a Markdown blockquote and
    // leaves the cursor on a fresh line below for the user to type their
    // question. Does NOT auto-send — the user controls the moment.
    function prefillInput(text, page) {
        const quoted = text.split("\n").map(function(l) { return "> " + l }).join("\n")
        const header = page > 0
                       ? qsTr("About this passage (page %1):").arg(page)
                       : qsTr("About this passage:")
        input.text = header + "\n" + quoted + "\n\n"
        input.cursorPosition = input.text.length
        input.forceActiveFocus()
    }
}
