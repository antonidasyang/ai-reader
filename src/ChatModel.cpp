#include "ChatModel.h"

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_messages.size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size())
        return {};
    const ChatMessage &m = m_messages.at(index.row());
    switch (role) {
    case RoleRole:    return m.role;
    case ContentRole: return m.content;
    case StatusRole:  return int(m.status);
    case ErrorRole:   return m.error;
    }
    return {};
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    return {
        { RoleRole,    "role"    },
        { ContentRole, "content" },
        { StatusRole,  "status"  },
        { ErrorRole,   "error"   },
    };
}

int ChatModel::appendMessage(const QString &role,
                             const QString &content,
                             ChatMessage::Status status)
{
    const int row = m_messages.size();
    beginInsertRows({}, row, row);
    m_messages.append({ role, content, status, {} });
    endInsertRows();
    return row;
}

void ChatModel::appendChunkToLast(const QString &chunk)
{
    if (m_messages.isEmpty() || chunk.isEmpty()) return;
    const int row = m_messages.size() - 1;
    m_messages[row].content += chunk;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { ContentRole });
}

void ChatModel::setLastStatus(ChatMessage::Status s, const QString &err)
{
    if (m_messages.isEmpty()) return;
    const int row = m_messages.size() - 1;
    m_messages[row].status = s;
    m_messages[row].error = err;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { StatusRole, ErrorRole });
}

void ChatModel::clear()
{
    if (m_messages.isEmpty()) return;
    beginResetModel();
    m_messages.clear();
    endResetModel();
}
