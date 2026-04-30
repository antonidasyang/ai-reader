#pragma once

#include "ChatModel.h"
#include "ChatSession.h"
#include "LlmClient.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

// On-disk JSON cache of a paper's chat sessions. One file per paper at
// <AppDataLocation>/cache/chat/<paperId>.json. Each file holds an array of
// sessions (id + metadata + messages + api history) plus the id of the
// session that was active when the file was written. The reader is
// backward-compatible with the single-session format produced by older
// builds — those files load as one session called "Chat".
class ChatHistoryCache : public QObject
{
    Q_OBJECT
public:
    struct Snapshot {
        QVector<ChatSession> sessions;
        QString activeId;
    };

    explicit ChatHistoryCache(QObject *parent = nullptr);

    void setPaperId(const QString &paperId);
    QString paperId() const { return m_paperId; }

    Snapshot load() const;
    void save(const QVector<ChatSession> &sessions, const QString &activeId);
    void clear();

private:
    void scheduleSave();
    void saveNow();
    QString filePath() const;

    QString m_paperId;
    QString m_cacheDir;

    QVector<ChatSession> m_pendingSessions;
    QString m_pendingActiveId;
    bool m_havePending = false;
    QTimer m_saveTimer;
};
