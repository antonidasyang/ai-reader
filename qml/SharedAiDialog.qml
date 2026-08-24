import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Shows every member's shared AI interpretation for the current paper, with
// author attribution. Bound to the `aiArtifacts` context property.
AppDialog {
    id: dlg
    title: qsTr("Shared AI interpretations")
    width: 520
    standardButtons: Dialog.Close

    property var items: []
    function refresh() { items = aiArtifacts.sharedForCurrent() }
    onAboutToShow: refresh()

    // Artifact-type codes come from the server; map the known ones to
    // a translated pill label and fall back to the raw code.
    function typeLabel(t) {
        return t === "summary" ? qsTr("interpretation") : (t || "")
    }

    // Small metadata pill (artifact type / model name).
    component MetaPill: Rectangle {
        property alias label: pillText.text
        visible: pillText.text.length > 0
        implicitWidth: pillText.implicitWidth + Theme.spaceM
        implicitHeight: pillText.implicitHeight + Theme.spaceXs + 2
        radius: height / 2
        color: Theme.fieldBg
        border.width: 1
        border.color: Theme.divider
        Label {
            id: pillText
            anchors.centerIn: parent
            font.pixelSize: 10
            color: Theme.dimText
        }
    }

    // ── Content ─────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceS

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 340
            clip: true
            model: dlg.items
            spacing: Theme.spaceS
            ScrollBar.vertical: ScrollBar { active: true }
            delegate: Rectangle {
                width: ListView.view ? ListView.view.width : 0
                implicitHeight: entryCol.implicitHeight + 2 * Theme.spaceM
                radius: Theme.radiusM
                color: Theme.cardBg
                border.width: 1
                border.color: Theme.divider

                ColumnLayout {
                    id: entryCol
                    anchors.fill: parent
                    anchors.margins: Theme.spaceM
                    spacing: Theme.spaceXs

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spaceS
                        Label {
                            text: (modelData.authorEmail || modelData.author)
                                  + (modelData.isMine ? qsTr(" (you)") : "")
                            color: Theme.accent
                            font.bold: true
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        MetaPill { label: dlg.typeLabel(modelData.type) }
                        MetaPill { label: modelData.model }
                    }
                    Label {
                        text: modelData.payload
                        color: Theme.text
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        font.pixelSize: 12
                    }
                }
            }
        }

        Label {
            visible: dlg.items.length === 0
            text: qsTr("No shared interpretations for this paper yet.")
            color: Theme.dimText
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceS
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Connections {
        target: aiArtifacts
        function onSharedCountChanged() { dlg.refresh() }
    }
}
