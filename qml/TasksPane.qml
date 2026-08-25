import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Everything the app is doing at once, in one list.
//
// Translating a paper, splitting it, reading its table of contents,
// interpreting it and analysing a whole project each used to report itself
// wherever it was started — a bar in one pane, a spinner in a dialog, nothing
// at all in the third — and two of them could quietly run against the same
// paper. They go through one queue now, so this is the one place that can say
// what is in flight, how far along it is, how long it has left, and why the
// thing that failed an hour ago failed.
Rectangle {
    id: root
    color: Theme.paneBg

    // True while a splitter handle is being dragged. Re-eliding a list of
    // titles on every mouse move is wasted work, so the content holds the
    // width it had and catches up on a timer -- the same trick the research,
    // interpretation and paragraph panes use.
    property bool resizing: false
    property real layoutWidth: width
    onResizingChanged: if (!resizing) {
        reflow.stop()
        root.layoutWidth = root.width
    }
    onWidthChanged: {
        if (!root.resizing) { root.layoutWidth = root.width; return }
        if (!reflow.running) reflow.start()
    }
    Timer {
        id: reflow
        interval: 32
        onTriggered: root.layoutWidth = root.width
    }

    readonly property int fs: 13

    // Tasks::State, in the order TaskTypes.h declares it. A row compares
    // numbers rather than naming the C++ enum — it lives in a Q_NAMESPACE
    // and is not a QML type — and every word the reader sees still comes
    // from the translated stateLabel role.
    readonly property int stateQueued: 0
    readonly property int stateRunning: 1
    readonly property int stateSucceeded: 2
    readonly property int stateFailed: 3
    readonly property int stateCanceled: 4
    readonly property int stateInterrupted: 5

    // m:ss. Everything here is minutes, not hours: a task that runs longer
    // than an hour still reads correctly, just with a large minute count.
    function clock(ms) {
        const total = Math.max(0, Math.round(ms / 1000))
        const secs = total % 60
        return Math.floor(total / 60) + ":" + (secs < 10 ? "0" : "") + secs
    }

    // "<title> — <paperTitle>", with no dash for project-level work, which
    // has no paper.
    function rowTitle(title, paperTitle) {
        const t = title ? String(title) : ""
        const p = paperTitle ? String(paperTitle) : ""
        return p.length > 0 ? (t + " — " + p) : t
    }

    // The right-hand end of the progress line: how many steps are done, how
    // long it has been going, and how long it has left. Each part drops out
    // when the task cannot say.
    function numbers(done, total, elapsedMs, etaMs) {
        const parts = []
        if (total > 0)
            parts.push(qsTr("%1/%2").arg(done).arg(total))
        if (elapsedMs > 0)
            parts.push(root.clock(elapsedMs))
        if (etaMs >= 0)
            parts.push(qsTr("~%1 left").arg(root.clock(etaMs)))
        return parts.join(" · ")
    }

    // The word a task that has stopped ends on. Every terminal state has
    // one -- that is the whole point of it: a row that shows neither a
    // state nor an error, which a task that failed with an empty error
    // string used to be, tells the reader nothing at all about what
    // happened. stateLabel arrives filled in today, so the fallbacks below
    // exist only so that no answer from the model can produce a blank row.
    function endLabel(state, stateLabel) {
        const label = stateLabel ? String(stateLabel) : ""
        if (label.length > 0)
            return label
        switch (state) {
        case root.stateSucceeded:   return qsTr("Done")
        case root.stateFailed:      return qsTr("Failed")
        case root.stateCanceled:    return qsTr("Canceled")
        case root.stateInterrupted: return qsTr("Interrupted")
        default:                    return qsTr("Finished")
        }
    }

    // The chip's colour is the only thing separating a page of translations
    // from a page of interpretations at a glance. Keys are Tasks::kindKey().
    function kindTint(kindKey) {
        switch (kindKey) {
        case "translate":        return Theme.accent
        case "segment":          return Theme.dimText
        case "toc":              return Theme.dimText
        case "vision":           return Theme.dimText
        case "quick_interpret":  return Theme.heading
        case "deep_interpret":   return Theme.heading
        case "batch_interpret":  return Theme.heading
        case "library_analysis": return Theme.success
        default:                 return Theme.dimText
        }
    }

    // Home / End / PageUp / PageDown walk the list once the pane has been
    // clicked into, the way every other scrolling pane does it.
    Keys.onPressed: (event) => ScrollKeys.handle(event, list)
    TapHandler { onTapped: root.forceActiveFocus() }

    component Chip: Rectangle {
        id: chip
        property string label: ""
        property color tint: Theme.dimText
        implicitWidth: chipText.implicitWidth + 12
        implicitHeight: root.fs + 5
        radius: height / 2
        color: Qt.alpha(chip.tint, Theme.dark ? 0.22 : 0.13)
        border.color: Qt.alpha(chip.tint, 0.5)
        Label {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.tint
            font.pixelSize: root.fs - 3
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── header ──────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: Theme.headerBg
            clip: true
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 6
                Label {
                    text: qsTr("Tasks")
                    font.bold: true
                    color: Theme.text
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: tasks.activeCount > 0
                    elide: Text.ElideRight
                    color: Theme.dimText
                    font.pixelSize: 11
                    text: tasks.queuedCount > 0
                          ? qsTr("%1 running · %2 waiting")
                            .arg(tasks.runningCount).arg(tasks.queuedCount)
                          : qsTr("%1 running").arg(tasks.runningCount)
                }
                Item {
                    Layout.fillWidth: true
                    visible: tasks.activeCount === 0
                }
                // Something is running. A spinner is a fresh bitmap every
                // frame, and in a remote session every frame is encoded and
                // pushed down the wire, so that session -- the one the app
                // marks with Theme.animMs === 0 -- is told the same thing by
                // a dot that never redraws.
                BusyIndicator {
                    visible: tasks.runningCount > 0 && Theme.animMs > 0
                    running: visible
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                Rectangle {
                    visible: tasks.runningCount > 0 && Theme.animMs === 0
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: width / 2
                    color: Theme.accent
                }
                AppButton {
                    ghost: true
                    Layout.preferredHeight: 24
                    enabled: tasks.activeCount > 0
                    text: qsTr("Cancel all")
                    onClicked: tasks.cancelAll()
                }
                AppButton {
                    ghost: true
                    Layout.preferredHeight: 24
                    enabled: tasks.finishedCount > 0
                    text: qsTr("Clear finished")
                    onClicked: tasks.clearFinished()
                }
            }
        }

        // Frozen width while a splitter drags (see `resizing` above): the
        // rows take their width from the view, so pinning this one item
        // pins every row with it.
        Item {
            Layout.fillWidth: false
            Layout.preferredWidth: root.layoutWidth
            Layout.fillHeight: true

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                visible: tasks.model.count > 0
                model: tasks.model
                ScrollBar.vertical: ScrollBar { active: true }

                delegate: Rectangle {
                    id: row

                    readonly property string taskId: model.id
                    readonly property int taskState: model.state
                    readonly property bool queued: row.taskState === root.stateQueued
                    readonly property bool running: row.taskState === root.stateRunning
                    readonly property bool succeeded: row.taskState === root.stateSucceeded
                    readonly property bool failed: row.taskState === root.stateFailed
                    // Queued and running are the only two states still going
                    // anywhere; every other one is where the task ended, and
                    // an ended task always has something to say.
                    readonly property bool stopped: !row.queued && !row.running
                    // A running task that cannot say how far along it is has
                    // nothing to fill a bar with. Locally it sweeps; in a
                    // remote session it gets the still bar below instead.
                    readonly property bool staticBar: row.running
                                                      && model.progress < 0
                                                      && Theme.animMs === 0

                    width: ListView.view ? ListView.view.width : 0
                    height: rowCol.implicitHeight + 12
                    color: "transparent"
                    // A queued task is here but not happening yet, and should
                    // not read as loudly as the one that is.
                    opacity: row.queued ? 0.65 : 1

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.divider
                    }

                    ColumnLayout {
                        id: rowCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                elide: Text.ElideRight
                                color: Theme.text
                                font.pixelSize: root.fs
                                text: root.rowTitle(model.title, model.paperTitle)
                            }
                            Chip {
                                label: model.kindLabel
                                tint: root.kindTint(model.kind)
                            }
                            // A finished task keeps only the two things worth
                            // knowing about it afterwards: that it worked, and
                            // what it cost.
                            Label {
                                visible: row.succeeded
                                color: Theme.success
                                font.pixelSize: root.fs - 1
                                text: qsTr("✓ %1").arg(root.clock(model.elapsedMs))
                            }
                            // Failed, Canceled and Interrupted all end up
                            // here, Failed in red. Failed used to be left out
                            // and the red line below trusted to speak for it,
                            // which drew a task that failed with an empty
                            // error -- what several services did to a task
                            // the user cancelled -- as a row with nothing on
                            // it at all.
                            Label {
                                visible: row.stopped && !row.succeeded
                                color: row.failed ? Theme.danger : Theme.dimText
                                font.pixelSize: root.fs - 1
                                text: root.endLabel(row.taskState, model.stateLabel)
                            }
                            ToolButton {
                                visible: model.canCancel
                                implicitWidth: 22
                                implicitHeight: 22
                                font.pixelSize: 11
                                text: "✕"
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: qsTr("Stop this task")
                                onClicked: tasks.cancel(row.taskId)
                            }
                            AppButton {
                                visible: model.canRetry
                                Layout.preferredHeight: 24
                                text: qsTr("Retry")
                                onClicked: tasks.retry(row.taskId)
                            }
                        }

                        // A finished task's last step is not news; the
                        // line goes away with it.
                        Label {
                            Layout.fillWidth: true
                            visible: !row.succeeded
                                     && !!(model.note && model.note.length > 0)
                            elide: Text.ElideRight
                            color: Theme.dimText
                            font.pixelSize: root.fs - 2
                            text: model.note
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: row.running || row.queued
                            spacing: 8
                            ProgressBar {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 6
                                visible: !row.staticBar
                                from: 0
                                to: 1
                                // A moving bar is the most expensive thing on
                                // this pane over a remote desktop, so only the
                                // task that is actually running gets one, only
                                // when it cannot say how far along it is, and
                                // never in a remote session -- there the row
                                // hands the job to the still bar beside it,
                                // and an invisible sweep would go on marking
                                // the scene dirty every frame.
                                indeterminate: row.running && model.progress < 0
                                               && Theme.animMs > 0
                                value: model.progress < 0 ? 0 : model.progress
                            }
                            // The same fact -- this is working, and cannot say
                            // how much is left -- at no frames per second.
                            // Splitting a paper and reading its contents both
                            // declare no steps, and splitting runs on every
                            // paper opened, so the sweep is the ordinary state
                            // of this pane rather than a rare one. Quieter
                            // than the bar's own fill on purpose: filled to
                            // the end must not read as finished.
                            Rectangle {
                                visible: row.staticBar
                                Layout.fillWidth: true
                                Layout.preferredHeight: 6
                                radius: 3
                                color: Qt.alpha(Theme.dimText, Theme.dark ? 0.5 : 0.4)
                            }
                            Label {
                                color: Theme.dimText
                                font.pixelSize: root.fs - 2
                                text: row.queued
                                      ? qsTr("Waiting")
                                      : root.numbers(model.done, model.total,
                                                     model.elapsedMs, model.etaMs)
                            }
                        }

                        // Why it failed, when the failure came with a
                        // reason. When it did not, the word above is the
                        // whole answer -- which is more than the row used
                        // to say.
                        Label {
                            Layout.fillWidth: true
                            visible: row.failed
                                     && !!(model.error && model.error.length > 0)
                            wrapMode: Text.Wrap
                            color: Theme.danger
                            font.pixelSize: root.fs - 2
                            text: model.error
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(320, Math.max(0, parent.width - 40))
                spacing: 6
                visible: tasks.model.count === 0
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.dimText
                    font.pixelSize: root.fs
                    text: qsTr("Nothing is running.")
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.dimText
                    font.pixelSize: root.fs - 2
                    text: qsTr("Translating a paper, splitting it, interpreting "
                               + "it and analysing the whole project all appear "
                               + "here while they run.")
                }
            }
        }
    }
}
