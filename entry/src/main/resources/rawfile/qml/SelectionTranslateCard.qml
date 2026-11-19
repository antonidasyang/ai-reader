import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// One pinned translation card, floating over the PDF. Created per row of
// TranslationService's snippet model: right-click → Translate opens one,
// and only its × closes it — it survives losing focus, a new selection,
// and scrolling. Drag it by the header; several can sit open at once.
//
// Position is plain x/y (never a binding) so dragging can write it. The
// card clamps itself into the viewport on creation and on resize.
Rectangle {
    id: card

    // ── Row data (set by the delegate) ────────────────────────────────
    property int snippetId: -1
    property string status: "translating"
    property string translatedText: ""
    property string errorText: ""
    property bool fromParagraph: false

    signal closeRequested()
    signal raiseRequested()

    readonly property bool busy: status === "translating"

    // Where to open, in parent coordinates. Applied once, then owned by
    // the drag.
    property real openX: 0
    property real openY: 0

    function clampIntoView() {
        if (!parent) return
        card.x = Math.max(0, Math.min(card.x, parent.width - card.width))
        card.y = Math.max(0, Math.min(card.y, parent.height - card.height))
    }

    Component.onCompleted: {
        card.x = openX
        card.y = openY
        clampIntoView()
        // A new card opens on top. z is assigned by the handler, never
        // bound — a binding that reads the stacking counter it also
        // bumps is a binding loop.
        card.raiseRequested()
    }

    width: Math.min(400, parent ? parent.width - 2 * Theme.spaceM : 400)
    // Grow with the text, but never past half the viewport.
    implicitHeight: Math.min(
        parent ? parent.height * 0.5 : 320,
        header.height + bodyColumn.implicitHeight + 3 * Theme.spaceM)
    height: implicitHeight

    Connections {
        target: card.parent
        function onWidthChanged()  { card.clampIntoView() }
        function onHeightChanged() { card.clampIntoView() }
    }
    onHeightChanged: clampIntoView()

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
    // new selection on the page underneath, and bring it to the front.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: card.raiseRequested()
    }

    // ── Header: status, drag handle, actions ──────────────────────────
    MouseArea {
        id: dragArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: header.height + 2 * Theme.spaceM
        cursorShape: Qt.SizeAllCursor
        drag.target: card
        drag.minimumX: 0
        drag.maximumX: card.parent ? Math.max(0, card.parent.width - card.width) : 0
        drag.minimumY: 0
        drag.maximumY: card.parent ? Math.max(0, card.parent.height - card.height) : 0
        drag.threshold: 0
        onPressed: card.raiseRequested()
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
            onClicked: card.closeRequested()
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
                text: card.translatedText
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
                text: card.errorText
                wrapMode: Text.Wrap
                color: Theme.danger
                font.pixelSize: 12
            }
        }
    }
}
