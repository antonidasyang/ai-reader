import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One fenced-code block (``` … ```) inside an assistant chat reply.
// Receives pre-rendered, syntax-highlighted HTML from C++
// (CodeHighlighter — the in-house substitute for KSyntaxHighlighting),
// plus the raw source for the Copy button. Fence info is shown as a
// small caption in the top strip.
Rectangle {
    id: root

    property string source: ""
    property string html: ""
    property string language: ""

    Layout.fillWidth: true
    implicitHeight: column.implicitHeight + 4
    color: "#f6f8fa"
    border.color: "#e1e4e8"
    border.width: 1
    radius: 4

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // Header strip: language label + Copy.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            color: "#eef0f3"
            radius: 4

            // Mask the bottom corners so the strip blends with the body.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: parent.height / 2
                color: parent.color
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 6

                Label {
                    text: root.language.length > 0 ? root.language : qsTr("code")
                    font.pixelSize: 10
                    color: "#666"
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: qsTr("Copy")
                    flat: true
                    font.pixelSize: 10
                    padding: 2
                    Layout.preferredHeight: 18
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Copy code to clipboard")
                    onClicked: {
                        // Pull from the off-screen plaintext helper so
                        // the clipboard gets raw source, not the colour
                        // spans baked into the visible body.
                        copyHelper.selectAll()
                        copyHelper.copy()
                        copyHelper.deselect()
                    }
                }
            }
        }

        // Highlighted code body. RichText so the colour spans coming
        // from CodeHighlighter render; selectByMouse for in-bubble
        // text selection.
        TextEdit {
            id: codeText
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 6
            Layout.bottomMargin: 8
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.RichText
            font.family: "monospace"
            font.pixelSize: 12
            color: "#1d1d1d"
            text: root.html
            cursorVisible: false
            activeFocusOnPress: false

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                cursorShape: Qt.IBeamCursor
            }
        }

        // Off-screen helper: holds the raw source so the Copy button
        // can put un-decorated text on the clipboard.
        TextEdit {
            id: copyHelper
            visible: false
            width: 0
            height: 0
            text: root.source
        }
    }
}
