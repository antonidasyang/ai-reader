import QtQuick
import QtQuick.Controls

// A segmented control rather than a strip of underlined labels: the tabs read
// as the same family as the buttons, which is what the rest of the dialog
// chrome is built from. Pair with AppTabButton.
TabBar {
    id: bar
    // One number decides both the pill and the frame around it. TabBar does
    // not stretch its buttons to the content height, so a bar that is taller
    // than a button plus its two insets leaves the whole row sitting against
    // the top edge -- which is exactly what it did: 3 px above the selected
    // tab and 7 below it. Deriving the height from the button keeps it
    // centred no matter what the metrics change to.
    readonly property int tabHeight: Theme.controlH - 4
    readonly property int inset: 3
    implicitHeight: tabHeight + inset * 2
    padding: inset
    spacing: inset
    background: Rectangle {
        radius: Theme.radiusM
        color: Theme.cardBg
        border.width: 1
        border.color: Theme.divider
    }
}
