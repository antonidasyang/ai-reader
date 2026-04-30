#include "ChatSessionListModel.h"

ChatSessionListModel::ChatSessionListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChatSessionListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_rows.size();
}

QVariant ChatSessionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case IdRole:           return r.id;
    case NameRole:         return r.name;
    case UpdatedAtRole:    return r.updatedAt;
    case IsActiveRole:     return r.isActive;
    case MessageCountRole: return r.messageCount;
    }
    return {};
}

QHash<int, QByteArray> ChatSessionListModel::roleNames() const
{
    return {
        { IdRole,           "sessionId"    },
        { NameRole,         "sessionName"  },
        { UpdatedAtRole,    "updatedAt"    },
        { IsActiveRole,     "isActive"     },
        { MessageCountRole, "messageCount" },
    };
}

QString ChatSessionListModel::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) return {};
    return m_rows.at(row).id;
}

void ChatSessionListModel::resetRows(QVector<Row> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}
