import QtQuick
import QtQuick.Controls

// A segmented control rather than a strip of underlined labels: the tabs read
// as the same family as the buttons, which is what the rest of the dialog
// chrome is built from. Pair with AppTabButton.
TabBar {
    id: bar
    implicitHeight: Theme.controlH + 6
    padding: 3
    spacing: 3
    background: Rectangle {
        radius: Theme.radiusM
        color: Theme.cardBg
        border.width: 1
        border.color: Theme.divider
    }
}
