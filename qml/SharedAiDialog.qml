import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Shows every member's shared AI interpretation for the current paper, with
// author attribution. Bound to the `aiArtifacts` context property.
Dialog {
    id: dlg
    title: qsTr("Shared AI interpretations")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: Theme.dialogPadding
    standardButtons: Dialog.Close

    property var items: []
    function refresh() { items = aiArtifacts.sharedForCurrent() }
    onAboutToShow: refresh()

    // Artifact-type codes come from the server; map the known ones to
    // a translated pill label and fall back to the raw code.
    function typeLabel(t) {
        return t === "summary" ? qsTr("interpretation") : (t || "")
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
            text: dlg.title
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
        // View-only dialog: the lone Close button is the primary action.
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

    // Small metadata pill (artifact type / model name).
    component MetaPill: Rectangle {
        property alias label: pillText.text
        visible: pillText.text.length > 0
        implicitWidth: pillText.implicitWidth + Theme.spaceM
        implicitHeight: pillText.implicitHeight + Theme.spaceXs + 2
        radius: height / 2
        color: Theme.fieldBg
        border.width: 1
        border.color: Theme.divider
        Label {
            id: pillText
            anchors.centerIn: parent
            font.pixelSize: 10
            color: Theme.dimText
        }
    }

    // ── Content ─────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceS

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 340
            clip: true
            model: dlg.items
            spacing: Theme.spaceS
            ScrollBar.vertical: ScrollBar { active: true }
            delegate: Rectangle {
                width: ListView.view ? ListView.view.width : 0
                implicitHeight: entryCol.implicitHeight + 2 * Theme.spaceM
                radius: Theme.radiusM
                color: Theme.cardBg
                border.width: 1
                border.color: Theme.divider

                ColumnLayout {
                    id: entryCol
                    anchors.fill: parent
                    anchors.margins: Theme.spaceM
                    spacing: Theme.spaceXs

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS
                        Label {
                            text: (modelData.authorEmail || modelData.author)
                                  + (modelData.isMine ? qsTr(" (you)") : "")
                            color: Theme.accent
                            font.bold: true
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        MetaPill { label: dlg.typeLabel(modelData.type) }
                        MetaPill { label: modelData.model }
                    }
                    Label {
                        text: modelData.payload
                        color: Theme.text
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        font.pixelSize: 12
                    }
                }
            }
        }

        Label {
            visible: dlg.items.length === 0
            text: qsTr("No shared interpretations for this paper yet.")
            color: Theme.dimText
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceS
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Connections {
        target: aiArtifacts
        function onSharedCountChanged() { dlg.refresh() }
    }
}
