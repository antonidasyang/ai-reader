#pragma once

#include "LlmClientCache.h"
#include "TocCache.h"
#include "TocModel.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class BlockListModel;
class LlmClient;
class LlmReply;
class PaperController;
class Settings;
class TaskManager;

class TocService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(TocModel *sections   READ sections   CONSTANT)
    Q_PROPERTY(Status   status      READ status     NOTIFY statusChanged)
    Q_PROPERTY(QString  lastError   READ lastError  NOTIFY statusChanged)
    Q_PROPERTY(int      sectionCount READ sectionCount NOTIFY sectionsChanged)
    Q_PROPERTY(Source   source      READ source     NOTIFY sectionsChanged)
    Q_PROPERTY(QString  defaultSystemPrompt READ defaultSystemPrompt CONSTANT)

public:
    enum Status { Idle, Generating, Done, Failed };
    Q_ENUM(Status)

    // Where the TOC on display came from. The UI says so out loud:
    // rebuilding with the LLM costs a request and discards a
    // structural outline that is usually better, so the user should
    // never have to guess what they are about to replace.
    enum Source { NoSource, Structural, Llm };
    Q_ENUM(Source)

    TocService(Settings *settings,
               PaperController *paper,
               QObject *parent = nullptr);
    ~TocService() override;

    TocModel *sections()    { return &m_model; }
    Status   status()       const { return m_status; }
    QString  lastError()    const { return m_lastError; }
    int      sectionCount() const { return m_model.sectionCount(); }
    Source   source()       const { return m_source; }
    QString  defaultSystemPrompt() const;

    // The queue every long run goes through. Optional: with no manager the
    // service generates directly, exactly as it always did, which is what
    // the harnesses rely on.
    void setTasks(TaskManager *tasks);

public slots:
    void generate();
    void cancel();
    void clear();

    // Adopt a TOC derived structurally from GROBID's TEI outline
    // (wired to StructureService::outlineExtracted in main.cpp).
    // Only fills the model when the user doesn't already have a TOC:
    // an LLM result — live, in flight, or rehydrated from cache —
    // always wins. Adopted outlines are cached under a reserved key
    // so they survive reopening the paper.
    void adoptStructuredOutline(const QVector<Section> &sections);

signals:
    void statusChanged();
    void sectionsChanged();
    // Emitted when user clicks a section (forwarded by QML).
    void navigationRequested(int blockId, int page);

private:
    // The generation itself, without the queue around it: the direct path
    // and the task's start callback both land here.
    void runGenerate();
    // Abort the request in flight without touching the task around it --
    // what a task's own body uses to clear the way before it starts.
    void cancelReply();
    // Whether a generation can even be attempted; sets the failure status
    // itself so a refusal reads the same on both paths.
    bool canGenerate();
    // The same question asked without saying anything out loud. A resumer
    // offering a run back on startup must not paint a refusal into the
    // sidebar for work the user never watched start.
    bool couldGenerate() const;
    void finishTask(bool ok, const QString &error = {});
    // The generation was stopped rather than lost — the paper was closed,
    // something else overtook it. The row ends Canceled, not Failed.
    void cancelTask();
    void onPaperChanged();
    void rehydrateFromCache();
    QString systemPrompt() const;
    QString userPrompt() const;
    void parseResponse(const QString &text);
    void setStatus(Status s, const QString &err = {});

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    QPointer<BlockListModel> m_blocks;
    QPointer<TaskManager> m_tasks;
    // The generation on record with the manager, and the paper it is for.
    // Empty whenever none is, which is also how the direct path is
    // recognised. The paper is kept because a deferred body or a stop
    // callback arriving late must never settle the generation that has
    // meanwhile taken its place.
    QString m_taskId;
    QString m_taskPaperId;
    LlmClientCache m_clients;
    QPointer<LlmClient> m_client;
    QPointer<LlmReply> m_reply;
    TocModel m_model;
    TocCache m_cache;

    // Map block_id → page recorded when we sent the heading list, so we can
    // resolve the LLM's `start_block` references back to a navigable page.
    QHash<int, int> m_blockIdToPage;

    QString m_buffer;
    Status m_status = Idle;
    Source m_source = NoSource;

    // Tracks which paper we last reset cache state for. blocksChanged
    // fires on every paragraph mutation as well as on paper-load, but
    // we only want to wipe + rehydrate the TOC when the user actually
    // switches to a different paper — otherwise editing one paragraph
    // would clear a generated TOC the user hasn't asked us to redo.
    QString m_lastPaperId;
    QString m_lastError;
};
