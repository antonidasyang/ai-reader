import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Settings, as a list of subjects rather than one long scroll.
//
// The old single column had grown to seven headings deep, and things that
// have nothing to do with the model — fonts, the chat input, segmentation,
// updates — sat in the same card as the API key because that card came
// first. Now each subject is a page, and the model page holds the model.
AppDialog {
    id: dialog
    title: qsTr("Settings")
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: 820
    height: Math.min(620, parent ? parent.height - 48 : 620)

    readonly property var providerOptions:
        ["anthropic", "openai", "deepseek", "openai-compatible"]

    // The three named providers have one endpoint each and it is not the
    // reader's to type; only "openai-compatible" has an address of its own.
    readonly property string providerNow:
        providerOptions[providerBox.currentIndex]
    readonly property bool customUrlAllowed:
        settings.providerTakesCustomUrl(dialog.providerNow)

    // Parallel arrays — display label shown in the combo, code persisted
    // to QSettings. Empty code = follow QLocale::system().
    readonly property var languageCodes:
        ["", "en", "zh_CN"]
    readonly property var languageLabels: [
        qsTr("System default"),
        qsTr("English"),
        qsTr("中文 (Simplified)")
    ]

    readonly property var remoteModes: ["auto", "on", "off"]
    readonly property var remoteModeLabels: [
        qsTr("Automatic (detect a remote desktop)"),
        qsTr("Always"),
        qsTr("Never")
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

    // What the translation section resolves to, so its Fetch probes the
    // endpoint translation will really use.
    readonly property string translationProviderResolved:
        translationProviderCodes[translationProviderBox.currentIndex]
        || dialog.providerNow
    readonly property bool translationCustomUrlAllowed:
        settings.providerTakesCustomUrl(dialog.translationProviderResolved)
    readonly property string translationBaseUrlResolved:
        dialog.translationCustomUrlAllowed
        ? (translationBaseUrlField.text.trim() || baseUrlField.text.trim())
        : settings.officialBaseUrl(dialog.translationProviderResolved)
    readonly property string translationApiKeyResolved:
        translationApiKeyField.text
        || (translationBaseUrlField.text.trim().length === 0
            || translationBaseUrlField.text.trim() === baseUrlField.text.trim()
            ? apiKeyField.text : "")

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
        translationModelBox.assign(settings.translationModel)
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
        chatSendKeyBox.currentIndex  = chatSendKeys.indexOf(settings.chatSendKey) >= 0
                                       ? chatSendKeys.indexOf(settings.chatSendKey) : 0
        chatInputHeightField.value   = settings.chatInputHeight
        remoteModeBox.currentIndex   = Math.max(0, remoteModes.indexOf(settings.remoteMode))
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
        settings.translationModel    = translationModelBox.editText.trim()
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
        settings.remoteMode         = remoteModes[remoteModeBox.currentIndex]
    }

    // ── Page scaffolding ────────────────────────────────────────────
    // Every page is the same shape: a scrolling column of cards, so a page
    // that outgrows the dialog scrolls on its own instead of stretching it.
    component Page: ScrollView {
        default property alias content: pageColumn.data
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical: ScrollBar { }
        ColumnLayout {
            id: pageColumn
            width: parent.width
            spacing: Theme.spaceM
        }
    }

    component Card: AppSectionCard {
        default property alias content: cardBody.data
        implicitHeight: cardBody.implicitHeight + 2 * Theme.spaceL
        GridLayout {
            id: cardBody
            anchors.fill: parent
            anchors.margins: Theme.spaceL
            columns: 2
            columnSpacing: Theme.spaceL
            rowSpacing: Theme.spaceS
        }
    }

    // The subject pages all scroll; the keys belong to whichever one is
    // showing, so they page that page rather than a fixed one.
    function currentFlickable() {
        const page = pageStack.children[pageStack.currentIndex]
        return page ? page.contentItem : null
    }

    readonly property var pageTitles: [
        qsTr("Model"),
        qsTr("Translation"),
        qsTr("Interpretation & chat"),
        qsTr("Appearance"),
        qsTr("Documents"),
        qsTr("Updates & privacy")
    ]

    contentItem: RowLayout {
        spacing: Theme.spaceL

        // Page/Home/End walk the open subject page.
        focus: true
        Keys.onPressed: (event) => ScrollKeys.handle(event, dialog.currentFlickable())

        // ── The subjects ────────────────────────────────────────────
        ColumnLayout {
            Layout.preferredWidth: 168
            Layout.fillHeight: true
            spacing: Theme.spaceS

            ListView {
                id: nav
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: dialog.pageTitles
                currentIndex: 0
                spacing: 2
                delegate: ItemDelegate {
                    required property int index
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 0
                    height: Theme.controlH
                    onClicked: nav.currentIndex = index
                    contentItem: Text {
                        text: modelData
                        font.pixelSize: 13
                        font.weight: nav.currentIndex === index ? Font.DemiBold
                                                                : Font.Normal
                        color: nav.currentIndex === index ? Theme.onPrimary
                               : (hovered ? Theme.text : Theme.bodyText)
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        Behavior on color { ColorAnimation { duration: Theme.animMs } }
                    }
                    background: Rectangle {
                        radius: Theme.radiusS
                        color: nav.currentIndex === index
                               ? Theme.primaryBg
                               : (hovered ? Theme.buttonHover : "transparent")
                        Behavior on color { ColorAnimation { duration: Theme.animMs } }
                    }
                }
            }

            // The version lives here rather than at the bottom of a page:
            // it belongs to the app, not to any one subject.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 1
                color: Theme.divider
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceXs
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
            }
        }

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 1
            color: Theme.divider
        }

        StackLayout {
            id: pageStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: nav.currentIndex

            // ── 1 · Model ───────────────────────────────────────────
            Page {
                AppSectionLabel { text: qsTr("The model that reads") }
                Card {
                    AppFormLabel { text: qsTr("Provider") }
                    AppComboBox {
                        id: providerBox
                        Layout.fillWidth: true
                        model: dialog.providerOptions
                    }

                    AppFormLabel { text: qsTr("Model") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS
                        AppComboBox {
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
                        AppButton {
                            text: settings.fetchingModels ? qsTr("Fetching…") : qsTr("Fetch")
                            enabled: !settings.fetchingModels
                                     && apiKeyField.text.length > 0
                                     && (!dialog.customUrlAllowed
                                         || baseUrlField.text.trim().length > 0)
                            onClicked: settings.fetchModels(
                                dialog.providerOptions[providerBox.currentIndex],
                                baseUrlField.text,
                                apiKeyField.text)
                        }
                    }

                    AppFormLabel { text: qsTr("Base URL") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS
                        // Editable only where it means something. For a named
                        // provider the endpoint is shown, not typed — a URL
                        // left behind by an earlier provider used to keep
                        // being used, and every request went to the wrong
                        // server with no way to tell.
                        AppTextField {
                            id: baseUrlField
                            Layout.fillWidth: true
                            visible: dialog.customUrlAllowed
                            placeholderText: qsTr("http://localhost:8080")
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !dialog.customUrlAllowed
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            color: Theme.dimText
                            text: settings.officialBaseUrl(dialog.providerNow)
                        }
                    }

                    AppFormLabel { text: qsTr("API key") }
                    AppTextField {
                        id: apiKeyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: qsTr("sk-…")
                    }

                    AppFormLabel { text: qsTr("Temperature") }
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

                    AppFormLabel { text: qsTr("Max output tokens") }
                    AppSpinBox {
                        id: maxTokensField
                        Layout.fillWidth: true
                        from: 256; to: 131072; stepSize: 256
                    }

                    AppFormLabel { text: qsTr("Context window") }
                    AppSpinBox {
                        id: contextWindowField
                        Layout.fillWidth: true
                        from: 0; to: 2000000; stepSize: 1024
                    }
                }

                AppHintLabel {
                    visible: !dialog.customUrlAllowed
                    text: qsTr("The endpoint follows the provider. Pick "
                               + "“openai-compatible” to point at a gateway of "
                               + "your own.")
                }
                AppHintLabel {
                    text: qsTr("This model does the reading: interpretation, the "
                               + "close reading, the project-wide analyses, chat, "
                               + "summaries and vision. Only translation can be "
                               + "pointed somewhere else.")
                }

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
                AppHintLabel { text: settings.keychainStatus }
                AppHintLabel {
                    text: qsTr("Settings are stored per-user in the OS-native QSettings " +
                               "location. The API key lives in the OS keychain " +
                               "(Keychain on macOS, Credential Manager on Windows, " +
                               "libsecret on Linux); when no keychain backend is " +
                               "available it falls back to plaintext QSettings.")
                }
            }

            // ── 2 · Translation ─────────────────────────────────────
            Page {
                AppSectionLabel { text: qsTr("Translation model") }
                Card {
                    AppFormLabel { text: qsTr("Provider") }
                    AppComboBox {
                        id: translationProviderBox
                        Layout.fillWidth: true
                        model: dialog.translationProviderLabels
                    }

                    AppFormLabel { text: qsTr("Model") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS
                        AppComboBox {
                            id: translationModelBox
                            Layout.fillWidth: true
                            editable: true
                            model: settings.availableTranslationModels
                            function assign(value) {
                                editText = value || ""
                                const idx = settings.availableTranslationModels.indexOf(value)
                                currentIndex = idx
                            }
                        }
                        AppButton {
                            text: settings.fetchingTranslationModels
                                  ? qsTr("Fetching…") : qsTr("Fetch")
                            enabled: !settings.fetchingTranslationModels
                                     && dialog.translationApiKeyResolved.length > 0
                            onClicked: settings.fetchTranslationModels(
                                dialog.translationProviderResolved,
                                dialog.translationBaseUrlResolved,
                                dialog.translationApiKeyResolved)
                        }
                    }

                    AppFormLabel { text: qsTr("Base URL") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS
                        AppTextField {
                            id: translationBaseUrlField
                            Layout.fillWidth: true
                            visible: dialog.translationCustomUrlAllowed
                            placeholderText: qsTr("Same as the main base URL")
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !dialog.translationCustomUrlAllowed
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            color: Theme.dimText
                            text: settings.officialBaseUrl(
                                      dialog.translationProviderResolved)
                        }
                    }

                    AppFormLabel { text: qsTr("API key") }
                    AppTextField {
                        id: translationApiKeyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: qsTr("Same as the main API key")
                    }
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    visible: text.length > 0
                    font.pixelSize: 11
                    color: settings.translationModelsError.length > 0 ? Theme.danger
                                                                      : Theme.success
                    text: settings.translationModelsError.length > 0
                          ? settings.translationModelsError
                          : (settings.availableTranslationModels.length > 0
                             ? qsTr("Loaded %1 models.")
                                   .arg(settings.availableTranslationModels.length)
                             : "")
                }
                AppHintLabel {
                    text: qsTr("Translation runs on every paragraph of every paper, so a "
                               + "fast, cheap model usually serves it better than the one "
                               + "doing the reading. Leave a field blank to use the main "
                               + "setting.")
                }
                AppHintLabel {
                    text: qsTr("Translation will run on: %1").arg(settings.translationModelInUse)
                }

                AppSectionLabel {
                    text: qsTr("How it translates")
                    Layout.topMargin: Theme.spaceS
                }
                Card {
                    AppFormLabel { text: qsTr("Translate into") }
                    AppTextField {
                        id: targetLangField
                        Layout.fillWidth: true
                        placeholderText: "zh-CN"
                    }

                    AppFormLabel { text: qsTr("Paragraphs at once") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM
                        AppSpinBox {
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
                }
            }

            // ── 3 · Interpretation & chat ───────────────────────────
            // Both are the model reading the paper and answering about it;
            // they were two pages holding three controls between them.
            Page {
                AppSectionLabel { text: qsTr("Interpretation") }
                Card {
                    AppFormLabel { text: qsTr("Max output tokens") }
                    AppSpinBox {
                        id: analysisMaxTokensField
                        Layout.fillWidth: true
                        from: 512; to: 64000; stepSize: 512
                    }

                    AppFormLabel { text: qsTr("Parallel interpretations") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceM
                        AppSpinBox {
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
                AppHintLabel {
                    text: qsTr("Interpretation runs on the main model. A close reading "
                               + "is nine separate requests, so give it room to answer.")
                }

                AppSectionLabel {
                    text: qsTr("Chat")
                    Layout.topMargin: Theme.spaceS
                }
                Card {
                    AppFormLabel { text: qsTr("Send with") }
                    AppComboBox {
                        id: chatSendKeyBox
                        Layout.fillWidth: true
                        model: dialog.chatSendKeyLabels
                    }

                    AppFormLabel { text: qsTr("Input box height (px)") }
                    AppSpinBox {
                        id: chatInputHeightField
                        Layout.fillWidth: true
                        from: 36; to: 400; stepSize: 8
                    }

                    AppFormLabel { text: qsTr("Max tool calls per chat turn") }
                    AppSpinBox {
                        id: toolBudgetField
                        Layout.fillWidth: true
                        from: 1; to: 100; stepSize: 1
                    }
                }
                AppHintLabel {
                    text: qsTr("A tool call is the model reading a page, searching the "
                               + "paper or looking at a figure. The budget stops a single "
                               + "question from turning into a long chain of them.")
                }
            }

            // ── 4 · Appearance ──────────────────────────────────────
            Page {
                AppSectionLabel { text: qsTr("Language") }
                Card {
                    AppFormLabel { text: qsTr("UI language") }
                    AppComboBox {
                        id: languageBox
                        Layout.fillWidth: true
                        model: dialog.languageLabels
                    }
                }

                AppSectionLabel {
                    text: qsTr("Font sizes (px)")
                    Layout.topMargin: Theme.spaceS
                }
                Card {
                    AppFormLabel { text: qsTr("Chapter menu") }
                    AppSpinBox {
                        id: tocFontSizeField
                        Layout.fillWidth: true
                        from: 8; to: 32; stepSize: 1
                    }

                    AppFormLabel { text: qsTr("Interpretation") }
                    AppSpinBox {
                        id: summaryFontSizeField
                        Layout.fillWidth: true
                        from: 8; to: 32; stepSize: 1
                    }

                    AppFormLabel { text: qsTr("Paragraphs") }
                    AppSpinBox {
                        id: paragraphFontSizeField
                        Layout.fillWidth: true
                        from: 8; to: 32; stepSize: 1
                    }

                    AppFormLabel { text: qsTr("Chat") }
                    AppSpinBox {
                        id: chatFontSizeField
                        Layout.fillWidth: true
                        from: 8; to: 32; stepSize: 1
                    }
                }
                AppHintLabel {
                    text: qsTr("Each pane's body text. Headings in that pane scale with it.")
                }

                AppSectionLabel {
                    text: qsTr("Remote desktop")
                    Layout.topMargin: Theme.spaceS
                }
                Card {
                    AppFormLabel { text: qsTr("Draw in software") }
                    AppComboBox {
                        id: remoteModeBox
                        Layout.fillWidth: true
                        model: dialog.remoteModeLabels
                    }
                }
                AppHintLabel {
                    text: qsTr("Over Remote Desktop there is no graphics card to "
                               + "draw with, so the usual path renders every frame "
                               + "as one big picture and sends the whole thing. "
                               + "Drawing in software instead repaints only what "
                               + "changed, and the small hover animations are "
                               + "switched off. Takes effect the next time the app "
                               + "starts.")
                }
                AppHintLabel {
                    visible: settings.remoteRenderingActive
                    text: qsTr("This session is drawing in software.")
                }
            }

            // ── 5 · Documents ───────────────────────────────────────
            Page {
                AppSectionLabel { text: qsTr("Paragraph segmentation") }
                Card {
                    AppFormLabel { text: qsTr("On open") }
                    CheckBox {
                        id: autoSegmentBox
                        text: qsTr("Segment a paper automatically the first time it is opened")
                    }

                    AppFormLabel { text: qsTr("GROBID service") }
                    CheckBox {
                        id: grobidEnabledBox
                        text: qsTr("Use GROBID for paragraph detection (best for academic papers)")
                    }

                    AppFormLabel { text: qsTr("Service URL") }
                    AppTextField {
                        id: grobidUrlField
                        Layout.fillWidth: true
                        enabled: grobidEnabledBox.checked
                        placeholderText: "https://aireader.d2ssoft.com/grobid"
                    }
                }
                AppHintLabel {
                    text: qsTr("Off by default, since segmenting a long paper costs seconds of "
                               + "work a reader who only wants to page through it never asked "
                               + "for — press Segment in the toolbar when you want paragraphs. "
                               + "GROBID is applied to whichever run does the segmenting; it "
                               + "falls back to the built-in splitter when the service is "
                               + "unreachable. Self-host with: "
                               + "docker run -d -p 8070:8070 grobid/grobid:0.9.1-crf")
                }

                AppSectionLabel {
                    text: qsTr("Sharing")
                    Layout.topMargin: Theme.spaceS
                }
                Card {
                    AppFormLabel { text: qsTr("Share with project") }
                    CheckBox {
                        id: sharePaperDataBox
                        text: qsTr("Upload paragraph segmentation and translations")
                    }
                }
                AppHintLabel {
                    text: qsTr("Segmenting and translating a paper costs CPU seconds and "
                               + "model tokens. Shared, that work is done once: your own "
                               + "other machines get it back automatically, and so does "
                               + "anyone in the research project who hasn't done it "
                               + "themselves. What you segment or translate yourself always "
                               + "wins over anything pulled down. Turn this off to keep a "
                               + "paper's text on this machine.")
                }
            }

            // ── 6 · Updates & privacy ───────────────────────────────
            Page {
                AppSectionLabel { text: qsTr("Updates") }
                Card {
                    AppFormLabel { text: qsTr("Auto-check for updates") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS
                        CheckBox {
                            id: autoCheckBox
                            text: qsTr("Check on launch")
                        }
                        Item { Layout.fillWidth: true }
                        AppButton {
                            text: updates.checking ? qsTr("Checking…") : qsTr("Check now")
                            enabled: !updates.checking
                            onClicked: updates.checkNow()
                        }
                    }

                    AppFormLabel { text: qsTr("Manifest URL") }
                    AppTextField {
                        id: manifestUrlField
                        Layout.fillWidth: true
                        placeholderText: "https://aireader.d2ssoft.com/update/manifest"
                    }
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
                    AppButton {
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

                AppSectionLabel {
                    text: qsTr("Privacy")
                    Layout.topMargin: Theme.spaceS
                }
                Card {
                    AppFormLabel { text: qsTr("Crash reports") }
                    CheckBox {
                        id: crashOptInBox
                        text: qsTr("Send anonymous crash reports (off by default)")
                    }
                }
                AppHintLabel {
                    text: qsTr("github.com/antonidasyang/ai-reader")
                }
            }
        }
    }
}
