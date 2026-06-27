import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Edit one item's bibliographic fields. openFor(id) loads from libraryModel;
// accepting writes back via libraryModel.updateItem (which syncs).
Dialog {
    id: dlg
    title: qsTr("Edit metadata")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: 14
    standardButtons: Dialog.Save | Dialog.Cancel

    property string itemId: ""

    background: Rectangle {
        color: Theme.paneBg
        border.color: Theme.border
        radius: 6
    }

    function openFor(id) {
        itemId = id
        const f = libraryModel.itemFields(id)
        titleF.text = f.title || ""
        creatorsF.text = (f.creators || []).join(", ")
        yearF.text = (f.year !== undefined && f.year !== null) ? String(f.year) : ""
        pubF.text = f.publication || ""
        doiF.text = f.doi || ""
        arxivF.text = f.arxivId || ""
        const t = f.itemType || "journalArticle"
        const i = typeBox.model.indexOf(t)
        typeBox.currentIndex = i >= 0 ? i : 0
        open()
    }

    onAccepted: {
        let fields = {
            title: titleF.text,
            creators: creatorsF.text.split(",")
                      .map(function(s) { return s.trim() })
                      .filter(function(s) { return s.length > 0 }),
            publication: pubF.text,
            doi: doiF.text,
            arxivId: arxivF.text,
            itemType: typeBox.currentText
        }
        const y = parseInt(yearF.text)
        if (!isNaN(y))
            fields.year = y
        libraryModel.updateItem(itemId, fields)
    }

    GridLayout {
        anchors.fill: parent
        columns: 2
        columnSpacing: 8
        rowSpacing: 6

        Label { text: qsTr("Type"); color: Theme.dimText }
        ComboBox {
            id: typeBox
            Layout.fillWidth: true
            model: ["journalArticle", "conferencePaper", "preprint", "book",
                "bookSection", "thesis", "report", "webpage"]
        }
        Label { text: qsTr("Title"); color: Theme.dimText }
        TextField { id: titleF; Layout.fillWidth: true }
        Label { text: qsTr("Authors"); color: Theme.dimText }
        TextField {
            id: creatorsF
            Layout.fillWidth: true
            placeholderText: qsTr("comma-separated")
        }
        Label { text: qsTr("Year"); color: Theme.dimText }
        TextField {
            id: yearF
            Layout.fillWidth: true
            inputMethodHints: Qt.ImhDigitsOnly
        }
        Label { text: qsTr("Source"); color: Theme.dimText }
        TextField {
            id: pubF
            Layout.fillWidth: true
            placeholderText: qsTr("journal / conference")
        }
        Label { text: qsTr("DOI"); color: Theme.dimText }
        TextField { id: doiF; Layout.fillWidth: true }
        Label { text: qsTr("arXiv"); color: Theme.dimText }
        TextField { id: arxivF; Layout.fillWidth: true }
    }
}
