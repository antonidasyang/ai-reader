#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

class LlmClient;
class LlmReply;

// One "answer with JSON that fits this schema" round trip.
//
// Everything in the interpretation layer needs structured output rather
// than prose: §4.3 wants a provenance code on every claim, §5 wants one
// module regenerated without disturbing the rest, §10 wants a table and
// §16 wants a clean export. None of that can be recovered from free-form
// Markdown after the fact. LlmClient has no JSON mode, but both providers
// carry tools -- so the schema is offered as a single tool the model is
// asked to call, and the answer is that call's arguments.
//
// Three ways to land it, in order: the tool call; JSON dug out of the
// prose (fenced or brace-balanced, with any <think> block dropped); then
// one stricter retry with the tools removed. Deletes itself once it has
// emitted.
class StructuredCall : public QObject
{
    Q_OBJECT
public:
    struct Request {
        QString system;
        QString user;
        QJsonObject schema;
        QString toolName = QStringLiteral("emit_result");
        QString toolDescription;
        double temperature = 0.1;
        int maxTokens = 8192;
    };

    // Starts immediately. `client` must outlive the call (it is the
    // caller's; only the reply belongs to us).
    static StructuredCall *start(LlmClient *client, const Request &req,
                                 QObject *parent = nullptr);

    void abort();

signals:
    void succeeded(const QJsonObject &result);
    void failed(const QString &error);
    // Bytes of answer received so far, across attempts. The answer is a
    // tool call, so until it is complete there is nothing else to show for
    // a model that is taking its time.
    void progress(qint64 bytes);

private:
    StructuredCall(LlmClient *client, const Request &req, QObject *parent);

    void attempt(int n);
    void handleFinished();
    void finishOk(const QJsonObject &o);
    void finishErr(const QString &e);

    // Best-effort JSON out of a model answer that ignored the tool.
    static QJsonObject parseLoose(const QString &text);

    QPointer<LlmClient> m_client;
    QPointer<LlmReply> m_reply;
    Request m_req;
    int m_attempt = 0;
    qint64 m_bytesBefore = 0;   // what the earlier attempt(s) brought in
    bool m_done = false;
};
