import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The one button style in the app: the OK / Cancel pair from Settings and
// Prompts, so every dialog's buttons look the same.
//
//   primary  the filled, committing action -- one per row
//   ghost    a quiet action that should not compete with it (Skip, Later);
//            no fill until hovered
//   danger   a destructive confirm (Delete). Filled, but red, because it
//            must not be reachable by muscle memory for "the blue one"
Button {
    id: ab
    property bool primary: false
    property bool ghost: false
    property bool danger: false
    implicitHeight: Theme.controlH
    leftPadding: Theme.spaceL
    rightPadding: Theme.spaceL
    contentItem: Text {
        text: ab.text
        font.pixelSize: 13
        font.weight: (ab.primary || ab.danger) ? Font.DemiBold : Font.Normal
        color: (ab.primary || ab.danger) ? Theme.onPrimary
               : ab.ghost ? (ab.hovered ? Theme.text : Theme.dimText)
               : Theme.text
        Behavior on color { ColorAnimation { duration: 120 } }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: ab.enabled ? 1 : 0.45
    }
    background: Rectangle {
        radius: Theme.radiusS
        color: ab.danger
               ? (ab.down ? Qt.darker(Theme.danger, 1.3)
                          : ab.hovered ? Qt.lighter(Theme.danger, 1.1) : Theme.danger)
               : ab.primary
               ? (ab.down ? Theme.primaryPressed : ab.hovered ? Theme.primaryHover : Theme.primaryBg)
               : ab.ghost
                 ? (ab.down ? Theme.buttonPressed : ab.hovered ? Theme.buttonHover : "transparent")
                 : (ab.down ? Theme.buttonPressed : ab.hovered ? Theme.buttonHover : Theme.buttonBg)
        border.width: (ab.primary || ab.ghost || ab.danger) ? 0 : 1
        border.color: ab.visualFocus ? Theme.accent : Theme.border
        opacity: ab.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
