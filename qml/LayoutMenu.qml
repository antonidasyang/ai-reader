import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The saved pane arrangements, as a menu. A row here does exactly one
// thing: it puts that arrangement back on screen.
//
// Every row used to carry a "✎" and a "✕" of its own. Everywhere else in
// this app "✕" closes a pane, so a reader arriving here read it as "close"
// and was one click away from losing a layout they had spent time on. A
// menu is the wrong place to destroy anything anyway: it is a list of
// things to pick, opened and dismissed in a second, with no room for a
// confirmation and no way to say what is about to go. Renaming and
// deleting now live in ManageLayoutsDialog, where the buttons have words
// on them and a delete is confirmed on the row it affects.
//
// Everything below is asked of the window rather than done here. This file
// knows what a layout menu looks like; Main.qml is the only place that
// knows what a SplitView is, which pane is which, and how wide the row is.
Menu {
    id: root

    signal applyRequested(string name)
    signal saveRequested()
    signal manageRequested()

    readonly property bool empty: layouts.names.length === 0

    // The column the tick sits in. Every name starts to the right of it,
    // ticked or not, so the menu reads as one column of names rather than
    // one indented name among the rest.
    readonly property int tickWidth: 18

    Instantiator {
        model: layouts.names
        delegate: MenuItem {
            id: presetItem
            required property var modelData

            // The name is on the item itself as well as in the row below,
            // so the menu is legible to anything that reads a MenuItem
            // rather than looking at it.
            text: presetItem.modelData
            onTriggered: root.applyRequested(presetItem.modelData)

            // Deliberately NOT `checkable`, and the tick is drawn by hand.
            // Fusion puts a checkable item's indicator at the item's own
            // left padding and relies on its stock contentItem to carry the
            // matching left inset that keeps the label clear of it -- and
            // this row replaces that contentItem, which is how the tick
            // came to be printed on top of the layout's name. A column of
            // our own can never be laid over, whatever the style does.
            contentItem: RowLayout {
                spacing: Theme.spaceS

                Item {
                    Layout.preferredWidth: root.tickWidth
                    Layout.preferredHeight: tick.implicitHeight
                    Layout.alignment: Qt.AlignVCenter
                    Label {
                        id: tick
                        anchors.centerIn: parent
                        // Hidden rather than absent: the Item keeps its
                        // width either way, which is the whole point.
                        visible: presetItem.modelData === layouts.current
                        text: "✓"
                        font.pixelSize: 13
                        color: Theme.accent
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: presetItem.text
                    color: Theme.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }
        }
        // The saved layouts go above everything declared below, whatever
        // order the engine builds them in.
        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }

    MenuItem {
        // Not an empty menu: a menu that says what it is for.
        text: qsTr("No saved layouts yet")
        enabled: false
        visible: root.empty
        height: visible ? implicitHeight : 0
    }

    MenuSeparator {
        visible: !root.empty
        height: visible ? implicitHeight : 0
    }

    MenuItem {
        text: qsTr("Save current layout…")
        onTriggered: root.saveRequested()
    }

    MenuItem {
        // Nothing to manage until something is saved, and an item that
        // opens an empty dialog is worse than no item at all.
        text: qsTr("Manage layouts…")
        visible: !root.empty
        height: visible ? implicitHeight : 0
        onTriggered: root.manageRequested()
    }
}
