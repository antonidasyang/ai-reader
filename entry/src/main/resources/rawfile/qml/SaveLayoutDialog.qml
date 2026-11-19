import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Naming a layout: both saving the arrangement on screen under a name and
// renaming one that is already saved. They are the same question with a
// different verb, and one dialog means one set of rules about what a name
// may be rather than two that drift apart.
//
// A name that is already taken is dealt with HERE, in front of the name
// that caused it, rather than in a second dialog stacked on top of this
// one. Saving says so and offers to Replace; renaming refuses, because
// renaming onto another layout would quietly destroy it and nothing the
// reader typed said to.
AppDialog {
    id: root

    // Empty when saving the current arrangement; otherwise the layout being
    // renamed, in the spelling it is stored under.
    property string renaming: ""

    signal saveConfirmed(string name)
    signal renameConfirmed(string from, string to)

    readonly property string typed: nameField.text.trim()
    // The saved layout this name would land on, in ITS spelling -- the
    // reader typing "reading" is about to replace "Reading", and the
    // warning has to name the one that will actually go.
    readonly property string clashesWith:
        root.typed.length === 0
            ? ""
            : (root.renaming.length > 0
               && root.typed.toLowerCase() === root.renaming.toLowerCase())
              ? ""
              : layouts.existingName(root.typed)
    readonly property bool renamingClash:
        root.renaming.length > 0 && root.clashesWith.length > 0
    readonly property bool acceptable:
        root.typed.length > 0 && !root.renamingClash

    title: root.renaming.length > 0 ? qsTr("Rename layout") : qsTr("Save layout")
    width: 400
    standardButtons: Dialog.NoButton

    // Pre-filled with the layout on screen when there is one, so saving
    // again over "Reading" is one keystroke rather than a retyped name.
    function openForSave() {
        root.renaming = ""
        nameField.text = layouts.current
        root.open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function openForRename(name) {
        root.renaming = name
        nameField.text = name
        root.open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function commit() {
        if (!root.acceptable)
            return
        const name = root.typed
        const from = root.renaming
        root.close()
        if (from.length > 0)
            root.renameConfirmed(from, name)
        else
            root.saveConfirmed(name)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceM

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: 12
            text: root.renaming.length > 0
                  ? qsTr("What should this layout be called?")
                  : qsTr("Saves which panes are showing, how wide each one "
                         + "is and the order they sit in. Widths are kept as "
                         + "a share of the window, so the layout still works "
                         + "on a smaller screen.")
        }

        AppTextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Layout name")
            onAccepted: root.commit()
        }

        Label {
            Layout.fillWidth: true
            visible: root.clashesWith.length > 0
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: root.renamingClash ? Theme.danger : Theme.dimText
            text: root.renamingClash
                  ? qsTr("Another layout is already called “%1”.")
                        .arg(root.clashesWith)
                  : qsTr("A layout called “%1” already exists. Saving "
                         + "replaces it.").arg(root.clashesWith)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceS
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }
            AppButton {
                text: root.renaming.length > 0 ? qsTr("Rename")
                      : root.clashesWith.length > 0 ? qsTr("Replace")
                      : qsTr("Save")
                enabled: root.acceptable
                // Replacing a layout the reader already has is the one
                // outcome here that throws something away, so it does not
                // wear the same blue as the ordinary Save.
                danger: root.renaming.length === 0 && root.clashesWith.length > 0
                primary: !danger
                onClicked: root.commit()
            }
        }
    }
}
