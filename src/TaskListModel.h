#pragma once

#include "TaskTypes.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVector>

// What the task viewer reads.
//
// Newest first: a task that just started belongs at the top, and the ones
// that finished sink under it. Everything the row needs is a role, including
// the two numbers that have to be recomputed on a timer rather than on a
// signal -- how long it has been running, and how long it has left.
class TaskListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        KindRole,          // stable key: "translate", "deep_interpret", ...
        KindLabelRole,     // translated
        TitleRole,         // the work
        PaperIdRole,
        PaperTitleRole,    // what it is being done to ("" for project work)
        NoteRole,          // the current step, in words
        StateRole,         // Tasks::State
        StateLabelRole,    // translated
        ProgressRole,      // 0..1, or -1 when the task cannot say
        DoneRole,          // steps
        TotalRole,
        ElapsedMsRole,     // wall clock since it started running
        EtaMsRole,         // -1 when unknown
        ErrorRole,
        CanCancelRole,     // queued or running
        CanRetryRole,      // failed or canceled, and it knows how to restart
        QueuedAtRole,      // QDateTime
        FinishedAtRole,
    };

    explicit TaskListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    friend class TaskManager;
    class Private;
    Private *d;
};
