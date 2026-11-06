import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// "Download from a link": paste an arXiv / OpenReview / PDF link (or just an
// arXiv id), and urlImport fetches the PDF into the folder open in the
// folder pane (or ~/Downloads when none is), opens it, and adds it to the
// current project when there is one to write to.
// The dialog closes itself once the paper is on disk; a failure stays on
// screen with the reason, so the link can be fixed and tried again.
AppDialog {
    id: dialog
    title: qsTr("Download from a link")
    width: 520

    // Emitted with a file:// URL once the PDF is saved.
    signal paperReady(string fileUrl)

    // The last failure, shown until the next attempt.
    property string errorText: ""

    function startDownload() {
        const text = linkField.text.trim()
        if (text.length === 0 || urlImport.busy)
            return
        errorText = ""
        urlImport.fetch(text)
    }

    // Open with `link` already filled in (a link dropped on the window), or
    // with whatever link the clipboard holds.
    function openWith(link) {
        linkField.text = link || ""
        open()
    }

    onOpened: {
        errorText = ""
        // A link already on the clipboard is almost always the one meant.
        if (linkField.text.trim().length === 0)
            linkField.text = urlImport.clipboardLink()
        linkField.selectAll()
        linkField.forceActiveFocus()
    }

    Connections {
        target: urlImport
        function onDownloaded(fileUrl, addedToLibrary) {
            linkField.text = ""
            dialog.paperReady(fileUrl)
            dialog.close()
        }
        function onFailed(message) {
            dialog.errorText = message
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceM

        AppTextField {
            id: linkField
            Layout.fillWidth: true
            enabled: !urlImport.busy
            placeholderText: "https://arxiv.org/abs/2609.02309"
            onAccepted: dialog.startDownload()
            onTextEdited: dialog.errorText = ""
        }
        AppHintLabel {
            text: qsTr("An arXiv link or id, a Hugging Face paper page, an OpenReview "
                       + "link, or any link to a PDF. Saved to %1 (the folder open in "
                       + "the folder pane, or Downloads when none is open), and added "
                       + "to the current project's library when you can write to it.")
                      .arg(urlImport.downloadFolder)
        }

        ProgressBar {
            Layout.fillWidth: true
            visible: urlImport.busy
            indeterminate: urlImport.progress < 0
            from: 0
            to: 1
            value: Math.max(0, urlImport.progress)
        }
        Label {
            Layout.fillWidth: true
            text: urlImport.busy ? urlImport.status : dialog.errorText
            visible: text.length > 0
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: urlImport.busy ? Theme.dimText : Theme.danger
        }
    }

    footer: Item {
        implicitHeight: buttonRow.implicitHeight + Theme.spaceM + Theme.spaceL
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.divider
        }
        RowLayout {
            id: buttonRow
            anchors.fill: parent
            anchors.leftMargin: Theme.dialogPadding
            anchors.rightMargin: Theme.dialogPadding
            anchors.topMargin: Theme.spaceM
            anchors.bottomMargin: Theme.spaceL
            spacing: Theme.spaceS
            Item { Layout.fillWidth: true }
            AppButton {
                ghost: true
                text: urlImport.busy ? qsTr("Stop") : qsTr("Close")
                onClicked: urlImport.busy ? urlImport.cancel() : dialog.close()
            }
            AppButton {
                primary: true
                text: qsTr("Download")
                enabled: !urlImport.busy && linkField.text.trim().length > 0
                onClicked: dialog.startDownload()
            }
        }
    }
}
