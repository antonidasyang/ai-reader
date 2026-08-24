import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

AppDialog {
    id: dialog
    title: qsTr("Password required")
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: 400

    property alias password: passwordField.text
    property string promptText: qsTr("This PDF is encrypted. Enter the password:")

    onOpened: {
        passwordField.text = ""
        passwordField.forceActiveFocus()
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
        AppTextField {
            id: passwordField
            echoMode: TextInput.Password
            Layout.fillWidth: true
            Layout.preferredWidth: 320
            onAccepted: dialog.accept()
        }
    }
}
