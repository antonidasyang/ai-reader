#pragma once

// An OpenAI-compatible endpoint that answers an interpretation request with a
// schema-shaped tool call, streamed the way a real gateway dribbles arguments
// out.
//
// It builds its answer FROM THE PROMPT it was given: it reads the
// "[b<id> p<page>] text" markers out of the user message and cites real
// paragraph ids with real quoted words. That keeps the harness independent of
// however the clusterer happened to split the fixture PDF, and lets the answer
// contain, on purpose:
//
//   * one citation that is correct                       → verified
//   * one whose block id does not exist, quoting words   → repaired by content
//     that really are in the paper                          and then verified
//   * one quoting words that appear nowhere              → unverified, and the
//                                                           claim demoted
//
// so the numbers EvidenceIndex reports are the assertion.

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>

// No Q_OBJECT: the harness links the app's aggregated moc output, which knows
// nothing about this class.
class FakeAnalysisLlm : public QObject
{
public:
    explicit FakeAnalysisLlm(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                hook(s);
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    // The sockets are the server's children and outlive m_buf, which is
    // declared after it: their disconnected() slot then removed a key from
    // a hash that was already gone, and every run ended in a segfault after
    // its last check had passed. Taken down first, with their slots
    // detached, while everything they reach is still alive.
    ~FakeAnalysisLlm() override
    {
        for (QTcpSocket *s : m_server.findChildren<QTcpSocket *>()) {
            s->disconnect(this);
            s->abort();
            delete s;
        }
        m_server.close();
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    int requests() const { return m_requests; }
    // Answer 400 to anything carrying `tools`, the way a gateway whose model
    // was deployed without a tool parser does. The app is supposed to fall
    // back to asking for JSON in prose rather than giving up.
    void setRefuseTools(bool on) { m_refuseTools = on; }
    // Reject every request with a JSON error body, the way a provider does
    // when the model name is wrong or the prompt is too long. What the app
    // shows the reader has to be this text, not Qt's "status code 400".
    void setRefuseAll(const QString &message)
    {
        m_refuseAll = message;
    }
    // The last request body, for asserting what the prompt actually carried.
    QJsonObject lastRequest() const
    {
        return QJsonDocument::fromJson(m_lastBody).object();
    }
    QString lastPrompt() const
    {
        QString all;
        for (const QJsonValue &m : lastRequest().value("messages").toArray())
            all += m.toObject().value("content").toString();
        return all;
    }
    // The ids this answer cited, in the order described above.
    int citedGood() const { return m_citedGood; }
    int citedMissing() const { return m_citedMissing; }
    int citedFabricated() const { return m_citedFabricated; }

private:
    struct Marker {
        int id = 0;
        int page = 0;
        QString text;
    };

    void hook(QTcpSocket *s)
    {
        connect(s, &QTcpSocket::readyRead, this, [this, s] {
            m_buf[s] += s->readAll();
            const int hdrEnd = m_buf[s].indexOf("\r\n\r\n");
            if (hdrEnd < 0)
                return;
            int len = 0;
            for (const QByteArray &line : m_buf[s].left(hdrEnd).split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    len = line.mid(15).trimmed().toInt();
            }
            const QByteArray body = m_buf[s].mid(hdrEnd + 4);
            if (body.size() < len)
                return;                      // body still arriving
            m_buf.remove(s);
            ++m_requests;
            m_lastBody = body;
            const bool hasTools =
                !lastRequest().value("tools").toArray().isEmpty();
            if (!m_refuseAll.isEmpty()) {
                const QByteArray err =
                    QJsonDocument(QJsonObject{
                                      {"error",
                                       QJsonObject{{"message", m_refuseAll},
                                                   {"type", "invalid_request_error"}}}})
                        .toJson(QJsonDocument::Compact);
                s->write("HTTP/1.1 400 Bad Request\r\nContent-Type: "
                         "application/json\r\nConnection: close\r\n"
                         "Content-Length: "
                         + QByteArray::number(err.size()) + "\r\n\r\n" + err);
                s->flush();
                s->disconnectFromHost();
                return;
            }
            if (m_refuseTools && hasTools) {
                const QByteArray err =
                    "{\"error\":{\"message\":\"This model does not support "
                    "tools\"}}";
                s->write("HTTP/1.1 400 Bad Request\r\nContent-Type: "
                         "application/json\r\nConnection: close\r\n"
                         "Content-Length: "
                         + QByteArray::number(err.size()) + "\r\n\r\n" + err);
                s->flush();
                s->disconnectFromHost();
                return;
            }
            // Without tools the answer has to come back as prose, which is
            // the path a schema-blind model takes.
            if (!hasTools) {
                respondProse(s, buildAnswerForSchemaless());
                return;
            }
            respond(s, buildAnswer(toolName()));
        });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            m_buf.remove(s);
            s->deleteLater();
        });
    }

