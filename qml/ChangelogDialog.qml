import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pops once on the first launch of a new version. Reads CHANGELOG.md
// out of the resource system (qt_add_resources writes it to qrc:/),
// renders it via TextEdit's MarkdownText format. The dialog stamps
// settings.appVersion into layoutSettings.lastSeenVersion when it
// closes, so the user sees this exactly once per version bump.
Dialog {
    id: root

    title: qsTr("What's new in v%1").arg(settings.appVersion)
    modal: true
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    width: Math.min(parent ? parent.width  - 80 : 640, 640)
    height: Math.min(parent ? parent.height - 80 : 520, 520)

    function loadChangelog() {
        // Read via the C++ helper, which picks the localized
        // CHANGELOG.<locale>.md when one is bundled (CN today; add
        // more by dropping CHANGELOG.<locale>.md in the repo root
        // and listing it in CMakeLists's qt_add_resources block)
        // and falls back to plain CHANGELOG.md otherwise. Empty
        // settings.uiLanguage tells the helper to follow
        // QLocale::system().
        const txt = layoutSettings.readChangelog(settings.uiLanguage)
        body.text = txt && txt.length > 0
                    ? txt
                    : qsTr("(changelog unavailable)")
    }

    onAboutToShow: loadChangelog()
    onClosed: {
        if (typeof layoutSettings !== "undefined")
            layoutSettings.setLastSeenVersion(settings.appVersion)
    }

    contentItem: Flickable {
        id: scroll
        contentWidth: width
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }

        TextArea {
            id: body
            width: parent.width
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            // MarkdownText is sufficient for our headings + bullets;
            // we don't need cmark-gfm's tables/footnotes here, so we
            // skip MarkdownRenderer to keep the dialog dependency-
            // free.
            textFormat: TextEdit.MarkdownText
            background: null
            color: "#1d1d1d"
            font.pixelSize: 13
            leftPadding: 16
            rightPadding: 16
            topPadding: 12
            bottomPadding: 12
        }
    }
}
