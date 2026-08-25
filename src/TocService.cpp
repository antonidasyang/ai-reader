#include "TocService.h"

#include "Block.h"
#include "BlockListModel.h"
#include "LlmClient.h"
#include "PaperController.h"
#include "Settings.h"
#include "TaskManager.h"

#include <QAbstractItemModel>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <functional>

namespace {

// Reserved (model, promptHash) cache key for TOCs derived
// structurally from GROBID's TEI outline. Same TocCache entry shape
// as LLM results, but a key no real LLM configuration can produce:
// on rehydrate the entry for the user's actual model + prompt is
// tried first, so an LLM-generated TOC always beats the structural
// one, and a later LLM run never collides with it.
constexpr auto kGrobidCacheModel  = "grobid";
constexpr auto kGrobidCachePrompt = "tei-outline";

// Extracts the substring between the first '{' and the last '}', so we can
// tolerate ```json fences or stray prose around the JSON body.
QByteArray extractJsonObject(const QString &text)
{
    const int first = text.indexOf(QChar('{'));
    const int last  = text.lastIndexOf(QChar('}'));
    if (first < 0 || last <= first)
        return {};
    return text.mid(first, last - first + 1).toUtf8();
}

} // namespace

TocService::TocService(Settings *settings,
                       PaperController *paper,
                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paper(paper)
    , m_blocks(paper ? paper->blocks() : nullptr)
    , m_clients(settings, this)
{
    if (m_paper) {
        connect(m_paper, &PaperController::blocksChanged,
                this, &TocService::onPaperChanged);
    }
}

TocService::~TocService() = default;

void TocService::onPaperChanged()
{
    // blocksChanged fires both when a new paper loads and when the
    // user splits/merges/deletes a paragraph on the current paper.
    // Only the former is a real "paper changed" event for us; for
    // the latter, leave the TOC alone -- the user explicitly does
    // not want their generated TOC wiped by a small paragraph edit.
    const QString newId = m_paper ? m_paper->paperId() : QString();
    if (newId == m_lastPaperId) return;
    m_lastPaperId = newId;

    cancel();
    clear();
    m_cache.setPaperId(newId);
    rehydrateFromCache();
}

void TocService::rehydrateFromCache()
{
    if (!m_settings || !m_blocks) return;
    if (m_cache.paperId().isEmpty()) return;

    QVector<Section> cached = m_cache.lookup(
        m_settings->model(), TocCache::sha(systemPrompt()));
    Source src = Llm;
    if (cached.isEmpty()) {
        // No LLM result for the current model/prompt — fall back to a
        // structurally derived (GROBID) TOC if one was cached.
        cached = m_cache.lookup(QString::fromLatin1(kGrobidCacheModel),
                                QString::fromLatin1(kGrobidCachePrompt));
        src = Structural;
    }
    if (cached.isEmpty()) return;
    m_source = src;

    // Rebuild blockId → page map so any UI that resolves start_block back
    // to a page (TOC sidebar click) keeps working without regenerating.
    m_blockIdToPage.clear();
    for (int row = 0; row < m_blocks->blockCount(); ++row) {
        const Block *b = m_blocks->blockAt(row);
        if (b) m_blockIdToPage.insert(b->id, b->page);
    }

    m_model.setSections(QVector<Section>(cached));
    emit sectionsChanged();
    setStatus(Done);
}

void TocService::adoptStructuredOutline(const QVector<Section> &sections)
{
    // GROBID handed us the section structure for free. Segmentation
    // and the TOC are one operation: every applied GROBID result
    // refreshes the TOC, replacing whatever was on display — the old
    // TOC (LLM or structural) referenced block ids that the fresh
    // block list just invalidated. Only an LLM run the user explicitly
    // started right now wins over us; running generate() later also
    // overwrites, so the rule is simply "latest result wins".
    if (sections.size() < 2)
        return;                    // unusable outline — leave TOC alone
    if (m_status == Generating || !m_taskId.isEmpty())
        return;                    // explicit LLM generation wins, queued or not

    // Keep blockId → page resolvable, same as after a generate().
    m_blockIdToPage.clear();
    if (m_blocks) {
        for (int row = 0; row < m_blocks->blockCount(); ++row) {
            const Block *b = m_blocks->blockAt(row);
            if (b) m_blockIdToPage.insert(b->id, b->page);
        }
    }

    if (!m_cache.paperId().isEmpty()) {
        // Cached LLM TOCs are stale now too (their block ids died with
        // the old segmentation), and rehydrate prefers the LLM key —
        // drop everything so reopening shows this outline, not a
        // resurrected pre-segmentation TOC.
        m_cache.clearEntries();
        m_cache.store(QString::fromLatin1(kGrobidCacheModel),
                      QString::fromLatin1(kGrobidCachePrompt),
                      sections);
    }
    m_source = Structural;
    m_model.setSections(QVector<Section>(sections));
    emit sectionsChanged();
    setStatus(Done);
    qInfo().noquote() << "TOC: adopted GROBID outline,"
                      << sections.size() << "sections";
}

void TocService::clear()
{
    m_buffer.clear();
    m_blockIdToPage.clear();
    m_source = NoSource;
    if (m_model.sectionCount() == 0 && m_status == Idle && m_lastError.isEmpty())
        return;
    m_model.clear();
    m_lastError.clear();
    m_status = Idle;
    emit statusChanged();
    emit sectionsChanged();
}

void TocService::cancelReply()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    if (m_status == Generating)
        setStatus(Idle);
}