    QVector<Marker> markers() const
    {
        QVector<Marker> out;
        static const QRegularExpression re(
            QStringLiteral("\\[b(\\d+) p(\\d+)\\] ([^\\n]+)"));
        auto it = re.globalMatch(lastPrompt());
        while (it.hasNext()) {
            const auto m = it.next();
            Marker k;
            k.id = m.captured(1).toInt();
            k.page = m.captured(2).toInt();
            k.text = m.captured(3).trimmed();
            if (k.text.size() >= 40)
                out.append(k);
        }
        return out;
    }

    static QString quoteOf(const Marker &m)
    {
        // A span long enough that a content search can find it on its own.
        return m.text.mid(4, 60).trimmed();
    }

    // Which of the three schemas the app is asking to have filled in.
    QString toolName() const
    {
        const QJsonArray tools = lastRequest().value("tools").toArray();
        if (tools.isEmpty())
            return QString();
        return tools.first()
            .toObject()
            .value("function")
            .toObject()
            .value("name")
            .toString();
    }

    // Ids a cross-paper prompt carried, in order.
    QStringList paperIdsInPrompt() const
    {
        QStringList out;
        static const QRegularExpression re(
            QStringLiteral("\"paperId\": \"([^\"]+)\""));
        auto it = re.globalMatch(lastPrompt());
        while (it.hasNext()) {
            const QString id = it.next().captured(1);
            if (!out.contains(id))
                out.append(id);
        }
        return out;
    }

    QByteArray buildModuleAnswer()
    {
        const QVector<Marker> ms = markers();
        Q_ASSERT(ms.size() >= 2);
        const Marker good = ms.at(qMin(1, ms.size() - 1));
        const QJsonObject claim{
            {"text", "A point in this section."},
            {"source", "author_claim"},
            {"evidence",
             QJsonArray{QJsonObject{{"blockId", good.id},
                                    {"quote", quoteOf(good)}}}}};
        const QJsonObject fabricated{
            {"text", "A point with nothing behind it."},
            {"source", "experimental"},
            {"evidence",
             QJsonArray{QJsonObject{
                 {"blockId", ms.at(0).id},
                 {"quote", "measured on a dataset that is not in this paper"}}}}};
        const QJsonObject module{
            {"summary", "What this section is about, in a line."},
            {"sections",
             QJsonArray{QJsonObject{
                 {"heading", "The first part"},
                 {"items", QJsonArray{claim, fabricated}}}}},
            {"terms",
             QJsonArray{QJsonObject{{"term", "A term"},
                                    {"plain", "What it means."},
                                    {"roleInPaper", "What it does here."}}}},
            {"coreQuestion", claim},
            {"openQuestions", QJsonArray{"Something still unanswered."}},
            {"checklist",
             QJsonArray{QJsonObject{{"what", "code"},
                                    {"status", "unclear"},
                                    {"detail", ""},
                                    {"evidence", QJsonArray{}}}}},
            {"acknowledged", QJsonArray{claim}},
            {"additional", QJsonArray{}},
            {"contributions", QJsonArray{}},
            {"dimensions", QJsonArray{}},
            {"steps", QJsonArray{}}};
        return QJsonDocument(module).toJson(QJsonDocument::Compact);
    }

    // The library-level calls all use one tool name, so which analysis is
    // being asked for is read off the schema it was handed.
    QStringList schemaKeys() const
    {
        const QJsonArray tools = lastRequest().value("tools").toArray();
        if (tools.isEmpty())
            return {};
        return tools.first()
            .toObject()
            .value("function")
            .toObject()
            .value("parameters")
            .toObject()
            .value("properties")
            .toObject()
            .keys();
    }

