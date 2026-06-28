import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// CAS single sign-on. Opens the system browser; the backend redirects the
// token back to the app's loopback. Bound to the `auth` context property;
// auto-closes once authenticated.
Dialog {
    id: dlg
    title: qsTr("Sign in")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 380
    padding: 16
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.paneBg
        border.color: Theme.border
        radius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            text: qsTr("Server")
            color: Theme.dimText
            font.pixelSize: 11
        }
        TextField {
            id: serverField
            Layout.fillWidth: true
            text: auth.serverUrl
            placeholderText: "https://aireader.d2ssoft.com"
        }

        Button {
            Layout.fillWidth: true
            text: auth.busy ? qsTr("Waiting for browser…")
                            : qsTr("Sign in with CAS")
            enabled: !auth.busy && serverField.text.trim().length > 0
            onClicked: {
                auth.serverUrl = serverField.text.trim()
                auth.startCasLogin()
            }
        }
        BusyIndicator {
            running: auth.busy
            visible: auth.busy
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 22
            implicitHeight: 22
        }

        Label {
            text: auth.status
            visible: auth.status.length > 0
            color: Theme.dimText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
        }
        Label {
            text: qsTr("Your browser will open for single sign-on; come back here when done.")
            color: Theme.dimText
            font.pixelSize: 10
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
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
