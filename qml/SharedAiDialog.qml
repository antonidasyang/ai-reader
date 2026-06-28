import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Shows every member's shared AI interpretation for the current paper, with
// author attribution. Bound to the `aiArtifacts` context property.
Dialog {
    id: dlg
    title: qsTr("Shared AI interpretations")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: 12
    standardButtons: Dialog.Close

    property var items: []
    function refresh() { items = aiArtifacts.sharedForCurrent() }
    onAboutToShow: refresh()

    background: Rectangle {
        color: Theme.paneBg
        border.color: Theme.border
        radius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 340
            clip: true
            model: dlg.items
            spacing: 8
            ScrollBar.vertical: ScrollBar { active: true }
            delegate: ColumnLayout {
                width: ListView.view ? ListView.view.width : 0
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: (modelData.authorEmail || modelData.author)
                              + (modelData.isMine ? qsTr(" (you)") : "")
                        color: Theme.accent
                        font.bold: true
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label { text: modelData.type; color: Theme.dimText; font.pixelSize: 10 }
                    Label { text: modelData.model; color: Theme.dimText; font.pixelSize: 10 }
                }
                Label {
                    text: modelData.payload
                    color: Theme.text
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    font.pixelSize: 12
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
            }
        }

        Label {
            visible: dlg.items.length === 0
            text: qsTr("No shared interpretations for this paper yet.")
            color: Theme.dimText
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Connections {
        target: aiArtifacts
        function onSharedCountChanged() { dlg.refresh() }
    }
}
