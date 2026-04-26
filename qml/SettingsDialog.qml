import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape
    width: 540

    readonly property var providerOptions:
        ["anthropic", "openai", "deepseek", "openai-compatible"]

    onOpened: {
        const idx = providerOptions.indexOf(settings.provider)
        providerBox.currentIndex = idx >= 0 ? idx : 0
        modelField.text       = settings.model
        baseUrlField.text     = settings.baseUrl
        apiKeyField.text      = settings.apiKey
        tempSlider.value      = settings.temperature
        targetLangField.text  = settings.targetLang
        apiKeyField.forceActiveFocus()
    }

    onAccepted: {
        settings.provider    = providerOptions[providerBox.currentIndex]
        settings.model       = modelField.text.trim()
        settings.baseUrl     = baseUrlField.text.trim()
        settings.apiKey      = apiKeyField.text
        settings.temperature = tempSlider.value
        settings.targetLang  = targetLangField.text.trim()
    }

    contentItem: ColumnLayout {
        spacing: 10

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8
            Layout.fillWidth: true

            Label { text: qsTr("Provider") }
            ComboBox {
                id: providerBox
                Layout.fillWidth: true
                model: dialog.providerOptions
            }

            Label { text: qsTr("Model") }
            TextField {
                id: modelField
                Layout.fillWidth: true
                placeholderText: providerBox.currentText === "anthropic"
                                 ? "claude-opus-4-7"
                                 : providerBox.currentText === "deepseek"
                                   ? "deepseek-chat"
                                   : "gpt-4o"
            }

            Label { text: qsTr("Base URL") }
            TextField {
                id: baseUrlField
                Layout.fillWidth: true
                placeholderText: providerBox.currentText === "anthropic"
                                 ? "https://api.anthropic.com (default)"
                                 : providerBox.currentText === "deepseek"
                                   ? "https://api.deepseek.com"
                                   : providerBox.currentText === "openai-compatible"
                                     ? "http://localhost:8080"
                                     : "https://api.openai.com (default)"
            }

            Label { text: qsTr("API key") }
            TextField {
                id: apiKeyField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("sk-…")
            }

            Label { text: qsTr("Temperature") }
            RowLayout {
                Layout.fillWidth: true
                Slider {
                    id: tempSlider
                    Layout.fillWidth: true
                    from: 0; to: 1; stepSize: 0.05
                }
                Label {
                    text: tempSlider.value.toFixed(2)
                    Layout.preferredWidth: 36
                }
            }

            Label { text: qsTr("Translate into") }
            TextField {
                id: targetLangField
                Layout.fillWidth: true
                placeholderText: "zh-CN"
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#888"
            font.pixelSize: 11
            text: qsTr("Settings are stored per-user in the OS-native QSettings " +
                       "location. The API key is currently saved in plain text; " +
                       "QtKeychain integration is planned.")
        }
    }
}
