import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Edit one item's bibliographic fields. openFor(id) loads from libraryModel;
// the Fetch row resolves a DOI/arXiv id via metadata service; Save writes back.
AppDialog {
    id: dlg
    title: qsTr("Edit metadata")
    width: 500
    standardButtons: Dialog.Save | Dialog.Cancel

    property string itemId: ""

    // Parallel arrays — the Zotero-style code is what gets persisted to
    // the library; the combo shows the translated label.
    readonly property var itemTypeCodes:
        ["journalArticle", "conferencePaper", "preprint", "book",
         "bookSection", "thesis", "report", "webpage"]
    readonly property var itemTypeLabels: [
        qsTr("Journal article"), qsTr("Conference paper"), qsTr("Preprint"),
        qsTr("Book"), qsTr("Book section"), qsTr("Thesis"), qsTr("Report"),
        qsTr("Webpage")
    ]

    function loadFields() {
        const f = libraryModel.itemFields(itemId)
        titleF.text = f.title || ""
        creatorsF.text = (f.creators || []).join(", ")
        yearF.text = (f.year !== undefined && f.year !== null) ? String(f.year) : ""
        pubF.text = f.publication || ""
        doiF.text = f.doi || ""
        arxivF.text = f.arxivId || ""
        tagsF.text = (f.tags || []).join(", ")
        const t = f.itemType || "journalArticle"
        const i = dlg.itemTypeCodes.indexOf(t)
        typeBox.currentIndex = i >= 0 ? i : 0
    }
    function openFor(id) {
        itemId = id
        loadFields()
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
            tags: tagsF.text.split(",")
                  .map(function(s) { return s.trim() })
                  .filter(function(s) { return s.length > 0 }),
            itemType: dlg.itemTypeCodes[typeBox.currentIndex]
        }
        const y = parseInt(yearF.text)
        if (!isNaN(y))
            fields.year = y
        libraryModel.updateItem(itemId, fields)
    }

    // ── Content ─────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceL

        // Auto-fill card: paste an identifier, fetch the rest.
        AppSectionCard {
            implicitHeight: fetchRow.implicitHeight + 2 * Theme.spaceM

            RowLayout {
                id: fetchRow
                anchors.fill: parent
                anchors.margins: Theme.spaceM
                spacing: Theme.spaceS
                AppTextField {
                    id: identF
                    Layout.fillWidth: true
                    placeholderText: qsTr("Paste a DOI or arXiv id to auto-fill")
                }
                BusyIndicator {
                    running: metadata.busy
                    visible: metadata.busy
                    implicitWidth: 18
                    implicitHeight: 18
                }
                AppButton {
                    text: qsTr("Fetch")
                    enabled: !metadata.busy && identF.text.trim().length > 0
                    onClicked: metadata.resolveIdentifier(dlg.itemId, identF.text)
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spaceM
            rowSpacing: Theme.spaceS

            AppFormLabel { text: qsTr("Type") }
            AppComboBox {
                id: typeBox
                Layout.fillWidth: true
                model: dlg.itemTypeLabels
            }
            AppFormLabel { text: qsTr("Title") }
            AppTextField { id: titleF; Layout.fillWidth: true }
            AppFormLabel { text: qsTr("Authors") }
            AppTextField {
                id: creatorsF
                Layout.fillWidth: true
                placeholderText: qsTr("comma-separated")
            }
            AppFormLabel { text: qsTr("Year") }
            AppTextField {
                id: yearF
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
            }
            AppFormLabel { text: qsTr("Source") }
            AppTextField {
                id: pubF
                Layout.fillWidth: true
                placeholderText: qsTr("journal / conference")
            }
            AppFormLabel { text: qsTr("DOI") }
            AppTextField { id: doiF; Layout.fillWidth: true }
            AppFormLabel { text: qsTr("arXiv") }
            AppTextField { id: arxivF; Layout.fillWidth: true }
            AppFormLabel { text: qsTr("Tags") }
            AppTextField {
                id: tagsF
                Layout.fillWidth: true
                placeholderText: qsTr("comma-separated")
            }
        }

        Label {
            text: metadata.status
            visible: metadata.status.length > 0
            color: Theme.dimText
            font.pixelSize: 11
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    // When a fetch completes for this item, reload the fields it filled.
    Connections {
        target: metadata
        function onResolved(id, ok) {
            if (id === dlg.itemId && ok)
                dlg.loadFields()
        }
    }
}
