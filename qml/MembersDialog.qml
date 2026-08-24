import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// View / manage a project's members. Owner-only controls are disabled for
// non-owners. Bound to the `projects` context property.
AppDialog {
    id: dlg
    title: qsTr("Members — %1").arg(projects.currentName)
    width: 480
    standardButtons: Dialog.Close

    readonly property bool owner: projects.currentRole === "owner"

    // Parallel arrays — the API role code is what gets sent to the
    // server; the combos show the translated label. The invite combo
    // uses a different order (editor first) so keep two code lists.
    readonly property var roleCodes: ["owner", "editor", "viewer"]
    readonly property var roleLabels:
        [qsTr("Owner"), qsTr("Editor"), qsTr("Viewer")]
    readonly property var inviteRoleCodes: ["editor", "viewer", "owner"]
    readonly property var inviteRoleLabels:
        [qsTr("Editor"), qsTr("Viewer"), qsTr("Owner")]

    // ── Content ─────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceM

        AppSectionCard {
            Layout.preferredHeight: 220
            clip: true

            ListView {
                anchors.fill: parent
                anchors.margins: Theme.spaceS - 2
                clip: true
                model: projects.members
                ScrollBar.vertical: ScrollBar { active: true }
                delegate: Item {
                    width: ListView.view ? ListView.view.width : 0
                    implicitHeight: memberRow.implicitHeight + Theme.spaceM
                    RowLayout {
                        id: memberRow
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spaceS
                        anchors.rightMargin: Theme.spaceS
                        spacing: Theme.spaceS
                        Label {
                            text: modelData.email
                            color: Theme.text
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        AppComboBox {
                            enabled: dlg.owner
                            model: dlg.roleLabels
                            currentIndex: dlg.roleCodes.indexOf(modelData.role)
                            Layout.preferredWidth: 110
                            opacity: enabled ? 1 : 0.6
                            onActivated: function(i) {
                                projects.updateMemberRole(modelData.userId, dlg.roleCodes[i])
                            }
                        }
                        ToolButton {
                            id: rmBtn
                            text: "×"
                            enabled: dlg.owner
                            implicitWidth: 26
                            implicitHeight: 26
                            contentItem: Text {
                                text: rmBtn.text
                                font.pixelSize: 16
                                color: rmBtn.hovered ? Theme.danger : Theme.dimText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                opacity: rmBtn.enabled ? 1 : 0.4
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                            background: Rectangle {
                                radius: Theme.radiusS
                                color: rmBtn.down ? Theme.buttonPressed
                                       : rmBtn.hovered ? Theme.hover : "transparent"
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                            onClicked: projects.removeMember(modelData.userId)
                        }
                    }
                }
            }
        }

        RowLayout {
            visible: dlg.owner
            Layout.fillWidth: true
            spacing: Theme.spaceS
            AppTextField {
                id: emailF
                Layout.fillWidth: true
                placeholderText: qsTr("Invite by email (they must have an account)")
            }
            AppComboBox {
                id: roleC
                model: dlg.inviteRoleLabels
                Layout.preferredWidth: 110
            }
            AppButton {
                text: qsTr("Invite")
                primary: true
                enabled: emailF.text.length > 0
                onClicked: {
                    projects.addMember(emailF.text,
                                       dlg.inviteRoleCodes[roleC.currentIndex])
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
