import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The one combo box style, shared by every dialog.
ComboBox {
    id: fc
    font.pixelSize: 13
    implicitHeight: Theme.controlH
    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Theme.controlH
        radius: Theme.radiusS
        color: fc.down ? Theme.buttonPressed : fc.hovered ? Theme.buttonHover : Theme.fieldBg
        border.width: 1
        border.color: (fc.activeFocus || fc.visualFocus) ? Theme.accent : Theme.fieldBorder
        Behavior on color { ColorAnimation { duration: Theme.animMs } }
        Behavior on border.color { ColorAnimation { duration: Theme.animMs } }
    }
    contentItem: TextField {
        leftPadding: Theme.spaceS + 2
        rightPadding: Theme.spaceXs
        text: fc.editable ? fc.editText : fc.displayText
        enabled: fc.editable
        autoScroll: fc.editable
        readOnly: fc.down
        inputMethodHints: fc.inputMethodHints
        validator: fc.validator
        selectByMouse: true
        color: Theme.text
        placeholderTextColor: Theme.dimText
        selectionColor: Theme.accent
        selectedTextColor: Theme.onAccent
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: 13
        background: null
    }
    delegate: ItemDelegate {
        id: fcDel
        required property var model
        required property int index
        width: ListView.view ? ListView.view.width : 0
        height: 28
        text: model.display !== undefined ? model.display : model.modelData
        highlighted: fc.highlightedIndex === index
        contentItem: Text {
            text: fcDel.text
            font.pixelSize: 13
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusS - 2
            color: fcDel.highlighted ? Theme.hover : "transparent"
        }
    }
    popup: Popup {
        y: fc.height + 4
        width: fc.width
        padding: Theme.spaceXs
        implicitHeight: Math.min(contentItem.implicitHeight
                                 + topPadding + bottomPadding, 320)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: fc.popup.visible ? fc.delegateModel : null
            currentIndex: fc.highlightedIndex
            ScrollBar.vertical: ScrollBar { }
        }
        background: Rectangle {
            color: Theme.dialogBg
            border.width: 1
            border.color: Theme.border
            radius: Theme.radiusM
        }
    }
}
