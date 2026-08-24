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
AppDialog {
    id: root
    title: qsTr("Research profile")
    width: 560
    height: Math.min(660, Overlay.overlay ? Overlay.overlay.height - 60 : 660)
    standardButtons: Dialog.NoButton

    // One label style for the nine fields below. Inline components have to
    // live on the file's root object, which is why it sits up here rather
    // than next to its uses.
    component FieldLabel: Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        color: Theme.dimText
        wrapMode: Text.Wrap
    }

    // The multi-line fields: a scrolling TextArea wearing AppTextField's
    // frame, so they sit flush with the single-line AppTextFields.
    component FieldBox: ScrollView {
        Layout.fillWidth: true
        clip: true
        background: Rectangle {
            radius: Theme.radiusS
            color: Theme.fieldBg
            border.width: 1
            border.color: Theme.fieldBorder
        }
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
                FieldBox {
                    Layout.preferredHeight: 54
                    TextArea {
                        id: goalField
                        wrapMode: TextEdit.Wrap
                        background: null
                        enabled: profile.canEdit
                        placeholderText: qsTr("What are you trying to find out or build?")
                    }
                }

                FieldLabel { text: qsTr("Core questions (one per line)") }
                FieldBox {
                    Layout.preferredHeight: 66
                    TextArea {
                        id: questionsField
                        wrapMode: TextEdit.Wrap
                        background: null
                        enabled: profile.canEdit
                        placeholderText: qsTr("The questions this project has to answer")
                    }
                }

                FieldLabel { text: qsTr("Application setting") }
                AppTextField {
                    id: scenariosField
                    Layout.fillWidth: true
                    enabled: profile.canEdit
                    placeholderText: qsTr("Where the work has to hold up in practice")
                }

                FieldLabel { text: qsTr("Current hypotheses (one per line)") }
                FieldBox {
                    Layout.preferredHeight: 54
                    TextArea {
                        id: hypothesesField
                        wrapMode: TextEdit.Wrap
                        background: null
                        enabled: profile.canEdit
                        placeholderText: qsTr("What you currently believe, and want tested")
                    }
                }

                FieldLabel { text: qsTr("In scope") }
                FieldBox {
                    Layout.preferredHeight: 48
                    TextArea {
                        id: scopeField
                        wrapMode: TextEdit.Wrap
                        background: null
                        enabled: profile.canEdit
                        placeholderText: qsTr("Methods, settings or data this project covers")
                    }
                }

                FieldLabel { text: qsTr("Explicitly out of scope") }
                FieldBox {
                    Layout.preferredHeight: 48
                    TextArea {
                        id: outOfScopeField
                        wrapMode: TextEdit.Wrap
                        background: null
                        enabled: profile.canEdit
                        placeholderText: qsTr("What you have decided not to pursue — this keeps "
                                              + "papers from being recommended back at you")
                    }
                }

                FieldLabel { text: qsTr("Dimensions to pay attention to") }
                FieldBox {
                    Layout.preferredHeight: 48
                    TextArea {
                        id: dimensionsField
                        wrapMode: TextEdit.Wrap
                        background: null
                        enabled: profile.canEdit
                        placeholderText: qsTr("e.g. inference cost, data requirements, "
                                              + "reproducibility, real-world validation")
                    }
                }

                FieldLabel { text: qsTr("Where you are in the work") }
                AppTextField {
                    id: stageField
                    Layout.fillWidth: true
                    enabled: profile.canEdit
                    placeholderText: qsTr("e.g. first survey, narrowing a topic, running experiments")
                }

                FieldLabel { text: qsTr("Your background") }
                AppTextField {
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
            AppButton {
                text: qsTr("Save")
                primary: true
                enabled: profile.canEdit
                onClicked: {
                    profile.save(root.collect())
                    root.close()
                }
            }
            AppButton {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }
}