void TocService::cancel()
{
    cancelReply();
    // A generation still waiting its turn in the queue is over too:
    // whatever came next -- another paper, another request -- has
    // overtaken it, and nothing would ever collect its answer.
    if (m_tasks && !m_taskId.isEmpty()) {
        const QString id = m_taskId;
        m_taskId.clear();
        m_taskPaperId.clear();
        m_tasks->cancel(id);
    }
}

void TocService::setTasks(TaskManager *tasks)
{
    m_tasks = tasks;
    if (!m_tasks)
        return;

    // A generation the app closed on can be started again from nothing --
    // but only where its paper is the one on screen, since the sections it
    // produces refer to that paper's blocks.
    m_tasks->registerResumer(Tasks::Kind::Toc,
                             [this](const QJsonObject &resume) {
        if (!m_paper)
            return false;
        if (m_paper->paperId()
            != resume.value(QStringLiteral("paperId")).toString())
            return false;
        // A resumer that cannot start says no and leaves the screen alone:
        // generate() would refuse a launch with no paragraphs loaded by
        // painting "No paper open." into the sidebar, for a generation the
        // user never watched start.
        if (!couldGenerate())
            return false;
        generate();
        return true;
    });
}

void TocService::finishTask(bool ok, const QString &error)
{
    if (!m_tasks || m_taskId.isEmpty())
        return;
    const QString id = m_taskId;
    m_taskId.clear();
    m_taskPaperId.clear();
    m_tasks->finish(id, ok, error);
}

void TocService::cancelTask()
{
    if (!m_tasks || m_taskId.isEmpty())
        return;
    const QString id = m_taskId;
    m_taskId.clear();
    m_taskPaperId.clear();
    m_tasks->markCanceled(id);
}

bool TocService::couldGenerate() const
{
    // The silent twin of canGenerate() below; the two refuse on the same
    // three grounds and have to stay in step.
    return m_settings && m_blocks && m_settings->isConfigured()
        && m_blocks->blockCount() > 0;
}

bool TocService::canGenerate()
{
    if (!m_settings || !m_blocks)
        return false;
    if (!m_settings->isConfigured()) {
        setStatus(Failed,
                  tr("LLM is not configured. Open Settings to add a model and API key."));
        return false;
    }
    if (m_blocks->blockCount() == 0) {
        setStatus(Failed, tr("No paper open."));
        return false;
    }
    return true;
}

