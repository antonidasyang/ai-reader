import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Top strip: research-project (课题) selector + members + account. Bound to the
// `auth` and `projects` context properties.
Rectangle {
    id: bar
    implicitHeight: 34
    color: Theme.headerBg

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        Label {
            text: qsTr("Library")
            font.bold: true
            color: Theme.text
        }

        ComboBox {
            id: projectCombo
            visible: auth.authenticated && projects.list.length > 0
            Layout.preferredWidth: 220
            model: projects.list
            textRole: "name"
            function syncIndex() {
                for (let i = 0; i < projects.list.length; ++i) {
                    if (projects.list[i].id === projects.currentId) {
                        currentIndex = i
                        return
                    }
                }
                currentIndex = -1
            }
            onActivated: function(idx) {
                if (idx >= 0)
                    projects.selectProject(projects.list[idx].id)
            }
            Component.onCompleted: syncIndex()
            Connections {
                target: projects
                function onCurrentChanged() { projectCombo.syncIndex() }
                function onListChanged() { projectCombo.syncIndex() }
            }
        }

        ToolButton {
            visible: auth.authenticated
            text: qsTr("New")
            ToolTip.visible: hovered
            ToolTip.delay: 400
            ToolTip.text: qsTr("Create a research project")
            onClicked: createDlg.open()
        }
        ToolButton {
            visible: auth.authenticated && projects.currentId.length > 0
            text: qsTr("Members")
            onClicked: {
                projects.refreshMembers()
                membersDlg.open()
            }
        }
        Label {
            visible: auth.authenticated && projects.currentRole.length > 0
            text: projects.currentRole
            color: Theme.dimText
            font.pixelSize: 10
        }

        Item { Layout.fillWidth: true }

        Label {
            visible: auth.authenticated
            text: auth.userEmail
            color: Theme.dimText
            font.pixelSize: 11
            elide: Text.ElideMiddle
            Layout.maximumWidth: 180
        }
        ToolButton {
            text: auth.authenticated ? qsTr("Sign out") : qsTr("Sign in")
            onClicked: auth.authenticated ? auth.logout() : loginDlg.open()
        }
    }

    LoginDialog { id: loginDlg }
    MembersDialog { id: membersDlg }

    Dialog {
        id: createDlg
        title: qsTr("New project")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 360
        padding: 14
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle {
            color: Theme.paneBg
            border.color: Theme.border
            radius: 6
        }
        onAccepted: {
            if (nameF.text.trim().length > 0)
                projects.createProject(nameF.text.trim(), descF.text)
            nameF.text = ""
            descF.text = ""
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            TextField {
                id: nameF
                Layout.fillWidth: true
                placeholderText: qsTr("Project name")
            }
            TextField {
                id: descF
                Layout.fillWidth: true
                placeholderText: qsTr("Description (optional)")
            }
        }
    }
}
