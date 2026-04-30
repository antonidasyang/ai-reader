#pragma once

#include "ChatSession.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVector>

// QML-facing model of the per-paper session list. Holds metadata only
// (id / name / counts / active flag) — the live message arrays stay in
// ChatService. ChatService rebuilds this whenever sessions are added,
// removed, renamed, or activated.
class ChatSessionListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        UpdatedAtRole,
        IsActiveRole,
        MessageCountRole,
    };

    struct Row {
        QString id;
        QString name;
        QDateTime updatedAt;
        bool isActive = false;
        int messageCount = 0;
    };

    explicit ChatSessionListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE int sessionCount() const { return m_rows.size(); }

    void resetRows(QVector<Row> rows);

private:
    QVector<Row> m_rows;
};