void TocService::generate()
{
    // The refusals stay in front of the queue: a request that could only
    // fail should say so at once rather than wait its turn to do it.
    if (!canGenerate())
        return;

    if (!m_tasks) {
        runGenerate();
        return;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::Toc;
    req.title = tr("Extract contents");
    req.paperId = m_paper ? m_paper->paperId() : QString();
    req.paperTitle = m_paper ? m_paper->fileName() : QString();
    // One request over the whole paper: there is nothing to count off
    // until the model answers.
    req.steps = 0;
    req.resume = QJsonObject{
        {QStringLiteral("paperId"), req.paperId},
        {QStringLiteral("path"),
         m_paper ? m_paper->pdfSource().toLocalFile() : QString()},
    };

    const QString id = m_tasks->submit(req,
        [this, paperId = req.paperId] {
            // submit() only hands the id back when it returns, and it may
            // call this from inside itself -- one turn of the event loop
            // and the generation has its id on record.
            QTimer::singleShot(0, this, [this, paperId] {
                // Cancelled in the turn between being admitted and
                // starting: the stop callback has already dropped the id,
                // or another paper's generation has taken its place. The
                // id on record is not ours to settle either way.
                if (m_taskId.isEmpty() || m_taskPaperId != paperId)
                    return;
                if (!m_paper || m_paper->paperId() != paperId) {
                    // Admitted after the reader moved on; the sections it
                    // would produce belong to a paper that is not open.
                    // Nothing failed — the paper closed under it.
                    cancelTask();
                    return;
                }
                runGenerate();
            });
        },
        [this, paperId = req.paperId] {
            // The manager is stopping us; it owns the outcome from here, so
            // drop the id rather than finishing the task ourselves -- and
            // only while the id is still this generation's, since a newer
            // one may already have taken its place and that one's id is not
            // ours to drop. The request in flight is aborted either way.
            if (m_taskPaperId == paperId) {
                m_taskId.clear();
                m_taskPaperId.clear();
            }
            cancelReply();
        });
    if (id.isEmpty())
        return;             // this paper's contents are already being read
    m_taskId = id;
    m_taskPaperId = req.paperId;
}

void TocService::runGenerate()
{
    if (!canGenerate()) {
        // canGenerate() puts its own reason on screen for the two refusals
        // it knows; with no settings or no block list at all it says
        // nothing, and the task still has to be told something honest.
        finishTask(false, m_lastError.isEmpty() ? tr("No paper open.")
                                                : m_lastError);
        return;
    }

    // Only the reply, never the task: this is the task's own body.
    cancelReply();
    m_buffer.clear();
    m_blockIdToPage.clear();
    setStatus(Generating);

    // Rebuilt when the configuration moved: switching provider needs a
    // different client, not different fields on the old one.
    m_client = m_clients.client();
    if (!m_client) {
        setStatus(Failed, tr("No model is configured."));
        return;
    }
    if (m_tasks && !m_taskId.isEmpty())
        m_tasks->setNote(m_taskId,
                         tr("Reading %n paragraph(s)", "", m_blocks->blockCount()));

    LlmClient::Request req;
    req.system = systemPrompt();
    req.messages.append({QStringLiteral("user"), userPrompt()});
    req.temperature = 0.0;
    req.maxTokens = m_settings->maxTokens();
    req.stream = true;

    m_reply = m_client->send(req);

    connect(m_reply, &LlmReply::chunkReceived, this,
            [this](const QString &chunk) { m_buffer += chunk; });
    connect(m_reply, &LlmReply::finished, this, [this]() {
        const QString text = m_buffer;
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        if (m_status != Generating)
            return;
        parseResponse(text);
    });
    connect(m_reply, &LlmReply::errorOccurred, this,
            [this](const QString &message) {
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        setStatus(Failed, message);
    });
}

QString TocService::systemPrompt() const
{
    if (m_settings) {
        const QString custom = m_settings->tocPrompt();
        if (!custom.isEmpty()) return custom;
    }
    return defaultSystemPrompt();
}

QString TocService::defaultSystemPrompt() const
{
    return QStringLiteral(
        "You build a hierarchical table of contents from the full text of a "
        "paper.\n"
        "\n"
        "Input is a JSON object: `{\"blocks\": [{\"block_id\": N, "
        "\"page\": P, \"text\": \"…\"}, …]}`. Each block is the full text "
        "of one extracted region (often a whole page or a long paragraph). "
        "Section headings appear INSIDE the text — possibly mid-block — "
        "since our extractor does not split them out.\n"
        "\n"
        "Your job: scan every block's text and emit a hierarchical TOC. For "
        "each heading you find, set `start_block` to the `block_id` of the "
        "block where the heading appears.\n"
        "\n"
        "Output JSON ONLY in this exact shape:\n"
        "\n"
        "{\n"
        "  \"sections\": [\n"
        "    {\"id\": \"s1\", \"level\": 1, \"title\": \"Introduction\",\n"
        "     \"start_block\": 12, \"children\": [\n"
        "       {\"id\": \"s1.1\", \"level\": 2, \"title\": \"Motivation\",\n"
        "        \"start_block\": 14, \"children\": []}\n"
        "     ]}\n"
        "  ]\n"
        "}\n"
        "\n"
        "Rules:\n"
        "- Find real section headings in the block text. Look for:\n"
        "  • numbered headings: \"1 Introduction\", \"3.2 Method\", "
        "\"IV. Results\"\n"
        "  • named headings: \"Abstract\", \"Introduction\", \"Related "
        "Work\", \"Method\", \"Experiments\", \"Results\", \"Discussion\", "
        "\"Conclusion\", \"Limitations\", \"References\", "
        "\"Acknowledgments\", \"Appendix\", \"Supplementary\"\n"
        "  • appendix subsections: \"A.1 Dataset\", \"B Implementation\"\n"
        "- Include every plausible section heading. When in doubt, include "
        "it. List headings in the order they appear in the paper.\n"
        "- IGNORE running headers/footers: lone page numbers, author names "
        "(\"K. You et al.\"), the paper title repeated on every page, "
        "conference/journal banners, figure/table captions.\n"
        "- `start_block` MUST be a real `block_id` from the input — the one "
        "where that heading's text appears.\n"
        "- Title may be cleaned (strip leading numbering like \"1\" or "
        "\"1.1\").\n"
        "- Infer level from numbering (\"1.1\" → level 2; bare title → "
        "level 1).\n"
        "- DO NOT return an empty sections array unless the input truly "
        "contains no real headings.\n"
        "- Use the EXACT key names: `sections`, `id`, `level`, `title`, "
        "`start_block`, `children`.\n"
        "- Output JSON ONLY. No prose. No Markdown fences. No explanation.");
}

QString TocService::userPrompt() const
{
    // Send the full text of every block. PDFium often merges a whole page
    // (or a heading + its body) into one block, so only the full text
    // contains the section headings the model needs to extract. Use compact
    // JSON to save tokens.
    QJsonArray blocks;
    for (int row = 0; row < m_blocks->blockCount(); ++row) {
        const Block *b = m_blocks->blockAt(row);
        if (!b) continue;
        QJsonObject o;
        o[QStringLiteral("block_id")] = b->id;
        o[QStringLiteral("page")] = b->page + 1;
        o[QStringLiteral("text")] = b->text;
        blocks.append(o);
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"blocks", blocks}})
            .toJson(QJsonDocument::Compact));
}

