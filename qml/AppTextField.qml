import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The one text field style, shared by every dialog.
TextField {
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
