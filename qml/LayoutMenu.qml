import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The saved pane arrangements, as a menu: pick one to put the panes back
// the way they were, save the arrangement on screen under a name, rename
// one, throw one away.
//
// Every one of those is asked of the window rather than done here. This
// file knows what a layout menu looks like; Main.qml is the only place that
// knows what a SplitView is, which pane is which, and how wide the row is.
Menu {
    id: root

    signal applyRequested(string name)
    signal saveRequested()
    signal renameRequested(string name)
    signal deleteRequested(string name)

    readonly property bool empty: layouts.names.length === 0

    // The two per-item actions. Small, quiet, and on the row they act on --
    // a rename that lives three menus away is a rename nobody finds.
    component RowAction: Rectangle {
        id: act
        property alias glyph: actGlyph.text
        property string tip: ""
        signal activated()

        width: 22
        height: 20
        radius: Theme.radiusS
        color: actMouse.containsMouse ? Theme.buttonHover : "transparent"

        Text {
            id: actGlyph
            anchors.centerIn: parent
            font.pixelSize: 12
            color: actMouse.containsMouse ? Theme.text : Theme.dimText
        }

        MouseArea {
            id: actMouse
            anchors.fill: parent
            hoverEnabled: true
            // Accepting the press here is what keeps it from reaching the
            // menu item underneath and applying the layout instead. (No id
            // from the file around it is in scope here: an inline component
            // has a scope of its own, so closing the menu is the caller's
            // job, one level out.)
            onClicked: act.activated()
        }

        ToolTip.visible: actMouse.containsMouse && act.tip.length > 0
        ToolTip.delay: 500
        ToolTip.text: act.tip
    }

    Instantiator {
        model: layouts.names
        delegate: MenuItem {
            id: presetItem
            required property var modelData

            // The name is on the item itself as well as in the row below,
            // so the menu is legible to anything that reads a MenuItem
            // rather than looking at it.
            text: presetItem.modelData
            checkable: true
            // Bound, not assigned: triggering a checkable item writes
            // `checked` itself, and a plain binding would not survive that
            // -- the tick would stop following the applied layout the
            // moment anybody clicked anything.
            Binding on checked {
                value: presetItem.modelData === layouts.current
                restoreMode: Binding.RestoreNone
            }
            onTriggered: root.applyRequested(presetItem.modelData)

            // A layout, then the two things that can be done to it. The
            // name takes the slack so the actions line up down the menu
            // rather than trailing each name at a different distance.
            contentItem: RowLayout {
                spacing: Theme.spaceS
                Label {
                    Layout.fillWidth: true
                    text: presetItem.text
                    color: Theme.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                RowAction {
                    Layout.alignment: Qt.AlignVCenter
                    glyph: "✎"
                    tip: qsTr("Rename this layout")
                    onActivated: {
                        root.dismiss()
                        root.renameRequested(presetItem.modelData)
                    }
                }
                RowAction {
                    Layout.alignment: Qt.AlignVCenter
                    glyph: "✕"
                    tip: qsTr("Delete this layout")
                    onActivated: {
                        root.dismiss()
                        root.deleteRequested(presetItem.modelData)
                    }
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
}
