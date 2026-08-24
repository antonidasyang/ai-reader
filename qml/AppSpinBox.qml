import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The one spin box style, shared by every dialog.
SpinBox {
    id: fs
    editable: true
    font.pixelSize: 13
    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Theme.controlH
        radius: Theme.radiusS
        color: Theme.fieldBg
        border.width: 1
        border.color: fs.activeFocus ? Theme.accent : Theme.fieldBorder
        Behavior on border.color { ColorAnimation { duration: 120 } }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.radius + 2
            color: "transparent"
            border.width: 2
            border.color: Theme.focusRing
            visible: fs.activeFocus
        }
    }
}
