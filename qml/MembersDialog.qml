import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// View / manage a project's members. Owner-only controls are disabled for
// non-owners. Bound to the `projects` context property.
Dialog {
    id: dlg
    title: qsTr("Members — %1").arg(projects.currentName)
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: 12
    standardButtons: Dialog.Close

    readonly property bool owner: projects.currentRole === "owner"

    background: Rectangle {
        color: Theme.paneBg
        border.color: Theme.border
        radius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            clip: true
            model: projects.members
            ScrollBar.vertical: ScrollBar { active: true }
            delegate: RowLayout {
                width: ListView.view ? ListView.view.width : 0
                spacing: 8
                Label {
                    text: modelData.email
                    color: Theme.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                ComboBox {
                    enabled: dlg.owner
                    model: ["owner", "editor", "viewer"]
                    currentIndex: model.indexOf(modelData.role)
                    Layout.preferredWidth: 110
                    onActivated: function(i) {
                        projects.updateMemberRole(modelData.userId, model[i])
                    }
                }
                ToolButton {
                    text: "×"
                    enabled: dlg.owner
                    onClicked: projects.removeMember(modelData.userId)
                }
            }
        }

        RowLayout {
            visible: dlg.owner
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: emailF
                Layout.fillWidth: true
                placeholderText: qsTr("Invite by email (they must have an account)")
            }
            ComboBox {
                id: roleC
                model: ["editor", "viewer", "owner"]
                Layout.preferredWidth: 110
            }
            Button {
                text: qsTr("Invite")
                enabled: emailF.text.length > 0
                onClicked: {
                    projects.addMember(emailF.text, roleC.currentText)
                    emailF.text = ""
                }
            }
        }

        Label {
            text: projects.status
            visible: projects.status.length > 0
            color: Theme.danger
            font.pixelSize: 11
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
