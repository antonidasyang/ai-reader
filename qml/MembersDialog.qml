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
    width: 480
    padding: Theme.dialogPadding
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
            text: dlg.title
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
        // View/manage dialog: the lone Close button is the primary action.
        delegate: ActionButton { primary: true }
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

    component FieldCombo: ComboBox {
        id: fc
        font.pixelSize: 13
        implicitHeight: Theme.controlH
        background: Rectangle {
            implicitWidth: 120
            implicitHeight: Theme.controlH
            radius: Theme.radiusS
            color: fc.down ? Theme.buttonPressed : fc.hovered ? Theme.buttonHover : Theme.fieldBg
            border.width: 1
            border.color: (fc.activeFocus || fc.visualFocus) ? Theme.accent : Theme.fieldBorder
            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on border.color { ColorAnimation { duration: 120 } }
        }
        contentItem: TextField {
            leftPadding: Theme.spaceS + 2
            rightPadding: Theme.spaceXs
            text: fc.editable ? fc.editText : fc.displayText
            enabled: fc.editable
            autoScroll: fc.editable
            readOnly: fc.down
            inputMethodHints: fc.inputMethodHints
            validator: fc.validator
            selectByMouse: true
            color: Theme.text
            placeholderTextColor: Theme.dimText
            selectionColor: Theme.accent
            selectedTextColor: Theme.onAccent
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 13
            background: null
        }
        delegate: ItemDelegate {
            id: fcDel
            required property var model
            required property int index
            width: ListView.view ? ListView.view.width : 0
            height: 28
            text: model.display !== undefined ? model.display : model.modelData
            highlighted: fc.highlightedIndex === index
            contentItem: Text {
                text: fcDel.text
                font.pixelSize: 13
                color: Theme.text
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: Theme.radiusS - 2
                color: fcDel.highlighted ? Theme.hover : "transparent"
            }
        }
        popup: Popup {
            y: fc.height + 4
            width: fc.width
            padding: Theme.spaceXs
            implicitHeight: Math.min(contentItem.implicitHeight
                                     + topPadding + bottomPadding, 320)
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: fc.popup.visible ? fc.delegateModel : null
                currentIndex: fc.highlightedIndex
                ScrollBar.vertical: ScrollBar { }
            }
            background: Rectangle {
                color: Theme.dialogBg
                border.width: 1
                border.color: Theme.border
                radius: Theme.radiusM
            }
        }
    }

    // ── Content ─────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceM

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            radius: Theme.radiusM
            color: Theme.cardBg
            border.width: 1
            border.color: Theme.divider
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
                        FieldCombo {
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
            FieldText {
                id: emailF
                Layout.fillWidth: true
                placeholderText: qsTr("Invite by email (they must have an account)")
            }
            FieldCombo {
                id: roleC
                model: dlg.inviteRoleLabels
                Layout.preferredWidth: 110
            }
            ActionButton {
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
