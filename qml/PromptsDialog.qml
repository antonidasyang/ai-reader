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

    onOpened: {
        summaryArea.text     = settings.summaryPrompt
        translationArea.text = settings.translationPrompt
        tocArea.text         = settings.tocPrompt
        visionArea.text      = settings.visionPrompt
    }

    onAccepted: {
        settings.summaryPrompt     = summaryArea.text
        settings.translationPrompt = translationArea.text
        settings.tocPrompt         = tocArea.text
        settings.visionPrompt      = visionArea.text
    }

    onReset: {
        // Clearing the field reverts the service to its built-in default
        // on next request.
        switch (tabBar.currentIndex) {
        case 0: summaryArea.text = ""; break
        case 1: translationArea.text = ""; break
        case 2: tocArea.text = ""; break
        case 3: visionArea.text = ""; break
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
                            placeholderText: qsTr("(uses built-in default)")
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
                        text: qsTr("System prompt for per-block translation. " +
                                   "Variable: {{lang}} → target language. " +
                                   "Leave empty to use the built-in default.")
                    }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: translationArea
                            wrapMode: TextEdit.Wrap
                            placeholderText: qsTr("(uses built-in default)")
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
                            placeholderText: qsTr("(uses built-in default)")
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
                            placeholderText: qsTr("(uses built-in default)")
                            font.family: "monospace"
                        }
                    }
                }
            }
        }
    }
}
