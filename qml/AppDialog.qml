import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The dialog chrome every popup in the app wears: the surface, the dimmed
// backdrop, the open/close transition, the titled header with its rule, and
// a footer whose buttons are AppButton. It was Settings' and Prompts' look;
// it is now everyone's, so a new dialog gets it by inheriting instead of by
// copying two hundred lines of it.
//
// A dialog that wants footer buttons sets `standardButtons`; one that builds
// its own row of buttons uses AppButton for them.
Dialog {
    id: root
    modal: true
    closePolicy: Popup.CloseOnEscape
    padding: Theme.dialogPadding
    topPadding: Theme.spaceL
    anchors.centerIn: Overlay.overlay

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
        // A dialog with no title gets no header bar at all, rather than an
        // empty strip and a rule across the top of its content.
        visible: root.title.length > 0
        implicitHeight: visible
                        ? headerTitle.implicitHeight + Theme.spaceL + Theme.spaceM + 1
                        : 0
        Label {
            id: headerTitle
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.dialogPadding
            anchors.rightMargin: Theme.dialogPadding
            anchors.topMargin: Theme.spaceL
            text: root.title
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
        id: footerBox
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: Theme.spaceS
        leftPadding: Theme.dialogPadding
        rightPadding: Theme.dialogPadding
        topPadding: Theme.spaceM
        bottomPadding: Theme.spaceL
        delegate: AppButton {
            // The accepting button is the filled one -- and so is a lone
            // button, since a dialog with nothing but Close should not look
            // like it has no action at all.
            primary: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                     || footerBox.count === 1
        }
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

    // ── Shared control styles (same look across all dialogs) ────────
}
