#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVector>

class QNetworkReply;

class LlmReply : public QObject
{
    Q_OBJECT
public:
    explicit LlmReply(QObject *parent = nullptr);
    ~LlmReply() override;

    QString text() const { return m_text; }
    bool isFinished() const { return m_finished; }
    QString errorString() const { return m_error; }

    // Used by client implementations.
    void appendChunk(const QString &chunk);
    void markFinished();
    void setError(const QString &message);
    void attachNetworkReply(QNetworkReply *reply);

public slots:
    void abort();

signals:
    void chunkReceived(const QString &chunk);
    void finished();
    void errorOccurred(const QString &message);

private:
    QPointer<QNetworkReply> m_networkReply;
    QString m_text;
    QString m_error;
    bool m_finished = false;
};

class LlmClient : public QObject
{
    Q_OBJECT
public:
    struct Message {
        QString role;     // "user" / "assistant"
        QString content;
    };

    struct Request {
        QString system;
        QVector<Message> messages;
        double temperature = 0.2;
        int maxTokens = 4096;
        bool stream = true;
    };

    explicit LlmClient(QObject *parent = nullptr);
    ~LlmClient() override;

    virtual LlmReply *send(const Request &req) = 0;

    QString apiKey() const { return m_apiKey; }
    QUrl baseUrl() const { return m_baseUrl; }
    QString model() const { return m_model; }

    void setApiKey(const QString &key) { m_apiKey = key; }
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }
    void setModel(const QString &model) { m_model = model; }

protected:
    QNetworkAccessManager *m_nam;
    QString m_apiKey;
    QUrl m_baseUrl;
    QString m_model;
};
