import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Sign-in / sign-up against the cloud backend. Bound to the `auth` context
// property (AuthController); auto-closes once authenticated.
Dialog {
    id: dlg
    title: registerMode ? qsTr("Create account") : qsTr("Sign in")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 380
    padding: 16
    closePolicy: Popup.CloseOnEscape

    property bool registerMode: false

    background: Rectangle {
        color: Theme.paneBg
        border.color: Theme.border
        radius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            text: qsTr("Server")
            color: Theme.dimText
            font.pixelSize: 11
        }
        TextField {
            id: serverField
            Layout.fillWidth: true
            text: auth.serverUrl
            placeholderText: "http://host:3000"
        }
        TextField {
            id: emailField
            Layout.fillWidth: true
            placeholderText: qsTr("Email")
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
        }
        TextField {
            id: pwField
            Layout.fillWidth: true
            placeholderText: qsTr("Password")
            echoMode: TextInput.Password
            onAccepted: signInButton.clicked()
        }
        TextField {
            id: nameField
            Layout.fillWidth: true
            visible: dlg.registerMode
            placeholderText: qsTr("Display name (optional)")
        }

        Label {
            text: auth.status
            visible: auth.status.length > 0
            color: Theme.danger
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.pixelSize: 11
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            CheckBox {
                text: qsTr("New account")
                checked: dlg.registerMode
                onToggled: dlg.registerMode = checked
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                running: auth.busy
                visible: auth.busy
                implicitWidth: 18
                implicitHeight: 18
            }
            Button {
                id: signInButton
                text: dlg.registerMode ? qsTr("Create") : qsTr("Sign in")
                enabled: !auth.busy && emailField.text.length > 0
                         && pwField.text.length > 0
                onClicked: {
                    auth.serverUrl = serverField.text
                    if (dlg.registerMode)
                        auth.registerUser(emailField.text, pwField.text,
                                          nameField.text)
                    else
                        auth.login(emailField.text, pwField.text)
                }
            }
        }
    }

    Connections {
        target: auth
        function onAuthenticatedChanged() {
            if (auth.authenticated)
                dlg.close()
        }
    }
}
