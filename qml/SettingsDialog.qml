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

    // Parallel arrays — display label shown in the combo, code persisted
    // to QSettings. Empty code = follow QLocale::system().
    readonly property var languageCodes:
        ["", "en", "zh_CN"]
    readonly property var languageLabels: [
        qsTr("System default"),
        qsTr("English"),
        qsTr("中文 (Simplified)")
    ]

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
        const lidx = languageCodes.indexOf(settings.uiLanguage)
        languageBox.currentIndex = lidx >= 0 ? lidx : 0
        autoCheckBox.checked    = settings.autoCheckUpdates
        manifestUrlField.text   = settings.updateManifestUrl
        crashOptInBox.checked   = settings.crashReportsOptIn
        tocFontSizeField.value       = settings.tocFontSize
        summaryFontSizeField.value   = settings.summaryFontSize
        paragraphFontSizeField.value = settings.paragraphFontSize
        chatFontSizeField.value      = settings.chatFontSize
        apiKeyField.forceActiveFocus()
    }

    onAccepted: {
        settings.provider          = providerOptions[providerBox.currentIndex]
        settings.model             = modelBox.editText.trim()
        settings.baseUrl           = baseUrlField.text.trim()
        settings.apiKey            = apiKeyField.text
        settings.temperature       = tempSlider.value
        settings.maxTokens         = maxTokensField.value
        settings.contextWindow     = contextWindowField.value
        settings.toolBudget        = toolBudgetField.value
        settings.targetLang        = targetLangField.text.trim()
        settings.uiLanguage        = languageCodes[languageBox.currentIndex]
        settings.autoCheckUpdates  = autoCheckBox.checked
        settings.updateManifestUrl = manifestUrlField.text.trim()
        settings.crashReportsOptIn = crashOptInBox.checked
        settings.tocFontSize        = tocFontSizeField.value
        settings.summaryFontSize    = summaryFontSizeField.value
        settings.paragraphFontSize  = paragraphFontSizeField.value
        settings.chatFontSize       = chatFontSizeField.value
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

            Label { text: qsTr("UI language") }
            ComboBox {
                id: languageBox
                Layout.fillWidth: true
                model: dialog.languageLabels
            }
        }

        // ── Font sizes ──────────────────────────────────────────────
        // Per-pane body font size; headings/labels in each pane scale
        // up relative to the value below. Range 8–32 px matches the
        // qBound() guard in Settings.cpp.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: 4
            color: "#e0e0e0"
        }
        Label {
            text: qsTr("Font sizes (px)")
            font.bold: true
            font.pixelSize: 12
            color: "#444"
            Layout.topMargin: 4
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 6
            Layout.fillWidth: true

            Label { text: qsTr("Chapter menu") }
            SpinBox {
                id: tocFontSizeField
                Layout.fillWidth: true
                from: 8; to: 32; stepSize: 1; editable: true
            }

            Label { text: qsTr("Interpretation") }
            SpinBox {
                id: summaryFontSizeField
                Layout.fillWidth: true
                from: 8; to: 32; stepSize: 1; editable: true
            }

            Label { text: qsTr("Paragraphs") }
            SpinBox {
                id: paragraphFontSizeField
                Layout.fillWidth: true
                from: 8; to: 32; stepSize: 1; editable: true
            }

            Label { text: qsTr("Chat") }
            SpinBox {
                id: chatFontSizeField
                Layout.fillWidth: true
                from: 8; to: 32; stepSize: 1; editable: true
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
            visible: text.length > 0
            font.pixelSize: 11
            color: "#555"
            text: settings.keychainStatus
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#888"
            font.pixelSize: 11
            text: qsTr("Settings are stored per-user in the OS-native QSettings " +
                       "location. The API key lives in the OS keychain " +
                       "(Keychain on macOS, Credential Manager on Windows, " +
                       "libsecret on Linux); when no keychain backend is " +
                       "available it falls back to plaintext QSettings.")
        }

        // ── Updates & privacy ──────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: 4
            color: "#e0e0e0"
        }
        Label {
            text: qsTr("Updates & privacy")
            font.bold: true
            font.pixelSize: 12
            color: "#444"
            Layout.topMargin: 4
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 6
            Layout.fillWidth: true

            Label { text: qsTr("Auto-check for updates") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                CheckBox {
                    id: autoCheckBox
                    text: qsTr("Check on launch")
                }
                Button {
                    text: updates.checking ? qsTr("Checking…") : qsTr("Check now")
                    enabled: !updates.checking
                    onClicked: updates.checkNow()
                }
            }

            Label { text: qsTr("Manifest URL") }
            TextField {
                id: manifestUrlField
                Layout.fillWidth: true
                placeholderText: "https://raw.githubusercontent.com/antonidasyang/ai-reader/main/manifest.json"
            }

            Label { text: qsTr("Crash reports") }
            CheckBox {
                id: crashOptInBox
                text: qsTr("Send anonymous crash reports (off by default)")
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: updates.lastError.length > 0 ? "#c62828" : "#666"
            font.pixelSize: 11
            visible: text.length > 0
            text: updates.lastError.length > 0
                  ? qsTr("Update check failed: %1").arg(updates.lastError)
                  : (updates.latestVersion.length > 0
                     ? (updates.updateAvailable
                        ? qsTr("v%1 is available — see the banner at the bottom of the window.")
                              .arg(updates.latestVersion)
                        : qsTr("You're on the latest version (v%1).").arg(updates.latestVersion))
                     : "")
        }

        // ── Version footer ──────────────────────────────────────────
        // settings.appVersion is sourced from AIREADER_VERSION baked in
        // via CMake's target_compile_definitions, so it tracks
        // PROJECT_VERSION automatically — no second number to keep in
        // sync with CMakeLists.txt.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: 6
            color: "#e0e0e0"
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("AI Reader")
                font.pixelSize: 11
                color: "#666"
            }
            Label {
                text: "v" + settings.appVersion
                font.pixelSize: 11
                font.bold: true
                color: "#3949AB"
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("github.com/antonidasyang/ai-reader")
                font.pixelSize: 11
                color: "#666"
            }
        }
    }
}
