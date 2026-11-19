import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The quiet explanatory line under a control.
Label {
    Layout.fillWidth: true
    wrapMode: Text.Wrap
    visible: text.length > 0
    font.pixelSize: 11
    lineHeight: 1.25
    color: Theme.dimText
}
