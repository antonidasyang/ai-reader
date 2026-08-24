import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Rename / re-describe the current project, and — for owners — delete
// it. Deletion is genuinely destructive (the server cascades every
// item, annotation and note, for every member), so it lives in a
// visually separate danger zone behind a second confirmation that
// spells out what disappears.
AppDialog {
    id: root
    title: qsTr("Project settings")
    width: 420
    standardButtons: Dialog.NoButton

    readonly property bool owner: projects.currentRole === "owner"

    // Load the current values every time the dialog opens, so it never
    // shows a stale name after someone else renamed the project.
    onOpened: {
        nameField.text = projects.currentName
        descField.text = projects.descriptionOf(projects.currentId)
        nameField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: qsTr("Name")
            color: Theme.dimText
        }
        AppTextField {
            id: nameField
            Layout.fillWidth: true
            enabled: projects.canWrite
            placeholderText: qsTr("Project name")
            onAccepted: if (saveBtn.enabled) saveBtn.clicked()
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Description")
            color: Theme.dimText
        }
        AppTextField {
            id: descField
            Layout.fillWidth: true
            enabled: projects.canWrite
            placeholderText: qsTr("Description (optional)")
            onAccepted: if (saveBtn.enabled) saveBtn.clicked()
        }

        Label {
            Layout.fillWidth: true
            visible: !projects.canWrite
            wrapMode: Text.Wrap
            color: Theme.dimText
            text: qsTr("You have view-only access to this project, so its "
                       + "details can't be changed.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Close")
                onClicked: root.close()
            }
            AppButton {
                id: saveBtn
                text: qsTr("Save")
                primary: true
                enabled: projects.canWrite
                         && nameField.text.trim().length > 0
                         && (nameField.text.trim() !== projects.currentName
                             || descField.text
                                !== projects.descriptionOf(projects.currentId))
                onClicked: {
                    projects.updateProject(projects.currentId,
                                           nameField.text.trim(),
                                           descField.text)
                    root.close()
                }
            }
        }

        // ── Danger zone ────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 6
            visible: root.owner
            implicitHeight: dangerCol.implicitHeight + 20
            color: "transparent"
            border.color: Theme.danger
            radius: 4

            ColumnLayout {
                id: dangerCol
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                Label {
                    text: qsTr("Delete this project")
                    color: Theme.danger
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.dimText
                    text: qsTr("Everything in it — papers, notes, "
                               + "annotations and AI results — is deleted "
                               + "for you and for every member. This can't "
                               + "be undone.")
                }
                AppButton {
                    text: qsTr("Delete project…")
                    onClicked: {
                        confirmDelete.pending = projects.currentId
                        confirmDelete.pendingName = projects.currentName
                        confirmDelete.unsynced =
                            projects.unsyncedCount(projects.currentId)
                        confirmDelete.open()
                    }
                }
            }
        }
    }

    AppDialog {
        id: confirmDelete
        title: qsTr("Delete project?")
        width: 400
        standardButtons: Dialog.NoButton

        property string pending: ""
        property string pendingName: ""
        property int unsynced: 0

        onOpened: confirmField.text = ""

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("\"%1\" and everything in it will be permanently "
                           + "deleted, for every member of the project.")
                      .arg(confirmDelete.pendingName)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: confirmDelete.unsynced > 0
                color: Theme.danger
                text: qsTr("%1 local change(s) have not been synced yet and "
                           + "will be lost.").arg(confirmDelete.unsynced)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.dimText
                text: qsTr("Type the project name to confirm.")
            }
            AppTextField {
                id: confirmField
                Layout.fillWidth: true
                placeholderText: confirmDelete.pendingName
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Cancel")
                    onClicked: confirmDelete.close()
                }
                AppButton {
                    id: reallyDeleteBtn
                    text: qsTr("Delete")
                    // Red, not the ordinary blue confirm: this cascades the
                    // whole project for every member.
                    danger: true
                    enabled: confirmField.text.trim()
                             === confirmDelete.pendingName
                    onClicked: {
                        projects.deleteProject(confirmDelete.pending)
                        confirmDelete.close()
                        root.close()
                    }
                }
            }
        }
    }
}
