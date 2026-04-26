import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    title: qsTr("Password required")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape

    property alias password: passwordField.text
    property string promptText: qsTr("This PDF is encrypted. Enter the password:")

    onOpened: {
        passwordField.text = ""
        passwordField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 8
        Label {
            text: dialog.promptText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        TextField {
            id: passwordField
            echoMode: TextInput.Password
            Layout.fillWidth: true
            Layout.preferredWidth: 320
            onAccepted: dialog.accept()
        }
    }
}
