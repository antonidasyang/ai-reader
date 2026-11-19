import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What the last session did not finish, offered back.
//
// A task that was running when the app closed is written to disk as
// interrupted rather than failed, because it is neither the user's mistake
// nor the model's: the window shut. On the next launch it is worth picking
// up exactly once, here, with the work named and how far it had got — and
// then it is either resumed or thrown away, never left to haunt a later
// launch as a question that keeps being asked.
//
// Resuming is not the same as restarting everything on the spot: work on a
// paper needs that paper open, so the manager starts what it can, asks for
// the papers it still needs, and keeps whatever it could not pick up. The
// wording below promises exactly that and nothing more.
AppDialog {
    id: root
    title: qsTr("Unfinished from last time")
    width: 460
    standardButtons: Dialog.NoButton

    // pending() is a plain call with no change notification, so it is read
    // once when the dialog opens rather than bound to.
    property var items: []
    onOpened: root.items = tasks.pending()

    function rowTitle(item) {
        const t = item && item.title ? String(item.title) : ""
        const p = item && item.paperTitle ? String(item.paperTitle) : ""
        return p.length > 0 ? (t + " — " + p) : t
    }
    // How far it had got, when it knew how much there was to do.
    function rowSteps(item) {
        const steps = item && item.steps ? Number(item.steps) : 0
        if (steps <= 0)
            return ""
        const done = item && item.done ? Number(item.done) : 0
        return qsTr("%1/%2").arg(done).arg(steps)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // Page/Home/End walk the list below.
        focus: true
        Keys.onPressed: (event) => ScrollKeys.handle(event, pendingScroll.contentItem)

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.text
            text: qsTr("The app closed while these were still running.")
        }

        ScrollView {
            id: pendingScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(200,
                                             Math.max(26, root.items.length * 26))
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 2
                Repeater {
                    model: root.items
                    delegate: RowLayout {
                        id: pendingRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Theme.text
                            font.pixelSize: 13
                            text: root.rowTitle(pendingRow.modelData)
                        }
                        Label {
                            color: Theme.dimText
                            font.pixelSize: 11
                            text: pendingRow.modelData && pendingRow.modelData.kindLabel
                                  ? pendingRow.modelData.kindLabel : ""
                        }
                        Label {
                            visible: text.length > 0
                            color: Theme.dimText
                            font.pixelSize: 11
                            text: root.rowSteps(pendingRow.modelData)
                        }
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            // Honest about what a resume can actually do. It used to say
            // the work started again, full stop; anything belonging to a
            // paper that was not open simply did not, and nothing on screen
            // admitted it.
            text: qsTr("Resuming picks this work back up as each paper comes "
                       + "back — starting now with whatever is already open, "
                       + "and continuing as the others are reopened. Anything "
                       + "it cannot pick up stays on this list and is offered "
                       + "again next time. Discarding forgets it all — you can "
                       + "always start the work again yourself.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: Theme.spaceS
            Item { Layout.fillWidth: true }
            // Quiet, because throwing the work away is the cheaper mistake to
            // make by accident only if it does not look like the way out.
            AppButton {
                ghost: true
                text: qsTr("Discard")
                onClicked: {
                    tasks.discardPending()
                    root.close()
                }
            }
            AppButton {
                primary: true
                text: qsTr("Resume")
                onClicked: {
                    tasks.resumePending()
                    root.close()
                }
            }
        }
    }
}
