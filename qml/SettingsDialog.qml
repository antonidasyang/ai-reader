import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

AppDialog {
    id: dialog
    title: qsTr("Settings")
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: 600
    height: Math.min(implicitHeight, parent ? parent.height - 48 : 700)

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

    readonly property var chatSendKeys:
        ["enter", "ctrl-enter"]
    readonly property var chatSendKeyLabels: [
        qsTr("Enter sends (Shift+Enter = newline)"),
        qsTr("Ctrl+Enter sends (Enter = newline)")
    ]

    // Same providers as the main control, with a leading entry whose empty
    // code means "whatever the main provider above is set to".
    readonly property var translationProviderCodes:
        [""].concat(dialog.providerOptions)
    readonly property var translationProviderLabels:
        [qsTr("Same as the main provider")].concat(dialog.providerOptions)

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
        concurrencyField.value  = settings.translationConcurrency
        const lidx = languageCodes.indexOf(settings.uiLanguage)
        languageBox.currentIndex = lidx >= 0 ? lidx : 0
        const aidx = translationProviderCodes.indexOf(settings.translationProvider)
        translationProviderBox.currentIndex = aidx >= 0 ? aidx : 0
        translationModelField.text   = settings.translationModel
        translationBaseUrlField.text = settings.translationBaseUrl
        translationApiKeyField.text  = settings.translationApiKey
        analysisMaxTokensField.value = settings.analysisMaxTokens
        analysisConcurrencyField.value = settings.analysisConcurrency
        autoCheckBox.checked    = settings.autoCheckUpdates
        manifestUrlField.text   = settings.updateManifestUrl
        crashOptInBox.checked   = settings.crashReportsOptIn
        sharePaperDataBox.checked = settings.sharePaperData
        autoSegmentBox.checked   = settings.autoSegment
        grobidEnabledBox.checked = settings.grobidEnabled
        grobidUrlField.text      = settings.grobidUrl
        tocFontSizeField.value       = settings.tocFontSize
        summaryFontSizeField.value   = settings.summaryFontSize
        paragraphFontSizeField.value = settings.paragraphFontSize
        chatFontSizeField.value      = settings.chatFontSize
        const sidx = chatSendKeys.indexOf(settings.chatSendKey)
        chatSendKeyBox.currentIndex  = sidx >= 0 ? sidx : 0
        chatInputHeightField.value   = settings.chatInputHeight
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
        settings.translationConcurrency = concurrencyField.value
        settings.uiLanguage        = languageCodes[languageBox.currentIndex]
        settings.translationProvider = translationProviderCodes[translationProviderBox.currentIndex]
        settings.translationModel    = translationModelField.text.trim()
        settings.translationBaseUrl  = translationBaseUrlField.text.trim()
        settings.translationApiKey   = translationApiKeyField.text
        settings.analysisMaxTokens = analysisMaxTokensField.value
        settings.analysisConcurrency = analysisConcurrencyField.value
        settings.autoCheckUpdates  = autoCheckBox.checked
        settings.updateManifestUrl = manifestUrlField.text.trim()
        settings.crashReportsOptIn = crashOptInBox.checked
        settings.sharePaperData    = sharePaperDataBox.checked
        settings.autoSegment       = autoSegmentBox.checked
        settings.grobidEnabled     = grobidEnabledBox.checked
        settings.grobidUrl         = grobidUrlField.text.trim()
        settings.tocFontSize        = tocFontSizeField.value
        settings.summaryFontSize    = summaryFontSizeField.value
        settings.paragraphFontSize  = paragraphFontSizeField.value
        settings.chatFontSize       = chatFontSizeField.value
        settings.chatSendKey        = chatSendKeys[chatSendKeyBox.currentIndex]
        settings.chatInputHeight    = chatInputHeightField.value
    }

    // ── Shared dialog chrome ────────────────────────────────────────
    component ActionButton: AppButton {}

    component FieldText: AppTextField {}

    component FieldCombo: AppComboBox {}

    component FieldSpin: AppSpinBox {}

    component FormLabel: AppFormLabel {}

    component SectionLabel: AppSectionLabel {}

    component SectionCard: AppSectionCard {}

    component HintLabel: AppHintLabel {}

    // ── Content ─────────────────────────────────────────────────────
    contentItem: Flickable {
        id: flick
        implicitWidth: contentColumn.implicitWidth
        implicitHeight: contentColumn.implicitHeight
        contentWidth: width
        contentHeight: contentColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: contentColumn
            width: flick.width
            spacing: Theme.spaceM

            // ── Provider / model / generation settings ──────────────
            SectionLabel {
                text: qsTr("Model & language")
            }
            SectionCard {
                implicitHeight: apiGrid.implicitHeight + 2 * Theme.spaceL

                GridLayout {
                    id: apiGrid
                    anchors.fill: parent
                    anchors.margins: Theme.spaceL
                    columns: 2
                    columnSpacing: Theme.spaceL
                    rowSpacing: Theme.spaceS

                    FormLabel { text: qsTr("Provider") }
                    FieldCombo {
                        id: providerBox
                        Layout.fillWidth: true
                        model: dialog.providerOptions
                    }

                    FormLabel { text: qsTr("Model") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS

                        FieldCombo {
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
                        ActionButton {
                            id: fetchBtn
                            text: settings.fetchingModels ? qsTr("Fetching…") : qsTr("Fetch")
                            enabled: !settings.fetchingModels && apiKeyField.text.length > 0
                            onClicked: settings.fetchModels(
                                dialog.providerOptions[providerBox.currentIndex],
                                baseUrlField.text,
                                apiKeyField.text)
                        }
                    }

                    FormLabel { text: qsTr("Base URL") }
                    FieldText {
                        id: baseUrlField
                        Layout.fillWidth: true
                        placeholderText: providerBox.currentText === "anthropic"
                                         ? qsTr("https://api.anthropic.com (default)")
                                         : providerBox.currentText === "deepseek"
                                           ? "https://api.deepseek.com"
                                           : providerBox.currentText === "openai-compatible"
                                             ? "http://localhost:8080"
                                             : qsTr("https://api.openai.com (default)")
                    }

                    FormLabel { text: qsTr("API key") }
                    FieldText {
                        id: apiKeyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: qsTr("sk-…")
                    }

                    FormLabel { text: qsTr("Temperature") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM
                        Slider {
                            id: tempSlider
                            Layout.fillWidth: true
                            from: 0; to: 1; stepSize: 0.05
                        }
                        Label {
                            text: tempSlider.value.toFixed(2)
                            color: Theme.text
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 36
                        }
                    }

                    FormLabel { text: qsTr("Max output tokens") }
                    FieldSpin {
                        id: maxTokensField
                        Layout.fillWidth: true
                        from: 256; to: 131072; stepSize: 256
                    }

                    FormLabel { text: qsTr("Context window") }
                    FieldSpin {
                        id: contextWindowField
                        Layout.fillWidth: true
                        from: 0; to: 2000000; stepSize: 1024
                    }

                    FormLabel { text: qsTr("Max tool calls per chat turn") }
                    FieldSpin {
                        id: toolBudgetField
                        Layout.fillWidth: true
                        from: 1; to: 100; stepSize: 1
                    }

                    FormLabel { text: qsTr("Translate into") }
                    FieldText {
                        id: targetLangField
                        Layout.fillWidth: true
                        placeholderText: "zh-CN"
                    }

                    FormLabel { text: qsTr("Paragraphs at once") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM
                        FieldSpin {
                            id: concurrencyField
                            from: 1; to: 16; stepSize: 1
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                            color: Theme.dimText
                            text: qsTr("Shared across every paper being translated. "
                                       + "Raising it is limited by what your provider "
                                       + "will accept.")
                        }
                    }

                    FormLabel { text: qsTr("UI language") }
                    FieldCombo {
                        id: languageBox
                        Layout.fillWidth: true
                        model: dialog.languageLabels
                    }
                }
            }

            // Status line for the model fetch.
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: text.length > 0
                font.pixelSize: 11
                color: settings.modelsError.length > 0 ? Theme.danger : Theme.success
                text: settings.modelsError.length > 0
                      ? settings.modelsError
                      : (settings.availableModels.length > 0
                         ? qsTr("Loaded %1 models.").arg(settings.availableModels.length)
                         : "")
            }

            HintLabel { text: settings.keychainStatus }

            HintLabel {
                text: qsTr("Settings are stored per-user in the OS-native QSettings " +
                           "location. The API key lives in the OS keychain " +
                           "(Keychain on macOS, Credential Manager on Windows, " +
                           "libsecret on Linux); when no keychain backend is " +
                           "available it falls back to plaintext QSettings.")
            }

            // ── Translation model ───────────────────────────────────
            // Translation is the odd job out: it runs on every paragraph of
            // every paper, so it wants something fast and cheap, while
            // reading, chatting and comparing want the best model there is.
            // Field by field, whatever is left blank here falls through to
            // the main configuration above.
            SectionLabel {
                text: qsTr("Translation model")
                Layout.topMargin: Theme.spaceS
            }
            SectionCard {
                implicitHeight: translationModelGrid.implicitHeight + 2 * Theme.spaceL

                GridLayout {
                    id: translationModelGrid
                    anchors.fill: parent
                    anchors.margins: Theme.spaceL
                    columns: 2
                    columnSpacing: Theme.spaceL
                    rowSpacing: Theme.spaceS

                    FormLabel { text: qsTr("Provider") }
                    FieldCombo {
                        id: translationProviderBox
                        Layout.fillWidth: true
                        model: dialog.translationProviderLabels
                    }

                    FormLabel { text: qsTr("Model") }
                    FieldText {
                        id: translationModelField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Same as the main model")
                    }

                    FormLabel { text: qsTr("Base URL") }
                    FieldText {
                        id: translationBaseUrlField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Same as the main base URL")
                    }

                    FormLabel { text: qsTr("API key") }
                    FieldText {
                        id: translationApiKeyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: qsTr("Same as the main API key")
                    }
                }
            }
            HintLabel {
                text: qsTr("The main model above does the reading: interpretation, "
                           + "chat, summaries and vision. Translation is the one job "
                           + "that can be pointed somewhere else — it runs on every "
                           + "paragraph, so a fast, cheap model usually serves it "
                           + "better. Leave a field blank to use the main setting.")
            }
            HintLabel {
                text: qsTr("Translation will run on: %1").arg(settings.translationModelInUse)
            }

            // ── Interpretation ──────────────────────────────────────
            SectionLabel {
                text: qsTr("Interpretation")
                Layout.topMargin: Theme.spaceS
            }
            SectionCard {
                implicitHeight: analysisGrid.implicitHeight + 2 * Theme.spaceL

                GridLayout {
                    id: analysisGrid
                    anchors.fill: parent
                    anchors.margins: Theme.spaceL
                    columns: 2
                    columnSpacing: Theme.spaceL
                    rowSpacing: Theme.spaceS

                    FormLabel { text: qsTr("Max output tokens") }
                    FieldSpin {
                        id: analysisMaxTokensField
                        Layout.fillWidth: true
                        from: 512; to: 64000; stepSize: 512
                    }

                    FormLabel { text: qsTr("Parallel interpretations") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM
                        FieldSpin {
                            id: analysisConcurrencyField
                            from: 1; to: 8; stepSize: 1
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                            color: Theme.dimText
                            text: qsTr("How many papers a batch interpretation "
                                       + "reads at once.")
                        }
                    }
                }
            }
            HintLabel {
                text: qsTr("Interpretation runs on the main model. A close reading "
                           + "is nine separate requests, so give it room to answer.")
            }

            // ── Font sizes ──────────────────────────────────────────
            // Per-pane body font size; headings/labels in each pane scale
            // up relative to the value below. Range 8–32 px matches the
            // qBound() guard in Settings.cpp.
            SectionLabel {
                text: qsTr("Font sizes (px)")
                Layout.topMargin: Theme.spaceS
            }
            SectionCard {
                implicitHeight: fontGrid.implicitHeight + 2 * Theme.spaceL

                GridLayout {
                    id: fontGrid
                    anchors.fill: parent
                    anchors.margins: Theme.spaceL
                    columns: 4
                    columnSpacing: Theme.spaceL
                    rowSpacing: Theme.spaceS

                    FormLabel { text: qsTr("Chapter menu") }
                    FieldSpin {
                        id: tocFontSizeField
                        Layout.fillWidth: true
                        from: 8; to: 32; stepSize: 1
                    }

                    FormLabel { text: qsTr("Interpretation") }
                    FieldSpin {
                        id: summaryFontSizeField
                        Layout.fillWidth: true
                        from: 8; to: 32; stepSize: 1
                    }

                    FormLabel { text: qsTr("Paragraphs") }
                    FieldSpin {
                        id: paragraphFontSizeField
                        Layout.fillWidth: true
                        from: 8; to: 32; stepSize: 1
                    }

                    FormLabel { text: qsTr("Chat") }
                    FieldSpin {
                        id: chatFontSizeField
                        Layout.fillWidth: true
                        from: 8; to: 32; stepSize: 1
                    }
                }
            }

            // ── Chat input ──────────────────────────────────────────
            SectionLabel {
                text: qsTr("Chat input")
                Layout.topMargin: Theme.spaceS
            }
            SectionCard {
                implicitHeight: chatInputGrid.implicitHeight + 2 * Theme.spaceL

                GridLayout {
                    id: chatInputGrid
                    anchors.fill: parent
                    anchors.margins: Theme.spaceL
                    columns: 2
                    columnSpacing: Theme.spaceL
                    rowSpacing: Theme.spaceS

                    FormLabel { text: qsTr("Send with") }
                    FieldCombo {
                        id: chatSendKeyBox
                        Layout.fillWidth: true
                        model: dialog.chatSendKeyLabels
                    }

                    FormLabel { text: qsTr("Input box height (px)") }
                    FieldSpin {
                        id: chatInputHeightField
                        Layout.fillWidth: true
                        from: 36; to: 400; stepSize: 8
                    }
                }
            }

            // ── Paragraph segmentation ──────────────────────────────
            SectionLabel {
                text: qsTr("Paragraph segmentation")
                Layout.topMargin: Theme.spaceS
            }
            SectionCard {
                implicitHeight: grobidGrid.implicitHeight + 2 * Theme.spaceL

                GridLayout {
                    id: grobidGrid
                    anchors.fill: parent
                    anchors.margins: Theme.spaceL
                    columns: 2
                    columnSpacing: Theme.spaceL
                    rowSpacing: Theme.spaceS

                    FormLabel { text: qsTr("On open") }
                    CheckBox {
                        id: autoSegmentBox
                        text: qsTr("Segment a paper automatically the first time it is opened")
                    }

                    FormLabel { text: qsTr("GROBID service") }
                    CheckBox {
                        id: grobidEnabledBox
                        text: qsTr("Use GROBID for paragraph detection (best for academic papers)")
                    }

                    FormLabel { text: qsTr("Service URL") }
                    FieldText {
                        id: grobidUrlField
                        Layout.fillWidth: true
                        enabled: grobidEnabledBox.checked
                        placeholderText: "https://aireader.d2ssoft.com/grobid"
                    }
                }
            }
            HintLabel {
                text: qsTr("Off by default, since segmenting a long paper costs seconds of "
                           + "work a reader who only wants to page through it never asked "
                           + "for — press Segment in the toolbar when you want paragraphs. "
                           + "GROBID is applied to whichever run does the segmenting; it "
                           + "falls back to the built-in splitter when the service is "
                           + "unreachable. Self-host with: "
                           + "docker run -d -p 8070:8070 grobid/grobid:0.9.1-crf")
            }

            // ── Updates & privacy ───────────────────────────────────
            SectionLabel {
                text: qsTr("Updates & privacy")
                Layout.topMargin: Theme.spaceS
            }
            SectionCard {
                implicitHeight: updatesGrid.implicitHeight + 2 * Theme.spaceL

                GridLayout {
                    id: updatesGrid
                    anchors.fill: parent
                    anchors.margins: Theme.spaceL
                    columns: 2
                    columnSpacing: Theme.spaceL
                    rowSpacing: Theme.spaceS

                    FormLabel { text: qsTr("Auto-check for updates") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS
                        CheckBox {
                            id: autoCheckBox
                            text: qsTr("Check on launch")
                        }
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            text: updates.checking ? qsTr("Checking…") : qsTr("Check now")
                            enabled: !updates.checking
                            onClicked: updates.checkNow()
                        }
                    }

                    FormLabel { text: qsTr("Manifest URL") }
                    FieldText {
                        id: manifestUrlField
                        Layout.fillWidth: true
                        placeholderText: "https://aireader.d2ssoft.com/update/manifest"
                    }

                    FormLabel { text: qsTr("Crash reports") }
                    CheckBox {
                        id: crashOptInBox
                        text: qsTr("Send anonymous crash reports (off by default)")
                    }

                    FormLabel { text: qsTr("Share with project") }
                    CheckBox {
                        id: sharePaperDataBox
                        text: qsTr("Upload paragraph segmentation and translations")
                    }
                }
            }
            HintLabel {
                text: qsTr("Segmenting and translating a paper costs CPU seconds and "
                           + "model tokens. Shared, that work is done once: your own "
                           + "other machines get it back automatically, and so does "
                           + "anyone in the research project who hasn't done it "
                           + "themselves. What you segment or translate yourself always "
                           + "wins over anything pulled down. Turn this off to keep a "
                           + "paper's text on this machine.")
            }

            // Check-result row. The download action lives HERE: the
            // window-bottom banner is dimmed behind this modal dialog,
            // so "see the banner" looked like the check did nothing.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceM
                visible: statusLabel.text.length > 0
                Label {
                    id: statusLabel
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: updates.lastError.length > 0 ? Theme.danger
                                                        : Theme.dimText
                    font.pixelSize: 11
                    text: updates.lastError.length > 0
                          ? qsTr("Update check failed: %1").arg(updates.lastError)
                          : (updates.latestVersion.length > 0
                             ? (updates.updateAvailable
                                ? qsTr("v%1 is available.").arg(updates.latestVersion)
                                : qsTr("You're on the latest version (v%1).").arg(updates.latestVersion))
                             : "")
                }
                ActionButton {
                    visible: updates.updateAvailable
                             && updates.lastError.length === 0
                    enabled: !updates.downloading && !updates.installing
                    primary: true
                    text: updates.installing
                          ? qsTr("Restarting…")
                          : updates.downloading
                            ? qsTr("Downloading… %1%")
                                  .arg(Math.round(updates.downloadProgress * 100))
                            : qsTr("Update to v%1").arg(updates.latestVersion)
                    onClicked: updates.downloadAndInstall()
                }
            }

            // ── Version footer ──────────────────────────────────────
            // settings.appVersion is sourced from AIREADER_VERSION baked in
            // via CMake's target_compile_definitions, so it tracks
            // PROJECT_VERSION automatically — no second number to keep in
            // sync with CMakeLists.txt.
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spaceXs
                implicitHeight: 1
                color: Theme.divider
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceS
                Label {
                    text: qsTr("AI Reader")
                    font.pixelSize: 11
                    color: Theme.dimText
                }
                Label {
                    text: "v" + settings.appVersion
                    font.pixelSize: 11
                    font.bold: true
                    color: Theme.accent
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("github.com/antonidasyang/ai-reader")
                    font.pixelSize: 11
                    color: Theme.dimText
                }
            }
        }
    }
}
