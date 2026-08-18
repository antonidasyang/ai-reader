import QtQuick
import QtQuick.Controls
import AiReader

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
    padding: 0

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
            text: root.title
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
        // The lone Close button is the dialog's one action — style it
        // as the primary so the exit is obvious.
        delegate: ActionButton { primary: true }
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
            color: ab.primary ? Theme.onAccent : Theme.text
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

    // ── Content ─────────────────────────────────────────────────────
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
            color: Theme.text
            selectionColor: Theme.accent
            selectedTextColor: Theme.onAccent
            font.pixelSize: 13
            leftPadding: Theme.spaceXl
            rightPadding: Theme.spaceXl
            topPadding: Theme.spaceL
            bottomPadding: Theme.spaceL
        }
    }
}
