import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Dialog {
    id: dialog
    title: vision.page >= 0
           ? qsTr("Page %1 — Vision read").arg(vision.page + 1)
           : qsTr("Vision read")
    modal: true
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    width: Math.min(760, parent ? parent.width - 48 : 760)
    height: Math.min(620, parent ? parent.height - 48 : 620)
    padding: Theme.dialogPadding

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
        // View dialog: the lone Close button is the primary action.
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
    contentItem: ColumnLayout {
        spacing: Theme.spaceM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceS

            BusyIndicator {
                running: vision.status === VisionService.Rendering
                         || vision.status === VisionService.Generating
                visible: running
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            Label {
                Layout.fillWidth: true
                color: vision.status === VisionService.Failed ? Theme.danger : Theme.dimText
                font.pixelSize: 12
                elide: Text.ElideRight
                text: {
                    switch (vision.status) {
                    case VisionService.Rendering:  return qsTr("Rendering page…")
                    case VisionService.Generating: return qsTr("Reading with vision…")
                    case VisionService.Failed:     return qsTr("Failed: %1").arg(vision.lastError)
                    case VisionService.Done:       return qsTr("Done.")
                    default: return ""
                    }
                }
            }
            ActionButton {
                text: vision.status === VisionService.Generating
                      || vision.status === VisionService.Rendering
                      ? qsTr("Cancel")
                      : qsTr("Re-run")
                enabled: paperController.status === PaperController.Ready
                         && settings.isConfigured
                onClicked: {
                    if (vision.status === VisionService.Generating
                        || vision.status === VisionService.Rendering) {
                        vision.cancel()
                    } else {
                        vision.readPage(vision.page >= 0
                                        ? vision.page : pdfView.currentPage)
                    }
                }
            }
        }

        // Result card — readable Markdown output on a quiet surface.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusM
            color: Theme.cardBg
            border.width: 1
            border.color: Theme.divider
            clip: true

            ScrollView {
                id: visionScroll
                anchors.fill: parent
                anchors.margins: 1
                clip: true

                TextArea {
                    id: textArea
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.MarkdownText
                    background: null
                    color: Theme.text
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.onAccent
                    font.pixelSize: 13
                    leftPadding: Theme.spaceL
                    rightPadding: Theme.spaceL
                    topPadding: Theme.spaceM
                    bottomPadding: Theme.spaceM
                    text: vision.text.length > 0
                          ? vision.text
                          : qsTr("Click 'Read page (vision)' to send the current "
                                 + "page image to the LLM.")

                    // Follow the bottom while the vision model streams so
                    // newly-arrived tokens stay visible.
                    onContentHeightChanged: {
                        if (vision.status === VisionService.Generating)
                            Qt.callLater(visionScroll.scrollToEnd)
                    }
                }

                function scrollToEnd() {
                    const f = visionScroll.contentItem
                    if (f) f.contentY = Math.max(0, f.contentHeight - f.height)
                }
            }
        }
    }
}
