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
}

TranslationService::~TranslationService() = default;

void TranslationService::onPaperChanged()
{
    cancel();
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

    if (!m_client)
        m_client = m_settings->createClient(this);
    else {
        // Refresh in case settings changed.
        m_client->setApiKey(m_settings->apiKey());
        m_client->setModel(m_settings->model());
        if (!m_settings->baseUrl().isEmpty())
            m_client->setBaseUrl(QUrl(m_settings->baseUrl()));
    }

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
        if (r >= 0 && m_model)
            m_model->appendTranslationChunk(r, chunk);
    });
    connect(reply, &LlmReply::finished, this, [this, reply]() {
        const int r = m_replyToRow.take(reply);
        --m_inflight;
        if (r >= 0 && m_model && m_model->blockAt(r)
            && m_model->blockAt(r)->translationStatus == Block::Translating) {
            m_model->setTranslationStatus(r, Block::Translated);
            ++m_done;

            // Persist to disk so reopening this paper restores the
            // translation without another API round-trip.
            const Block *b = m_model->blockAt(r);
            if (b && m_settings && !m_cache.paperId().isEmpty()) {
                m_cache.store(b->id, b->text,
                              m_settings->model(),
                              TranslationCache::sha(systemPrompt()),
                              m_settings->targetLang(),
                              b->translation);
            }
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
        ++m_failed;
        setLastError(message);
        emit progressChanged();
        reply->deleteLater();
        scheduleNext();
        if (m_inflight == 0 && m_pending.isEmpty())
            emit busyChanged();
    });
}

void TranslationService::setLastError(const QString &err)
{
    if (err == m_lastError) return;
    m_lastError = err;
    emit lastErrorChanged();
}
