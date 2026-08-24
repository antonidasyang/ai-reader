#pragma once

#include "Block.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QVector>

class AnalysisListModel;
class AnalysisStore;
class LlmClient;
class PaperSource;
class ProjectProfileController;
class QuickAnalysisJob;
class Settings;

// Interpreting a whole library (§7).
//
// Two things are being paced at once. Getting a paper's paragraphs is
// serialized -- one download, one segmentation at a time -- because PDFium
// has a global lock and the reader's own window has to keep rendering. The
// model calls are not: as soon as one paper's paragraphs are in hand its
// interpretation goes out and the next paper starts being fetched, so the
// slow half overlaps with the expensive half rather than following it.
//
// Nothing here opens a paper in the reader. Papers that already carry a
// current interpretation are skipped, including one a collaborator generated
// -- a five-person project pays for a paper once.
class BatchAnalysisService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int failed READ failed NOTIFY progressChanged)
    Q_PROPERTY(int skipped READ skipped NOTIFY progressChanged)
    Q_PROPERTY(int running READ running NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY progressChanged)

public:
    BatchAnalysisService(Settings *settings, AnalysisStore *store,
                         ProjectProfileController *profile,
                         PaperSource *source, AnalysisListModel *model,
                         QObject *parent = nullptr);

    bool busy() const { return m_running > 0 || !m_queue.isEmpty(); }
    int total() const { return m_total; }
    int done() const { return m_done; }
    int failed() const { return m_failed; }
    int skipped() const { return m_skipped; }
    int running() const { return m_running; }
    QString status() const { return m_status; }
    bool canRun() const;

    // Everything in the project without a current interpretation.
    Q_INVOKABLE void startPending();
    // A specific set, e.g. what the filters are showing.
    Q_INVOKABLE void startItems(const QStringList &itemIds, bool force = false);
    Q_INVOKABLE void retryFailed();
    Q_INVOKABLE void cancel();
    // Why one paper failed, for the row that shows it.
    Q_INVOKABLE QString errorFor(const QString &itemId) const;
    Q_INVOKABLE QStringList failedItems() const;

signals:
    void progressChanged();
    void statusChanged();
    void finished(int done, int failed, int skipped);

private:
    void pump();
    void onSourceReady(const QString &itemId, const QString &paperId,
                       const QString &title, const QVector<Block> &blocks);
    void onSourceFailed(const QString &itemId, const QString &reason);
    void recordFailure(const QString &itemId, const QString &reason);
    void finishIfIdle();
    void setStatus(const QString &s);

    QPointer<Settings> m_settings;
    AnalysisStore *m_store;
    ProjectProfileController *m_profile;
    PaperSource *m_source;
    AnalysisListModel *m_model;
    QPointer<LlmClient> m_client;

    QQueue<QString> m_queue;
    QHash<QString, QString> m_errors;     // itemId -> why
    QStringList m_failedItems;
    bool m_sourceBusy = false;
    bool m_force = false;
    bool m_cancelled = false;
    int m_running = 0;
    int m_total = 0;
    int m_done = 0;
    int m_failed = 0;
    int m_skipped = 0;
    QString m_status;
};
