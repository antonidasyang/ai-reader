#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class LlmClient;
class LlmReply;
class PaperController;
class Settings;

class VisionService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Status  status     READ status     NOTIFY statusChanged)
    Q_PROPERTY(QString text       READ text       NOTIFY textChanged)
    Q_PROPERTY(int     page       READ page       NOTIFY pageChanged)
    Q_PROPERTY(QString lastError  READ lastError  NOTIFY statusChanged)

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

public slots:
    void readPage(int page);
    void cancel();
    void clear();

signals:
    void statusChanged();
    void textChanged();
    void pageChanged();

private:
    void setStatus(Status s, const QString &err = {});
    QString systemPrompt() const;
    QString userPrompt(int pageIdx) const;

    Settings *m_settings = nullptr;
    PaperController *m_paper = nullptr;
    LlmClient *m_client = nullptr;
    QPointer<LlmReply> m_reply;

    Status  m_status = Idle;
    QString m_text;
    QString m_lastError;
    int     m_page = -1;
};
