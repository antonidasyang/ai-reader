import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Floating translation card for a PDF selection: right-click →
// Translate opens it next to the text. It reads TranslationService's
// snippet channel, which either mirrors the paragraph row the selection
// landed in (so the stream shows here and in the right pane at once) or
// streams an ad-hoc translation of the selected text.
//
// Anchored in the coordinates of whatever it is parented to (the PDF
// viewport), and clamped to stay inside it. It must be invisible when
// idle: anything stacked over the page area steals hover from the
// selection layer and kills the I-beam.
Rectangle {
    id: card

    // Where the user right-clicked, in parent coordinates.
    property real anchorX: 0
    property real anchorY: 0

    readonly property string status: translation.snippetStatus
    readonly property bool fromParagraph: translation.snippetIsParagraph
    readonly property bool busy: status === "translating"

    visible: status !== "idle"

    function showAt(x, y) {
        card.anchorX = x
        card.anchorY = y
    }

    function dismiss() {
        translation.clearSnippet()
    }

    width: Math.min(400, parent ? parent.width - 2 * Theme.spaceM : 400)
    // Grow with the text, but never past half the viewport.
    implicitHeight: Math.min(
        parent ? parent.height * 0.5 : 320,
        header.height + bodyColumn.implicitHeight + 3 * Theme.spaceM)
    height: implicitHeight

    // Prefer below-right of the click; flip above when there's no room,
    // and keep both edges inside the viewport either way.
    x: parent ? Math.max(Theme.spaceM,
                         Math.min(anchorX + 12, parent.width - width - Theme.spaceM))
              : 0
    y: parent
       ? (anchorY + 16 + height <= parent.height
          ? anchorY + 16
          : Math.max(Theme.spaceM, anchorY - height - 12))
       : 0

    radius: Theme.radiusM
    color: Theme.dialogBg
    border.width: 1
    border.color: Theme.border

    // Soft elevation without QtQuick.Effects: one offset, barely-there
    // slab behind the card. Cheap, and it reads on both themes.
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.margins: -1
        anchors.topMargin: 1
        anchors.bottomMargin: -3
        radius: card.radius + 1
        color: Theme.dialogShadow
    }

    // Swallow clicks/hover so a press meant for the card can't start a
    // new selection on the page underneath.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
    }

    Shortcut {
        sequence: "Escape"
        enabled: card.visible
        onActivated: card.dismiss()
    }

    RowLayout {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spaceM
        spacing: Theme.spaceS

        Rectangle {
            width: 8
            height: 8
            radius: 4
            Layout.alignment: Qt.AlignVCenter
            color: card.status === "failed" ? Theme.danger
                 : card.busy ? Theme.dimText
                             : Theme.accent
            SequentialAnimation on opacity {
                running: card.busy
                loops: Animation.Infinite
                NumberAnimation { to: 0.25; duration: 600 }
                NumberAnimation { to: 1.0;  duration: 600 }
            }
        }

        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.pixelSize: 11
            color: card.status === "failed" ? Theme.danger : Theme.dimText
            text: card.status === "failed"
                  ? qsTr("Translation failed")
                  : card.busy
                    ? (card.fromParagraph ? qsTr("Paragraph · translating…")
                                          : qsTr("Selection · translating…"))
                    : (card.fromParagraph ? qsTr("Paragraph · translated")
                                          : qsTr("Selection · translated"))
        }

        ToolButton {
            text: qsTr("Copy")
            font.pixelSize: 11
            flat: true
            implicitHeight: 20
            padding: 4
            enabled: bodyText.text.length > 0
            onClicked: {
                bodyText.selectAll()
                bodyText.copy()
                bodyText.deselect()
            }
        }

        ToolButton {
            text: "×"
            flat: true
            implicitWidth: 20
            implicitHeight: 20
            padding: 0
            onClicked: card.dismiss()
        }
    }

    ScrollView {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spaceM
        anchors.rightMargin: Theme.spaceS
        anchors.topMargin: Theme.spaceS
        anchors.bottomMargin: Theme.spaceM
        clip: true
        contentWidth: availableWidth

        Column {
            id: bodyColumn
            width: body.availableWidth
            spacing: Theme.spaceS

            TextEdit {
                id: bodyText
                width: parent.width
                visible: card.status !== "failed"
                // Streams in while the model writes; empty until the
                // first chunk lands, which the pulsing dot covers.
                text: translation.snippetText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                color: Theme.text
                font.pixelSize: settings.paragraphFontSize + 1
            }

            Text {
                width: parent.width
                visible: card.status === "failed"
                text: translation.snippetError
                wrapMode: Text.Wrap
                color: Theme.danger
                font.pixelSize: 12
            }
        }
    }
}
