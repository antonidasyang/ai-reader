#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>

#include "LibraryDb.h"

#include "AnalysisStore.h"

class LibraryModel;
class ProjectController;

// Every paper in the project alongside what has been made of it: whether it
// has been interpreted, by whom, how relevant it came out, what the advice
// was, and whether the reader has since marked it for a close read or ruled
// it out.
//
// This is the list §7 works on -- generate in bulk, watch the progress, see
// why one failed, filter by relevance or by advice, then send a whole
// filtered set to the deep-read list or out of the project's way. Runtime
// state (queued / running / just failed) is pushed in by BatchAnalysisService
// rather than stored, so a restart shows what is really on disk.
class AnalysisListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countsChanged)
    Q_PROPERTY(int totalPapers READ totalPapers NOTIFY countsChanged)
    Q_PROPERTY(int interpretedCount READ interpretedCount NOTIFY countsChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY countsChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY countsChanged)
    Q_PROPERTY(int excludedCount READ excludedCount NOTIFY countsChanged)
    Q_PROPERTY(int toReadCount READ toReadCount NOTIFY countsChanged)

    // "" = no filter. relevance: high|medium|low|unclear.
    Q_PROPERTY(QString filterRelevance READ filterRelevance WRITE setFilterRelevance NOTIFY filtersChanged)
    Q_PROPERTY(QString filterAdvice READ filterAdvice WRITE setFilterAdvice NOTIFY filtersChanged)
    // "" | none | done | failed | stale | toRead
    Q_PROPERTY(QString filterState READ filterState WRITE setFilterState NOTIFY filtersChanged)
    Q_PROPERTY(bool hideExcluded READ hideExcluded WRITE setHideExcluded NOTIFY filtersChanged)

public:
    enum Roles {
        ItemIdRole = Qt::UserRole + 1,
        PaperIdRole,
        TitleRole,
        StateRole,          // none|queued|running|done|failed|insufficient
        StaleRole,
        ErrorRole,
        OneLinerRole,
        RelevanceRole,
        AdviceRole,
        AuthorEmailRole,
        MineRole,
        ToReadRole,
        ExcludedRole,
        HasFileRole,
    };

    AnalysisListModel(LibraryDb *db, LibraryModel *library,
                      ProjectController *projects, AnalysisStore *store,
                      QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int totalPapers() const { return m_all.size(); }
    int interpretedCount() const;
    int pendingCount() const;
    int failedCount() const;
    int excludedCount() const;
    int toReadCount() const;

    QString filterRelevance() const { return m_filterRelevance; }
    QString filterAdvice() const { return m_filterAdvice; }
    QString filterState() const { return m_filterState; }
    bool hideExcluded() const { return m_hideExcluded; }
    void setFilterRelevance(const QString &v);
    void setFilterAdvice(const QString &v);
    void setFilterState(const QString &v);
    void setHideExcluded(bool v);

    Q_INVOKABLE void reload();
    // Item ids currently passing the filters -- what a "do this to all of
    // them" button acts on.
    Q_INVOKABLE QStringList visibleItemIds() const;
    // Ids with no current interpretation, in list order.
    Q_INVOKABLE QStringList pendingItemIds() const;

    Q_INVOKABLE void setToRead(const QString &itemId, bool on);
    Q_INVOKABLE void setExcluded(const QString &itemId, bool on);
    Q_INVOKABLE void applyToRead(const QStringList &itemIds, bool on);
    Q_INVOKABLE void applyExcluded(const QStringList &itemIds, bool on);

    // Called by BatchAnalysisService as papers move through the queue.
    void setRuntime(const QString &itemId, const QString &state,
                    const QString &error = QString());
    void clearRuntime();

signals:
    void countsChanged();
    void filtersChanged();

private:
    struct Row {
        QString itemId;
        QString paperId;
        QString title;
        bool toRead = false;
        bool excluded = false;
        bool hasFile = false;
    };

    void rebuildVisible();
    bool passes(const Row &row) const;
    QString stateOf(const Row &row) const;

    LibraryDb *m_db;
    LibraryModel *m_library;
    ProjectController *m_projects;
    AnalysisStore *m_store;

    QList<Row> m_all;
    QList<int> m_visible;                 // indices into m_all
    // One snapshot of every digest in the project per reload, keyed by
    // paperId. Looking each row up through the store instead would decode
    // the whole project's payloads once per row per repaint.
    QHash<QString, AnalysisRecord> m_digests;
    QHash<QString, QString> m_runtime;    // itemId -> queued|running
    QHash<QString, QString> m_runtimeError;

    QString m_filterRelevance;
    QString m_filterAdvice;
    QString m_filterState;
    bool m_hideExcluded = true;
};
