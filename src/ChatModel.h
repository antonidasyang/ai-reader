#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct ChatMessage {
    enum Status { Done, Streaming, Failed };
    QString role;     // "user" / "assistant" / "system" (system is hidden)
    QString content;
    Status  status = Done;
    QString error;
};

class ChatModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        RoleRole = Qt::UserRole + 1,
        ContentRole,
        StatusRole,
        ErrorRole,
    };

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int messageCount() const { return m_messages.size(); }
    const QVector<ChatMessage> &messages() const { return m_messages; }

    int  appendMessage(const QString &role,
                       const QString &content,
                       ChatMessage::Status status = ChatMessage::Done);
    void appendChunkToLast(const QString &chunk);
    void setLastStatus(ChatMessage::Status s, const QString &err = {});
    void setMessages(QVector<ChatMessage> messages);
    void clear();

private:
    QVector<ChatMessage> m_messages;
};
