#include "TranslationService.h"

#include "Block.h"
#include "BlockListModel.h"
#include "LlmClient.h"
#include "PaperController.h"
#include "Settings.h"

namespace {

QString resolveLanguageName(const QString &code)
{
    if (code.isEmpty() || code.compare(QStringLiteral("zh-CN"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Simplified Chinese (zh-CN)");
    if (code.compare(QStringLiteral("zh-TW"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Traditional Chinese (zh-TW)");
    if (code.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0
        || code.compare(QStringLiteral("en-US"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("English");
    if (code.compare(QStringLiteral("ja"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Japanese");
    return code;
}

// Whitespace-free copy, for matching a PDF selection against a block's
// text: the two agree on words but not on where the lines broke.
QString squeezed(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (!c.isSpace())
            out.append(c);
    }
    return out;
}

// Cache slot for ad-hoc selection translations. Real blocks use their
// own ids (positive), so a negative one can't collide; the source-text
// hash in the composite key keeps different selections apart.
constexpr int kSnippetBlockId = -1;

// Below this many non-space characters a selection is a term or half a
// sentence — too little to place inside a paragraph with confidence, so
// it gets translated on its own.
constexpr int kMinMatchChars = 12;

} // namespace

TranslationService::TranslationService(Settings *settings,
                                       PaperController *paper,
                                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paper(paper)
    , m_model(paper ? paper->blocks() : nullptr)
{
    if (m_paper) {
        connect(m_paper, &PaperController::blocksChanged,
                this, &TranslationService::onPaperChanged);
    }
    if (m_model) {
        // Splitting/merging/deleting paragraphs renumbers rows, which
        // would leave a pinned card mirroring somebody else's text. The
        // cards stay open with the text they already have; they just
        // stop following a row.
        connect(m_model, &BlockListModel::blocksMutated, this, [this] {
            m_snippets.detachBlockRows();
        });
    }
}

TranslationService::~TranslationService() = default;

void TranslationService::onPaperChanged()
{
    cancel();
    // PaperController re-emits blocksChanged for paragraph edits too, so
    // "the blocks changed" is not the same as "a different paper". Cards
    // belong to the paper they were opened on and close with it; an edit
    // to the current paper only costs them their row (see the
    // blocksMutated hookup in the constructor).
    const QString paperId = m_paper ? m_paper->paperId() : QString();
    if (paperId != m_cache.paperId())
        closeAllSnippets();
    m_done = 0;
    m_failed = 0;
    m_total = 0;
    emit progressChanged();

    // Switch the cache to the new paper and rehydrate any matching rows
    // straight into the BlockListModel — translations the user already
    // paid for show up instantly without another API call.
    m_cache.setPaperId(m_paper ? m_paper->paperId() : QString());
    if (!m_cache.paperId().isEmpty())
        emit translationCacheReady(m_cache.paperId());
    rehydrateFromCache();
}

void TranslationService::refreshFromCache()
{
    if (busy())
        return;
    rehydrateFromCache();
}

void TranslationService::rehydrateFromCache()
{
    if (!m_settings || !m_model) return;
    if (m_cache.paperId().isEmpty()) return;

    const QString model      = m_settings->model();
    const QString promptHash = TranslationCache::sha(systemPrompt());
    const QString lang       = m_settings->targetLang();

    int hits = 0;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b) continue;
        if (b->translationStatus == Block::Translated) {
            ++hits;     // already on screen — count it, never overwrite it
            continue;
        }
        const QString cached =
            m_cache.lookup(b->id, b->text, model, promptHash, lang);
        if (cached.isEmpty()) continue;
        m_model->setTranslation(row, cached);
        m_model->setTranslationStatus(row, Block::Translated);
        ++hits;
    }
    if (hits > 0) {
        m_done = hits;
        m_total = hits;
        emit progressChanged();
    }
}

void TranslationService::cancel()
{
    if (m_pending.isEmpty() && m_inflight == 0)
        return;

    // Reset queued (not yet started) rows so the user can start over later.
    if (m_model) {
        for (int row : std::as_const(m_pending)) {
            const Block *b = m_model->blockAt(row);
            if (b && b->translationStatus == Block::Queued)
                m_model->setTranslationStatus(row, Block::NotTranslated);
        }
    }
    m_pending.clear();

    // Let in-flight requests finish naturally — they'll mark their rows
    // Translated or Failed and decrement m_inflight via their handlers.
    emit progressChanged();
    if (m_inflight == 0)
        emit busyChanged();
}

void TranslationService::translateAll()
{
    if (!m_settings || !m_model) return;

    if (!m_settings->isConfigured()) {
        setLastError(tr("LLM is not configured. Open Settings to add a model and API key."));
        return;
    }

    refreshClient();

    m_pending.clear();
    m_done = 0;
    m_failed = 0;
    m_total = 0;

    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b) continue;
        if (shouldSkip(b->text)) {
            m_model->setTranslationStatus(row, Block::Skipped);
            m_model->setTranslation(row, b->text);
            continue;
        }
        if (b->translationStatus == Block::Translated)
            continue;

        m_pending.enqueue(row);
        m_model->setTranslationStatus(row, Block::Queued);
        ++m_total;
    }

    setLastError({});
    emit progressChanged();
    if (m_pending.isEmpty())
        return;

    emit busyChanged();
    scheduleNext();
}

void TranslationService::retryFailed()
{
    if (!m_model) return;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b) continue;
        if (b->translationStatus == Block::Failed) {
            m_pending.enqueue(row);
            m_model->setTranslationStatus(row, Block::Queued);
            ++m_total;
        }
    }
    if (m_pending.isEmpty()) return;
    if (!m_client) {
        translateAll();
        return;
    }
    emit progressChanged();
    emit busyChanged();
    scheduleNext();
}

void TranslationService::translateBlock(int row)
{
    if (!m_settings || !m_model) return;
    if (row < 0 || row >= m_model->blockCount()) return;
    const Block *b = m_model->blockAt(row);
    if (!b) return;

    if (!m_settings->isConfigured()) {
        setLastError(tr("LLM is not configured. Open Settings to add a model and API key."));
        return;
    }
    refreshClient();

    if (shouldSkip(b->text)) {
        m_model->setTranslationStatus(row, Block::Skipped);
        m_model->setTranslation(row, b->text);
        return;
    }
    if (b->translationStatus == Block::Translating
        || b->translationStatus == Block::Queued) {
        return;  // already in flight
    }
    if (m_pending.contains(row)) return;

    m_pending.enqueue(row);
    m_model->setTranslationStatus(row, Block::Queued);
    ++m_total;
    setLastError({});
    emit progressChanged();
    if (m_inflight == 0)
        emit busyChanged();
    scheduleNext();
}

void TranslationService::refreshClient()
{
    if (!m_settings) return;
    if (!m_client) {
        m_client = m_settings->createClient(this);
        return;
    }
    // Settings may have changed since the client was built.
    m_client->setApiKey(m_settings->apiKey());
    m_client->setModel(m_settings->model());
    if (!m_settings->baseUrl().isEmpty())
        m_client->setBaseUrl(QUrl(m_settings->baseUrl()));
}

void TranslationService::scheduleNext()
{
    while (m_inflight < m_maxInflight && !m_pending.isEmpty()) {
        const int row = m_pending.dequeue();
        translateRow(row);
    }
}

bool TranslationService::shouldSkip(const QString &text) const
{
    if (text.trimmed().isEmpty()) return true;
    int letters = 0;
    for (QChar c : text) {
        if (c.isLetter()) ++letters;
    }
    // If <20% letters, treat as math/numeric/formula and pass through.
    return letters * 5 < text.size();
}

QString TranslationService::defaultSystemPrompt() const
{
    return QStringLiteral(
        "You are a precise academic translator. Translate the user's text into {{lang}}.\n"
        "\n"
        "Rules:\n"
        "- Preserve all citations like [12], [13, 14], (Smith et al., 2020) unchanged.\n"
        "- Preserve inline math notation ($x$, $$y$$, \\begin{...}) unchanged.\n"
        "- Preserve code, URLs, file paths, and proper nouns unchanged.\n"
        "- Output ONLY the translation. No quotes around the result, no notes, "
        "no source text, no \"Translation:\" prefix.");
}

QString TranslationService::systemPrompt() const
{
    const QString lang = resolveLanguageName(m_settings ? m_settings->targetLang()
                                                       : QString());
    QString tmpl;
    if (m_settings && !m_settings->translationPrompt().isEmpty())
        tmpl = m_settings->translationPrompt();
    else
        tmpl = defaultSystemPrompt();
    return tmpl.replace(QStringLiteral("{{lang}}"), lang);
}

void TranslationService::translateRow(int row)
{
    if (!m_client || !m_model) return;
    const Block *b = m_model->blockAt(row);
    if (!b) return;

    LlmClient::Request req;
    req.system = systemPrompt();
    req.messages.append({QStringLiteral("user"), b->text});
    req.temperature = m_settings ? m_settings->temperature() : 0.2;
    req.stream = true;
    req.maxTokens = m_settings ? m_settings->maxTokens() : 4096;

    LlmReply *reply = m_client->send(req);
    m_replyToRow.insert(reply, row);
    ++m_inflight;
    m_model->setTranslationStatus(row, Block::Translating);
    // Clear any previous translation text before streaming the new one.
    m_model->setTranslation(row, QString());

    connect(reply, &LlmReply::chunkReceived, this,
            [this, reply](const QString &chunk) {
        const int r = m_replyToRow.value(reply, -1);
        if (r >= 0 && m_model) {
            m_model->appendTranslationChunk(r, chunk);
            // Any card pinned to this paragraph streams along with it.
            syncBlockRow(r);
        }
    });
    connect(reply, &LlmReply::finished, this, [this, reply]() {
        const int r = m_replyToRow.take(reply);
        --m_inflight;
        const Block *b = (r >= 0 && m_model) ? m_model->blockAt(r) : nullptr;
        if (b && b->translationStatus == Block::Translating) {
            if (b->translation.trimmed().isEmpty()) {
                // A stream can end having delivered nothing at all. Calling
                // that Translated hides the failure behind a blank line and,
                // worse, caches the blank — so reopening the paper "restores"
                // it and the block never gets retried.
                const QString msg =
                    tr("The model returned an empty translation.");
                m_model->setTranslationStatus(r, Block::Failed, msg);
                ++m_failed;
                setLastError(msg);
            } else {
                m_model->setTranslationStatus(r, Block::Translated);
                ++m_done;

                // Persist to disk so reopening this paper restores the
                // translation without another API round-trip.
                if (m_settings && !m_cache.paperId().isEmpty()) {
                    m_cache.store(b->id, b->text,
                                  m_settings->model(),
                                  TranslationCache::sha(systemPrompt()),
                                  m_settings->targetLang(),
                                  b->translation);
                }
            }
        }
        if (r >= 0)
            syncBlockRow(r);
        emit progressChanged();
        reply->deleteLater();
        scheduleNext();
        if (m_inflight == 0 && m_pending.isEmpty())
            emit busyChanged();
    });
    connect(reply, &LlmReply::errorOccurred, this,
            [this, reply](const QString &message) {
        const int r = m_replyToRow.take(reply);
        --m_inflight;
        if (r >= 0 && m_model) {
            m_model->setTranslationStatus(r, Block::Failed, message);
            syncBlockRow(r);
        }
        ++m_failed;
        setLastError(message);
        emit progressChanged();
        reply->deleteLater();
        scheduleNext();
        if (m_inflight == 0 && m_pending.isEmpty())
            emit busyChanged();
    });
}

// ── Selection translation ──────────────────────────────────────────────

int TranslationService::findBlockRow(const QString &text, int page) const
{
    if (!m_model) return -1;
    const QString needle = squeezed(text);
    if (needle.size() < kMinMatchChars) return -1;

    int fallback = -1;
    for (int row = 0; row < m_model->blockCount(); ++row) {
        const Block *b = m_model->blockAt(row);
        if (!b) continue;
        if (!squeezed(b->text).contains(needle)) continue;
        // A block's page is where it starts, so prefer the page the
        // selection began on and fall back to the first match (the
        // selection may have run onto the next page).
        if (page < 0 || b->page == page) return row;
        if (fallback < 0) fallback = row;
    }
    return fallback;
}

void TranslationService::syncBlockRow(int row)
{
    if (!m_model || !m_snippets.hasBlockRow(row)) return;
    const Block *b = m_model->blockAt(row);
    if (!b) return;

    QString status;
    switch (b->translationStatus) {
    case Block::Translated:
    case Block::Skipped:
        status = QStringLiteral("done");
        break;
    case Block::Failed:
        status = QStringLiteral("failed");
        break;
    default:
        status = QStringLiteral("translating");
        break;
    }
    const QVector<int> ids = m_snippets.idsForBlockRow(row);
    for (const int id : ids) {
        m_snippets.setText(id, b->translation);
        m_snippets.setStatus(id, status, b->translationError);
    }
}

void TranslationService::closeSnippet(int id)
{
    for (auto it = m_snippetReplies.begin(); it != m_snippetReplies.end(); ) {
        if (it.value() != id) { ++it; continue; }
        if (it.key())
            it.key()->abort();
        it = m_snippetReplies.erase(it);
    }
    m_snippets.remove(id);
}

void TranslationService::closeAllSnippets()
{
    for (auto it = m_snippetReplies.begin(); it != m_snippetReplies.end(); ++it) {
        if (it.key())
            it.key()->abort();
    }
    m_snippetReplies.clear();
    m_snippets.clear();
}

int TranslationService::translateSelection(const QString &text, int page)
{
    const QString src = text.trimmed();
    if (src.isEmpty() || !m_settings)
        return -1;

    SnippetModel::Snippet s;
    s.source = src;
    s.status = QStringLiteral("translating");

    // Paragraph path — hand the work to the normal per-block pipeline so
    // the result is cached and the right pane fills in as well. Work the
    // model already did is shown whether or not an LLM is configured.
    const int row = findBlockRow(src, page);
    const Block *b = (row >= 0 && m_model) ? m_model->blockAt(row) : nullptr;
    if (b) {
        s.paragraph = true;
        s.blockRow = row;
        s.text = b->translation;
        switch (b->translationStatus) {
        case Block::Translated:
        case Block::Skipped:
            s.status = QStringLiteral("done");
            return m_snippets.add(s);
        case Block::Translating:
        case Block::Queued:
            return m_snippets.add(s);   // in flight; syncBlockRow feeds it
        default:
            break;
        }
        if (!m_settings->isConfigured()) {
            s.status = QStringLiteral("failed");
            s.error = tr("LLM is not configured. Open Settings to add a "
                         "model and API key.");
            return m_snippets.add(s);
        }

        const int id = m_snippets.add(s);
        translateBlock(row);
        // translateBlock can resolve the row on the spot (formula-only
        // text is passed through as Skipped) or refuse it — so read the
        // row back rather than leaving the card spinning.
        syncBlockRow(row);
        const Block *after = m_model->blockAt(row);
        if (after && after->translationStatus == Block::NotTranslated) {
            m_snippets.setStatus(id, QStringLiteral("failed"),
                                 m_lastError.isEmpty()
                                     ? tr("Could not translate this paragraph.")
                                     : m_lastError);
        }
        return id;
    }

    // Ad-hoc path — the selection spans paragraphs, is too short to
    // place, or the paper hasn't been segmented yet.
    const int id = m_snippets.add(s);
    translateSelectionAdHoc(id, src);
    return id;
}

void TranslationService::translateSelectionAdHoc(int snippetId, const QString &text)
{
    // The cache key carries the model and language, so a lookup without
    // a configured model can only miss — no point trying first.
    if (!m_settings->isConfigured()) {
        m_snippets.setStatus(snippetId, QStringLiteral("failed"),
                             tr("LLM is not configured. Open Settings to add "
                                "a model and API key."));
        return;
    }

    const QString promptHash = TranslationCache::sha(systemPrompt());
    const QString model = m_settings->model();
    const QString lang  = m_settings->targetLang();

    const QString cached =
        m_cache.lookup(kSnippetBlockId, text, model, promptHash, lang);
    if (!cached.isEmpty()) {
        m_snippets.setText(snippetId, cached);
        m_snippets.setStatus(snippetId, QStringLiteral("done"));
        return;
    }

    refreshClient();
    if (!m_client) {
        m_snippets.setStatus(snippetId, QStringLiteral("failed"),
                             tr("LLM is not configured. Open Settings to add "
                                "a model and API key."));
        return;
    }

    LlmClient::Request req;
    req.system = systemPrompt();
    req.messages.append({QStringLiteral("user"), text});
    req.temperature = m_settings->temperature();
    req.stream = true;
    req.maxTokens = m_settings->maxTokens();

    LlmReply *reply = m_client->send(req);
    m_snippetReplies.insert(reply, snippetId);

    connect(reply, &LlmReply::chunkReceived, this,
            [this, reply](const QString &chunk) {
        const int id = m_snippetReplies.value(reply, -1);
        if (id >= 0)
            m_snippets.appendText(id, chunk);
    });
    connect(reply, &LlmReply::finished, this, [this, reply, text,
                                               model, promptHash, lang]() {
        const int id = m_snippetReplies.take(reply);
        reply->deleteLater();
        const SnippetModel::Snippet *s = id >= 0 ? m_snippets.byId(id) : nullptr;
        if (!s) return;   // card was closed while the reply was in flight
        if (s->text.trimmed().isEmpty()) {
            m_snippets.setStatus(id, QStringLiteral("failed"),
                                 tr("The model returned an empty translation."));
            return;
        }
        // Worth caching: people re-select the same sentence.
        if (!m_cache.paperId().isEmpty())
            m_cache.store(kSnippetBlockId, text, model, promptHash, lang, s->text);
        m_snippets.setStatus(id, QStringLiteral("done"));
    });
    connect(reply, &LlmReply::errorOccurred, this,
            [this, reply](const QString &message) {
        const int id = m_snippetReplies.take(reply);
        reply->deleteLater();
        if (id >= 0)
            m_snippets.setStatus(id, QStringLiteral("failed"), message);
    });
}

void TranslationService::setLastError(const QString &err)
{
    if (err == m_lastError) return;
    m_lastError = err;
    emit lastErrorChanged();
}
