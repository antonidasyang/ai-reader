#pragma once

#include "LlmClientCache.h"
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>

class AnalysisStore;
class LlmClient;
class ProjectController;
class ProjectProfileController;
class Settings;
class StructuredCall;

// Comparing papers the reader picked out (§10).
//
// The basket comes first and lives on its own: while reading, "this one goes
// next to that one" is a thought worth capturing before there is any intention
// to run a comparison. It is kept per project and survives restarts.
//
// The comparison itself reads the digests, never the papers — that is what
// makes comparing twelve papers a single call. And it is required to say when
// two papers cannot honestly be compared (different task, different data,
// different metric), rather than lining their numbers up in a table and
// letting the layout imply a ranking (§10.3).
class CompareService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList basket READ basket NOTIFY basketChanged)
    Q_PROPERTY(int count READ count NOTIFY basketChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap result READ result NOTIFY resultChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)
    Q_PROPERTY(QString resultAuthorEmail READ resultAuthorEmail NOTIFY resultChanged)
    Q_PROPERTY(QString resultUpdatedAt READ resultUpdatedAt NOTIFY resultChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY stateChanged)

public:
    CompareService(Settings *settings, AnalysisStore *store,
                   ProjectController *projects,
                   ProjectProfileController *profile,
                   QObject *parent = nullptr);

    QVariantList basket() const;
    int count() const { return m_basket.size(); }
    bool busy() const { return m_call != nullptr; }
    QString lastError() const { return m_lastError; }
    QVariantMap result() const { return m_result.toVariantMap(); }
    bool hasResult() const { return !m_result.isEmpty(); }
    QString resultAuthorEmail() const { return m_resultAuthor; }
    QString resultUpdatedAt() const { return m_resultUpdatedAt; }
    bool canRun() const;

    // Put a paper in the basket, optionally with the statement that made the
    // reader reach for it.
    Q_INVOKABLE void add(const QString &paperId, const QString &title,
                         const QString &note = QString());
    Q_INVOKABLE void removePaper(const QString &paperId);
    Q_INVOKABLE bool contains(const QString &paperId) const;
    Q_INVOKABLE void clearBasket();

    // Compare what is in the basket. Reads the stored digests.
    Q_INVOKABLE void compare();
    Q_INVOKABLE void cancel();
    // Load whatever was last stored for this exact set of papers.
    Q_INVOKABLE void loadStored();

signals:
    void basketChanged();
    void stateChanged();
    void resultChanged();

private:
    struct Entry {
        QString paperId;
        QString title;
        QStringList notes;
    };

    void load();
    void save();
    QString scopeKey() const;
    void setError(const QString &e);

    QPointer<Settings> m_settings;
    AnalysisStore *m_store;
    ProjectController *m_projects;
    ProjectProfileController *m_profile;
    LlmClientCache m_clients;
    QPointer<LlmClient> m_client;
    QPointer<StructuredCall> m_call;

    QList<Entry> m_basket;
    QJsonObject m_result;
    QString m_resultAuthor;
    QString m_resultUpdatedAt;
    QString m_lastError;
    QSettings m_qs;
};
