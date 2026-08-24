import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

AppDialog {
    id: dialog
    title: qsTr("Prompts")
    standardButtons: Dialog.Ok | Dialog.Cancel | Dialog.Reset
    width: Math.min(760, parent ? parent.width - 48 : 760)
    height: Math.min(620, parent ? parent.height - 48 : 620)

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

    // ── Shared dialog chrome ────────────────────────────────────────
    component ActionButton: AppButton {}

    // Underline-style tab: quiet text, accent indicator when current.
    component PromptTab: AppTabButton {}

    component PromptHint: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 11
        lineHeight: 1.25
        color: Theme.dimText
    }

    // Card wrapping an editor; brightens its border while the editor
    // has focus.
    component EditorCard: Rectangle {
        property var editor: null
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: Theme.radiusM
        color: Theme.fieldBg
        border.width: 1
        border.color: editor && editor.activeFocus ? Theme.accent : Theme.fieldBorder
        clip: true
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }

    // ── Content ─────────────────────────────────────────────────────
    contentItem: ColumnLayout {
        spacing: Theme.spaceM

        AppTabBar {
            id: tabBar
            Layout.fillWidth: true
            spacing: Theme.spaceXs
            background: Rectangle {
                color: "transparent"
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.divider
                }
            }
            PromptTab { text: qsTr("Summary") }
            PromptTab { text: qsTr("Translation") }
            PromptTab { text: qsTr("TOC") }
            PromptTab { text: qsTr("Vision") }
            PromptTab { text: qsTr("Chat") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            Item {  // Summary
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spaceS
                    PromptHint {
                        text: qsTr("System prompt for the Interpret command. " +
                                   "Variable: {{lang}} → target language. " +
                                   "Leave empty to use the built-in default.")
                    }
                    EditorCard {
                        editor: summaryArea
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            TextArea {
                                id: summaryArea
                                wrapMode: TextEdit.Wrap
                                font.family: "monospace"
                                font.pixelSize: 12
                                color: Theme.text
                                placeholderTextColor: Theme.dimText
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.onAccent
                                background: null
                                padding: Theme.spaceM
                                placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            }
                        }
                    }
                }
            }

            Item {  // Translation
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spaceS
                    PromptHint {
                        text: qsTr("System prompt for per-paragraph translation. " +
                                   "Variable: {{lang}} → target language. " +
                                   "Leave empty to use the built-in default.")
                    }
                    EditorCard {
                        editor: translationArea
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            TextArea {
                                id: translationArea
                                wrapMode: TextEdit.Wrap
                                font.family: "monospace"
                                font.pixelSize: 12
                                color: Theme.text
                                placeholderTextColor: Theme.dimText
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.onAccent
                                background: null
                                padding: Theme.spaceM
                                placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            }
                        }
                    }
                }
            }

            Item {  // TOC
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spaceS
                    PromptHint {
                        text: qsTr("System prompt for TOC extraction. " +
                                   "No variables. Output must be JSON only. " +
                                   "Leave empty to use the built-in default.")
                    }
                    EditorCard {
                        editor: tocArea
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            TextArea {
                                id: tocArea
                                wrapMode: TextEdit.Wrap
                                font.family: "monospace"
                                font.pixelSize: 12
                                color: Theme.text
                                placeholderTextColor: Theme.dimText
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.onAccent
                                background: null
                                padding: Theme.spaceM
                                placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            }
                        }
                    }
                }
            }

            Item {  // Vision
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spaceS
                    PromptHint {
                        text: qsTr("System prompt for the Read-page-with-vision command. " +
                                   "No variables. " +
                                   "Leave empty to use the built-in default.")
                    }
                    EditorCard {
                        editor: visionArea
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            TextArea {
                                id: visionArea
                                wrapMode: TextEdit.Wrap
                                font.family: "monospace"
                                font.pixelSize: 12
                                color: Theme.text
                                placeholderTextColor: Theme.dimText
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.onAccent
                                background: null
                                padding: Theme.spaceM
                                placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            }
                        }
                    }
                }
            }

            Item {  // Chat
                ColumnLayout {
                    anchors.fill: parent
                    spacing: Theme.spaceS
                    PromptHint {
                        text: qsTr("System prompt for the Chat pane. The paper file " +
                                   "name, page count, and (when generated) the TOC " +
                                   "are appended automatically. No variables. " +
                                   "Leave empty to use the built-in default.")
                    }
                    EditorCard {
                        editor: chatArea
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            TextArea {
                                id: chatArea
                                wrapMode: TextEdit.Wrap
                                font.family: "monospace"
                                font.pixelSize: 12
                                color: Theme.text
                                placeholderTextColor: Theme.dimText
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.onAccent
                                background: null
                                padding: Theme.spaceM
                                placeholderText: qsTr("(empty ⇒ built-in default applies on next request)")
                            }
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
