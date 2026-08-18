import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Dialog {
    id: dialog
    title: qsTr("Prompts")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel | Dialog.Reset
    closePolicy: Popup.CloseOnEscape
    width: Math.min(760, parent ? parent.width - 48 : 760)
    height: Math.min(620, parent ? parent.height - 48 : 620)
    padding: Theme.dialogPadding
    topPadding: Theme.spaceL

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
    palette.window: Theme.dialogBg
    palette.windowText: Theme.text
    palette.base: Theme.fieldBg
    palette.text: Theme.text
    palette.button: Theme.buttonBg
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    palette.placeholderText: Theme.dimText

    background: Rectangle {
        color: Theme.dialogBg
        border.color: Theme.border
        border.width: 1
        radius: Theme.radiusL
        Rectangle {
            z: -1
            x: 0; y: 2
            width: parent.width
            height: parent.height
            radius: parent.radius + 1
            color: Theme.dialogShadow
        }
    }

    Overlay.modal: Rectangle {
        color: Theme.overlayDim
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100; easing.type: Easing.InCubic }
    }

    header: Item {
        implicitHeight: headerTitle.implicitHeight + Theme.spaceL + Theme.spaceM + 1
        Label {
            id: headerTitle
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.dialogPadding
            anchors.rightMargin: Theme.dialogPadding
            anchors.topMargin: Theme.spaceL
            text: dialog.title
            elide: Text.ElideRight
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.divider
        }
    }

    footer: DialogButtonBox {
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: Theme.spaceS
        leftPadding: Theme.dialogPadding
        rightPadding: Theme.dialogPadding
        topPadding: Theme.spaceM
        bottomPadding: Theme.spaceL
        delegate: ActionButton {
            primary: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
        }
        background: Rectangle {
            color: "transparent"
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.divider
            }
        }
    }

    component ActionButton: Button {
        id: ab
        property bool primary: false
        implicitHeight: Theme.controlH
        leftPadding: Theme.spaceL
        rightPadding: Theme.spaceL
        contentItem: Text {
            text: ab.text
            font.pixelSize: 13
            font.weight: ab.primary ? Font.DemiBold : Font.Normal
            color: ab.primary ? Theme.onPrimary : Theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            opacity: ab.enabled ? 1 : 0.45
        }
        background: Rectangle {
            radius: Theme.radiusS
            color: ab.primary
                   ? (ab.down ? Theme.primaryPressed : ab.hovered ? Theme.primaryHover : Theme.primaryBg)
                   : (ab.down ? Theme.buttonPressed : ab.hovered ? Theme.buttonHover : Theme.buttonBg)
            border.width: ab.primary ? 0 : 1
            border.color: ab.visualFocus ? Theme.accent : Theme.border
            opacity: ab.enabled ? 1 : 0.45
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    // Underline-style tab: quiet text, accent indicator when current.
    component PromptTab: TabButton {
        id: pt
        implicitHeight: 34
        contentItem: Text {
            text: pt.text
            font.pixelSize: 13
            font.weight: pt.checked ? Font.DemiBold : Font.Normal
            color: pt.checked ? Theme.accent
                   : (pt.hovered ? Theme.text : Theme.bodyText)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        background: Rectangle {
            color: "transparent"
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                height: 2
                color: pt.checked ? Theme.accent : "transparent"
            }
        }
    }

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

        TabBar {
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
