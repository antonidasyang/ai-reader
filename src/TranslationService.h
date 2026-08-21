#pragma once

#include "TranslationCache.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>

class BlockListModel;
class LlmClient;
class LlmReply;
class PaperController;
class Settings;

class TranslationService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy             READ busy             NOTIFY busyChanged)
    Q_PROPERTY(int  doneCount        READ doneCount        NOTIFY progressChanged)
    Q_PROPERTY(int  totalCount       READ totalCount       NOTIFY progressChanged)
    Q_PROPERTY(int  failedCount      READ failedCount      NOTIFY progressChanged)
    Q_PROPERTY(QString lastError     READ lastError        NOTIFY lastErrorChanged)
    Q_PROPERTY(QString defaultSystemPrompt READ defaultSystemPrompt CONSTANT)

    // ── Selection translation (the card that pops up over the PDF) ────
    // One at a time: a new request replaces whatever the card shows.
    Q_PROPERTY(QString snippetSource READ snippetSource NOTIFY snippetChanged)
    Q_PROPERTY(QString snippetText   READ snippetText   NOTIFY snippetChanged)
    // "idle" | "translating" | "done" | "failed" — mirrors the string
    // status names the block model already exposes to QML.
    Q_PROPERTY(QString snippetStatus READ snippetStatus NOTIFY snippetChanged)
    Q_PROPERTY(QString snippetError  READ snippetError  NOTIFY snippetChanged)
    // True when the selection was placed inside a known paragraph, so
    // the card shows that whole paragraph's translation (and shares the
    // right pane's row + on-disk cache) instead of an ad-hoc snippet.
    Q_PROPERTY(bool snippetIsParagraph READ snippetIsParagraph NOTIFY snippetChanged)
    Q_PROPERTY(int  snippetRow       READ snippetRow    NOTIFY snippetChanged)

public:
    TranslationService(Settings *settings,
                       PaperController *paper,
                       QObject *parent = nullptr);
    ~TranslationService() override;

    bool busy() const { return m_inflight > 0 || !m_pending.isEmpty(); }
    int doneCount()   const { return m_done; }
    int totalCount()  const { return m_total; }
    int failedCount() const { return m_failed; }
    QString lastError() const { return m_lastError; }
    QString defaultSystemPrompt() const;

    QString snippetSource() const { return m_snippetSource; }
    QString snippetText() const;
    QString snippetStatus() const { return m_snippetStatus; }
    QString snippetError() const { return m_snippetError; }
    bool snippetIsParagraph() const { return m_snippetRow >= 0; }
    int snippetRow() const { return m_snippetRow; }

public slots:
    void translateAll();
    void retryFailed();
    void cancel();
    // Translate a single paragraph (used by the BlockList right-click
    // menu). No-op when the row is invalid, the LLM isn't configured,
    // or the row is already translating.
    void translateBlock(int row);
    // Translate what the user selected in the PDF (right-click →
    // Translate). When the selection can be placed inside a paragraph
    // the app knows, that paragraph is translated instead — same row,
    // same cache, so the right pane fills in too and a second look is
    // free. Otherwise the selected text is translated on its own.
    // `page` is a hint used to disambiguate identical paragraphs.
    void translateSnippet(const QString &text, int page = -1);
    // Drop the card's contents and abort an in-flight ad-hoc request.
    // A paragraph translation already under way is left running — it
    // belongs to the right pane too.
    void clearSnippet();

signals:
    void busyChanged();
    void progressChanged();
    void lastErrorChanged();
    void snippetChanged();

private:
    void onPaperChanged();
    void rehydrateFromCache();
    void scheduleNext();
    void translateRow(int row);
    bool shouldSkip(const QString &text) const;
    QString systemPrompt() const;
    void setLastError(const QString &err);
    // Ensures m_client exists and carries the current settings.
    void refreshClient();
    // Row whose source text contains `text`, or -1. Whitespace is
    // ignored on both sides so PDF line breaks don't defeat the match.
    int findBlockRow(const QString &text, int page) const;
    void setSnippetFailed(const QString &message);
    void translateSnippetAdHoc(const QString &text);

    QPointer<Settings> m_settings;
    QPointer<PaperController> m_paper;
    QPointer<BlockListModel> m_model;
    QPointer<LlmClient> m_client;
    TranslationCache m_cache;

    QQueue<int> m_pending;
    QHash<LlmReply *, int> m_replyToRow;
    int m_inflight = 0;
    int m_maxInflight = 2;
    int m_done = 0;
    int m_failed = 0;
    int m_total = 0;
    QString m_lastError;

    // Selection card state. m_snippetRow >= 0 means the card mirrors a
    // block row (its text is read live off the model, so a stream shows
    // up in the card and the right pane at once); otherwise the text
    // streams into m_snippetText.
    QPointer<LlmReply> m_snippetReply;
    int m_snippetRow = -1;
    QString m_snippetSource;
    QString m_snippetText;
    QString m_snippetStatus = QStringLiteral("idle");
    QString m_snippetError;
};
