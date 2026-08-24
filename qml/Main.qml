import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Pdf
import AiReader

ApplicationWindow {
    id: window
    width: 1400
    height: 900
    visible: true
    title: paperController.fileName.length > 0
           ? "AI Reader — " + paperController.fileName
           : "AI Reader"

    // Zoom limits picked to match the rest of the Qt PDF demos: below
    // 25 % the text becomes unreadable, above 500 % we burn memory on
    // raster pages the user could see by scrolling instead. Step is
    // 1.2× per click so seven clicks span the full range.
    readonly property real _zoomMin: 0.25
    readonly property real _zoomMax: 5.0
    readonly property real _zoomStep: 1.2

    // Hand/pan tool: when on, dragging the PDF moves the page (hand
    // cursor) instead of selecting text. Toggled from the toolbar.
    property bool panMode: false

    // Wheel-zoom modifier. On macOS Qt maps Qt.ControlModifier to the ⌘
    // Command key and Qt.MetaModifier to the physical Control key; the
    // conventional gesture is physical Control + scroll, so pick per OS.
    readonly property int _zoomModifier:
        (Qt.platform.os === "osx" || Qt.platform.os === "macos")
        ? Qt.MetaModifier : Qt.ControlModifier

    function _setZoom(s) {
        pdfView.renderScale = Math.max(_zoomMin, Math.min(_zoomMax, s))
    }
    function zoomIn()    { _setZoom(pdfView.renderScale * _zoomStep) }
    function zoomOut()   { _setZoom(pdfView.renderScale / _zoomStep) }
    function resetZoom() { _setZoom(1.0) }
    function fitWidth()  {
        // Ask the view for the scale, rather than dividing by its outer
        // width: the page layout is a couple of pixels narrower than the
        // view, so this landed just past the edge of overflow and popped
        // the horizontal scrollbar every time. Still routed through
        // _setZoom for the min/max clamp — fitWidthScale clamps nothing.
        const s = pdfView.fitWidthScale()
        if (s > 0)
            _setZoom(s)
    }

    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        enabled: pdfDoc.status === PdfDocument.Ready
        onActivated: window.zoomIn()
    }
    Shortcut {
        sequence: StandardKey.ZoomOut
        enabled: pdfDoc.status === PdfDocument.Ready
        onActivated: window.zoomOut()
    }
    Shortcut {
        sequence: "Ctrl+0"
        enabled: pdfDoc.status === PdfDocument.Ready
        onActivated: window.resetZoom()
    }

    PdfDocument {
        id: pdfDoc
        source: paperController.pdfSource
        password: paperController.pdfPassword
        // Password handling lives on PaperController; suppress duplicate dialogs.
    }

    PasswordDialog {
        id: passwordDialog
        anchors.centerIn: Overlay.overlay
        promptText: qsTr("\"%1\" is encrypted. Enter the password:").arg(paperController.fileName)
        onAccepted: paperController.setPassword(password)
        // User chose not to unlock this paper — drop it from the tab
        // list rather than leaving an inert "stuck on password" tab.
        onRejected: {
            if (tabs.activeIndex >= 0)
                tabs.closePaper(tabs.activeIndex)
            else
                paperController.clear()
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open PDF")
        nameFilters: [qsTr("PDF files (*.pdf)"), qsTr("All files (*)")]
        onAccepted: tabs.openPaper(selectedFile)
    }

    FileDialog {
        id: exportTextDialog
        title: qsTr("Export extracted text")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("All files (*)")]
        onAccepted: {
            const ok = paperController.exportExtractedText(selectedFile)
            if (!ok) {
                errorBanner.text = qsTr("Failed to write extracted text.")
                errorBanner.visible = true
            }
        }
    }

    FolderDialog {
        id: openFolderDialog
        title: qsTr("Open folder")
        currentFolder: library.currentFolder.length > 0
                       ? Qt.url("file://" + library.currentFolder)
                       : ""
        onAccepted: {
            library.openFolder(selectedFolder)
            folderPane.visible = true
        }
    }

    SettingsDialog {
        id: settingsDialog
        anchors.centerIn: Overlay.overlay
    }

    VisionDialog {
        id: visionDialog
        anchors.centerIn: Overlay.overlay
    }

    PromptsDialog {
        id: promptsDialog
        anchors.centerIn: Overlay.overlay
    }

    WelcomeWizard {
        id: welcomeWizard
    }

    ChangelogDialog {
        id: changelogDialog
        anchors.centerIn: Overlay.overlay
    }

    // Cloud-library dialogs (the toolbar account/project group drives these).
    MembersDialog { id: membersDialog }
    ProjectSettingsDialog { id: projectSettingsDialog }
    ProjectProfileDialog { id: projectProfileDialog }
    BatchAnalysisDialog { id: batchAnalysisDialog }
    CompareDialog { id: compareDialog }

    Dialog {
        id: createProjectDialog
        title: qsTr("New project")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 360
        padding: 14
        standardButtons: Dialog.Ok | Dialog.Cancel
        // Pin the palette to Theme tokens like the other dialogs, so the
        // stock buttons/fields inside can never fall back to a palette
        // that disagrees with the themed background.
        palette.window: Theme.paneBg
        palette.windowText: Theme.text
        palette.base: Theme.fieldBg
        palette.text: Theme.text
        palette.button: Theme.buttonBg
        palette.buttonText: Theme.text
        palette.highlight: Theme.accent
        palette.highlightedText: Theme.onAccent
        palette.placeholderText: Theme.dimText
        background: Rectangle {
            color: Theme.paneBg
            border.color: Theme.border
            radius: 6
        }
        onAccepted: {
            if (newProjName.text.trim().length > 0)
                projects.createProject(newProjName.text.trim(), newProjDesc.text)
            newProjName.text = ""
            newProjDesc.text = ""
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            TextField {
                id: newProjName
                Layout.fillWidth: true
                placeholderText: qsTr("Project name")
            }
            TextField {
                id: newProjDesc
                Layout.fillWidth: true
                placeholderText: qsTr("Description (optional)")
            }
        }
    }

    // Asked when Translate is pressed on a paper that already has some
    // translations: filling the gaps and starting over are both reasonable,
    // and which one is meant isn't guessable. A paper with nothing translated
    // never gets here — the button just goes.
    Dialog {
        id: translateChoiceDialog
        title: qsTr("Translate this paper")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 420
        padding: 14
        standardButtons: Dialog.Cancel
        palette.window: Theme.paneBg
        palette.windowText: Theme.text
        palette.base: Theme.fieldBg
        palette.text: Theme.text
        palette.button: Theme.buttonBg
        palette.buttonText: Theme.text
        palette.highlight: Theme.accent
        palette.highlightedText: Theme.onAccent
        palette.placeholderText: Theme.dimText
        background: Rectangle {
            color: Theme.paneBg
            border.color: Theme.border
            radius: 6
        }

        // Sampled when the dialog opens, so the numbers can't shift under the
        // reader while they are looking at them.
        property int doneCount: 0
        property int leftCount: 0

        function ask() {
            doneCount = translation.translatedParagraphs()
            leftCount = translation.untranslatedParagraphs()
            open()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: translateChoiceDialog.leftCount > 0
                      ? qsTr("%1 of these paragraphs are already translated and %2 are not.")
                            .arg(translateChoiceDialog.doneCount)
                            .arg(translateChoiceDialog.leftCount)
                      : qsTr("All %1 paragraphs are already translated.")
                            .arg(translateChoiceDialog.doneCount)
            }
            Button {
                Layout.fillWidth: true
                visible: translateChoiceDialog.leftCount > 0
                text: qsTr("Translate the remaining %1")
                          .arg(translateChoiceDialog.leftCount)
                onClicked: {
                    translateChoiceDialog.close()
                    translation.translateAll()
                }
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Start over — re-translate all %1")
                          .arg(translateChoiceDialog.doneCount
                               + translateChoiceDialog.leftCount)
                onClicked: {
                    translateChoiceDialog.close()
                    translation.retranslateAll()
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: Theme.dimText
                text: qsTr("Starting over asks the model again for every paragraph, "
                           + "including the ones already done.")
            }
        }
    }

    // Steps are wired up after the toolbar buttons / panes have been
    // instantiated, so the spotlight target references resolve. The
    // wizard auto-opens on first run via the Component.onCompleted
    // hook below; users can also re-trigger it from the "?" toolbar
    // button.
    function buildWizardSteps() {
        return [
            {
                target: [openBtn, openFolderBtn],
                title: qsTr("1 · Open a paper"),
                body: qsTr("Click <b>Open…</b> to load a single PDF, or <b>Open folder…</b> to browse a whole library. You can also drag a .pdf into the window. Each paper opens in its own tab.")
            },
            {
                target: [folderToggleBtn, tocToggleBtn, chatToggleBtn],
                title: qsTr("2 · Toggle panels"),
                body: qsTr("Use the toolbar's <b>Folder / TOC / Chat</b> buttons to show or hide each pane.")
            },
            {
                target: pdfPaneGrip,
                title: qsTr("3 · Move panels"),
                body: qsTr("Each pane has a small <b>⋮⋮ grip</b> in its top-left corner. Press and drag a grip horizontally to slide that pane to a new slot in the layout — a vertical marker shows where it will land. The arrangement is remembered between launches.")
            },
            {
                target: translateBtn,
                title: qsTr("4 · Translate paragraphs"),
                body: qsTr("Click <b>Translate</b> to translate every paragraph at once. Or right-click a single paragraph to translate, split, merge, delete, or quote it into the chat.")
            },
            {
                target: chatToggleBtn,
                title: qsTr("5 · Chat with the paper"),
                body: qsTr("Open the <b>Chat</b> pane and ask questions. The model can read pages, search the text, and view rendered figures with vision. Each paper keeps its own list of <b>chat sessions</b> in the tab strip on top — + to add, × to close, double-click to rename.")
            },
            {
                // Both buttons: exactly one is visible at a time
                // (signed out vs in), the spotlight skips the hidden
                // one, and it tracks auth changes live — a ternary
                // evaluated at build time went stale once the CAS
                // session finished restoring.
                target: [signInBtn, accountBtn],
                title: qsTr("6 · Sign in to sync"),
                body: qsTr("Click <b>Sign in</b> to log in through your organisation's CAS page in the browser — the app never stores your password. Signed in, your library lives in the cloud: papers, metadata and AI interpretations sync across devices and stay readable offline.")
            },
            {
                target: libToggleBtn,
                title: qsTr("7 · Share a project library"),
                body: qsTr("The <b>Lib</b> pane organises papers by research project. Invite teammates by email as <b>owner / editor / viewer</b> — everyone shares the same library, full-text search included, and shared AI interpretations appear under <b>Shared</b>.")
            },
            {
                target: settingsBtn,
                title: qsTr("8 · Configure your LLM"),
                body: qsTr("Open <b>Settings…</b> to add a model and API key (Anthropic Claude or any OpenAI-compatible endpoint). Use <b>Prompts…</b> to customise system prompts. Re-open this tour any time from the <b>?</b> button.")
            }
        ]
    }

    function showError(prefix, message) {
        if (!message || message.length === 0) return
        errorBanner.text = prefix && prefix.length > 0
                           ? qsTr("%1: %2").arg(prefix).arg(message)
                           : message
        errorBanner.visible = true
        bannerHideTimer.restart()
    }

    Connections {
        target: paperController
        function onPasswordRequired() { passwordDialog.open() }
        function onStatusChanged() {
            if (paperController.status === PaperController.Error) {
                showError(qsTr("PDF"), paperController.errorString)
            } else if (paperController.status === PaperController.Ready) {
                errorBanner.visible = false
                bannerHideTimer.stop()
            }
        }
    }

    Connections {
        target: translation
        function onLastErrorChanged() { showError(qsTr("Translation"), translation.lastError) }
    }
    Connections {
        target: summary
        function onStatusChanged() {
            if (summary.status === SummaryService.Failed)
                showError(qsTr("Summary"), summary.lastError)
        }
    }
    Connections {
        target: toc
        function onStatusChanged() {
            if (toc.status === TocService.Failed)
                showError(qsTr("TOC"), toc.lastError)
        }
    }
    Connections {
        target: vision
        function onStatusChanged() {
            if (vision.status === VisionService.Failed)
                showError(qsTr("Vision"), vision.lastError)
        }
    }
    Connections {
        target: chat
        function onLastErrorChanged() { showError(qsTr("Chat"), chat.lastError) }
    }

    // ── Bidirectional scroll sync ─────────────────────────────────────
    // Two re-entrancy guards prevent a feedback loop:
    //   • blockList.syncEnabled is dropped when we drive the list from PDF.
    //   • _suppressPdfSync is set when we drive the PDF from the list.
    QtObject {
        id: scrollSync
        property bool suppressPdfSync: false
        property int  lastShownPage: -1
    }

    // ── Pane reorder helpers ──────────────────────────────────────────
    // The DockGrip in each pane drags the pane to a new slot in the
    // SplitView; on release we serialize the order to QSettings so it
    // survives across launches. Pane identity is tracked by objectName.
    function persistPaneOrder() {
        const arr = []
        for (let i = 0; i < split.count; ++i) {
            const it = split.itemAt(i)
            if (it && it.objectName && it.objectName.length > 0)
                arr.push(it.objectName)
        }
        layoutSettings.setPaneOrder(arr.join(","))
    }

    function applySavedPaneOrder() {
        const csv = layoutSettings.paneOrder()
        if (!csv || csv.length === 0) return
        const desired = csv.split(",")
        // Walk each desired position and pull the matching pane (by
        // objectName) into that slot. Skip ids that aren't present
        // any longer (e.g., a pane was removed in a new build).
        for (let dst = 0; dst < desired.length; ++dst) {
            const id = desired[dst]
            for (let i = dst; i < split.count; ++i) {
                const it = split.itemAt(i)
                if (it && it.objectName === id) {
                    if (i !== dst) {
                        const item = split.takeItem(i)
                        split.insertItem(dst, item)
                    }
                    break
                }
            }
        }
    }

    Component.onCompleted: {
        applySavedPaneOrder()

        // Pane sizes are restored automatically: each pane binds
        // SplitView.preferredWidth to layoutSettings.paneWidth(...)
        // and persists via onWidthChanged. The C++ setter debounces
        // writes so a drag becomes one disk write.

        // Wire spotlight targets now that the toolbar / panes exist.
        welcomeWizard.steps = buildWizardSteps()

        // First-render popups: the welcome tour replays on the first
        // launch of EVERY new version (it also stamps
        // lastSeenVersion, so the changelog dialog never stacks on
        // top of it in the same session).
        if (layoutSettings.wizardSeenVersion() !== settings.appVersion) {
            Qt.callLater(function() { welcomeWizard.start() })
        } else if (layoutSettings.lastSeenVersion() !== settings.appVersion) {
            Qt.callLater(function() { changelogDialog.open() })
        }
    }

    // PDF → block list. Watch pdfView.currentPage via a side-effect binding.
    Item {
        property int observedPage: pdfView.currentPage
        onObservedPageChanged: {
            if (scrollSync.suppressPdfSync) return
            if (observedPage === scrollSync.lastShownPage) return
            scrollSync.lastShownPage = observedPage
            blockList.showPage(observedPage)
        }
    }

    // Block list → PDF.
    Connections {
        target: blockList
        function onPageRequested(page) {
            if (page < 0) return
            if (page === pdfView.currentPage) return
            scrollSync.suppressPdfSync = true
            scrollSync.lastShownPage = page
            pdfView.goToPage(page)
            Qt.callLater(function() { scrollSync.suppressPdfSync = false })
        }
        function onAskInChatRequested(text, page) {
            chatPane.visible = true
            chatPane.prefillInput(text, page + 1)
        }
        function onTranslateBlockRequested(row) {
            translation.translateBlock(row)
        }
        function onSegmentRequested() {
            paperController.rebuildBlocks()
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            ToolButton {
                id: openBtn
                text: qsTr("Open…")
                onClicked: fileDialog.open()
            }
            ToolButton {
                id: openFolderBtn
                text: qsTr("Open folder…")
                onClicked: openFolderDialog.open()
            }
            ToolButton {
                text: qsTr("Export text…")
                enabled: paperController.status === PaperController.Ready
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Save the raw PDF text + per-line bboxes + detected paragraphs to a .txt file")
                onClicked: exportTextDialog.open()
            }
            ToolButton {
                // One button, two readings: with no paragraphs yet this
                // is the primary "segment this paper" action (auto
                // segmentation is off by default); once there are
                // paragraphs it means "throw them away and redo it".
                readonly property bool _firstRun: paperController.blockCount === 0
                text: _firstRun ? qsTr("Segment") : qsTr("Re-extract")
                enabled: paperController.status === PaperController.Ready
                         && !paperController.extracting
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: _firstRun
                    ? qsTr("Split this paper into paragraphs (needed for translation, TOC and chat)")
                    : qsTr("Discard manual paragraph edits and re-run automatic extraction")
                onClicked: paperController.rebuildBlocks()
            }
            ToolSeparator {}
            ToolButton {
                id: translateBtn
                text: translation.busy ? qsTr("Cancel") : qsTr("Translate")
                enabled: paperController.status === PaperController.Ready
                         && (translation.busy || settings.isConfigured)
                onClicked: {
                    if (translation.busy) {
                        translation.cancel()
                    } else if (translation.translatedParagraphs() > 0) {
                        translateChoiceDialog.ask()
                    } else {
                        translation.translateAll()
                    }
                }
            }
            ToolButton {
                text: qsTr("Retry failed")
                visible: !translation.busy && translation.failedCount > 0
                onClicked: translation.retryFailed()
            }
            ToolButton {
                // The older free-form summary. It and the structured
                // interpretation next to it answer different questions, so
                // they get names that say which is which.
                text: qsTr("Summary")
                checkable: true
                checked: summaryPane.visible
                onClicked: summaryPane.visible = !summaryPane.visible
            }
            ToolButton {
                text: compare.count > 0 ? qsTr("Compare (%1)").arg(compare.count)
                                        : qsTr("Compare")
                visible: auth.authenticated && projects.currentId.length > 0
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Put papers side by side, with a warning "
                                   + "where they cannot honestly be compared")
                onClicked: compareDialog.open()
            }
            ToolButton {
                text: qsTr("Interpret library")
                visible: auth.authenticated && projects.currentId.length > 0
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Interpret every paper in this project, "
                                   + "then filter by relevance")
                onClicked: batchAnalysisDialog.open()
            }
            ToolButton {
                text: qsTr("Interpret")
                checkable: true
                checked: analysisPane.visible
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Structured interpretation: relevance to this "
                                   + "project, what to read first, and every "
                                   + "statement traced back to the paper")
                onClicked: analysisPane.visible = !analysisPane.visible
            }
            ToolButton {
                text: qsTr("Read page (vision)")
                enabled: paperController.status === PaperController.Ready
                         && settings.isConfigured
                         && vision.status !== VisionService.Generating
                         && vision.status !== VisionService.Rendering
                onClicked: {
                    visionDialog.open()
                    vision.readPage(pdfView.currentPage)
                }
            }
            ToolSeparator {}
            ToolButton {
                text: "−"
                enabled: pdfDoc.status === PdfDocument.Ready
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Zoom out")
                onClicked: window.zoomOut()
            }
            ToolButton {
                // Doubles as a "current zoom" readout and a zoom action:
                // click = fit the page to the window width, double-click
                // = back to 100%. A short timer tells the two apart.
                text: pdfDoc.status === PdfDocument.Ready
                      ? Math.round(pdfView.renderScale * 100) + "%"
                      : "—"
                enabled: pdfDoc.status === PdfDocument.Ready
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Click: fit page width · double-click: 100%")
                Timer {
                    id: zoomClickTimer
                    interval: 240
                    onTriggered: window.fitWidth()
                }
                onClicked: {
                    if (zoomClickTimer.running) {
                        zoomClickTimer.stop()
                        window.resetZoom()
                    } else {
                        zoomClickTimer.start()
                    }
                }
            }
            ToolButton {
                text: "+"
                enabled: pdfDoc.status === PdfDocument.Ready
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Zoom in")
                onClicked: window.zoomIn()
            }
            ToolButton {
                id: panToggleBtn
                checkable: true
                checked: window.panMode
                enabled: pdfDoc.status === PdfDocument.Ready
                display: AbstractButton.IconOnly
                icon.source: "qrc:/icons/pan-hand.svg"
                icon.width: 18
                icon.height: 18
                // Tint the single-stroke glyph with the theme's text color
                // so it matches the neighboring toolbar labels; active
                // state shows via the button's checked highlight.
                icon.color: Theme.text
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Hand tool: drag to move the page. Off = select text.")
                onClicked: window.panMode = !window.panMode
            }
            ToolSeparator {}
            ToolButton {
                id: folderToggleBtn
                text: qsTr("Folder")
                checkable: true
                checked: folderPane.visible
                onClicked: folderPane.visible = !folderPane.visible
            }
            ToolButton {
                id: libToggleBtn
                text: qsTr("Lib")
                checkable: true
                checked: libraryPane.visible
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Cloud-synced literature library")
                onClicked: libraryPane.visible = !libraryPane.visible
            }
            ToolButton {
                id: tocToggleBtn
                text: qsTr("TOC")
                checkable: true
                checked: tocSidebar.visible
                onClicked: tocSidebar.visible = !tocSidebar.visible
            }
            ToolButton {
                id: chatToggleBtn
                text: qsTr("Chat")
                checkable: true
                checked: chatPane.visible
                onClicked: chatPane.visible = !chatPane.visible
            }
            ToolButton {
                text: qsTr("Quote → Chat")
                visible: paperController.currentSelection.length > 0
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Quote the highlighted PDF text into the chat input")
                onClicked: {
                    chatPane.visible = true
                    chatPane.prefillInput(paperController.currentSelection,
                                          paperController.currentSelectionPage + 1)
                }
            }
            Label {
                text: pdfDoc.status === PdfDocument.Ready
                      ? qsTr("%1 pages · %2 paragraphs")
                            .arg(pdfDoc.pageCount)
                            .arg(paperController.blockCount)
                        + (paperController.extracting ? qsTr(" · Segmenting…")
                           : structure.busy ? qsTr(" · GROBID…") : "")
                      : ""
                color: Theme.dimText
                Layout.leftMargin: 8
            }
            Item { Layout.fillWidth: true }

            // ── Cloud library: project picker + account (was the ProjectBar) ──
            ToolButton {
                id: signInBtn
                text: qsTr("Sign in")
                visible: !auth.authenticated
                onClicked: auth.startCasLogin()
            }
            BusyIndicator {
                running: auth.busy
                visible: auth.busy
                implicitWidth: 16
                implicitHeight: 16
            }
            ComboBox {
                id: projectCombo
                visible: auth.authenticated && projects.list.length > 0
                Layout.preferredWidth: 170
                model: projects.list
                textRole: "name"
                function syncIndex() {
                    for (let i = 0; i < projects.list.length; ++i) {
                        if (projects.list[i].id === projects.currentId) {
                            currentIndex = i
                            return
                        }
                    }
                    currentIndex = -1
                }
                onActivated: function(idx) {
                    if (idx >= 0)
                        projects.selectProject(projects.list[idx].id)
                }
                Component.onCompleted: syncIndex()
                Connections {
                    target: projects
                    function onCurrentChanged() { projectCombo.syncIndex() }
                    function onListChanged() { projectCombo.syncIndex() }
                }
            }
            ToolButton {
                text: qsTr("New project")
                visible: auth.authenticated
                onClicked: createProjectDialog.open()
            }
            ToolButton {
                text: qsTr("Edit project")
                visible: auth.authenticated && projects.currentId.length > 0
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Rename this project, or delete it")
                onClicked: projectSettingsDialog.open()
            }
            ToolButton {
                text: qsTr("Members")
                visible: auth.authenticated && projects.currentId.length > 0
                onClicked: {
                    projects.refreshMembers()
                    membersDialog.open()
                }
            }
            ToolButton {
                // The one place the reader tells the app what this project
                // is for. Every interpretation is prompted with it, so the
                // button carries a dot until it has been filled in.
                text: profile.hasProfile ? qsTr("Profile") : qsTr("Profile •")
                visible: auth.authenticated && projects.currentId.length > 0
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: profile.hasProfile
                              ? qsTr("Research profile: %1").arg(profile.summary)
                              : qsTr("Describe what this project is trying to find "
                                     + "out — every interpretation is written against it")
                onClicked: projectProfileDialog.open()
            }
            ToolButton {
                id: accountBtn
                visible: auth.authenticated
                text: (auth.userDisplayName.length > 0 ? auth.userDisplayName
                                                       : auth.userEmail) + " ▾"
                onClicked: accountMenu.popup()
                Menu {
                    id: accountMenu
                    MenuItem { text: qsTr("Sign out"); onTriggered: auth.logout() }
                }
            }
            ToolSeparator { visible: auth.authenticated }

            Label {
                text: settings.isConfigured
                      ? qsTr("%1 · %2").arg(settings.provider).arg(settings.model)
                      : qsTr("LLM not configured")
                // Theme tokens, not hardcoded hex: the old indigo/red pair
                // was unreadable against the dark toolbar in dark mode.
                color: settings.isConfigured ? Theme.accent : Theme.danger
                font.pixelSize: 11
                Layout.rightMargin: 8
            }
            ToolButton {
                text: qsTr("Prompts…")
                onClicked: promptsDialog.open()
            }
            ToolButton {
                id: settingsBtn
                text: qsTr("Settings…")
                onClicked: settingsDialog.open()
            }
            ToolButton {
                text: "?"
                font.pixelSize: 16
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Show getting-started tour")
                onClicked: welcomeWizard.start()
            }
        }
    }

    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length > 0) {
                for (let i = 0; i < drop.urls.length; ++i) {
                    const u = drop.urls[i].toString()
                    if (u.toLowerCase().endsWith(".pdf")) {
                        tabs.openPaper(drop.urls[i])
                        drop.accepted = true
                        return
                    }
                }
                errorBanner.text = qsTr("Dropped file is not a PDF.")
                errorBanner.visible = true
            }
        }

        SplitView {
            id: split
            anchors.fill: parent
            orientation: Qt.Horizontal

            // ── Far left: folder browser (toggleable) ──────────────────
            // Visible by default if the user previously had a folder
            // open; auto-hidden otherwise so first-launch isn't crowded.
            FolderPane {
                id: folderPane
                objectName: "folder"
                // Default: visible iff the user has a folder open
                // already (so a brand-new install with no library
                // doesn't waste a column on an empty pane). Once
                // the user toggles via the toolbar, the imperative
                // assignment breaks the binding and the new value
                // is persisted via onVisibleChanged below.
                visible: layoutSettings.paneVisible("folder",
                    library.currentFolder.length > 0)
                onVisibleChanged: layoutSettings.setPaneVisible("folder", visible)
                SplitView.preferredWidth: layoutSettings.paneWidth("folder", 240)
                SplitView.minimumWidth: 0
                onWidthChanged: layoutSettings.setPaneWidth("folder", width)
                onPdfChosen: function(path) { tabs.openPaper(path) }

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: folderPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Library pane (cloud-synced bibliography, toggleable) ───
            LibraryPane {
                id: libraryPane
                objectName: "library"
                visible: layoutSettings.paneVisible("library", false)
                onVisibleChanged: layoutSettings.setPaneVisible("library", visible)
                SplitView.preferredWidth: layoutSettings.paneWidth("library", 280)
                SplitView.minimumWidth: 0
                onWidthChanged: layoutSettings.setPaneWidth("library", width)
                onOpenRequested: function(path) { tabs.openPaper(path) }

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: libraryPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── TOC sidebar ────────────────────────────────────────────
            TocSidebar {
                id: tocSidebar
                objectName: "toc"
                visible: layoutSettings.paneVisible("toc", true)
                onVisibleChanged: layoutSettings.setPaneVisible("toc", visible)
                SplitView.preferredWidth: layoutSettings.paneWidth("toc", 220)
                SplitView.minimumWidth: 0
                onWidthChanged: layoutSettings.setPaneWidth("toc", width)
                onSectionClicked: function(blockId, page) {
                    blockList.showPage(page)
                    pdfView.goToPage(page)
                }

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: tocSidebar
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Middle: PDF reader ─────────────────────────────────────
            Item {
                id: pdfPane
                objectName: "pdf"
                SplitView.preferredWidth: layoutSettings.paneWidth("pdf",
                    Math.max(280, Math.round(split.width * 0.45)))
                SplitView.minimumWidth: 280
                onWidthChanged: layoutSettings.setPaneWidth("pdf", width)
                // Without clip the PdfMultiPageView (a Flickable) can paint
                // pages past the pane's right/left edges when the user
                // shrinks the splitter and scrolls horizontally — the
                // overflow draws over the TOC and BlockList panes.
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // ── VS Code-style tab strip ─────────────────────────
                    // Hidden when no papers are open so the empty-state
                    // hero card stays the dominant element on first run.
                    Rectangle {
                        id: pdfTabBar
                        Layout.fillWidth: true
                        Layout.preferredHeight: tabs.count > 0 ? 30 : 0
                        visible: tabs.count > 0
                        // Intentionally theme-independent: fixed VS Code-style
                        // dark chrome in BOTH themes (it frames the PDF
                        // viewport, whose empty state is the same fixed dark).
                        // Every foreground inside is explicit light-on-dark;
                        // never place a stock (palette-following) control here.
                        color: "#2d2d30"

                        // One shared tab context menu (VS Code's Close /
                        // Close Others / Close All). tabIndex is stamped by
                        // whichever tab was right-clicked.
                        Menu {
                            id: tabCtxMenu
                            property int tabIndex: -1
                            MenuItem {
                                text: qsTr("Close Tab")
                                onTriggered: tabs.closePaper(tabCtxMenu.tabIndex)
                            }
                            MenuItem {
                                text: qsTr("Close Others")
                                enabled: tabs.count > 1
                                onTriggered: tabs.closeOthers(tabCtxMenu.tabIndex)
                            }
                            MenuItem {
                                text: qsTr("Close All")
                                onTriggered: tabs.closeAll()
                            }
                        }

                        Flickable {
                            anchors.left: parent.left
                            anchors.leftMargin: 28   // space for the DockGrip
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            contentWidth: tabRow.width
                            contentHeight: height
                            clip: true
                            interactive: contentWidth > width

                            Row {
                                id: tabRow
                                spacing: 1
                                height: parent.height

                                Repeater {
                                    model: tabs.count
                                    delegate: Rectangle {
                                        id: tabDelegate
                                        readonly property bool isActive: index === tabs.activeIndex
                                        // Captured for the context menu, whose
                                        // actions mutate the tab list and would
                                        // otherwise see a re-bound `index`.
                                        readonly property int tabIndex: index
                                        height: parent.height
                                        // Cap tabs at ~220px so a long paper name
                                        // doesn't push the others off-screen.
                                        width: Math.min(220,
                                                        Math.max(80,
                                                                 nameLabel.implicitWidth + 44))
                                        color: isActive ? "#1e1f22"
                                                        : (tabHover.containsMouse ? "#3e3e42" : "#252526")

                                        // Active-tab accent stripe (top edge).
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            height: 2
                                            color: tabDelegate.isActive ? "#5b8def" : "transparent"
                                        }

                                        // Right-click menu, VS Code-style. Declared
                                        // before the other mouse areas so it sits
                                        // *below* them: they only take the left and
                                        // middle buttons, so a right-click anywhere
                                        // on the tab — the × included — falls
                                        // through to here. Right-clicking does not
                                        // activate the tab; the menu acts on the tab
                                        // that was clicked. The menu itself lives on
                                        // the bar, not in here: its actions destroy
                                        // this delegate, and a popup must outlive the
                                        // item that triggered it.
                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.RightButton
                                            onPressed: {
                                                tabCtxMenu.tabIndex = tabDelegate.tabIndex
                                                tabCtxMenu.popup()
                                            }
                                        }

                                        Label {
                                            id: nameLabel
                                            anchors.left: parent.left
                                            anchors.leftMargin: 10
                                            anchors.right: closeBtn.left
                                            anchors.rightMargin: 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: tabs.nameAt(index)
                                            color: tabDelegate.isActive ? "white" : "#c8c8c8"
                                            elide: Text.ElideMiddle
                                            font.pixelSize: 12
                                        }

                                        // Click anywhere on the tab body
                                        // (other than the × button) to
                                        // activate. Middle-click closes,
                                        // matching VS Code.
                                        MouseArea {
                                            id: tabHover
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            anchors.right: closeBtn.left
                                            hoverEnabled: true
                                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                            onClicked: function(mouse) {
                                                if (mouse.button === Qt.LeftButton)
                                                    tabs.activatePaper(index)
                                                else
                                                    tabs.closePaper(index)
                                            }
                                        }

                                        Rectangle {
                                            id: closeBtn
                                            anchors.right: parent.right
                                            anchors.rightMargin: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 18
                                            height: 18
                                            radius: 3
                                            color: closeArea.containsMouse ? "#c0392b" : "transparent"
                                            Text {
                                                anchors.centerIn: parent
                                                text: "×"
                                                color: closeArea.containsMouse ? "white"
                                                                                : (tabDelegate.isActive ? "#bbb" : "#888")
                                                font.pixelSize: 14
                                            }
                                            MouseArea {
                                                id: closeArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: tabs.closePaper(index)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    FocusScope {
                        id: pdfViewport
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        // ── Pinned translation cards ──────────────────
                        // Where the next card opens. Set just before the
                        // service appends its row, since the delegate is
                        // created during that call.
                        property point nextCardPos: Qt.point(24, 24)
                        // Monotonic stacking order: clicking a card puts
                        // it on top of the others.
                        property int cardZTop: 2
                        function raiseCard() { return ++cardZTop }
                        // A spot near (x, y) that isn't already occupied,
                        // so translating twice from the same place doesn't
                        // hide one card exactly behind another.
                        function freeCardSpot(x, y) {
                            let px = x + 12
                            let py = y + 16
                            for (let guard = 0; guard < 10; ++guard) {
                                let clash = false
                                for (let i = 0; i < cardRepeater.count; ++i) {
                                    const it = cardRepeater.itemAt(i)
                                    if (it && Math.abs(it.x - px) < 12
                                           && Math.abs(it.y - py) < 12) {
                                        clash = true
                                        break
                                    }
                                }
                                if (!clash)
                                    break
                                px += 24
                                py += 24
                            }
                            return Qt.point(px, py)
                        }

                        // Click the PDF area to focus it, then use the arrow
                        // keys to scroll the whole document. The TapHandler is
                        // passive (DragThreshold) so it never steals a text-
                        // selection or pan drag; it just puts this scope in the
                        // focus chain. Arrow keys then reach this Keys handler
                        // (directly, or bubbled up from pdfView's selection,
                        // which ignores them) and scroll the inner flickable.
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            gesturePolicy: TapHandler.DragThreshold
                            onTapped: pdfViewport.forceActiveFocus()
                        }
                        // Ctrl/⌘ + key over the PDF. StandardKey.Copy is
                        // ⌘C on macOS, so the literal physical-Control
                        // chord is matched too (Qt reports it as
                        // Qt.MetaModifier there) — people coming from
                        // Windows reach for Ctrl+C and expect it to copy.
                        function _isChord(event, key) {
                            return event.key === key
                                && (event.modifiers
                                    & (Qt.ControlModifier | Qt.MetaModifier))
                        }
                        Keys.onPressed: function(event) {
                            if (event.matches(StandardKey.Copy)
                                    || pdfViewport._isChord(event, Qt.Key_C)) {
                                pdfSelection.copyToClipboard()
                                event.accepted = true
                                return
                            }
                            if (event.matches(StandardKey.SelectAll)
                                    || pdfViewport._isChord(event, Qt.Key_A)) {
                                pdfSelection.selectAllOnPage(pdfView.currentPage)
                                event.accepted = true
                                return
                            }
                            const f = pdfMouse._flick()
                            const k = event.key
                            const isArrow = k === Qt.Key_Up || k === Qt.Key_Down
                                         || k === Qt.Key_Left || k === Qt.Key_Right
                            if (!f || !isArrow) { event.accepted = false; return }
                            const step = 60
                            if (k === Qt.Key_Up)         f.contentY -= step
                            else if (k === Qt.Key_Down)  f.contentY += step
                            else if (k === Qt.Key_Left)  f.contentX -= step
                            else                         f.contentX += step
                            f.returnToBounds()
                            event.accepted = true
                        }

                        AiPdfView {
                            id: pdfView
                            anchors.fill: parent
                            document: pdfDoc
                            visible: pdfDoc.status === PdfDocument.Ready
                            // C++ selection model: cross-page ranges, word/
                            // paragraph snapping, I-beam hover, link hits.
                            selectionModel: pdfSelection
                            selectionEnabled: !window.panMode
                            // Turn off the inner TableView's own drag/flick (and
                            // its wheel handling) so the page can't be dragged
                            // around in arrow mode — only the hand tool, arrow
                            // keys, the wheel handler below, and the scrollbar
                            // move it. pdfMouse._flick() finds that TableView.
                            Component.onCompleted: {
                                const f = pdfMouse._flick()
                                if (f) f.interactive = false
                            }
                            // Mirror the user's PDF selection into the controller so
                            // the chat tool `get_user_selection` can read it.
                            onSelectedTextChanged: paperController.setCurrentSelection(
                                selectedText,
                                pdfSelection.startPage >= 0 ? pdfSelection.startPage
                                                            : currentPage)

                            // Right-click → Translate: opens a card beside
                            // the click. Cards are pinned — a new selection
                            // leaves the old ones alone. The service places
                            // the selection in a paragraph when it can, so
                            // the right pane and the on-disk cache get the
                            // same translation.
                            canTranslateSelection: settings.isConfigured
                            onTranslateSelectionRequested: function(x, y) {
                                const p = pdfView.mapToItem(pdfViewport, x, y)
                                pdfViewport.nextCardPos = pdfViewport.freeCardSpot(p.x, p.y)
                                translation.translateSelection(
                                    pdfSelection.text,
                                    pdfSelection.startPage >= 0
                                        ? pdfSelection.startPage : pdfView.currentPage)
                            }

                            // Wheel router as a *child* of pdfView so it sits in
                            // the event chain ABOVE the inner Flickable but
                            // INSIDE the pdfView item, which means a non-accepted
                            // wheel cleanly bubbles up to pdfView's own scroll
                            // handling. acceptedButtons=NoButton keeps clicks and
                            // drags flowing to the selection layer untouched, and
                            // it must NOT set cursorShape or hoverEnabled — a
                            // cursor here would sit above the selection layer's
                            // I-beam/pointing-hand and override it.
                            //  • Ctrl+wheel zoom — checks pixelDelta too, since
                            //    macOS trackpads report angleDelta == 0.
                            MouseArea {
                                id: pdfMouse
                                anchors.fill: parent
                                // Below the view's selection layer (z 1):
                                // anything stacked above it can block hover
                                // delivery and kill the I-beam. Wheel still
                                // arrives here — the inner TableView is
                                // interactive:false and never accepts it.
                                z: 0
                                acceptedButtons: Qt.NoButton

                                // The scrollable inside AiPdfView is a private
                                // TableView (no public id); find it by
                                // duck-typing pdfView's children for a Flickable.
                                function _flick() {
                                    const kids = pdfView.children
                                    for (let i = 0; i < kids.length; ++i) {
                                        const k = kids[i]
                                        if (k && k.contentX !== undefined
                                              && k.contentY !== undefined)
                                            return k
                                    }
                                    return null
                                }

                                onWheel: function(wheel) {
                                    if (wheel.modifiers & window._zoomModifier) {
                                        const dz = wheel.angleDelta.y !== 0
                                                   ? wheel.angleDelta.y
                                                   : wheel.pixelDelta.y
                                        if (dz > 0)      window.zoomIn()
                                        else if (dz < 0) window.zoomOut()
                                        wheel.accepted = true
                                        return
                                    }
                                    // Inner flickable is interactive:false (so a
                                    // drag can't move the page), which also turns
                                    // off its wheel scrolling — so do it here.
                                    const f = _flick()
                                    if (!f) { wheel.accepted = false; return }
                                    const px = wheel.pixelDelta
                                    const ad = wheel.angleDelta
                                    const dx = px.x !== 0 ? px.x : ad.x / 120 * 100
                                    const dy = px.y !== 0 ? px.y : ad.y / 120 * 100
                                    // Same clamping as the hand tool: no
                                    // horizontal blank margins.
                                    const maxY = f.originY + Math.max(0,
                                        f.contentHeight - f.height)
                                    f.contentX = pdfView.clampedContentX(
                                        f.contentX - dx)
                                    f.contentY = Math.max(f.originY,
                                        Math.min(maxY, f.contentY - dy))
                                    wheel.accepted = true
                                }
                            }

                            // Hand/pan tool overlay. Only instantiated while
                            // panMode is on, so its hand cursor can never mask
                            // the selection layer's I-beam.
                            MouseArea {
                                id: pdfPan
                                anchors.fill: parent
                                // Leave the scrollbars reachable: this
                                // overlay sits above them, so without the
                                // margins the hand tool would swallow every
                                // click meant for the scrollbar.
                                anchors.rightMargin: pdfView.vScrollWidth
                                anchors.bottomMargin: pdfView.hScrollHeight
                                z: 3
                                visible: window.panMode
                                acceptedButtons: Qt.LeftButton
                                preventStealing: true
                                // Custom artwork cursor (open hand, and the
                                // short-fingered grab while dragging) — QML's
                                // cursorShape can't take pixmap cursors, so
                                // C++ sets it via QQuickItem::setCursor.
                                onVisibleChanged: visible
                                    ? cursorUtil.setPanCursor(pdfPan, pressed)
                                    : cursorUtil.clearCursor(pdfPan)
                                onPressedChanged: if (visible)
                                    cursorUtil.setPanCursor(pdfPan, pressed)
                                Component.onCompleted: if (visible)
                                    cursorUtil.setPanCursor(pdfPan, pressed)
                                property real _sx: 0
                                property real _sy: 0
                                property real _scx: 0
                                property real _scy: 0
                                onPressed: function(mouse) {
                                    const f = pdfMouse._flick()
                                    if (!f) { mouse.accepted = false; return }
                                    _sx = mouse.x; _sy = mouse.y
                                    _scx = f.contentX; _scy = f.contentY
                                }
                                onPositionChanged: function(mouse) {
                                    if (!pressed) return
                                    const f = pdfMouse._flick()
                                    if (!f) return
                                    // Horizontal pan only when the page is
                                    // wider than the viewport, clamped so a
                                    // page edge stops at the window edge —
                                    // no dragging blank margins into view.
                                    const maxY = f.originY + Math.max(0,
                                        f.contentHeight - f.height)
                                    f.contentX = pdfView.clampedContentX(
                                        _scx - (mouse.x - _sx))
                                    f.contentY = Math.max(f.originY,
                                        Math.min(maxY, _scy - (mouse.y - _sy)))
                                }
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            visible: paperController.status === PaperController.Empty
                                     || (paperController.status === PaperController.Error
                                         && paperController.pdfSource.toString().length === 0)
                            // Intentionally theme-independent: fixed dark hero
                            // matching the dark tab strip above. Foregrounds
                            // are explicit light-on-dark values.
                            color: "#1e1f22"
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 12
                                Label {
                                    text: qsTr("Drag a PDF here, or click Open…")
                                    color: "#bbbbbb"
                                    font.pixelSize: 18
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Label {
                                    text: qsTr("AI Reader — milestone 3.2 (TOC sidebar)")
                                    // #666666 failed contrast (~2.9:1) even on
                                    // this fixed dark surface; lifted to match
                                    // the dark-mode dimText gray (~6.3:1).
                                    color: "#9aa0a6"
                                    font.pixelSize: 12
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: paperController.status === PaperController.Loading
                                     || pdfDoc.status === PdfDocument.Loading
                                     || (paperController.extracting
                                         && paperController.blockCount === 0)
                            visible: running
                        }

                        // Pinned selection-translation cards, one per row of
                        // the service's snippet model. They sit above the
                        // page (z clears the selection layer at z 1) and
                        // only close on their own × button.
                        Repeater {
                            id: cardRepeater
                            model: translation.snippets

                            delegate: SelectionTranslateCard {
                                // Roles are read through `model.` so they
                                // can't collide with the card's own
                                // `status` / `text` properties.
                                required property var model

                                snippetId: model.snippetId
                                translatedText: model.text
                                status: model.status
                                errorText: model.error
                                fromParagraph: model.paragraph

                                // Consume the spot Main picked for this
                                // card just before the row was appended.
                                openX: pdfViewport.nextCardPos.x
                                openY: pdfViewport.nextCardPos.y

                                onCloseRequested: translation.closeSnippet(model.snippetId)
                                onRaiseRequested: z = pdfViewport.raiseCard()
                            }
                        }
                    }
                }

                DockGrip {
                    id: pdfPaneGrip
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 6
                    pane: pdfPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Right: extracted blocks / translations ─────────────────
            BlockList {
                id: blockList
                objectName: "blocks"
                SplitView.fillWidth: true
                SplitView.minimumWidth: 240
                model: paperController.blocks
                paperStatus: paperController.status
                // Wrapped text is expensive to re-lay-out; the pane holds
                // its layout width while a handle is being dragged.
                resizing: split.resizing

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: blockList
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Interpretation pane (toggleable) ───────────────────────
            SummaryPane {
                id: summaryPane
                objectName: "summary"
                resizing: split.resizing
                visible: layoutSettings.paneVisible("summary", false)
                onVisibleChanged: layoutSettings.setPaneVisible("summary", visible)
                SplitView.preferredWidth: layoutSettings.paneWidth("summary", 360)
                SplitView.minimumWidth: 240
                onWidthChanged: layoutSettings.setPaneWidth("summary", width)

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: summaryPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Structured interpretation pane (toggleable) ────────────
            AnalysisPane {
                id: analysisPane
                objectName: "analysis"
                visible: layoutSettings.paneVisible("analysis", false)
                onVisibleChanged: layoutSettings.setPaneVisible("analysis", visible)
                SplitView.preferredWidth: layoutSettings.paneWidth("analysis", 380)
                SplitView.minimumWidth: 260
                onWidthChanged: layoutSettings.setPaneWidth("analysis", width)

                // A citation names a paragraph; jumping means scrolling the
                // paragraph list to it and taking the PDF to its page.
                onEvidenceRequested: function(page, blockId) {
                    let targetPage = page - 1
                    if (blockId >= 0) {
                        const row = paperController.blocks.rowForBlockId(blockId)
                        if (row >= 0) {
                            blockList.showRow(row)
                            targetPage = paperController.blocks.pageOfRow(row)
                        }
                    }
                    if (targetPage >= 0)
                        pdfView.goToPage(targetPage)
                }
                onAskAiRequested: function(text) {
                    chatPane.visible = true
                    chatPane.prefillInput(text, 0)
                }
                onCompareRequested: function(paperId, title, note) {
                    compare.add(paperId, title, note)
                    compareDialog.open()
                }

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: analysisPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── Far right: chat pane (toggleable) ──────────────────────
            ChatPane {
                id: chatPane
                objectName: "chat"
                resizing: split.resizing
                visible: layoutSettings.paneVisible("chat", false)
                onVisibleChanged: layoutSettings.setPaneVisible("chat", visible)
                SplitView.preferredWidth: layoutSettings.paneWidth("chat", 360)
                SplitView.minimumWidth: 240
                onWidthChanged: layoutSettings.setPaneWidth("chat", width)

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: chatPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // The grab area is wider than the line it draws: 4 px is a
            // hard target to hit, and missing it reads as the handle not
            // following the mouse. The visible bar stays 4 px.
            handle: Item {
                implicitWidth: 10

                // Without this the pointer stays an arrow over the
                // handle, so there is nothing telling the user the
                // splitter can be dragged at all.
                HoverHandler {
                    cursorShape: Qt.SplitHCursor
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 4
                    height: parent.height
                    // Idle/hover follow the theme (the old fixed light grays
                    // glowed against the dark UI); pressed keeps the same
                    // fixed drag-accent blue as the DockGrip/dropMarker,
                    // which reads on both themes.
                    color: parent.SplitHandle.pressed ? "#5b8def"
                           : parent.SplitHandle.hovered ? Theme.dimText : Theme.border
                }
            }
        }

        // Floating insertion marker shown by DockGrip during a drag.
        // Sibling of `split` so the absolute-positioned x/y from the
        // grip's mapToItem(parent, ...) line up correctly.
        Rectangle {
            id: dropMarker
            visible: false
            width: 3
            color: "#5b8def"
            opacity: 0.85
            z: 1000
        }

        Rectangle {
            anchors.fill: parent
            visible: dropArea.containsDrag
            // Translucent wash over arbitrary content — intentionally
            // theme-independent. The caption sits on its own opaque dark
            // chip: white-on-wash alone disappeared over light PDF pages.
            color: "#332b6cff"
            border.color: "#5b8def"
            border.width: 2
            Rectangle {
                anchors.centerIn: parent
                width: dropHintLabel.implicitWidth + 32
                height: dropHintLabel.implicitHeight + 16
                radius: Theme.radiusM
                color: "#d91f3a5a"
                Label {
                    id: dropHintLabel
                    anchors.centerIn: parent
                    text: qsTr("Drop PDF to open")
                    color: "#ffffff"
                    font.pixelSize: 20
                }
            }
        }
    }

    Rectangle {
        id: errorBanner
        property alias text: errorLabel.text
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: visible ? 36 : 0
        visible: false
        // Intentionally theme-independent: a fixed dark-red alert surface
        // in BOTH themes. Every child must set its foreground explicitly —
        // stock controls here inherit the app palette (black text in light
        // mode) and become unreadable on this dark fill.
        color: "#5a1f1f"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            Label {
                id: errorLabel
                color: "#ffffff"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            ToolButton {
                id: errorCloseBtn
                text: "✕"
                contentItem: Text {
                    text: errorCloseBtn.text
                    color: "#ffffff"
                    font: errorCloseBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusS
                    color: errorCloseBtn.pressed ? "#59ffffff"
                         : errorCloseBtn.hovered ? "#33ffffff" : "transparent"
                }
                onClicked: {
                    errorBanner.visible = false
                    bannerHideTimer.stop()
                }
            }
        }
    }

    // Auto-dismiss the banner so a transient failure doesn't sit there
    // forever once the user has seen it. Restarted by showError().
    Timer {
        id: bannerHideTimer
        interval: 10000
        onTriggered: errorBanner.visible = false
    }

    // ── Update-available banner ─────────────────────────────────────
    // Sits above the error banner, anchored to the window bottom. The
    // C++ UpdateChecker decides when updateAvailable flips on; the
    // user can either Download (opens the release URL in the default
    // browser) or Dismiss (suppresses for the rest of this process).
    Rectangle {
        id: updateBanner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: errorBanner.visible ? errorBanner.top : parent.bottom
        height: visible ? 38 : 0
        visible: updates.updateAvailable && !updates.dismissed
        // Intentionally theme-independent: a fixed dark-blue notice surface
        // in BOTH themes. Nothing inside may rely on the inherited palette —
        // stock controls would draw black text here in light mode. Every
        // foreground below is set explicitly (white / near-white).
        color: "#1f3a5a"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 12

            Label {
                color: "#ffffff"
                text: qsTr("Update available: v%1").arg(updates.latestVersion)
                font.bold: true
            }
            Label {
                visible: updates.releaseDate.length > 0
                color: "#c6d9ff"
                font.pixelSize: 11
                text: "(" + updates.releaseDate + ")"
            }
            Item { Layout.fillWidth: true }
            Button {
                id: updateDownloadBtn
                text: updates.installing
                      ? qsTr("Restarting…")
                      : updates.downloading
                        ? qsTr("Downloading… %1%")
                              .arg(Math.round(updates.downloadProgress * 100))
                        : qsTr("Update now")
                enabled: updates.downloadUrl.length > 0
                         && !updates.downloading && !updates.installing
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: updates.downloadUrl.length > 0
                              ? qsTr("Downloads and installs automatically, then restarts the app.")
                              : qsTr("No download for this platform yet — check the website.")
                // Filled primary: Fluent blue + white, explicit so the
                // banner never inherits palette colors.
                contentItem: Text {
                    text: updateDownloadBtn.text
                    color: updateDownloadBtn.enabled
                           ? Theme.onPrimary : "#a6b8cc"
                    font: updateDownloadBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitHeight: 28
                    radius: Theme.radiusS
                    color: !updateDownloadBtn.enabled ? "#33ffffff"
                         : updateDownloadBtn.pressed ? Theme.primaryPressed
                         : updateDownloadBtn.hovered ? Theme.primaryHover
                         : Theme.primaryBg
                }
                onClicked: updates.downloadAndInstall()
            }
            ToolButton {
                id: updateDismissBtn
                text: "✕"
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Dismiss")
                contentItem: Text {
                    text: updateDismissBtn.text
                    color: "#ffffff"
                    font: updateDismissBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusS
                    color: updateDismissBtn.pressed ? "#59ffffff"
                         : updateDismissBtn.hovered ? "#33ffffff" : "transparent"
                }
                onClicked: updates.dismiss()
            }
        }
    }
}
