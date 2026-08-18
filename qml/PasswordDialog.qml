import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

Dialog {
    id: dialog
    title: qsTr("Password required")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape
    width: 400
    padding: Theme.dialogPadding

    property alias password: passwordField.text
    property string promptText: qsTr("This PDF is encrypted. Enter the password:")

    onOpened: {
        passwordField.text = ""
        passwordField.forceActiveFocus()
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

    // ── Content ─────────────────────────────────────────────────────
    contentItem: ColumnLayout {
        spacing: Theme.spaceM
        Label {
            text: dialog.promptText
            wrapMode: Text.WordWrap
            color: Theme.bodyText
            font.pixelSize: 13
            Layout.fillWidth: true
        }
        FieldText {
            id: passwordField
            echoMode: TextInput.Password
            Layout.fillWidth: true
            Layout.preferredWidth: 320
            onAccepted: dialog.accept()
        }
    }
}
