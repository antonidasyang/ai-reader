#include "StructuredLlm.h"

#include "LlmClient.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>
#include <QTimer>

StructuredCall *StructuredCall::start(LlmClient *client, const Request &req,
                                      QObject *parent)
{
    auto *call = new StructuredCall(client, req, parent);
    // Start on the event loop so the caller can connect to the signals
    // before anything can possibly fire.
    QTimer::singleShot(0, call, [call]() {
        if (!call->m_done)
            call->attempt(0);
    });
    return call;
}

StructuredCall::StructuredCall(LlmClient *client, const Request &req,
                               QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_req(req)
{
}

void StructuredCall::abort()
{
    if (m_done)
        return;
    m_done = true;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    deleteLater();
}

void StructuredCall::attempt(int n)
{
    m_attempt = n;
    if (!m_client) {
        finishErr(tr("No model is configured."));
        return;
    }

    LlmClient::Request r;
    r.system = m_req.system;
    if (n > 0) {
        // The retry drops the tool and asks in the bluntest possible
        // terms; a model that ignored the schema once tends to answer a
        // direct instruction.
        r.system += QStringLiteral(
            "\n\nRespond with a SINGLE JSON object and nothing else. No prose, "
            "no explanation, no Markdown code fences. It must match this JSON "
            "Schema exactly:\n");
        r.system += QString::fromUtf8(
            QJsonDocument(m_req.schema).toJson(QJsonDocument::Compact));
    } else {
        ToolDef tool;
        tool.name = m_req.toolName;
        tool.description = m_req.toolDescription.isEmpty()
                               ? QStringLiteral("Return the structured result. "
                                                "Call this exactly once.")
                               : m_req.toolDescription;
        tool.inputSchema = m_req.schema;
        r.tools.append(tool);
        r.system += QStringLiteral(
            "\n\nReturn your answer by calling the `%1` tool exactly once. Do "
            "not write the answer as prose.")
                        .arg(m_req.toolName);
    }
    r.messages.append({QStringLiteral("user"), m_req.user});
    r.temperature = m_req.temperature;
    r.maxTokens = m_req.maxTokens;
    // Streamed: a large structured answer takes long enough that a
    // non-streaming request runs into the gateway's idle cut-off.
    r.stream = true;

    m_reply = m_client->send(r);
    if (!m_reply) {
        finishErr(tr("The model client refused the request."));
        return;
    }
    connect(m_reply, &LlmReply::finished, this,
            &StructuredCall::handleFinished);
    connect(m_reply, &LlmReply::progressed, this, [this](qint64 bytes) {
        emit progress(m_bytesBefore + bytes);
    });
    connect(m_reply, &LlmReply::errorOccurred, this,
            [this](const QString &message) {
                if (m_reply) {
                    m_bytesBefore += m_reply->bytesReceived();
                    m_reply->deleteLater();
                    m_reply.clear();
                }
                // Some gateways reject a request that carries `tools` at all
                // -- a model deployed without a tool parser answers 400
                // rather than ignoring them. That is not a reason to give up
                // on the whole call: ask again in prose.
                if (m_attempt == 0) {
                    qInfo().noquote()
                        << "StructuredCall: tool call refused (" << message
                        << "); retrying without tools";
                    attempt(1);
                    return;
                }
                finishErr(message);
            });
}

void StructuredCall::handleFinished()
{
    if (m_done)
        return;
    LlmReply *reply = m_reply;
    m_reply.clear();
    if (!reply)
        return;
    const QList<ToolCall> calls = reply->toolCalls();
    const QString text = reply->text();
    m_bytesBefore += reply->bytesReceived();
    reply->deleteLater();

    for (const ToolCall &c : calls) {
        if (!c.input.isEmpty()
            && (c.name == m_req.toolName || calls.size() == 1)) {
            finishOk(c.input);
            return;
        }
    }

    const QJsonObject loose = parseLoose(text);
    if (!loose.isEmpty()) {
        finishOk(loose);
        return;
    }

    if (m_attempt == 0) {
        attempt(1);
        return;
    }
    finishErr(text.trimmed().isEmpty()
                  ? tr("The model returned nothing.")
                  : tr("The model did not return usable JSON."));
}

void StructuredCall::finishOk(const QJsonObject &o)
{
    if (m_done)
        return;
    m_done = true;
    emit succeeded(o);
    deleteLater();
}

void StructuredCall::finishErr(const QString &e)
{
    if (m_done)
        return;
    m_done = true;
    // The shape of what we asked for, so a failure in the field can be
    // diagnosed from launch.log without reproducing it.
    qWarning().noquote()
        << "StructuredCall failed:" << e
        << "| model:" << (m_client ? m_client->model() : QStringLiteral("-"))
        << "| maxTokens:" << m_req.maxTokens
        << "| promptChars:" << m_req.user.size()
        << "| attempt:" << m_attempt;
    emit failed(e);
    deleteLater();
}

QJsonObject StructuredCall::parseLoose(const QString &text)
{
    QString t = text;
    // Reasoning models put their scratch work in <think> blocks, which is
    // never JSON and often contains braces.
    for (;;) {
        const int a = t.indexOf(QStringLiteral("<think>"), Qt::CaseInsensitive);
        if (a < 0)
            break;
        const int b = t.indexOf(QStringLiteral("</think>"), a,
                                Qt::CaseInsensitive);
        if (b < 0) {
            t = t.left(a);
            break;
        }
        t.remove(a, b - a + 8);
    }
    t = t.trimmed();
    if (t.isEmpty())
        return {};

    // ```json … ```
    const int fence = t.indexOf(QStringLiteral("```"));
    if (fence >= 0) {
        const int nl = t.indexOf(QChar('\n'), fence);
        const int end = nl > 0 ? t.indexOf(QStringLiteral("```"), nl) : -1;
        if (nl > 0 && end > nl)
            t = t.mid(nl + 1, end - nl - 1).trimmed();
    }

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(t.toUtf8(), &err);
    if (doc.isObject())
        return doc.object();

    // Otherwise take the first balanced {...} run, ignoring braces that
    // live inside strings.
    const int first = t.indexOf(QChar('{'));
    if (first < 0)
        return {};
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    for (int i = first; i < t.size(); ++i) {
        const QChar c = t.at(i);
        if (inStr) {
            if (esc)
                esc = false;
            else if (c == QChar('\\'))
                esc = true;
            else if (c == QChar('"'))
                inStr = false;
            continue;
        }
        if (c == QChar('"')) {
            inStr = true;
        } else if (c == QChar('{')) {
            ++depth;
        } else if (c == QChar('}')) {
            if (--depth == 0) {
                doc = QJsonDocument::fromJson(
                    t.mid(first, i - first + 1).toUtf8());
                return doc.object();
            }
        }
    }
    return {};
}