    // Ids of the categories the prompt carried (classification only).
    QStringList categoryIdsInPrompt() const
    {
        QStringList out;
        static const QRegularExpression re(
            QStringLiteral("\"id\": \"([^\"]+)\""));
        auto it = re.globalMatch(lastPrompt());
        while (it.hasNext())
            out.append(it.next().captured(1));
        return out;
    }

    QByteArray buildTaxonomyAnswer()
    {
        const QStringList ids = paperIdsInPrompt();
        QJsonArray cats;
        for (int i = 0; i < ids.size() && i < 2; ++i) {
            cats.append(QJsonObject{
                {"name", i == 0 ? "route A" : "route B"},
                {"description", "A way of doing it."},
                {"paperIds", QJsonArray{ids.at(i)}}});
        }
        return QJsonDocument(
                   QJsonObject{
                       {"dimensions",
                        QJsonArray{QJsonObject{{"dimension", "method_route"},
                                               {"categories", cats}}}},
                       {"ambiguous", QJsonArray{}}})
            .toJson(QJsonDocument::Compact);
    }

    QByteArray buildClassifyAnswer()
    {
        const QStringList cats = categoryIdsInPrompt();
        const QStringList papers = paperIdsInPrompt();
        QJsonArray assignments;
        for (const QString &p : papers) {
            if (cats.contains(p))
                continue;
            assignments.append(QJsonObject{
                {"paperId", p},
                {"categoryIds",
                 cats.isEmpty() ? QJsonArray{} : QJsonArray{cats.first()}},
                {"ambiguous", false},
                {"note", ""}});
        }
        return QJsonDocument(QJsonObject{{"assignments", assignments}})
            .toJson(QJsonDocument::Compact);
    }

    QByteArray buildAnswer(const QString &tool)
    {
        if (tool == QLatin1String("emit_section"))
            return buildModuleAnswer();
        if (tool == QLatin1String("emit_analysis")) {
            const QStringList keys = schemaKeys();
            if (keys.contains(QStringLiteral("assignments")))
                return buildClassifyAnswer();
            return buildTaxonomyAnswer();
        }
        const QVector<Marker> ms = markers();
        Q_ASSERT(ms.size() >= 3);
        const Marker good = ms.at(qMin(1, ms.size() - 1));
        const Marker moved = ms.at(ms.size() / 2);
        const Marker fabricated = ms.at(0);
        m_citedGood = good.id;
        m_citedMissing = 99999;
        m_citedFabricated = fabricated.id;

        auto claim = [](const QString &text, const QString &source,
                        const QJsonArray &ev) {
            return QJsonObject{{"text", text},
                               {"source", source},
                               {"evidence", ev}};
        };
        auto ev = [](int blockId, const QString &quote) {
            return QJsonArray{
                QJsonObject{{"blockId", blockId}, {"quote", quote}}};
        };

        const QJsonObject digest{
            {"oneLiner", "A paper about something, said in one line."},
            {"problem", claim("The problem the paper attacks.", "author_claim",
                              ev(good.id, quoteOf(good)))},
            {"importance",
             claim("Why that problem matters.", "experimental",
                   ev(99999, quoteOf(moved)))},
            {"method",
             claim("They route the packets with quantum annealing.",
                   "author_claim",
                   ev(fabricated.id,
                      "we route every packet with a quantum annealer"))},
            {"results",
             QJsonArray{claim("Reads as an improvement.", "ai_analysis",
                              QJsonArray{})}},
            {"contributions",
             QJsonArray{QJsonObject{{"text", "A way of doing the thing."},
                                    {"type", "method"},
                                    {"source", "ai_analysis"},
                                    {"evidence", QJsonArray{}}}}},
            {"limitations",
             QJsonArray{claim("It assumes a lot.", "ai_analysis",
                              QJsonArray{})}},
            {"relevance",
             QJsonObject{{"level", "high"},
                         {"reason", "Same task as the reader's project."},
                         {"evidence", QJsonArray{}}}},
            {"advice",
             QJsonObject{{"code", "read_method_experiments"},
                         {"reason", "The method is the useful part."}}},
            {"priority",
             QJsonArray{QJsonObject{{"what", "The experiments"},
                                    {"why", "Where the numbers are."},
                                    {"blockId", moved.id}}}},
            {"facets",
             QJsonObject{{"researchProblem", "traffic forecasting"},
                         {"paperType", "new_method"},
                         {"methodRoute", "graph neural network"},
                         {"datasets", QJsonArray{"METR-LA"}},
                         {"metrics", QJsonArray{"MAE"}},
                         {"baselines", QJsonArray{"DCRNN"}},
                         {"mainLimitations", QJsonArray{"stationary sensors"}}}}};
        return QJsonDocument(digest).toJson(QJsonDocument::Compact);
    }

