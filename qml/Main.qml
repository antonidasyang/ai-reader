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
    // Every toolbar button: the icon carries the meaning, the tooltip
    // carries the words. `tip` rather than ToolTip.text so each button is
    // one line at the call site.
    component ToolIcon: ToolButton {
        property string tip: ""
        // A button that only means something inside a cloud project. It
        // stays on the toolbar when there is no project rather than
        // disappearing -- a control that vanishes reads as a feature that
        // was removed, and the user cannot ask a missing button what it
        // wants. It dims, says what it needs, and clicking it does that
        // thing: signs in, or points at the project list.
        property bool needsProject: false
        readonly property bool blocked: needsProject && !window.projectReady
        display: AbstractButton.IconOnly
        icon.width: 18
        icon.height: 18
        // Tinted to the theme's text colour, so a set of single-stroke
        // glyphs reads as one family in both themes.
        icon.color: (enabled && !blocked) ? Theme.text : Theme.dimText
        ToolTip.visible: hovered && ToolTip.text.length > 0
        ToolTip.delay: 400
        ToolTip.text: blocked
                      ? (auth.authenticated
                         ? qsTr("Choose a project first — then: %1").arg(tip)
                         : qsTr("Sign in first — then: %1").arg(tip))
                      : tip
    }

    // Everything project-wide -- members, the profile, batch interpretation,
    // comparison, the project analyses -- needs both an account and a chosen
    // project.
    readonly property bool projectReady: auth.authenticated
                                         && projects.currentId.length > 0

    // What a blocked project button does instead of its own job: the step
    // that is actually missing. Never nothing -- a button that answers a
    // click with silence is worse than one that is not there.
    function resolveProjectBlock() {
        if (!auth.authenticated)
            auth.startCasLogin()
        else
            projectCombo.popup.open()
    }

    // Closing with work in flight asks first (see quitTasksDialog); this is
    // how the answer gets back to the second close.
    property bool forceClose: false
    // A version popup that had to wait behind the resume prompt.
    property string pendingVersionPopup: ""

    onClosing: function(close) {
        if (window.forceClose || tasks.activeCount === 0)
            return
        close.accepted = false
        quitTasksDialog.runningCount = tasks.runningCount
        quitTasksDialog.queuedCount = tasks.queuedCount
        quitTasksDialog.open()
    }

    // ── Where the reader was in each paper ──────────────────────────
    // Saved as they scroll (the C++ side debounces the writes) and put back
    // when the paper comes round again. The key is the paperId, so it
    // survives the file being moved — and a paper opened from a project,
    // whose file on disk is a checksum.
    property string readingKey: ""
    property bool restoringPosition: false

    function restoreReadingPosition() {
        const key = paperController.paperId
        if (key.length === 0 || pdfView.pageRows === 0)
            return                       // nothing laid out to scroll yet
        window.readingKey = key
        const y = layoutSettings.readingPosition(key)
        if (y <= 0)
            return
        window.restoringPosition = true
        pdfView.viewportY = y
        Qt.callLater(function() { window.restoringPosition = false })
    }

    Timer {
        id: restorePositionTimer
        interval: 120
        onTriggered: window.restoreReadingPosition()
    }
    Connections {
        target: paperController
        function onBlocksChanged() {
            // A different paper: stop attributing scrolling to the old one
            // until the new one's position has been put back.
            window.readingKey = ""
            restorePositionTimer.restart()
        }
    }
    Connections {
        target: pdfView
        function onPageRowsChanged() { restorePositionTimer.restart() }
        function onViewportYChanged() {
            if (window.readingKey.length > 0 && !window.restoringPosition)
                layoutSettings.setReadingPosition(window.readingKey,
                                                  pdfView.viewportY)
        }
    }

    // The paper on screen, named the way the library names it. One opened
    // from a project plays out of the content-addressed cache, so its file
    // name is a sha256; the tab bar already resolves that through the
    // library and the caption has to give the same answer. tabs.count is
    // named so the binding re-runs when the titles are refreshed after a
    // sync — nameAt is an invokable and notifies nothing by itself.
    readonly property string paperDisplayName: {
        const named = (tabs.count, tabs.activeIndex >= 0
                       ? tabs.nameAt(tabs.activeIndex) : "")
        return named.length > 0 ? named : paperController.fileName
    }
    title: paperDisplayName.length > 0
           ? "AI Reader — " + paperDisplayName
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

    // Closing while a model is still working, and picking that work back up
    // on the next launch.
    QuitTasksDialog {
        id: quitTasksDialog
        anchors.centerIn: Overlay.overlay
        onConfirmed: {
            window.forceClose = true
            window.close()
        }
    }
    ResumeTasksDialog {
        id: resumeTasksDialog
        anchors.centerIn: Overlay.overlay
    }

    // ── Saved layouts ─────────────────────────────
    SaveLayoutDialog {
        id: saveLayoutDialog
        anchors.centerIn: Overlay.overlay
        onSaveConfirmed: function(name) { window.saveLayout(name) }
        onRenameConfirmed: function(from, to) { layouts.rename(from, to) }
    }

    // Renaming and deleting live here rather than on the menu rows: a
    // small ✕ sitting beside a layout's name reads as "close this", which
    // is what ✕ means everywhere else in this app, and it deleted the
    // layout instead. In here the buttons say what they do in words and
    // the delete is confirmed on the row it belongs to.
    ManageLayoutsDialog {
        id: manageLayoutsDialog
        anchors.centerIn: Overlay.overlay
        paneLabels: window.paneLabels
        onRenameRequested: function(name) {
            saveLayoutDialog.openForRename(name)
        }
        onSaveRequested: saveLayoutDialog.openForSave()
    }

    // Cloud-library dialogs (the toolbar account/project group drives these).
    MembersDialog { id: membersDialog }
    ProjectSettingsDialog { id: projectSettingsDialog }
    ProjectProfileDialog { id: projectProfileDialog }
    BatchAnalysisDialog { id: batchAnalysisDialog }
    CompareDialog { id: compareDialog }
    // Re-segmenting is destructive in a way that is not obvious: the
    // paragraph ids change, so translations keyed to them stop matching and
    // any manual split/merge is gone. Asked once, with the numbers, rather
    // than silently redone.
    AppDialog {
        id: resegmentDialog
        title: qsTr("Segment this paper again?")
        width: 460
        standardButtons: Dialog.NoButton

        property int paragraphs: 0
        property int translated: 0

        function ask() {
            paragraphs = paperController.blockCount
            translated = translation.translatedParagraphs()
            if (paragraphs === 0) {          // nothing to lose: just do it
                paperController.rebuildBlocks()
                return
            }
            open()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: resegmentDialog.translated > 0
                      ? qsTr("This paper is already split into %1 paragraphs, and "
                             + "%2 of them are translated.")
                            .arg(resegmentDialog.paragraphs)
                            .arg(resegmentDialog.translated)
                      : qsTr("This paper is already split into %1 paragraphs.")
                            .arg(resegmentDialog.paragraphs)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.dimText
                font.pixelSize: 12
                text: resegmentDialog.translated > 0
                      ? qsTr("Splitting it again replaces that division and any "
                             + "paragraph you merged or split by hand. The "
                             + "existing translations are tied to the old "
                             + "paragraphs, so most of them will no longer match "
                             + "and would have to be translated again.")
                      : qsTr("Splitting it again replaces that division, including "
                             + "any paragraph you merged or split by hand.")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Cancel")
                    onClicked: resegmentDialog.close()
                }
                AppButton {
                    text: qsTr("Segment again")
                    danger: resegmentDialog.translated > 0
                    primary: resegmentDialog.translated === 0
                    onClicked: {
                        resegmentDialog.close()
                        paperController.rebuildBlocks()
                    }
                }
            }
        }
    }

    AppDialog {
        id: createProjectDialog
        title: qsTr("New project")
        width: 360
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (newProjName.text.trim().length > 0)
                projects.createProject(newProjName.text.trim(), newProjDesc.text)
            newProjName.text = ""
            newProjDesc.text = ""
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            AppTextField {
                id: newProjName
                Layout.fillWidth: true
                placeholderText: qsTr("Project name")
            }
            AppTextField {
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
    AppDialog {
        id: translateChoiceDialog
        title: qsTr("Translate this paper")
        width: 420
        standardButtons: Dialog.Cancel

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
            AppButton {
                Layout.fillWidth: true
                primary: true
                visible: translateChoiceDialog.leftCount > 0
                text: qsTr("Translate the remaining %1")
                          .arg(translateChoiceDialog.leftCount)
                onClicked: {
                    translateChoiceDialog.close()
                    translation.translateAll()
                }
            }
            AppButton {
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
        window.layoutTouched()
    }

    // Put the panes in `desired` order, left to right. Ids this build has
    // no pane for are stepped over rather than counted -- a saved order (or
    // a saved layout) written by another version may name a pane that is
    // not here, and letting it consume a slot would shift every pane after
    // it one place to the right.
    function applyPaneOrder(desired) {
        if (!desired || desired.length === 0) return
        let dst = 0
        for (let k = 0; k < desired.length; ++k) {
            const id = desired[k]
            for (let i = dst; i < split.count; ++i) {
                const it = split.itemAt(i)
                if (it && it.objectName === id) {
                    if (i !== dst) {
                        const item = split.takeItem(i)
                        split.insertItem(dst, item)
                    }
                    ++dst
                    break
                }
            }
        }
    }

    function applySavedPaneOrder() {
        const csv = layoutSettings.paneOrder()
        if (!csv || csv.length === 0) return
        window.applyPaneOrder(csv.split(","))
    }

    // ── Saved layouts ─────────────────────────────────────────────────
    // A layout is the arrangement itself -- which panes are showing, how
    // wide each one is, what order they sit in -- saved under a name and
    // put back on demand. `layouts` (LayoutPresets) owns the storage, the
    // naming rules and the JSON; the two functions below are the half only
    // this file can do, because only this file can see the SplitView.
    //
    // While a layout is being applied every change it makes goes through
    // the same properties a click or a drag writes, so this flag is what
    // tells the two apart: the reader moving something means the panes are
    // no longer the saved layout they came from, the layout putting them
    // there does not.
    // What each pane is called, for anything that has to list panes by
    // name rather than draw them -- the layout manager, telling one saved
    // arrangement from another. The ids are the SplitView children's
    // objectNames, which is what a saved layout stores; this file is the
    // only one that knows both halves, so the map lives here and is handed
    // to whoever needs it. A pane missing from the map (a layout saved by
    // a newer build) shows its id rather than disappearing.
    readonly property var paneLabels: ({
        "folder":   qsTr("Folder"),
        "library":  qsTr("Library"),
        "toc":      qsTr("Outline"),
        "pdf":      qsTr("PDF"),
        "blocks":   qsTr("Paragraphs"),
        "analysis": qsTr("Interpretation"),
        "research": qsTr("Project analyses"),
        "tasks":    qsTr("Tasks"),
        "chat":     qsTr("Chat")
    })

    property bool applyingLayout: false
    // True until the window has finished building itself. Every pane's
    // `visible` settles while the SplitView is being created, which fires
    // onVisibleChanged for each pane that was saved hidden -- that is the
    // arrangement being restored, not the reader changing it, and reading
    // it as a change would clear the tick beside the applied layout on
    // every single launch.
    property bool startingUp: true

    function layoutTouched() {
        if (window.applyingLayout || window.startingUp) return
        layouts.current = ""
    }

    // The arrangement on screen, ready to be saved. Widths are FRACTIONS of
    // the row rather than pixels: a layout saved on a 4K desktop has to be
    // usable on a 1366-wide laptop, and pixel widths carried across would
    // leave the reader nothing but scrollbars. Window geometry is left out
    // on purpose -- a position from another machine's monitors is a window
    // nobody can find.
    function paneLayoutSnapshot() {
        const total = split.width
        const order = []
        const panes = {}
        for (let i = 0; i < split.count; ++i) {
            const it = split.itemAt(i)
            if (!it || !it.objectName || it.objectName.length === 0)
                continue
            order.push(it.objectName)
            // A hidden pane is 0 wide on screen; what it would come back at
            // is its preferred width, and failing that the width the app
            // has been remembering for it all along.
            let px = it.visible ? it.width : it.SplitView.preferredWidth
            if (!(px > 0))
                px = layoutSettings.paneWidth(it.objectName, 0)
            panes[it.objectName] = {
                "visible": it.visible,
                "width": (total > 0 && px > 0) ? (px / total) : 0
            }
        }
        return { "order": order, "panes": panes }
    }

    // Put a saved arrangement back. Everything below is written through the
    // properties the reader's own clicks and drags write -- `visible`, the
    // pane's preferred width, and the take/insert the DockGrip uses -- so
    // the result is a hand-made arrangement as far as the rest of the app
    // is concerned, and it persists exactly like one.
    function applyLayout(name) {
        const mins = {}
        for (let i = 0; i < split.count; ++i) {
            const it = split.itemAt(i)
            if (it && it.objectName && it.objectName.length > 0)
                mins[it.objectName] = it.SplitView.minimumWidth
        }
        // C++ turns the fractions back into pixels for this window and
        // clamps them: nothing below its minimum, nothing wider than the
        // row, and the whole thing made to fit.
        const plan = layouts.resolve(name, split.width, mins)
        if (!plan || !plan.found)
            return false

        window.applyingLayout = true
        const panes = plan.panes
        for (let i = 0; i < split.count; ++i) {
            const it = split.itemAt(i)
            if (!it || !it.objectName || it.objectName.length === 0)
                continue
            const want = panes[it.objectName]
            // A pane the layout says nothing about is left exactly as it
            // is. A layout saved by an older build has no opinion about a
            // pane that build did not have, and reading silence as "hide
            // it" would make every upgrade look like a pane had been taken
            // away.
            if (!want) continue
            it.visible = want.visible
            // The filling pane takes whatever the others leave it;
            // assigning it a width would only fight the SplitView.
            if (want.width > 0 && !it.SplitView.fillWidth)
                it.SplitView.preferredWidth = want.width
        }
        window.applyPaneOrder(plan.order)
        window.persistPaneOrder()
        window.applyingLayout = false
        layouts.current = name
        return true
    }

    function saveLayout(name) {
        layouts.save(name, window.paneLayoutSnapshot())
    }

    Component.onCompleted: {
        // A remote desktop pays for every animated frame in encoded pixels,
        // so this session does without the small fades.
        if (settings.remoteRenderingActive)
            Theme.animMs = 0

        applySavedPaneOrder()

        // Pane sizes are restored automatically: each pane binds
        // SplitView.preferredWidth to layoutSettings.paneWidth(...)
        // and persists via onWidthChanged. The C++ setter debounces
        // writes so a drag becomes one disk write.

        // Every pane has settled by now, so anything that moves from here
        // on is the reader moving it.
        window.startingUp = false

        // Wire spotlight targets now that the toolbar / panes exist.
        welcomeWizard.steps = buildWizardSteps()

        // First-render popups: the welcome tour replays on the first
        // launch of EVERY new version (it also stamps
        // lastSeenVersion, so the changelog dialog never stacks on
        // top of it in the same session).
        const versionPopup =
            layoutSettings.wizardSeenVersion() !== settings.appVersion ? "wizard"
            : layoutSettings.lastSeenVersion() !== settings.appVersion ? "changelog"
            : ""

        // Work the last session did not finish is the more urgent question,
        // and it is the one the user can still answer wrongly by ignoring
        // it, so it goes first and the version popup waits its turn.
        if (tasks.pendingCount > 0) {
            window.pendingVersionPopup = versionPopup
            Qt.callLater(function() { resumeTasksDialog.open() })
        } else if (versionPopup === "wizard") {
            Qt.callLater(function() { welcomeWizard.start() })
        } else if (versionPopup === "changelog") {
            Qt.callLater(function() { changelogDialog.open() })
        }
    }

    Connections {
        target: resumeTasksDialog
        function onClosed() {
            const next = window.pendingVersionPopup
            window.pendingVersionPopup = ""
            if (next === "wizard")
                welcomeWizard.start()
            else if (next === "changelog")
                changelogDialog.open()
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
            resegmentDialog.ask()
        }
    }

    header: ToolBar {
        id: toolBar
        // Grouped by what the buttons act on — the file, the view, this
        // paper, the panes, the project — with the words moved into
        // tooltips. Twenty-five labelled buttons in one row had stopped
        // being a toolbar and become a sentence.
        //
        // A sentence that long outgrows a narrow window, and it used to do
        // it in silence: the row kept every button at its natural size and
        // drew the right-hand end — settings, help, the account, half the
        // project group — past the edge of the window, where it could be
        // neither seen nor clicked. It now gives ground in three steps.
        // The readouts go first, since they are the only things here that
        // cannot be clicked. What is still too wide scrolls, with an arrow
        // at each end pointing at the part that is off. And the app's own
        // group never scrolls at all: settings and the tour sit against the
        // right edge at every window size, one click away.

        // How much room the row has, counting the arrows' lanes as spent
        // whether or not they are showing: it keeps this number a plain
        // function of the window, which is what lets the row measure itself
        // below without measuring its own answer.
        readonly property real room: width - appTail.width - 56
        // What the row wants in each state, recorded rather than guessed:
        // the row's own implicitWidth, sampled whenever it is in that
        // state. A step is then undone against the very number that called
        // for it, which is what keeps the answer still -- decide from the
        // width the row happens to have and every step changes the question.
        property real fullWidth: 0     // with every readout spelled out
        property real compactWidth: 0  // with the words out of them
        property bool compact: false
        property bool tight: false
        readonly property bool overflowing:
            toolRow.implicitWidth > width - appTail.width - 12
        // Two children now instead of one filling child, so the Pane can no
        // longer work its own height out.
        contentHeight: Math.max(toolRow.implicitHeight, appTail.implicitHeight)

        function reflow() {
            if (fullWidth <= 0)
                return
            compact = fullWidth > room
            // Only if there was something left to shorten: with no paper
            // open and no account there is nothing in the row that has a
            // long form, and taking a step that saves nothing would just
            // be a readout blinking out for no gain.
            tight = compact && compactWidth > room
        }
        onRoomChanged: reflow()
        Component.onCompleted: {
            fullWidth = toolRow.implicitWidth
            reflow()
        }

        // The tour spotlights toolbar buttons by where they are on the
        // glass, and a button scrolled off the end is nowhere: put the
        // step's targets back in view before it goes looking for them.
        function reveal(items) {
            if (!items || items.length === 0)
                return
            let lo = Infinity, hi = -Infinity
            for (let i = 0; i < items.length; ++i) {
                const it = items[i]
                if (!it || !it.visible)
                    continue
                let inRow = false
                for (let a = it.parent; a; a = a.parent)
                    if (a === toolRow) { inRow = true; break }
                if (!inRow)
                    continue
                const x = it.mapToItem(toolRow, 0, 0).x
                lo = Math.min(lo, x)
                hi = Math.max(hi, x + it.width)
            }
            if (lo === Infinity)
                return
            if (lo < toolFlick.contentX)
                toolFlick.glideTo(lo - 8)
            else if (hi > toolFlick.contentX + toolFlick.width)
                toolFlick.glideTo(hi - toolFlick.width + 8)
        }

        Connections {
            target: welcomeWizard
            // On every step, and on the first one too: opening the tour
            // does not change which buttons it is pointing at.
            function onTargetItemsChanged() {
                if (welcomeWizard.opened)
                    toolBar.reveal(welcomeWizard.targetItems)
            }
            function onOpenedChanged() {
                if (welcomeWizard.opened)
                    toolBar.reveal(welcomeWizard.targetItems)
            }
        }

        Flickable {
            id: toolFlick
            anchors.left: parent.left
            anchors.right: appTail.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            // The arrows get lanes of their own rather than floating over
            // the row: half a covered button reads worse than a gap does.
            anchors.leftMargin: toolBar.overflowing ? 28 : 8
            anchors.rightMargin: toolBar.overflowing ? 28 : 4
            clip: true
            contentWidth: toolRow.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            // Every way of moving the row goes through these two, which is
            // also where it gets clamped: a wider window, or a button that
            // came or went, can otherwise leave the row parked past its end.
            function jumpTo(x) {
                scrollAnim.stop()
                contentX = Math.max(0, Math.min(x, contentWidth - width))
            }
            function glideTo(x) {
                scrollAnim.stop()
                scrollAnim.to = Math.max(0, Math.min(x, contentWidth - width))
                scrollAnim.start()
            }
            onWidthChanged: jumpTo(contentX)
            onContentWidthChanged: jumpTo(contentX)

            NumberAnimation {
                id: scrollAnim
                target: toolFlick
                property: "contentX"
                duration: Theme.animMs * 1.5
                easing.type: Easing.OutCubic
            }

            // Most mice have one wheel and this row runs sideways, so a
            // vertical turn scrolls it too; a trackpad's sideways swipe
            // arrives on the other axis and means the same thing.
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function (wheel) {
                    toolFlick.jumpTo(toolFlick.contentX
                                     - (wheel.angleDelta.x !== 0
                                        ? wheel.angleDelta.x
                                        : wheel.angleDelta.y))
                }
            }

            RowLayout {
                id: toolRow
                // Every change of state passes through here -- a paper
                // opening, a sign-in, a step taken above -- so this is
                // where the two natural widths are kept honest.
                onImplicitWidthChanged: {
                    if (!toolBar.compact)
                        toolBar.fullWidth = implicitWidth
                    else if (!toolBar.tight)
                        toolBar.compactWidth = implicitWidth
                    toolBar.reflow()
                }
                height: toolFlick.height
                // Its natural width when that is the larger — that is what
                // there is to scroll through — and the viewport's when there
                // is room to spare, so the spacer further down still pushes
                // the project group over to the right.
                width: Math.max(implicitWidth, toolFlick.width)
                spacing: 2

                // ── the file ────────────────────────────────────────────
                ToolIcon {
                    id: openBtn
                    icon.source: "qrc:/icons/open.svg"
                    tip: qsTr("Open a PDF…")
                    onClicked: fileDialog.open()
                }
                ToolIcon {
                    id: openFolderBtn
                    icon.source: "qrc:/icons/open-folder.svg"
                    tip: qsTr("Open a folder of PDFs…")
                    onClicked: openFolderDialog.open()
                }
                ToolIcon {
                    icon.source: "qrc:/icons/export.svg"
                    enabled: paperController.status === PaperController.Ready
                    tip: qsTr("Export text: the raw PDF text, per-line boxes and "
                              + "detected paragraphs, to a .txt file")
                    onClicked: exportTextDialog.open()
                }

                ToolSeparator {}

                // ── the view ────────────────────────────────────────────
                ToolIcon {
                    icon.source: "qrc:/icons/zoom-out.svg"
                    enabled: pdfDoc.status === PdfDocument.Ready
                    tip: qsTr("Zoom out")
                    onClicked: window.zoomOut()
                }
                ToolButton {
                    // Doubles as a "current zoom" readout and a zoom action:
                    // click = back to 100%. Fit-to-width lives on its own
                    // toolbar button.
                    text: pdfDoc.status === PdfDocument.Ready
                          ? Math.round(pdfView.renderScale * 100) + "%"
                          : "—"
                    enabled: pdfDoc.status === PdfDocument.Ready
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Back to 100%")
                    onClicked: window.resetZoom()
                }
                ToolIcon {
                    icon.source: "qrc:/icons/zoom-in.svg"
                    enabled: pdfDoc.status === PdfDocument.Ready
                    tip: qsTr("Zoom in")
                    onClicked: window.zoomIn()
                }
                ToolIcon {
                    icon.source: "qrc:/icons/fit-width.svg"
                    enabled: pdfDoc.status === PdfDocument.Ready
                    tip: qsTr("Fit the page to the window width")
                    onClicked: window.fitWidth()
                }
                ToolIcon {
                    id: panToggleBtn
                    icon.source: "qrc:/icons/pan.svg"
                    checkable: true
                    // Bound, not assigned: a click writes `checked` itself, and a
                    // plain binding would not survive that -- the button would stop
                    // following the pane the moment anything else showed it.
                    Binding on checked { value: window.panMode; restoreMode: Binding.RestoreNone }
                    enabled: pdfDoc.status === PdfDocument.Ready
                    tip: qsTr("Hand tool: drag to move the page. Off = select text.")
                    onClicked: window.panMode = !window.panMode
                }

                ToolSeparator {}

                // ── this paper ──────────────────────────────────────────
                ToolIcon {
                    // One button, two readings: with no paragraphs yet this is
                    // the primary "segment this paper" action (auto segmentation
                    // is off by default); once there are paragraphs it means
                    // "throw them away and redo it", which asks first.
                    readonly property bool _firstRun: paperController.blockCount === 0
                    icon.source: "qrc:/icons/segment.svg"
                    enabled: paperController.status === PaperController.Ready
                             && !paperController.extracting
                    tip: _firstRun
                        ? qsTr("Split this paper into paragraphs (needed for "
                               + "translation, the outline and chat)")
                        : qsTr("Split it into paragraphs again, discarding the "
                               + "current division")
                    onClicked: resegmentDialog.ask()
                }
                ToolIcon {
                    id: translateBtn
                    icon.source: "qrc:/icons/translate.svg"
                    enabled: paperController.status === PaperController.Ready
                             && (translation.busy || settings.isConfigured)
                    display: translation.busy ? AbstractButton.TextBesideIcon
                                              : AbstractButton.IconOnly
                    text: translation.busy ? qsTr("Stop") : ""
                    tip: translation.busy ? qsTr("Stop translating")
                                          : qsTr("Translate every paragraph")
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
                ToolIcon {
                    icon.source: "qrc:/icons/retry.svg"
                    visible: !translation.busy && translation.failedCount > 0
                    display: AbstractButton.TextBesideIcon
                    text: translation.failedCount
                    tip: qsTr("Translate the paragraphs that failed")
                    onClicked: translation.retryFailed()
                }
                ToolIcon {
                    icon.source: "qrc:/icons/vision.svg"
                    enabled: paperController.status === PaperController.Ready
                             && settings.isConfigured
                             && vision.status !== VisionService.Generating
                             && vision.status !== VisionService.Rendering
                    tip: qsTr("Read this page with vision: figures, tables and "
                              + "equations as the model sees them")
                    onClicked: {
                        visionDialog.open()
                        vision.readPage(pdfView.currentPage)
                    }
                }
                ToolIcon {
                    icon.source: "qrc:/icons/quote.svg"
                    visible: paperController.currentSelection.length > 0
                    tip: qsTr("Quote the highlighted text into the chat")
                    onClicked: {
                        chatPane.visible = true
                        chatPane.prefillInput(paperController.currentSelection,
                                              paperController.currentSelectionPage + 1)
                    }
                }

                ToolSeparator {}

                // ── the panes ───────────────────────────────────────────
                ToolIcon {
                    id: folderToggleBtn
                    icon.source: "qrc:/icons/pane-folder.svg"
                    checkable: true
                    // Bound, not assigned: a click writes `checked` itself, and a
                    // plain binding would not survive that -- the button would stop
                    // following the pane the moment anything else showed it.
                    Binding on checked { value: folderPane.visible; restoreMode: Binding.RestoreNone }
                    tip: qsTr("Folder pane: browse PDFs on this machine")
                    onClicked: folderPane.visible = !folderPane.visible
                }
                ToolIcon {
                    id: libToggleBtn
                    icon.source: "qrc:/icons/pane-library.svg"
                    checkable: true
                    // Bound, not assigned: a click writes `checked` itself, and a
                    // plain binding would not survive that -- the button would stop
                    // following the pane the moment anything else showed it.
                    Binding on checked { value: libraryPane.visible; restoreMode: Binding.RestoreNone }
                    tip: qsTr("Library pane: the papers in this project")
                    onClicked: libraryPane.visible = !libraryPane.visible
                }
                ToolIcon {
                    id: blocksToggleBtn
                    icon.source: "qrc:/icons/pane-paragraphs.svg"
                    checkable: true
                    // Bound, not assigned: a click writes `checked` itself, and a
                    // plain binding would not survive that -- the button would stop
                    // following the pane the moment anything else showed it.
                    Binding on checked { value: blockList.visible; restoreMode: Binding.RestoreNone }
                    tip: qsTr("Paragraph pane: the paper's text, its translation, "
                              + "and the per-paragraph actions")
                    onClicked: blockList.visible = !blockList.visible
                }
                ToolIcon {
                    id: tocToggleBtn
                    icon.source: "qrc:/icons/pane-toc.svg"
                    checkable: true
                    // Bound, not assigned: a click writes `checked` itself, and a
                    // plain binding would not survive that -- the button would stop
                    // following the pane the moment anything else showed it.
                    Binding on checked { value: tocSidebar.visible; restoreMode: Binding.RestoreNone }
                    tip: qsTr("Outline pane: the paper's sections")
                    onClicked: tocSidebar.visible = !tocSidebar.visible
                }
                ToolIcon {
                    icon.source: "qrc:/icons/pane-interpret.svg"
                    checkable: true
                    // Bound, not assigned: a click writes `checked` itself, and a
                    // plain binding would not survive that -- the button would stop
                    // following the pane the moment anything else showed it.
                    Binding on checked { value: analysisPane.visible; restoreMode: Binding.RestoreNone }
                    tip: qsTr("Interpretation pane: relevance to this project, what "
                              + "to read first, and every statement traced back to "
                              + "the paper")
                    onClicked: analysisPane.visible = !analysisPane.visible
                }
                ToolIcon {
                    id: chatToggleBtn
                    icon.source: "qrc:/icons/pane-chat.svg"
                    checkable: true
                    // Bound, not assigned: a click writes `checked` itself, and a
                    // plain binding would not survive that -- the button would stop
                    // following the pane the moment anything else showed it.
                    Binding on checked { value: chatPane.visible; restoreMode: Binding.RestoreNone }
                    tip: qsTr("Chat pane: ask about this paper")
                    onClicked: chatPane.visible = !chatPane.visible
                }
                ToolButton {
                    // The last entry in the pane group, because it is about all
                    // of them at once: the arrangement the other buttons make,
                    // saved under a name and switched between.
                    id: layoutsBtn
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: layouts.current.length > 0
                                  ? qsTr("Saved layouts — showing “%1”")
                                        .arg(layouts.current)
                                  : qsTr("Saved layouts: arrange the panes, save "
                                         + "that arrangement under a name, and "
                                         + "switch between them")
                    // Drawn rather than an icon file: three columns with a wide
                    // one in the middle, which is what every one of these
                    // arrangements looks like from far enough away.
                    contentItem: Item {
                        implicitWidth: 18
                        implicitHeight: 18
                        Row {
                            anchors.centerIn: parent
                            spacing: 2
                            Repeater {
                                model: [3, 7, 3]
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: modelData
                                    height: 14
                                    radius: 1
                                    color: Theme.text
                                    opacity: index === 1 ? 0.5 : 0.9
                                }
                            }
                        }
                    }
                    onClicked: layoutMenu.popup()

                    LayoutMenu {
                        id: layoutMenu
                        onApplyRequested: function(name) { window.applyLayout(name) }
                        onSaveRequested: saveLayoutDialog.openForSave()
                        onManageRequested: manageLayoutsDialog.open()
                    }
                }

                Label {
                    // A readout, not a control, so it is the first thing
                    // asked to give ground when the window narrows: the
                    // words go, then the counter itself. What the app is
                    // chewing on never goes — this is the only place the
                    // toolbar says a paper is still being taken apart.
                    readonly property string work:
                        paperController.extracting ? qsTr("Segmenting…")
                        : structure.busy ? qsTr("GROBID…") : ""
                    readonly property string counter:
                        pdfDoc.status !== PdfDocument.Ready || toolBar.tight
                        ? ""
                        : toolBar.compact
                          ? qsTr("%1 / %2").arg(pdfView.currentPage + 1)
                                           .arg(pdfDoc.pageCount)
                          : qsTr("page %1 / %2 · %3 paragraphs")
                                .arg(pdfView.currentPage + 1)
                                .arg(pdfDoc.pageCount)
                                .arg(paperController.blockCount)
                    text: counter.length > 0 && work.length > 0
                          ? counter + " · " + work : counter + work
                    visible: text.length > 0
                    color: Theme.dimText
                    Layout.leftMargin: 8
                }
                Item { Layout.fillWidth: true }

                // ── the project ─────────────────────────────────────────
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
                    // The one wide control in the row; a narrow window buys
                    // two buttons' worth of space back from it, and the
                    // names it truncates are still readable in the popup.
                    Layout.preferredWidth: toolBar.compact ? 120 : 170
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
                ToolIcon {
                    icon.source: "qrc:/icons/project-new.svg"
                    visible: auth.authenticated
                    tip: qsTr("New project")
                    onClicked: createProjectDialog.open()
                }
                ToolIcon {
                    icon.source: "qrc:/icons/project-edit.svg"
                    needsProject: true
                    tip: qsTr("Rename this project, or delete it")
                    onClicked: {
                        if (blocked) { window.resolveProjectBlock(); return }
                        projectSettingsDialog.open()
                    }
                }
                ToolIcon {
                    icon.source: "qrc:/icons/members.svg"
                    needsProject: true
                    tip: qsTr("Members of this project")
                    onClicked: {
                        if (blocked) { window.resolveProjectBlock(); return }
                        projects.refreshMembers()
                        membersDialog.open()
                    }
                }
                ToolIcon {
                    // The one place the reader tells the app what this project is
                    // for. Every interpretation is prompted with it, so the button
                    // carries a dot until it has been filled in.
                    icon.source: "qrc:/icons/profile.svg"
                    needsProject: true
                    display: profile.hasProfile ? AbstractButton.IconOnly
                                                : AbstractButton.TextBesideIcon
                    text: profile.hasProfile ? "" : "•"
                    tip: profile.hasProfile
                         ? qsTr("Research profile: %1").arg(profile.summary)
                         : qsTr("Describe what this project is trying to find out — "
                                + "every interpretation is written against it")
                    onClicked: {
                        if (blocked) { window.resolveProjectBlock(); return }
                        projectProfileDialog.open()
                    }
                }

                ToolSeparator { visible: auth.authenticated }

                ToolIcon {
                    // A running batch is owned by the app, not by the window it
                    // was started from — closing that window does not stop it, so
                    // the progress has to be visible here too.
                    icon.source: "qrc:/icons/batch.svg"
                    needsProject: true
                    display: batchAnalysis.busy ? AbstractButton.TextBesideIcon
                                                : AbstractButton.IconOnly
                    text: batchAnalysis.busy
                          ? (batchAnalysis.done + batchAnalysis.failed
                             + batchAnalysis.skipped) + "/" + batchAnalysis.total
                          : ""
                    tip: batchAnalysis.busy
                         ? qsTr("Still interpreting — click to watch or stop")
                         : qsTr("Interpret every paper in this project, then filter "
                                + "by relevance")
                    onClicked: {
                        if (blocked) { window.resolveProjectBlock(); return }
                        batchAnalysisDialog.open()
                    }
                }
                ToolIcon {
                    icon.source: "qrc:/icons/compare.svg"
                    needsProject: true
                    display: compare.count > 0 ? AbstractButton.TextBesideIcon
                                               : AbstractButton.IconOnly
                    text: compare.count > 0 ? compare.count : ""
                    tip: qsTr("Put papers side by side, with a warning where they "
                              + "cannot honestly be compared")
                    onClicked: {
                        if (blocked) { window.resolveProjectBlock(); return }
                        compareDialog.open()
                    }
                }
                ToolIcon {
                    icon.source: "qrc:/icons/research.svg"
                    needsProject: true
                    checkable: true
                    // Bound, not assigned: a click writes `checked` itself, and a
                    // plain binding would not survive that -- the button would stop
                    // following the pane the moment anything else showed it.
                    Binding on checked { value: researchPane.visible; restoreMode: Binding.RestoreNone }
                    tip: qsTr("What this whole project adds up to: categories, the "
                              + "research map, consensus and conflict, coverage, and "
                              + "what to do next")
                    onClicked: {
                        if (blocked) { window.resolveProjectBlock(); return }
                        researchPane.visible = !researchPane.visible
                    }
                }
                ToolIcon {
                    icon.source: "qrc:/icons/tasks.svg"
                    checkable: true
                    // Bound, not assigned: a click writes `checked` itself, and a
                    // plain binding would not survive that -- the button would stop
                    // following the pane the moment anything else showed it.
                    Binding on checked { value: tasksPane.visible; restoreMode: Binding.RestoreNone }
                    // The count is the point when something is running: it is
                    // the only place the toolbar admits the app is busy.
                    display: tasks.activeCount > 0 ? AbstractButton.TextBesideIcon
                                                   : AbstractButton.IconOnly
                    text: tasks.activeCount > 0 ? tasks.activeCount : ""
                    tip: qsTr("Everything the app is working on: what is running, "
                              + "how far along it is, and how long it has left")
                    onClicked: tasksPane.visible = !tasksPane.visible
                }
                ToolIcon {
                    id: accountBtn
                    icon.source: "qrc:/icons/account.svg"
                    visible: auth.authenticated
                    // Who is signed in is worth a name's width when there is
                    // one to spare; when there is not, the icon still says
                    // "signed in" and the tooltip still says who.
                    display: toolBar.compact ? AbstractButton.IconOnly
                                             : AbstractButton.TextBesideIcon
                    text: auth.userDisplayName.length > 0 ? auth.userDisplayName
                                                          : auth.userEmail
                    tip: qsTr("Signed in as %1 — click to sign out")
                             .arg(text.length > 0 ? text : auth.userEmail)
                    onClicked: accountMenu.popup()
                    Menu {
                        id: accountMenu
                        MenuItem { text: qsTr("Sign out"); onTriggered: auth.logout() }
                    }
                }

                Label {
                    // Informative when it is configured — the same two words
                    // head the settings dialog — so it steps out of a narrow
                    // toolbar. The unconfigured warning stays at every width:
                    // that one explains why half these buttons are dead.
                    visible: !toolBar.compact || !settings.isConfigured
                    text: settings.isConfigured
                          ? qsTr("%1 · %2").arg(settings.provider).arg(settings.model)
                          : qsTr("LLM not configured")
                    // Theme tokens, not hardcoded hex: the old indigo/red pair
                    // was unreadable against the dark toolbar in dark mode.
                    color: settings.isConfigured ? Theme.accent : Theme.danger
                    font.pixelSize: 11
                    Layout.rightMargin: 4
                }
            }
        }

        // Which way the rest of the row is. Dimmed rather than hidden at
        // either end, because a button that comes and goes as you scroll
        // moves the row under the pointer you are scrolling it with.
        ToolIcon {
            anchors.left: parent.left
            anchors.verticalCenter: toolFlick.verticalCenter
            width: 24
            visible: toolBar.overflowing
            enabled: toolFlick.contentX > 0.5
            autoRepeat: true
            icon.source: "qrc:/icons/chevron-left.svg"
            tip: qsTr("More toolbar buttons this way")
            onClicked: toolFlick.glideTo(toolFlick.contentX
                                         - toolFlick.width * 0.6)
        }
        ToolIcon {
            anchors.right: appTail.left
            anchors.rightMargin: 2
            anchors.verticalCenter: toolFlick.verticalCenter
            width: 24
            visible: toolBar.overflowing
            enabled: toolFlick.contentX
                     < toolFlick.contentWidth - toolFlick.width - 0.5
            autoRepeat: true
            icon.source: "qrc:/icons/chevron-right.svg"
            tip: qsTr("More toolbar buttons this way")
            onClicked: toolFlick.glideTo(toolFlick.contentX
                                         + toolFlick.width * 0.6)
        }

        // ── the app ─────────────────────────────────────────────────
        // Outside the scrolling stretch on purpose: whatever the window's
        // width costs the rest of the toolbar, the settings and the tour
        // stay exactly where they have always been.
        RowLayout {
            id: appTail
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: 2

            ToolSeparator {}
            ToolIcon {
                icon.source: "qrc:/icons/prompts.svg"
                tip: qsTr("Edit the prompts the model is given")
                onClicked: promptsDialog.open()
            }
            ToolIcon {
                id: settingsBtn
                icon.source: "qrc:/icons/settings.svg"
                tip: qsTr("Settings")
                onClicked: settingsDialog.open()
            }
            ToolIcon {
                icon.source: "qrc:/icons/help.svg"
                tip: qsTr("Show the getting-started tour")
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

            // A handle the reader dragged is a change to the arrangement,
            // so the panes are no longer the saved layout they came from.
            // A window resize is not: every pane keeps its share of the row,
            // which is exactly what a layout stores.
            onResizingChanged: if (!resizing) window.layoutTouched()

            // ── Far left: folder browser (toggleable) ──────────────────
            // Visible by default if the user previously had a folder
            // open; auto-hidden otherwise so first-launch isn't crowded.
            FolderPane {
                id: folderPane
                // The library knows which file on disk a paper came from,
                // which is how a paper opened out of the project (and so
                // playing from a sha256-named blob) still highlights its own
                // row here. Naming paperId re-runs the lookup on each open.
                openPaperPath: (paperController.paperId,
                                libraryModel.localPathForPaperId(
                                    paperController.paperId))
                objectName: "folder"
                // Default: visible iff the user has a folder open
                // already (so a brand-new install with no library
                // doesn't waste a column on an empty pane). Once
                // the user toggles via the toolbar, the imperative
                // assignment breaks the binding and the new value
                // is persisted via onVisibleChanged below.
                visible: layoutSettings.paneVisible("folder",
                    library.currentFolder.length > 0)
                onVisibleChanged: {
                    layoutSettings.setPaneVisible("folder", visible)
                    window.layoutTouched()
                }
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
                onVisibleChanged: {
                    layoutSettings.setPaneVisible("library", visible)
                    window.layoutTouched()
                }
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
                onVisibleChanged: {
                    layoutSettings.setPaneVisible("toc", visible)
                    window.layoutTouched()
                }
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
                            if (ScrollKeys.handle(event, f))
                                return
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
                            // Relaying out the page table per mouse move is
                            // what made dragging a handle crawl.
                            resizing: split.resizing
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
                // Like every other pane, it can be put away — and it
                // remembers, so a reader who works from the PDF alone does
                // not get it back on every launch.
                visible: layoutSettings.paneVisible("blocks", true)
                onVisibleChanged: {
                    layoutSettings.setPaneVisible("blocks", visible)
                    window.layoutTouched()
                }
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

            // ── Structured interpretation pane (toggleable) ────────────
            AnalysisPane {
                id: analysisPane
                objectName: "analysis"
                resizing: split.resizing
                visible: layoutSettings.paneVisible("analysis", false)
                onVisibleChanged: {
                    layoutSettings.setPaneVisible("analysis", visible)
                    window.layoutTouched()
                }
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

            // ── Project-wide analysis (toggleable) ─────────────────────
            // A pane rather than a dialog: reading a category or an opening
            // means looking at a paper at the same time, and a modal window
            // covering the library made that impossible.
            ResearchPane {
                id: researchPane
                objectName: "research"
                resizing: split.resizing
                visible: layoutSettings.paneVisible("research", false)
                onVisibleChanged: {
                    layoutSettings.setPaneVisible("research", visible)
                    window.layoutTouched()
                }
                SplitView.preferredWidth: layoutSettings.paneWidth("research", 420)
                SplitView.minimumWidth: 300
                onWidthChanged: layoutSettings.setPaneWidth("research", width)

                // A paper named anywhere in a project-wide analysis opens
                // from there.
                onPaperActivated: function(paperId) {
                    const id = libraryModel.findByPaperId(paperId)
                    if (!id || id.length === 0)
                        return
                    const fields = libraryModel.itemFields(id)
                    fileSync.openItem(id, fields.localPath ? fields.localPath : "")
                }
                onAskAiRequested: function(text) {
                    chatPane.visible = true
                    chatPane.prefillInput(text, 0)
                }

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: researchPane
                    split: split
                    marker: dropMarker
                    onReordered: window.persistPaneOrder()
                }
            }

            // ── What the app is working on (toggleable) ────────────────
            TasksPane {
                id: tasksPane
                objectName: "tasks"
                resizing: split.resizing
                visible: layoutSettings.paneVisible("tasks", false)
                onVisibleChanged: {
                    layoutSettings.setPaneVisible("tasks", visible)
                    window.layoutTouched()
                }
                SplitView.preferredWidth: layoutSettings.paneWidth("tasks", 360)
                SplitView.minimumWidth: 260
                onWidthChanged: layoutSettings.setPaneWidth("tasks", width)

                DockGrip {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 4
                    anchors.topMargin: 7
                    pane: tasksPane
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
                onVisibleChanged: {
                    layoutSettings.setPaneVisible("chat", visible)
                    window.layoutTouched()
                }
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
