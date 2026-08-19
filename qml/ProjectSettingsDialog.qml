import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Rename / re-describe the current project, and — for owners — delete
// it. Deletion is genuinely destructive (the server cascades every
// item, annotation and note, for every member), so it lives in a
// visually separate danger zone behind a second confirmation that
// spells out what disappears.
Dialog {
    id: root
    title: qsTr("Project settings")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 420
    padding: 14
    standardButtons: Dialog.NoButton

    readonly property bool owner: projects.currentRole === "owner"

    // One size for every button in this dialog. The primary/danger
    // buttons carry a custom background, which sizes itself from its
    // own implicit size rather than the control's padding, so without
    // a shared figure they end up taller and narrower than the plain
    // ones sitting next to them.
    readonly property int btnH: 30
    readonly property int btnW: 88

    palette.window: Theme.paneBg
    palette.windowText: Theme.text
    palette.base: Theme.fieldBg
    palette.text: Theme.text
    palette.button: Theme.buttonBg
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    palette.placeholderText: Theme.dimText
    background: Rectangle {
        color: Theme.paneBg
        border.color: Theme.border
        radius: 6
    }

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
        TextField {
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
        TextField {
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
            Button {
                text: qsTr("Close")
                Layout.preferredWidth: root.btnW
                Layout.preferredHeight: root.btnH
                onClicked: root.close()
            }
            Button {
                id: saveBtn
                text: qsTr("Save")
                Layout.preferredWidth: root.btnW
                Layout.preferredHeight: root.btnH
                enabled: projects.canWrite
                         && nameField.text.trim().length > 0
                         && (nameField.text.trim() !== projects.currentName
                             || descField.text
                                !== projects.descriptionOf(projects.currentId))
                // Primary action styling, matching the other dialogs.
                contentItem: Label {
                    text: saveBtn.text
                    color: saveBtn.enabled ? Theme.onPrimary : Theme.dimText
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: root.btnW
                    implicitHeight: root.btnH
                    radius: 4
                    color: !saveBtn.enabled ? Theme.buttonBg
                         : saveBtn.pressed  ? Theme.primaryPressed
                         : saveBtn.hovered  ? Theme.primaryHover
                                            : Theme.primaryBg
                    border.color: saveBtn.enabled ? "transparent" : Theme.border
                }
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
                Button {
                    text: qsTr("Delete project…")
                    Layout.preferredHeight: root.btnH
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

    Dialog {
        id: confirmDelete
        title: qsTr("Delete project?")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 400
        padding: 14
        standardButtons: Dialog.NoButton

        property string pending: ""
        property string pendingName: ""
        property int unsynced: 0

        palette.window: Theme.paneBg
        palette.windowText: Theme.text
        palette.base: Theme.fieldBg
        palette.text: Theme.text
        palette.button: Theme.buttonBg
        palette.buttonText: Theme.text
        palette.placeholderText: Theme.dimText
        background: Rectangle {
            color: Theme.paneBg
            border.color: Theme.border
            radius: 6
        }

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
            TextField {
                id: confirmField
                Layout.fillWidth: true
                placeholderText: confirmDelete.pendingName
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Cancel")
                    Layout.preferredWidth: root.btnW
                    Layout.preferredHeight: root.btnH
                    onClicked: confirmDelete.close()
                }
                Button {
                    id: reallyDeleteBtn
                    text: qsTr("Delete")
                    Layout.preferredWidth: root.btnW
                    Layout.preferredHeight: root.btnH
                    enabled: confirmField.text.trim()
                             === confirmDelete.pendingName
                    contentItem: Label {
                        text: reallyDeleteBtn.text
                        color: reallyDeleteBtn.enabled ? Theme.onPrimary
                                                       : Theme.dimText
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        implicitWidth: root.btnW
                        implicitHeight: root.btnH
                        radius: 4
                        color: !reallyDeleteBtn.enabled ? Theme.buttonBg
                             : reallyDeleteBtn.pressed  ? Qt.darker(Theme.danger, 1.3)
                             : reallyDeleteBtn.hovered  ? Qt.lighter(Theme.danger, 1.1)
                                                        : Theme.danger
                        border.color: reallyDeleteBtn.enabled ? "transparent"
                                                              : Theme.border
                    }
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
