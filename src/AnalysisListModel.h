#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariantList>
#include <QTimer>
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
// This is the list §7 works on, and since 1.3.26 it is also the model behind
// the library pane itself -- generate in bulk, watch the progress, see why one
// failed, filter by relevance or by advice, then send a whole filtered set to
// the deep read, to the comparison, or out of the project's way. Runtime state
// (queued / running / just failed) is pushed in by BatchAnalysisService rather
// than stored, so a restart shows what is really on disk.
//
// It carries the bibliographic fields as well (creators, year, publication,
// local path). There used to be two lists of the same papers -- LibraryModel's
// and this one -- each with its own filters and its own row menu; the pane
// draws from this one so that "what the filters are showing" and "what the
// batch buttons act on" are the same set by construction.
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
    // Papers carrying a complete close reading (all nine parts).
    Q_PROPERTY(int deepDoneCount READ deepDoneCount NOTIFY countsChanged)
    // Starred papers that do not have one yet -- what "close-read the starred"
    // would actually pay for.
    Q_PROPERTY(int deepPendingCount READ deepPendingCount NOTIFY countsChanged)
    // Bumped whenever the join is rebuilt. Bind it alongside a
    // stateForPaper() call to make that call re-run: it is an invokable,
    // and an invokable notifies nothing on its own.
    Q_PROPERTY(int revision READ revision NOTIFY countsChanged)

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
                            // exposed as "analysisState": plain "state"
                            // collides with Item.state in a delegate
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
        // The close reading: none | partial | done.
        DeepStateRole,
        // Bibliographic fields, so this model can be the library list too.
        CreatorsRole,
        YearRole,
        PublicationRole,
        LocalPathRole,
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
    int deepDoneCount() const;
    int deepPendingCount() const;
    int revision() const { return m_revision; }

    // §17, where the reader actually looks: the state of one paper's
    // interpretation, for the library list.
    Q_INVOKABLE QString stateForPaper(const QString &paperId) const;
    Q_INVOKABLE QString relevanceForPaper(const QString &paperId) const;

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
    // Starred ids, whether or not the filters are showing them: a star is a
    // decision the reader already made, and hiding it behind the current
    // filter would make "close-read the starred" mean something different
    // every time it is clicked.
    Q_INVOKABLE QStringList toReadItemIds() const;
    // Of a set of items, the ones a close reading would actually be run for:
    // they have a file, they are not set aside, and they are missing at least
    // one of the nine parts.
    Q_INVOKABLE QStringList deepPendingAmong(const QStringList &itemIds) const;
    // paperId + title for each visible row, which is what the comparison
    // basket takes.
    Q_INVOKABLE QVariantList visiblePapers() const;

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
        QString creators;
        QString year;
        QString publication;
        QString localPath;
        bool toRead = false;
        bool excluded = false;
        bool hasFile = false;
    };

    void rebuildVisible();
    bool passes(const Row &row) const;
    QString stateOf(const Row &row) const;
    QString deepStateOf(const Row &row) const;

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
    // The same snapshot for the close readings, so a row can say whether one
    // exists without decoding the payload on every repaint.
    QHash<QString, int> m_deepModuleCounts;   // paperId -> parts present
    QHash<QString, QString> m_runtime;    // itemId -> queued|running
    QHash<QString, QString> m_runtimeError;

    // A batch writes one analysis per paper, and every write is a store
    // change; rebuilding the whole join on each one made a hundred-paper run
    // quadratic in decompressions.
    QTimer m_reloadTimer;

    QString m_filterRelevance;
    QString m_filterAdvice;
    QString m_filterState;
    bool m_hideExcluded = true;
    int m_revision = 0;
};
