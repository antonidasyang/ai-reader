import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The research profile of the current project (§6 of the interpretation
// spec): what this project is actually trying to find out.
//
// Everything the app interprets — a quick read, a deep read, every
// project-wide analysis — is prompted with these answers, so a paper is
// judged against this work rather than summarised in the abstract. The
// fields are deliberately plain text: they are pasted into the prompt as
// written, so a sentence the reader would say out loud works better here
// than a keyword.
Dialog {
    id: root
    title: qsTr("Research profile")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 560
    height: Math.min(660, Overlay.overlay ? Overlay.overlay.height - 60 : 660)
    padding: 14
    standardButtons: Dialog.NoButton

    readonly property int btnH: 30
    readonly property int btnW: 88

    // One label style for the nine fields below. Inline components have to
    // live on the file's root object, which is why it sits up here rather
    // than next to its uses.
    component FieldLabel: Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        color: Theme.dimText
        wrapMode: Text.Wrap
    }

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

    // Re-read on every open: a collaborator may have edited the profile
    // since this window was last looked at.
    onOpened: load()

    function load() {
        const p = profile.profile
        goalField.text       = p.goal || ""
        questionsField.text  = p.questions || ""
        scenariosField.text  = p.scenarios || ""
        hypothesesField.text = p.hypotheses || ""
        scopeField.text      = p.scope || ""
        outOfScopeField.text = p.outOfScope || ""
        dimensionsField.text = p.dimensions || ""
        stageField.text      = p.stage || ""
        backgroundField.text = p.background || ""
    }

    function collect() {
        return {
            "goal":       goalField.text,
            "questions":  questionsField.text,
            "scenarios":  scenariosField.text,
            "hypotheses": hypothesesField.text,
            "scope":      scopeField.text,
            "outOfScope": outOfScopeField.text,
            "dimensions": dimensionsField.text,
            "stage":      stageField.text,
            "background": backgroundField.text
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            text: qsTr("Every interpretation in this project is written against "
                       + "these answers. Leave anything blank you don't know yet.")
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 4

                FieldLabel { text: qsTr("Research goal") }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 54
                    clip: true
                    TextArea {
                        id: goalField
                        wrapMode: TextEdit.Wrap
                        enabled: profile.canEdit
                        placeholderText: qsTr("What are you trying to find out or build?")
                    }
                }

                FieldLabel { text: qsTr("Core questions (one per line)") }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 66
                    clip: true
                    TextArea {
                        id: questionsField
                        wrapMode: TextEdit.Wrap
                        enabled: profile.canEdit
                        placeholderText: qsTr("The questions this project has to answer")
                    }
                }

                FieldLabel { text: qsTr("Application setting") }
                TextField {
                    id: scenariosField
                    Layout.fillWidth: true
                    enabled: profile.canEdit
                    placeholderText: qsTr("Where the work has to hold up in practice")
                }

                FieldLabel { text: qsTr("Current hypotheses (one per line)") }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 54
                    clip: true
                    TextArea {
                        id: hypothesesField
                        wrapMode: TextEdit.Wrap
                        enabled: profile.canEdit
                        placeholderText: qsTr("What you currently believe, and want tested")
                    }
                }

                FieldLabel { text: qsTr("In scope") }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    clip: true
                    TextArea {
                        id: scopeField
                        wrapMode: TextEdit.Wrap
                        enabled: profile.canEdit
                        placeholderText: qsTr("Methods, settings or data this project covers")
                    }
                }

                FieldLabel { text: qsTr("Explicitly out of scope") }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    clip: true
                    TextArea {
                        id: outOfScopeField
                        wrapMode: TextEdit.Wrap
                        enabled: profile.canEdit
                        placeholderText: qsTr("What you have decided not to pursue — this keeps "
                                              + "papers from being recommended back at you")
                    }
                }

                FieldLabel { text: qsTr("Dimensions to pay attention to") }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    clip: true
                    TextArea {
                        id: dimensionsField
                        wrapMode: TextEdit.Wrap
                        enabled: profile.canEdit
                        placeholderText: qsTr("e.g. inference cost, data requirements, "
                                              + "reproducibility, real-world validation")
                    }
                }

                FieldLabel { text: qsTr("Where you are in the work") }
                TextField {
                    id: stageField
                    Layout.fillWidth: true
                    enabled: profile.canEdit
                    placeholderText: qsTr("e.g. first survey, narrowing a topic, running experiments")
                }

                FieldLabel { text: qsTr("Your background") }
                TextField {
                    id: backgroundField
                    Layout.fillWidth: true
                    enabled: profile.canEdit
                    placeholderText: qsTr("So explanations land at the right level")
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !profile.canEdit
            wrapMode: Text.Wrap
            color: Theme.dimText
            text: projects.currentId.length === 0
                  ? qsTr("Select a project first — the profile belongs to a project.")
                  : qsTr("You have view-only access to this project, so its "
                         + "research profile can't be changed.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.dimText
                visible: profile.updatedByEmail.length > 0
                text: qsTr("Last edited by %1").arg(profile.updatedByEmail)
            }
            Item { Layout.fillWidth: profile.updatedByEmail.length === 0 }
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
                enabled: profile.canEdit
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
                    color: saveBtn.enabled
                           ? (saveBtn.down ? Theme.accentPressed : Theme.accent)
                           : Theme.buttonBg
                    border.color: saveBtn.enabled ? "transparent" : Theme.border
                }
                onClicked: {
                    profile.save(root.collect())
                    root.close()
                }
            }
        }
    }
}
