import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape
    width: 580

    readonly property var providerOptions:
        ["anthropic", "openai", "deepseek", "openai-compatible"]

    onOpened: {
        const idx = providerOptions.indexOf(settings.provider)
        providerBox.currentIndex = idx >= 0 ? idx : 0
        modelBox.assign(settings.model)
        baseUrlField.text       = settings.baseUrl
        apiKeyField.text        = settings.apiKey
        tempSlider.value        = settings.temperature
        maxTokensField.value    = settings.maxTokens
        contextWindowField.value = settings.contextWindow
        toolBudgetField.value   = settings.toolBudget
        targetLangField.text    = settings.targetLang
        apiKeyField.forceActiveFocus()
    }

    onAccepted: {
        settings.provider      = providerOptions[providerBox.currentIndex]
        settings.model         = modelBox.editText.trim()
        settings.baseUrl       = baseUrlField.text.trim()
        settings.apiKey        = apiKeyField.text
        settings.temperature   = tempSlider.value
        settings.maxTokens     = maxTokensField.value
        settings.contextWindow = contextWindowField.value
        settings.toolBudget    = toolBudgetField.value
        settings.targetLang    = targetLangField.text.trim()
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
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                ComboBox {
                    id: modelBox
                    Layout.fillWidth: true
                    editable: true
                    model: settings.availableModels
                    // The model list might not contain the saved value
                    // (free-typed, or fetched after save). assign() syncs
                    // both editText and currentIndex.
                    function assign(value) {
                        editText = value || ""
                        const idx = settings.availableModels.indexOf(value)
                        currentIndex = idx
                    }
                }
                Button {
                    id: fetchBtn
                    text: settings.fetchingModels ? qsTr("Fetching…") : qsTr("Fetch")
                    enabled: !settings.fetchingModels && apiKeyField.text.length > 0
                    onClicked: settings.fetchModels(
                        dialog.providerOptions[providerBox.currentIndex],
                        baseUrlField.text,
                        apiKeyField.text)
                }
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

            Label { text: qsTr("Max output tokens") }
            SpinBox {
                id: maxTokensField
                Layout.fillWidth: true
                from: 256; to: 131072; stepSize: 256; editable: true
            }

            Label { text: qsTr("Context window") }
            SpinBox {
                id: contextWindowField
                Layout.fillWidth: true
                from: 0; to: 2000000; stepSize: 1024; editable: true
            }

            Label { text: qsTr("Max tool calls per chat turn") }
            SpinBox {
                id: toolBudgetField
                Layout.fillWidth: true
                from: 1; to: 100; stepSize: 1; editable: true
            }

            Label { text: qsTr("Translate into") }
            TextField {
                id: targetLangField
                Layout.fillWidth: true
                placeholderText: "zh-CN"
            }
        }

        // Status line for the model fetch.
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: text.length > 0
            font.pixelSize: 11
            color: settings.modelsError.length > 0 ? "#c62828" : "#2e7d32"
            text: settings.modelsError.length > 0
                  ? settings.modelsError
                  : (settings.availableModels.length > 0
                     ? qsTr("Loaded %1 models.").arg(settings.availableModels.length)
                     : "")
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
