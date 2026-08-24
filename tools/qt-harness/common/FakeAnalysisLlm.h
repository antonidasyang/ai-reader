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

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    int requests() const { return m_requests; }
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

    // Ids the comparison prompt carried, in order.
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

    QByteArray buildCompareAnswer()
    {
        const QStringList ids = paperIdsInPrompt();
        QJsonArray cells;
        for (const QString &id : ids)
            cells.append(QJsonObject{{"paperId", id}, {"text", "not stated"}});
        const QJsonObject answer{
            {"rows",
             QJsonArray{QJsonObject{{"dimension", "research_problem"},
                                    {"cells", cells}},
                        QJsonObject{{"dimension", "metrics"},
                                    {"cells", cells}}}},
            {"comparability",
             QJsonArray{QJsonObject{
                 {"papers", QJsonArray::fromStringList(ids)},
                 {"issue", "Different datasets; the numbers do not line up."},
                 {"severity", "blocking"}}}},
            {"takeaways",
             QJsonArray{QJsonObject{{"text", "They answer different questions."},
                                    {"source", "ai_analysis"},
                                    {"evidence", QJsonArray{}}}}},
            {"ranking", "These cannot be ranked against each other."}};
        return QJsonDocument(answer).toJson(QJsonDocument::Compact);
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
        if (tool == QLatin1String("emit_comparison"))
            return buildCompareAnswer();
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
    int m_citedGood = 0;
    int m_citedMissing = 0;
    int m_citedFabricated = 0;
};
