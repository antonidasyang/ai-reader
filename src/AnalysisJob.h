#pragma once

#include "Block.h"
#include "EvidenceIndex.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class LlmClient;
class StructuredCall;

// One paper, one quick interpretation, start to finish: render the
// paragraphs as citable text, ask for structured JSON against the digest
// schema, check every citation against the paragraphs it names, and hand
// back the annotated result.
//
// It knows nothing about the UI or the store, which is what lets the same
// code serve the pane the reader is looking at and a batch running over a
// hundred papers nobody has opened.
class QuickAnalysisJob : public QObject
{
    Q_OBJECT
public:
    struct Input {
        QString paperId;
        QString title;
        QVector<Block> blocks;
        QString lang;
        QString profileBlock;
        // How much of the paper may go into one prompt, in characters.
        int contextChars = 180000;
        int maxTokens = 8192;
    };

    // Starts on the next event-loop turn, so the caller can connect first.
    static QuickAnalysisJob *start(LlmClient *client, const Input &in,
                                   QObject *parent = nullptr);
    void abort();

    QString paperId() const { return m_in.paperId; }
    // Digest of the text that was actually sent -- half of the staleness
    // check, so re-segmenting a paper invalidates its interpretation.
    QString contentHash() const { return m_contentHash; }

signals:
    void succeeded(const QJsonObject &digest);
    void failed(const QString &error);

private:
    QuickAnalysisJob(const Input &in, QObject *parent);
    void run(LlmClient *client);
    void finishOk(const QJsonObject &digest);
    void finishErr(const QString &error);

    Input m_in;
    QString m_contentHash;
    bool m_truncated = false;
    int m_blocksIncluded = 0;
    QPointer<StructuredCall> m_call;
    bool m_done = false;
};

// One module of the deep read (§3.1 … §3.9). Same shape as the quick job --
// citable text in, checked JSON out -- but each module is its own call, which
// is what makes "regenerate just this part" (§5) a single request instead of
// re-reading the whole paper.
class DeepModuleJob : public QObject
{
    Q_OBJECT
public:
    struct Input {
        QString paperId;
        QString title;
        QString moduleId;
        QVector<Block> blocks;
        QString lang;
        QString profileBlock;
        // The quick interpretation, so a module does not restate it.
        QJsonObject digest;
        int contextChars = 180000;
        int maxTokens = 8192;
    };

    static DeepModuleJob *start(LlmClient *client, const Input &in,
                                QObject *parent = nullptr);
    void abort();

    QString moduleId() const { return m_in.moduleId; }
    QString contentHash() const { return m_contentHash; }

signals:
    void succeeded(const QString &moduleId, const QJsonObject &result);
    void failed(const QString &moduleId, const QString &error);

private:
    DeepModuleJob(const Input &in, QObject *parent);
    void run(LlmClient *client);
    void finishOk(const QJsonObject &result);
    void finishErr(const QString &error);

    Input m_in;
    QString m_contentHash;
    bool m_truncated = false;
    QPointer<StructuredCall> m_call;
    bool m_done = false;
};
