import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Edit one item's bibliographic fields. openFor(id) loads from libraryModel;
// the Fetch row resolves a DOI/arXiv id via metadata service; Save writes back.
Dialog {
    id: dlg
    title: qsTr("Edit metadata")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 500
    padding: Theme.dialogPadding
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

    // ── Shared dialog chrome ────────────────────────────────────────
    palette.window: Theme.dialogBg
    palette.windowText: Theme.text
    palette.base: Theme.fieldBg
    palette.text: Theme.text
    palette.button: Theme.buttonBg
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
    palette.placeholderText: Theme.dimText

    background: Rectangle {
        color: Theme.dialogBg
        border.color: Theme.border
        border.width: 1
        radius: Theme.radiusL
        Rectangle {
            z: -1
            x: 0; y: 2
            width: parent.width
            height: parent.height
            radius: parent.radius + 1
            color: Theme.dialogShadow
        }
    }

    Overlay.modal: Rectangle {
        color: Theme.overlayDim
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100; easing.type: Easing.InCubic }
    }

    header: Item {
        implicitHeight: headerTitle.implicitHeight + Theme.spaceL + Theme.spaceM + 1
        Label {
            id: headerTitle
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.dialogPadding
            anchors.rightMargin: Theme.dialogPadding
            anchors.topMargin: Theme.spaceL
            text: dlg.title
            elide: Text.ElideRight
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.divider
        }
    }

    footer: DialogButtonBox {
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: Theme.spaceS
        leftPadding: Theme.dialogPadding
        rightPadding: Theme.dialogPadding
        topPadding: Theme.spaceM
        bottomPadding: Theme.spaceL
        delegate: ActionButton {
            primary: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
        }
        background: Rectangle {
            color: "transparent"
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.divider
            }
        }
    }

    component ActionButton: Button {
        id: ab
        property bool primary: false
        implicitHeight: Theme.controlH
        leftPadding: Theme.spaceL
        rightPadding: Theme.spaceL
        contentItem: Text {
            text: ab.text
            font.pixelSize: 13
            font.weight: ab.primary ? Font.DemiBold : Font.Normal
            color: ab.primary ? Theme.onPrimary : Theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            opacity: ab.enabled ? 1 : 0.45
        }
        background: Rectangle {
            radius: Theme.radiusS
            color: ab.primary
                   ? (ab.down ? Theme.primaryPressed : ab.hovered ? Theme.primaryHover : Theme.primaryBg)
                   : (ab.down ? Theme.buttonPressed : ab.hovered ? Theme.buttonHover : Theme.buttonBg)
            border.width: ab.primary ? 0 : 1
            border.color: ab.visualFocus ? Theme.accent : Theme.border
            opacity: ab.enabled ? 1 : 0.45
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    component FieldText: TextField {
        id: ft
        implicitHeight: Theme.controlH
        leftPadding: Theme.spaceM - 2
        rightPadding: Theme.spaceM - 2
        font.pixelSize: 13
        color: Theme.text
        placeholderTextColor: Theme.dimText
        selectionColor: Theme.accent
        selectedTextColor: Theme.onAccent
        background: Rectangle {
            radius: Theme.radiusS
            color: Theme.fieldBg
            border.width: 1
            border.color: ft.activeFocus ? Theme.accent : Theme.fieldBorder
            Behavior on border.color { ColorAnimation { duration: 120 } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                radius: parent.radius + 2
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
                visible: ft.activeFocus
            }
        }
    }

    component FieldCombo: ComboBox {
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
            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on border.color { ColorAnimation { duration: 120 } }
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

    component FormLabel: Label {
        color: Theme.bodyText
        font.pixelSize: 13
        horizontalAlignment: Text.AlignRight
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
    }

    // ── Content ─────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceL

        // Auto-fill card: paste an identifier, fetch the rest.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: fetchRow.implicitHeight + 2 * Theme.spaceM
            radius: Theme.radiusM
            color: Theme.cardBg
            border.width: 1
            border.color: Theme.divider

            RowLayout {
                id: fetchRow
                anchors.fill: parent
                anchors.margins: Theme.spaceM
                spacing: Theme.spaceS
                FieldText {
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
                ActionButton {
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

            FormLabel { text: qsTr("Type") }
            FieldCombo {
                id: typeBox
                Layout.fillWidth: true
                model: dlg.itemTypeLabels
            }
            FormLabel { text: qsTr("Title") }
            FieldText { id: titleF; Layout.fillWidth: true }
            FormLabel { text: qsTr("Authors") }
            FieldText {
                id: creatorsF
                Layout.fillWidth: true
                placeholderText: qsTr("comma-separated")
            }
            FormLabel { text: qsTr("Year") }
            FieldText {
                id: yearF
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
            }
            FormLabel { text: qsTr("Source") }
            FieldText {
                id: pubF
                Layout.fillWidth: true
                placeholderText: qsTr("journal / conference")
            }
            FormLabel { text: qsTr("DOI") }
            FieldText { id: doiF; Layout.fillWidth: true }
            FormLabel { text: qsTr("arXiv") }
            FieldText { id: arxivF; Layout.fillWidth: true }
            FormLabel { text: qsTr("Tags") }
            FieldText {
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