    // The retry drops the tools, so the schema has to be recognised from the
    // system prompt instead.
    QByteArray buildAnswerForSchemaless()
    {
        const QString sys = lastRequest()
                                .value("messages")
                                .toArray()
                                .first()
                                .toObject()
                                .value("content")
                                .toString();
        if (sys.contains(QStringLiteral("\"assignments\"")))
            return buildClassifyAnswer();
        if (sys.contains(QStringLiteral("\"dimensions\""))
            && sys.contains(QStringLiteral("\"categories\"")))
            return buildTaxonomyAnswer();
        if (sys.contains(QStringLiteral("\"sections\"")))
            return buildModuleAnswer();
        return buildAnswer(QString());
    }

    void respondProse(QTcpSocket *s, const QByteArray &json)
    {
        auto frame = [](const QJsonObject &o) {
            return QByteArray("data: ")
                   + QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n\n";
        };
        QByteArray out = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/event-stream\r\n"
                         "Connection: close\r\n\r\n";
        // Wrapped in prose and a code fence, the way a chatty model answers.
        const QByteArray body =
            "Sure! Here is the result:\n\n```json\n" + json + "\n```\n";
        for (int i = 0; i < body.size(); i += 400) {
            out += frame(QJsonObject{
                {"choices",
                 QJsonArray{QJsonObject{
                     {"index", 0},
                     {"delta",
                      QJsonObject{{"content",
                                   QString::fromUtf8(body.mid(i, 400))}}},
                     {"finish_reason", QJsonValue::Null}}}}});
        }
        out += frame(QJsonObject{
            {"choices", QJsonArray{QJsonObject{{"index", 0},
                                               {"delta", QJsonObject{}},
                                               {"finish_reason", "stop"}}}}});
        out += "data: [DONE]\n\n";
        s->write(out);
        s->flush();
        s->disconnectFromHost();
    }

    void respond(QTcpSocket *s, const QByteArray &args)
    {
        auto frame = [](const QJsonObject &o) {
            return QByteArray("data: ")
                   + QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n\n";
        };
        QByteArray out = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/event-stream\r\n"
                         "Cache-Control: no-cache\r\n"
                         "Connection: close\r\n\r\n";
        out += frame(QJsonObject{
            {"choices",
             QJsonArray{QJsonObject{
                 {"index", 0},
                 {"delta",
                  QJsonObject{
                      {"tool_calls",
                       QJsonArray{QJsonObject{
                           {"index", 0},
                           {"id", "call_1"},
                           {"function",
                            QJsonObject{{"name", "emit_interpretation"},
                                        {"arguments", ""}}}}}}}},
                 {"finish_reason", QJsonValue::Null}}}}});
        for (int i = 0; i < args.size(); i += 512) {
            out += frame(QJsonObject{
                {"choices",
                 QJsonArray{QJsonObject{
                     {"index", 0},
                     {"delta",
                      QJsonObject{
                          {"tool_calls",
                           QJsonArray{QJsonObject{
                               {"index", 0},
                               {"function",
                                QJsonObject{
                                    {"arguments",
                                     QString::fromUtf8(args.mid(i, 512))}}}}}}}},
                     {"finish_reason", QJsonValue::Null}}}}});
        }
        out += frame(QJsonObject{
            {"choices", QJsonArray{QJsonObject{{"index", 0},
                                               {"delta", QJsonObject{}},
                                               {"finish_reason", "tool_calls"}}}}});
        out += "data: [DONE]\n\n";
        s->write(out);
        s->flush();
        s->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buf;
    QByteArray m_lastBody;
    int m_requests = 0;
    bool m_refuseTools = false;
    QString m_refuseAll;
    int m_citedGood = 0;
    int m_citedMissing = 0;
    int m_citedFabricated = 0;
};
