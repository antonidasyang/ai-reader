#pragma once

#include "LlmClientCache.h"
#include <QObject>
#include <QPointer>
#include <QString>

class LlmClient;
class LlmReply;
class PaperController;
class Settings;
class TaskManager;

class VisionService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Status  status     READ status     NOTIFY statusChanged)
    Q_PROPERTY(QString text       READ text       NOTIFY textChanged)
    Q_PROPERTY(int     page       READ page       NOTIFY pageChanged)
    Q_PROPERTY(QString lastError  READ lastError  NOTIFY statusChanged)
    Q_PROPERTY(QString defaultSystemPrompt READ defaultSystemPrompt CONSTANT)

public:
    enum Status { Idle, Rendering, Generating, Done, Failed };
    Q_ENUM(Status)

    VisionService(Settings *settings,
                  PaperController *paper,
                  QObject *parent = nullptr);
    ~VisionService() override;

    Status  status()    const { return m_status; }
    QString text()      const { return m_text; }
    int     page()      const { return m_page; }
    QString lastError() const { return m_lastError; }
    QString defaultSystemPrompt() const;

    // The queue every long run goes through. Optional: with no manager the
    // service reads a page directly, exactly as it always did, which is what
    // the harnesses rely on.
    void setTasks(TaskManager *tasks);

public slots:
    void readPage(int page);
    void cancel();
    void clear();

signals:
    void statusChanged();
    void textChanged();
    void pageChanged();

private:
    // The read itself, without the queue around it: the direct path and the
    // task's start callback both land here.
    void runReadPage(int pageIdx);
    // Abort the request in flight without touching the task around it --
    // what a task's own body uses to clear the way before it starts.
    void cancelReply();
    // Whether a read of `pageIdx` can even be attempted; sets the failure
    // status itself so a refusal reads the same on both paths.
    bool canRead(int pageIdx);
    // The same question asked without saying anything out loud. A resumer
    // offering a read back on startup must not paint a refusal onto the
    // dialog for work the user never watched start.
    bool couldRead(int pageIdx) const;
    void finishTask(bool ok, const QString &error = {});
    // The read was stopped rather than lost — the paper was closed, another
    // page overtook it. The row ends Canceled, not Failed.
    void cancelTask();
    void setStatus(Status s, const QString &err = {});
    QString systemPrompt() const;
    QString userPrompt(int pageIdx) const;

    Settings *m_settings = nullptr;
    PaperController *m_paper = nullptr;
    QPointer<TaskManager> m_tasks;
    // The read on record with the manager, and the page and paper it is
    // for: a new page supersedes the one before it, and the superseded
    // task's stop callback must not clear the id of the read that replaced
    // it. The paper is kept as well, since page 3 of the paper now open is
    // not the page 3 that was asked for.
    QString m_taskId;
    QString m_taskPaperId;
    int     m_taskPage = -1;
    LlmClientCache m_clients;
    LlmClient *m_client = nullptr;
    QPointer<LlmReply> m_reply;

    Status  m_status = Idle;
    QString m_text;
    QString m_lastError;
    int     m_page = -1;
};
