import QtQuick
import QtQuick.Controls

// One segment of an AppTabBar, wearing AppButton's language: the selected one
// is filled the way a primary button is, the rest stay quiet until hovered.
TabButton {
    id: tb
    implicitHeight: Theme.controlH - 4
    leftPadding: Theme.spaceM
    rightPadding: Theme.spaceM
    contentItem: Text {
        text: tb.text
        font.pixelSize: 13
        font.weight: tb.checked ? Font.DemiBold : Font.Normal
        color: tb.checked ? Theme.onPrimary
               : (tb.hovered ? Theme.text : Theme.bodyText)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Behavior on color { ColorAnimation { duration: Theme.animMs } }
    }
    background: Rectangle {
        radius: Theme.radiusS
        color: tb.checked
               ? (tb.down ? Theme.primaryPressed : Theme.primaryBg)
               : (tb.down ? Theme.buttonPressed
                          : tb.hovered ? Theme.buttonHover : "transparent")
        Behavior on color { ColorAnimation { duration: Theme.animMs } }
    }
}
