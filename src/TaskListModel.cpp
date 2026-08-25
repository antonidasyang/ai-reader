#include "TaskListModel.h"

#include "TaskStore.h"

int TaskListModel::Private::indexOfId(const QString &id) const
{
    for (int i = 0; i < tasks.size(); ++i) {
        if (tasks.at(i).id == id)
            return i;
    }
    return -1;
}

void TaskListModel::Private::append(const Tasks::Record &record)
{
    // Appending to the back of the vector is inserting at the front of the
    // list, which is where a task that was just submitted belongs.
    q->beginInsertRows(QModelIndex(), 0, 0);
    tasks.append(record);
    q->endInsertRows();
    emit q->countChanged();
}

void TaskListModel::Private::removeAt(int index)
{
    if (index < 0 || index >= tasks.size())
        return;
    const int row = rowOf(index);
    q->beginRemoveRows(QModelIndex(), row, row);
    tasks.removeAt(index);
    q->endRemoveRows();
    emit q->countChanged();
}

void TaskListModel::Private::touch(int index, const QVector<int> &roles)
{
    // Bounds-checked rather than asserted: a delegate reacting to one of
    // these signals can remove a row before the next call gets here.
    if (index < 0 || index >= tasks.size())
        return;
    const QModelIndex ix = q->index(rowOf(index), 0);
    emit q->dataChanged(ix, ix, roles);
}

TaskListModel::TaskListModel(QObject *parent)
    : QAbstractListModel(parent)
    , d(new Private(this))
{
}

int TaskListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(d->tasks.size());
}

QHash<int, QByteArray> TaskListModel::roleNames() const
{
    return {{IdRole, "id"},
            {KindRole, "kind"},
            {KindLabelRole, "kindLabel"},
            {TitleRole, "title"},
            {PaperIdRole, "paperId"},
            {PaperTitleRole, "paperTitle"},
            {NoteRole, "note"},
            {StateRole, "state"},
            {StateLabelRole, "stateLabel"},
            {ProgressRole, "progress"},
            {DoneRole, "done"},
            {TotalRole, "total"},
            {ElapsedMsRole, "elapsedMs"},
            {EtaMsRole, "etaMs"},
            {ErrorRole, "error"},
            {CanCancelRole, "canCancel"},
            {CanRetryRole, "canRetry"},
            {QueuedAtRole, "queuedAt"},
            {FinishedAtRole, "finishedAt"}};
}

QVariant TaskListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= d->tasks.size())
        return {};
    const Tasks::Record &r = d->tasks.at(d->rowOf(index.row()));

    switch (role) {
    case IdRole:         return r.id;
    case KindRole:       return Tasks::kindKey(r.request.kind);
    case KindLabelRole:  return Tasks::kindLabel(r.request.kind);
    case TitleRole:      return r.request.title;
    case PaperIdRole:    return r.request.paperId;
    case PaperTitleRole: return r.request.paperTitle;
    case NoteRole:       return r.note;
    case StateRole:      return int(r.state);
    case StateLabelRole: return Tasks::stateLabel(r.state);
    case ProgressRole:   return r.progress();
    case DoneRole:       return r.done;
    case TotalRole:      return r.total;
    case ElapsedMsRole:  return QVariant::fromValue(r.elapsedMs());
    case EtaMsRole:      return QVariant::fromValue(r.etaMs());
    case ErrorRole:      return r.error;
    case CanCancelRole:  return r.active();
    case CanRetryRole:
        // Failed or canceled is not enough: something has to know how to
        // start it again, and the task has to have said what from.
        if (r.state != Tasks::State::Failed && r.state != Tasks::State::Canceled)
            return false;
        return !r.request.resume.isEmpty()
               && d->resumableKinds.contains(Tasks::kindKey(r.request.kind));
    case QueuedAtRole:   return r.queuedAt;
    case FinishedAtRole: return r.finishedAt;
    default:             break;
    }
    return {};
}