void TocService::parseResponse(const QString &text)
{
    auto snippet = [&text]() {
        QString s = text.trimmed();
        if (s.size() > 320) s = s.left(320) + QStringLiteral("…");
        return s;
    };

    const QByteArray jsonBytes = extractJsonObject(text);
    if (jsonBytes.isEmpty()) {
        setStatus(Failed,
                  tr("LLM returned no JSON object. Raw output: %1").arg(snippet()));
        return;
    }

    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &jerr);
    if (jerr.error != QJsonParseError::NoError || doc.isNull()) {
        setStatus(Failed,
                  tr("Invalid JSON: %1. Body: %2")
                      .arg(jerr.errorString(),
                           QString::fromUtf8(jsonBytes).left(320)));
        return;
    }

    // Locate the section array regardless of how the model wrapped it.
    QJsonArray topArr;
    if (doc.isArray()) {
        topArr = doc.array();
    } else {
        const QJsonObject obj = doc.object();
        for (const char *k : {"sections", "toc", "table_of_contents",
                              "contents", "items", "outline"}) {
            const QJsonValue v = obj.value(QString::fromUtf8(k));
            if (v.isArray()) { topArr = v.toArray(); break; }
        }
    }

    if (topArr.isEmpty()) {
        setStatus(Failed,
                  tr("JSON has no section array. Body: %1")
                      .arg(QString::fromUtf8(jsonBytes).left(320)));
        return;
    }

    if (m_blocks) {
        for (int row = 0; row < m_blocks->blockCount(); ++row) {
            const Block *b = m_blocks->blockAt(row);
            if (b) m_blockIdToPage.insert(b->id, b->page);
        }
    }

    auto stringField = [](const QJsonObject &o,
                          std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            const QString v = o.value(QString::fromUtf8(k)).toString().trimmed();
            if (!v.isEmpty()) return v;
        }
        return QString();
    };
    auto intField = [](const QJsonObject &o,
                       std::initializer_list<const char *> keys, int def = -1) {
        for (const char *k : keys) {
            const QJsonValue v = o.value(QString::fromUtf8(k));
            if (v.isDouble()) return v.toInt(def);
            if (v.isString()) {
                bool ok = false;
                const int x = v.toString().toInt(&ok);
                if (ok) return x;
            }
        }
        return def;
    };

    QVector<Section> flat;
    int counter = 0;

    std::function<void(const QJsonArray &, int)> walk;
    walk = [&](const QJsonArray &arr, int depthHint) {
        for (const QJsonValue &v : arr) {
            // Tolerate string-only entries.
            if (v.isString()) {
                Section s;
                s.id = QStringLiteral("s%1").arg(++counter);
                s.level = depthHint;
                s.title = v.toString().trimmed();
                if (!s.title.isEmpty()) flat.append(s);
                continue;
            }
            if (!v.isObject()) continue;

            const QJsonObject obj = v.toObject();
            Section s;
            s.id = stringField(obj, {"id"});
            if (s.id.isEmpty())
                s.id = QStringLiteral("s%1").arg(++counter);
            s.level = intField(obj, {"level", "depth"}, depthHint);
            if (s.level <= 0) s.level = depthHint;
            s.title = stringField(obj, {"title", "name", "heading", "text", "label"});
            s.startBlockId = intField(obj,
                {"start_block", "block_id", "block", "startBlock", "start", "id_block"});
            s.startPage = (s.startBlockId >= 0)
                ? m_blockIdToPage.value(s.startBlockId, 0)
                : intField(obj, {"page", "start_page"}, 0);

            if (!s.title.isEmpty())
                flat.append(s);

            // Recurse into whichever child key the model chose.
            for (const char *k : {"children", "subsections", "subsection",
                                  "sections", "items", "sub"}) {
                const QJsonValue cv = obj.value(QString::fromUtf8(k));
                if (cv.isArray()) {
                    walk(cv.toArray(), s.level + 1);
                    break;
                }
            }
        }
    };
    walk(topArr, 1);

    if (flat.isEmpty()) {
        setStatus(Failed,
                  tr("Walked JSON but found no titled sections. Body: %1")
                      .arg(QString::fromUtf8(jsonBytes).left(320)));
        return;
    }

    if (m_settings && !m_cache.paperId().isEmpty()) {
        m_cache.store(m_settings->model(),
                      TocCache::sha(systemPrompt()),
                      flat);
    }
    m_source = Llm;
    m_model.setSections(std::move(flat));
    emit sectionsChanged();
    setStatus(Done);
}

void TocService::setStatus(Status s, const QString &err)
{
    if (s == m_status && err == m_lastError)
        return;
    m_status = s;
    m_lastError = err;
    emit statusChanged();

    // Every way a generation can end goes through here -- parsed, refused,
    // aborted -- so this is the one place the task has to be told.
    switch (s) {
    case Done:   finishTask(true);        break;
    case Failed: finishTask(false, err);  break;
    // Back to Idle means the generation was stopped rather than lost: the
    // Cancel button, or the paper being closed under it. Nothing went
    // wrong, so the row ends Canceled instead of Failed.
    case Idle:   cancelTask();            break;
    case Generating: break;
    }
}
