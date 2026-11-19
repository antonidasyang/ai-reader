import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

// The last thing between unfinished work and a closed window.
//
// Closing the app kills whatever is in the queue, and a translation that was
// four fifths done is four fifths of a bill already paid. So the window says
// what is still going before it goes: named, not counted, because "3 tasks"
// does not tell anyone whether it is safe to close. Nothing is lost for good
// either way — the unfinished ones are written down and offered back on the
// next launch — and that promise is made here, where the decision is.
AppDialog {
    id: root
    title: qsTr("Work is still running")
    width: 460
    standardButtons: Dialog.NoButton

    property int runningCount: 0
    property int queuedCount: 0

    // Emitted when the user chooses to close anyway; the window quits on it.
    signal confirmed()

    readonly property int unfinished: root.runningCount + root.queuedCount
    // How many rows before the list stops naming and starts counting.
    readonly property int maxRows: 6
    readonly property int rowHeight: 22

    onAccepted: root.confirmed()
    // The delegate model has nothing in it until it has seen the task model,
    // and what is unfinished differs from one opening to the next, so the
    // group is sorted out as the box opens rather than once at startup.
    onOpened: unfinishedModel.refilter()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // Page/Home/End walk the list below.
        focus: true
        Keys.onPressed: (event) => ScrollKeys.handle(event, unfinishedList)

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.text
            text: root.queuedCount > 0
                  ? qsTr("%1 running, %2 waiting to start.")
                    .arg(root.runningCount).arg(root.queuedCount)
                  : qsTr("%1 still running.").arg(root.runningCount)
        }

        Rectangle {
            Layout.fillWidth: true
            // Only the rows that fit; the remainder is counted underneath.
            Layout.preferredHeight: Math.min(unfinishedList.contentHeight,
                                             root.maxRows * root.rowHeight) + 2
            color: Theme.fieldBg
            border.color: Theme.border
            radius: 4
            clip: true

            // The unfinished tasks, and only the first few of them.
            //
            // This box used to hand the whole task model to the view and
            // collapse the finished rows to height 0. A zero-height delegate
            // never advances a ListView's fill position, so the view went on
            // asking for one more row and one more row: after a long session
            // it built a delegate for every task that had ever run in order
            // to show at most six. There is no filter proxy to put in front
            // of the model, so the rows are sorted into a delegate model
            // group here and the view is given that group alone -- six
            // delegates, however long the session has been.
            DelegateModel {
                id: unfinishedModel
                model: tasks.model
                filterOnGroup: "unfinished"
                groups: DelegateModelGroup { id: unfinishedGroup; name: "unfinished" }

                // canCancel is exactly "queued or running", which is exactly
                // what closing is about to lose. Reading a row this way costs
                // a cache item rather than a delegate, and the walk stops the
                // moment it has them all, or has the six the box can name --
                // the manager, not the model, is what knows how many there
                // are to look for.
                function refilter() {
                    if (unfinishedGroup.count > 0)
                        unfinishedGroup.remove(0, unfinishedGroup.count)
                    const want = Math.min(root.maxRows, tasks.activeCount)
                    let found = 0
                    for (let i = 0; i < items.count && found < want; ++i) {
                        const entry = items.get(i)
                        if (entry && entry.model && entry.model.canCancel) {
                            items.addGroups(i, 1, ["unfinished"])
                            ++found
                        }
                    }
                }

                delegate: Item {
                    width: unfinishedList.width
                    height: root.rowHeight

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Theme.text
                            font.pixelSize: 12
                            text: model.paperTitle && model.paperTitle.length > 0
                                  ? (model.title + " — " + model.paperTitle)
                                  : model.title
                        }
                        Label {
                            color: Theme.dimText
                            font.pixelSize: 11
                            text: model.stateLabel
                        }
                    }
                }
            }

            // A task finishing or starting while the box is open changes what
            // belongs in it. Both arrive as a change of counts; the guard is
            // there because this dialog outlives every task in the session and
            // must not walk the model for each one.
            Connections {
                target: tasks
                function onCountsChanged() {
                    if (root.opened)
                        unfinishedModel.refilter()
                }
            }

            ListView {
                id: unfinishedList
                anchors.fill: parent
                anchors.margins: 1
                model: unfinishedModel
                clip: true
                ScrollBar.vertical: ScrollBar {}
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.unfinished > root.maxRows
            color: Theme.dimText
            font.pixelSize: 12
            text: qsTr("…and %1 more").arg(root.unfinished - root.maxRows)
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dimText
            text: qsTr("Closing stops them where they are. The unfinished ones "
                       + "are remembered, and offered back the next time you "
                       + "open the app.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: Theme.spaceS
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Keep working")
                onClicked: root.reject()
            }
            AppButton {
                danger: true
                text: qsTr("Close anyway")
                onClicked: root.accept()
            }
        }
    }
}
