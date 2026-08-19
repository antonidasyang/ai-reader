import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Dialog {
    id: dialog
    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape
    width: 600
    height: Math.min(implicitHeight, parent ? parent.height - 48 : 700)
    padding: Theme.dialogPadding
    topPadding: Theme.spaceL

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
        grobidEnabledBox.checked = settings.grobidEnabled
        grobidUrlField.text      = settings.grobidUrl
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
        settings.grobidEnabled     = grobidEnabledBox.checked
        settings.grobidUrl         = grobidUrlField.text.trim()
        settings.tocFontSize        = tocFontSizeField.value
        settings.summaryFontSize    = summaryFontSizeField.value
        settings.paragraphFontSize  = paragraphFontSizeField.value
        settings.chatFontSize       = chatFontSizeField.value
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

    // ── Shared control styles (same look across all dialogs) ────────
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

    component FieldText: TextField {
        id: ft
        implicitHeight: Theme.controlH
        leftPadding: Theme.spaceM - 2
        rightPadding: Theme.spaceM - 2
        font.pixelSize: 13
        color: Theme.text
        placeholderTextColor: Theme.dimText
        selectionColor: Theme.accent
        selectedTextColor: Theme.onAccent
        background: Rectangle {
            radius: Theme.radiusS
            color: Theme.fieldBg
            border.width: 1
            border.color: ft.activeFocus ? Theme.accent : Theme.fieldBorder
            Behavior on border.color { ColorAnimation { duration: 120 } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                radius: parent.radius + 2
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
                visible: ft.activeFocus
            }
        }
    }

    component FieldCombo: ComboBox {
        id: fc
        font.pixelSize: 13
        implicitHeight: Theme.controlH
        background: Rectangle {
            implicitWidth: 120
            implicitHeight: Theme.controlH
            radius: Theme.radiusS
            color: fc.down ? Theme.buttonPressed : fc.hovered ? Theme.buttonHover : Theme.fieldBg
            border.width: 1
            border.color: (fc.activeFocus || fc.visualFocus) ? Theme.accent : Theme.fieldBorder
            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on border.color { ColorAnimation { duration: 120 } }
        }
        contentItem: TextField {
            leftPadding: Theme.spaceS + 2
            rightPadding: Theme.spaceXs
            text: fc.editable ? fc.editText : fc.displayText
            enabled: fc.editable
            autoScroll: fc.editable
            readOnly: fc.down
            inputMethodHints: fc.inputMethodHints
            validator: fc.validator
            selectByMouse: true
            color: Theme.text
            placeholderTextColor: Theme.dimText
            selectionColor: Theme.accent
            selectedTextColor: Theme.onAccent
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 13
            background: null
        }
        delegate: ItemDelegate {
            id: fcDel
            required property var model
            required property int index
            width: ListView.view ? ListView.view.width : 0
            height: 28
            text: model.display !== undefined ? model.display : model.modelData
            highlighted: fc.highlightedIndex === index
            contentItem: Text {
                text: fcDel.text
                font.pixelSize: 13
                color: Theme.text
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: Theme.radiusS - 2
                color: fcDel.highlighted ? Theme.hover : "transparent"
            }
        }
        popup: Popup {
            y: fc.height + 4
            width: fc.width
            padding: Theme.spaceXs
            implicitHeight: Math.min(contentItem.implicitHeight
                                     + topPadding + bottomPadding, 320)
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: fc.popup.visible ? fc.delegateModel : null
                currentIndex: fc.highlightedIndex
                ScrollBar.vertical: ScrollBar { }
            }
            background: Rectangle {
                color: Theme.dialogBg
                border.width: 1
                border.color: Theme.border
                radius: Theme.radiusM
            }
        }
    }

    component FieldSpin: SpinBox {
        id: fs
        editable: true
        font.pixelSize: 13
        background: Rectangle {
            implicitWidth: 120
            implicitHeight: Theme.controlH
            radius: Theme.radiusS
            color: Theme.fieldBg
            border.width: 1
            border.color: fs.activeFocus ? Theme.accent : Theme.fieldBorder
            Behavior on border.color { ColorAnimation { duration: 120 } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                radius: parent.radius + 2
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
                visible: fs.activeFocus
            }
        }
    }

    component FormLabel: Label {
        color: Theme.bodyText
        font.pixelSize: 13
        horizontalAlignment: Text.AlignRight
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    component SectionLabel: Label {
        color: Theme.dimText
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 1
        font.capitalization: Font.AllUppercase
    }

    component SectionCard: Rectangle {
        Layout.fillWidth: true
        radius: Theme.radiusM
        color: Theme.cardBg
        border.width: 1
        border.color: Theme.divider
    }

    component HintLabel: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        visible: text.length > 0
        font.pixelSize: 11
        lineHeight: 1.25
        color: Theme.dimText
    }

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
                                         ? "https://api.anthropic.com (default)"
                                         : providerBox.currentText === "deepseek"
                                           ? "https://api.deepseek.com"
                                           : providerBox.currentText === "openai-compatible"
                                             ? "http://localhost:8080"
                                             : "https://api.openai.com (default)"
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
                text: qsTr("Applied when a paper is opened for the first time; falls back "
                           + "to the built-in splitter when the service is unreachable. "
                           + "Self-host with: docker run -d -p 8070:8070 grobid/grobid:0.9.1-crf")
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
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: updates.lastError.length > 0 ? Theme.danger : Theme.dimText
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
