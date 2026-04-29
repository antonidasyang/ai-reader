import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    title: qsTr("Prompts")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel | Dialog.Reset
    closePolicy: Popup.CloseOnEscape
    width: 760
    height: 620

    // When the saved value is empty, pre-fill with the built-in default so
    // the user can see and edit from it. On accept, if the text still
    // matches the default we save empty — that way the service keeps
    // tracking future default updates.
    onOpened: {
        summaryArea.text     = settings.summaryPrompt.length     > 0 ? settings.summaryPrompt
                                                                     : summary.defaultSystemPrompt
        translationArea.text = settings.translationPrompt.length > 0 ? settings.translationPrompt
                                                                     : translation.defaultSystemPrompt
        tocArea.text         = settings.tocPrompt.length         > 0 ? settings.tocPrompt
                                                                     : toc.defaultSystemPrompt
        visionArea.text      = settings.visionPrompt.length      > 0 ? settings.visionPrompt
                                                                     : vision.defaultSystemPrompt
        chatArea.text        = settings.chatPrompt.length        > 0 ? settings.chatPrompt
                                                                     : chat.defaultSystemPrompt
        chatIncludeCheck.checked = settings.chatIncludePaperText
    }

    onAccepted: {
        settings.summaryPrompt     = summaryArea.text     === summary.defaultSystemPrompt     ? "" : summaryArea.text
        settings.translationPrompt = translationArea.text === translation.defaultSystemPrompt ? "" : translationArea.text
        settings.tocPrompt         = tocArea.text         === toc.defaultSystemPrompt         ? "" : tocArea.text
        settings.visionPrompt      = visionArea.text      === vision.defaultSystemPrompt      ? "" : visionArea.text
        settings.chatPrompt        = chatArea.text        === chat.defaultSystemPrompt        ? "" : chatArea.text
        settings.chatIncludePaperText = chatIncludeCheck.checked
    }

    onReset: {
        // Restore the active tab's editor to the built-in default text.
        switch (tabBar.currentIndex) {
        case 0: summaryArea.text     = summary.defaultSystemPrompt;     break
        case 1: translationArea.text = translation.defaultSystemPrompt; break
        case 2: tocArea.text         = toc.defaultSystemPrompt;         break
        case 3: visionArea.text      = vision.defaultSystemPrompt;      break
        case 4: chatArea.text        = chat.defaultSystemPrompt;        break
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            TabButton { text: qsTr("Summary") }
            TabButton { text: qsTr("Translation") }
            TabButton { text: qsTr("TOC") }
            TabButton { text: qsTr("Vision") }
            TabButton { text: qsTr("Chat") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            Item {  // Summary
                Component.onCompleted: {}
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: "#666"
                        text: qsTr("System prompt for the Interpret command. " +
                                   "Variable: {{lang}} → target language. " +
                                   "Leave empty to use the built-in default.")
                    }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: summaryArea
                            wrapMode: TextEdit.Wrap
                            placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            font.family: "monospace"
                        }
                    }
                }
            }

            Item {  // Translation
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: "#666"
                        text: qsTr("System prompt for per-paragraph translation. " +
                                   "Variable: {{lang}} → target language. " +
                                   "Leave empty to use the built-in default.")
                    }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: translationArea
                            wrapMode: TextEdit.Wrap
                            placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            font.family: "monospace"
                        }
                    }
                }
            }

            Item {  // TOC
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: "#666"
                        text: qsTr("System prompt for TOC extraction. " +
                                   "No variables. Output must be JSON only. " +
                                   "Leave empty to use the built-in default.")
                    }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: tocArea
                            wrapMode: TextEdit.Wrap
                            placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            font.family: "monospace"
                        }
                    }
                }
            }

            Item {  // Vision
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: "#666"
                        text: qsTr("System prompt for the Read-page-with-vision command. " +
                                   "No variables. " +
                                   "Leave empty to use the built-in default.")
                    }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: visionArea
                            wrapMode: TextEdit.Wrap
                            placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            font.family: "monospace"
                        }
                    }
                }
            }

            Item {  // Chat
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: "#666"
                        text: qsTr("System prompt for the Chat pane. The paper file " +
                                   "name, page count, and (when generated) the TOC " +
                                   "are appended automatically. No variables. " +
                                   "Leave empty to use the built-in default.")
                    }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: chatArea
                            wrapMode: TextEdit.Wrap
                            placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            font.family: "monospace"
                        }
                    }
                    CheckBox {
                        id: chatIncludeCheck
                        Layout.fillWidth: true
                        text: qsTr("Append full paper text to the chat system prompt " +
                                   "(truncated to ≈70% of the configured context window)")
                    }
                }
            }
        }
    }
}
