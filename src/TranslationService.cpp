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
        // would leave the selection card mirroring somebody else's text.
        connect(m_model, &BlockListModel::blocksMutated,
                this, &TranslationService::clearSnippet);
    }
}

TranslationService::~TranslationService() = default;

void TranslationService::onPaperChanged()
{
    cancel();
    clearSnippet();
    m_done = 0;
    m_failed = 0;
    m_total = 0;
    emit progressChanged();

    // Switch the cache to the new paper and rehydrate any matching rows
    // straight into the BlockListModel — translations the user already
    // paid for show up instantly without another API call.
    m_cache.setPaperId(m_paper ? m_paper->paperId() : QString());
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
            // The selection card reads this row's text live.
            if (r == m_snippetRow)
                emit snippetChanged();
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
        if (r >= 0 && r == m_snippetRow) {
            const Block *cur = m_model ? m_model->blockAt(r) : nullptr;
            if (cur && cur->translationStatus == Block::Failed)
                setSnippetFailed(cur->translationError);
            else
                m_snippetStatus = QStringLiteral("done");
            emit snippetChanged();
        }
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
        if (r >= 0 && m_model)
            m_model->setTranslationStatus(r, Block::Failed, message);
        if (r >= 0 && r == m_snippetRow)
            setSnippetFailed(message);
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

QString TranslationService::snippetText() const
{
    // Paragraph mode reads straight off the model, so a stream lands in
    // the card and the right pane together and nothing can drift.
    if (m_snippetRow >= 0 && m_model) {
        if (const Block *b = m_model->blockAt(m_snippetRow))
            return b->translation;
    }
    return m_snippetText;
}

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

void TranslationService::clearSnippet()
{
    if (m_snippetReply) {
        m_snippetReply->abort();
        m_snippetReply.clear();
    }
    const bool wasIdle = m_snippetStatus == QLatin1String("idle")
                         && m_snippetRow < 0 && m_snippetSource.isEmpty();
    m_snippetRow = -1;
    m_snippetSource.clear();
    m_snippetText.clear();
    m_snippetError.clear();
    m_snippetStatus = QStringLiteral("idle");
    if (!wasIdle)
        emit snippetChanged();
}

void TranslationService::setSnippetFailed(const QString &message)
{
    m_snippetStatus = QStringLiteral("failed");
    m_snippetError = message;
    emit snippetChanged();
}

void TranslationService::translateSnippet(const QString &text, int page)
{
    const QString src = text.trimmed();
    clearSnippet();
    if (src.isEmpty() || !m_settings)
        return;

    m_snippetSource = src;

    // Paragraph path — hand the work to the normal per-block pipeline so
    // the result is cached and the right pane fills in as well. Work the
    // model already did is shown whether or not an LLM is configured.
    const int row = findBlockRow(src, page);
    if (row >= 0) {
        const Block *b = m_model ? m_model->blockAt(row) : nullptr;
        if (b) {
            m_snippetRow = row;
            switch (b->translationStatus) {
            case Block::Translated:
            case Block::Skipped:
                m_snippetStatus = QStringLiteral("done");
                emit snippetChanged();
                return;
            case Block::Translating:
            case Block::Queued:
                m_snippetStatus = QStringLiteral("translating");
                emit snippetChanged();
                return;   // already in flight; translateRow mirrors it
            default:
                break;
            }
            if (!m_settings->isConfigured()) {
                setSnippetFailed(tr("LLM is not configured. Open Settings to "
                                    "add a model and API key."));
                return;
            }
            m_snippetStatus = QStringLiteral("translating");
            translateBlock(row);
            // translateBlock can resolve the row on the spot (formula-
            // only text is passed through as Skipped) or refuse it — so
            // read the row back rather than leaving the card spinning.
            const Block *after = m_model->blockAt(row);
            const Block::TranslationStatus st =
                after ? after->translationStatus : Block::NotTranslated;
            if (st == Block::Translated || st == Block::Skipped)
                m_snippetStatus = QStringLiteral("done");
            else if (st == Block::NotTranslated || st == Block::Failed)
                setSnippetFailed(m_lastError.isEmpty()
                                     ? tr("Could not translate this paragraph.")
                                     : m_lastError);
            emit snippetChanged();
            return;
        }
    }

    // Ad-hoc path — the selection spans paragraphs, is too short to
    // place, or the paper hasn't been segmented yet.
    translateSnippetAdHoc(src);
}

void TranslationService::translateSnippetAdHoc(const QString &text)
{
    // The cache key carries the model and language, so a lookup without
    // a configured model can only miss — no point trying first.
    if (!m_settings->isConfigured()) {
        setSnippetFailed(
            tr("LLM is not configured. Open Settings to add a model and API key."));
        return;
    }

    const QString promptHash = TranslationCache::sha(systemPrompt());
    const QString model = m_settings->model();
    const QString lang  = m_settings->targetLang();

    const QString cached =
        m_cache.lookup(kSnippetBlockId, text, model, promptHash, lang);
    if (!cached.isEmpty()) {
        m_snippetText = cached;
        m_snippetStatus = QStringLiteral("done");
        emit snippetChanged();
        return;
    }

    refreshClient();
    if (!m_client) {
        setSnippetFailed(tr("LLM is not configured. Open Settings to add a model and API key."));
        return;
    }

    LlmClient::Request req;
    req.system = systemPrompt();
    req.messages.append({QStringLiteral("user"), text});
    req.temperature = m_settings->temperature();
    req.stream = true;
    req.maxTokens = m_settings->maxTokens();

    m_snippetStatus = QStringLiteral("translating");
    emit snippetChanged();

    LlmReply *reply = m_client->send(req);
    m_snippetReply = reply;

    connect(reply, &LlmReply::chunkReceived, this,
            [this, reply](const QString &chunk) {
        if (m_snippetReply != reply) return;   // superseded
        m_snippetText += chunk;
        emit snippetChanged();
    });
    connect(reply, &LlmReply::finished, this, [this, reply, text,
                                               model, promptHash, lang]() {
        reply->deleteLater();
        if (m_snippetReply != reply) return;
        m_snippetReply.clear();
        if (m_snippetText.trimmed().isEmpty()) {
            setSnippetFailed(tr("The model returned an empty translation."));
            return;
        }
        m_snippetStatus = QStringLiteral("done");
        // Worth caching: people re-select the same sentence.
        if (!m_cache.paperId().isEmpty()) {
            m_cache.store(kSnippetBlockId, text, model, promptHash, lang,
                          m_snippetText);
        }
        emit snippetChanged();
    });
    connect(reply, &LlmReply::errorOccurred, this,
            [this, reply](const QString &message) {
        reply->deleteLater();
        if (m_snippetReply != reply) return;
        m_snippetReply.clear();
        setSnippetFailed(message);
    });
}

void TranslationService::setLastError(const QString &err)
{
    if (err == m_lastError) return;
    m_lastError = err;
    emit lastErrorChanged();
}
