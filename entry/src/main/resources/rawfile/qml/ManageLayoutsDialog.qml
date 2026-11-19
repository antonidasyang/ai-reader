import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Housekeeping for the saved pane arrangements: rename one, throw one
// away. Nothing here moves a pane -- switching layouts stays in the menu,
// which is where the reader is when they want to switch.
//
// Renaming and deleting used to be two glyphs on each row of that menu,
// and the "✕" among them meant "delete" in the one app where "✕" means
// "close a pane" everywhere else. They live here now, as buttons with
// words on them, in a dialog the reader has to open on purpose.
//
// The rows say what each layout DOES, not just what it was called: a name
// chosen months ago ("Reading", "Reading 2") tells nobody which one opens
// the chat pane, and picking the wrong one to delete is exactly the
// accident this dialog exists to prevent. The delete confirmation is on
// the row itself for the same reason -- a second dialog stacked on top
// would cover the very row whose identity is in question.
AppDialog {
    id: root

    title: qsTr("Manage layouts")
    width: 520
    standardButtons: Dialog.NoButton

    // Main.qml owns the rename dialog (the same one that names a new
    // layout), and Main.qml is the only place that can measure and save
    // the arrangement on screen. Both are asked for rather than done here.
    signal renameRequested(string name)
    signal saveRequested()

    // Pane id -> the word the reader sees, set by Main.qml. A layout
    // written by a newer build can name a pane this one has never heard
    // of; that id is shown raw rather than dropped, so the summary still
    // accounts for every pane the layout opens.
    property var paneLabels: ({})

    // The layout whose Delete is waiting to be confirmed, BY NAME -- never
    // by row index. The list is sorted and can reflow under the dialog
    // (a rename lands the name elsewhere, another window's sync brings a
    // layout in), and an index would then point the pending confirm at
    // whatever slid into that slot. Empty when nothing is pending; being
    // a single name is also what limits it to one row at a time.
    property string confirmingName: ""

    // Bumped on every write to the stored document. layouts.load() is a
    // plain call with no change signal, so this is the only thing that can
    // tell a row's summary that the layout it describes was saved over
    // while the dialog stood open.
    property int revision: 0

    readonly property bool empty: layouts.names.length === 0

    // How much list the dialog will grow to hold before it starts
    // scrolling instead. Past this it is taller than the reader's screen.
    readonly property int maxListHeight: 420
    // Row buttons sit inside a list row, so they run shorter than the
    // full-height buttons of a dialog footer.
    readonly property int rowButtonH: 26

    // A pending confirm must never outlive the sight of the row it belongs
    // to: closing and re-opening starts clean.
    onClosed: root.confirmingName = ""

    Connections {
        target: layouts
        function onNamesChanged() {
            // Something appeared, went or was renamed under the dialog --
            // the row the reader was looking at when they pressed Delete
            // is not necessarily the row that is there now.
            root.confirmingName = ""
            ++root.revision
        }
        function onPresetsChanged() { ++root.revision }
    }

    // The panes a layout OPENS, in the layout's own order: what it does,
    // in one line. `revision` is not read in here -- it is a parameter so
    // that a binding calling this function names it and so re-runs when
    // the stored document changes.
    function paneSummary(name, revision) {
        const preset = layouts.load(name)
        const order = (preset && preset.order) ? preset.order : []
        const panes = (preset && preset.panes) ? preset.panes : ({})
        const labels = root.paneLabels ? root.paneLabels : ({})
        const shown = []
        for (let i = 0; i < order.length; ++i) {
            const id = String(order[i])
            const pane = panes[id]
            if (!pane || !pane.visible)
                continue
            const label = labels[id]
            shown.push((label === undefined || label === null) ? id
                                                               : String(label))
        }
        return shown.length > 0 ? shown.join(" · ") : qsTr("No panes showing")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceM

        // Page/Home/End walk the list, as they do in every other pane and
        // dialog with one.
        focus: true
        Keys.onPressed: (event) => ScrollKeys.handle(event, layoutList)

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: 12
            // Said once, up front: nothing on this screen rearranges the
            // window. A reader who came here looking for the switch should
            // be sent back to the menu before they start clicking.
            text: qsTr("Rename a saved arrangement or throw one away. "
                       + "Switching to a layout is done from the layout "
                       + "menu; nothing here moves the panes on screen.")
        }

        Label {
            Layout.fillWidth: true
            visible: root.empty
            wrapMode: Text.Wrap
            color: Theme.dimText
            font.pixelSize: 12
            // Reachable only if the dialog is opened with nothing saved --
            // the menu hides its entry until there is something to manage
            // -- but an empty frame with no explanation is worse than the
            // three lines it costs to say so.
            text: qsTr("Nothing saved yet — arrange the panes the way you "
                       + "like and save the layout from the layout menu.")
        }

        AppSectionCard {
            visible: !root.empty
            Layout.fillHeight: true
            // Tall enough for what is there, never taller than the cap:
            // three layouts should not open a dialog sized for thirty.
            Layout.preferredHeight:
                Math.min(layoutList.contentHeight + 2 * Theme.spaceXs,
                         root.maxListHeight)
            clip: true

            ListView {
                id: layoutList
                anchors.fill: parent
                anchors.margins: Theme.spaceXs
                clip: true
                model: layouts.names
                currentIndex: -1
                ScrollBar.vertical: ScrollBar { active: true }

                delegate: Item {
                    id: row
                    required property string modelData
                    required property int index

                    readonly property bool isCurrent:
                        row.modelData === layouts.current
                    readonly property bool pendingDelete:
                        root.confirmingName === row.modelData

                    width: ListView.view ? ListView.view.width : 0
                    implicitHeight: body.implicitHeight + Theme.spaceM

                    ColumnLayout {
                        id: body
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Theme.spaceS
                        anchors.rightMargin: Theme.spaceS
                        spacing: Theme.spaceXs

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spaceS

                            Label {
                                Layout.fillWidth: true
                                text: row.modelData
                                color: Theme.text
                                font.pixelSize: 14
                                // The arrangement on screen is the one the
                                // reader already has a feel for; it is
                                // worth picking out of the list.
                                font.weight: row.isCurrent ? Font.DemiBold
                                                           : Font.Normal
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                visible: row.isCurrent
                                implicitWidth: inUse.implicitWidth + Theme.spaceS
                                implicitHeight: inUse.implicitHeight + Theme.spaceXs
                                radius: Theme.radiusS
                                // Outlined rather than filled: it labels
                                // the row, it is not a thing to press.
                                color: "transparent"
                                border.width: 1
                                border.color: Theme.border
                                Label {
                                    id: inUse
                                    anchors.centerIn: parent
                                    text: qsTr("In use")
                                    font.pixelSize: 11
                                    color: Theme.dimText
                                }
                            }

                            AppButton {
                                Layout.alignment: Qt.AlignVCenter
                                visible: !row.pendingDelete
                                implicitHeight: root.rowButtonH
                                leftPadding: Theme.spaceM
                                rightPadding: Theme.spaceM
                                text: qsTr("Rename")
                                onClicked: {
                                    // Nobody asked for the other row's
                                    // Delete to still be armed behind the
                                    // rename dialog.
                                    root.confirmingName = ""
                                    root.renameRequested(row.modelData)
                                }
                            }

                            AppButton {
                                Layout.alignment: Qt.AlignVCenter
                                visible: !row.pendingDelete
                                implicitHeight: root.rowButtonH
                                leftPadding: Theme.spaceM
                                rightPadding: Theme.spaceM
                                danger: true
                                text: qsTr("Delete")
                                // Arms the confirm strip below; nothing is
                                // removed until the second press. Writing
                                // the name here is also what disarms any
                                // other row.
                                onClicked: root.confirmingName = row.modelData
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: !row.pendingDelete
                            // What this layout does, which is how one
                            // "Reading" is told from another.
                            text: root.paneSummary(row.modelData, root.revision)
                            color: Theme.dimText
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: row.pendingDelete
                            wrapMode: Text.Wrap
                            color: Theme.text
                            font.pixelSize: 12
                            // The sentence answers the question that made
                            // the old "✕" dangerous: no, this does not
                            // close anything on screen.
                            text: qsTr("This layout will be gone. The panes "
                                       + "on screen stay exactly as they are.")
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: row.pendingDelete
                            spacing: Theme.spaceS
                            Item { Layout.fillWidth: true }
                            AppButton {
                                implicitHeight: root.rowButtonH
                                leftPadding: Theme.spaceM
                                rightPadding: Theme.spaceM
                                text: qsTr("Cancel")
                                onClicked: root.confirmingName = ""
                            }
                            AppButton {
                                implicitHeight: root.rowButtonH
                                leftPadding: Theme.spaceM
                                rightPadding: Theme.spaceM
                                danger: true
                                text: qsTr("Delete")
                                onClicked: {
                                    // Read the name before the row goes:
                                    // removing it takes this delegate with
                                    // it, part-way through the handler.
                                    const doomed = row.modelData
                                    root.confirmingName = ""
                                    layouts.remove(doomed)
                                }
                            }
                        }
                    }

                    // Rows run two and three lines tall, so they need a
                    // rule between them to read as separate layouts.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.divider
                        visible: row.index < layoutList.count - 1
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceS
            // Saving is here as well as in the menu: a reader who opened
            // this to tidy up and found the arrangement they want is
            // already worth keeping should not have to go back out for it.
            AppButton {
                text: qsTr("Save current layout…")
                onClicked: root.saveRequested()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Close")
                primary: true
                onClicked: root.close()
            }
        }
    }
}
