#include "Settings.h"

#include "AnthropicClient.h"
#include "OpenAiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
constexpr auto kKeyProvider    = "llm/provider";
constexpr auto kKeyModel       = "llm/model";
constexpr auto kKeyBaseUrl     = "llm/baseUrl";
constexpr auto kKeyApiKey      = "llm/apiKey";
constexpr auto kKeyTemperature   = "llm/temperature";
constexpr auto kKeyMaxTokens     = "llm/maxTokens";
constexpr auto kKeyContextWindow = "llm/contextWindow";
constexpr auto kKeyToolBudget    = "chat/toolBudget";
constexpr auto kKeyTargetLang    = "translation/targetLang";
constexpr auto kKeySummaryPrompt     = "prompts/summary";
constexpr auto kKeyTranslationPrompt = "prompts/translation";
constexpr auto kKeyTocPrompt         = "prompts/toc";
constexpr auto kKeyVisionPrompt      = "prompts/vision";
constexpr auto kKeyChatPrompt           = "prompts/chat";
constexpr auto kKeyChatIncludePaperText = "chat/includePaperText";

QUrl defaultBaseUrlFor(const QString &providerLower)
{
    if (providerLower == QLatin1String("anthropic"))
        return QUrl(QStringLiteral("https://api.anthropic.com"));
    if (providerLower == QLatin1String("deepseek"))
        return QUrl(QStringLiteral("https://api.deepseek.com"));
    return QUrl(QStringLiteral("https://api.openai.com"));
}
} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    load();
}

Settings::~Settings() = default;

bool Settings::isConfigured() const
{
    return !m_apiKey.isEmpty() && !m_model.isEmpty() && !m_provider.isEmpty();
}

void Settings::setProvider(const QString &v)
{
    if (v == m_provider) return;
    m_provider = v;
    save();
    emit providerChanged();
    emit configurationChanged();
}

void Settings::setModel(const QString &v)
{
    if (v == m_model) return;
    m_model = v;
    save();
    emit modelChanged();
    emit configurationChanged();
}

void Settings::setBaseUrl(const QString &v)
{
    if (v == m_baseUrl) return;
    m_baseUrl = v;
    save();
    emit baseUrlChanged();
}

void Settings::setApiKey(const QString &v)
{
    if (v == m_apiKey) return;
    m_apiKey = v;
    save();
    emit apiKeyChanged();
    emit configurationChanged();
}

void Settings::setTemperature(double v)
{
    if (qFuzzyCompare(v, m_temperature)) return;
    m_temperature = v;
    save();
    emit temperatureChanged();
}

void Settings::setMaxTokens(int v)
{
    if (v < 1) v = 1;
    if (v == m_maxTokens) return;
    m_maxTokens = v;
    save();
    emit maxTokensChanged();
}

void Settings::setContextWindow(int v)
{
    if (v < 0) v = 0;
    if (v == m_contextWindow) return;
    m_contextWindow = v;
    save();
    emit contextWindowChanged();
}

void Settings::setToolBudget(int v)
{
    if (v < 1) v = 1;
    if (v > 100) v = 100;
    if (v == m_toolBudget) return;
    m_toolBudget = v;
    save();
    emit toolBudgetChanged();
}

void Settings::setTargetLang(const QString &v)
{
    if (v == m_targetLang) return;
    m_targetLang = v;
    save();
    emit targetLangChanged();
}

void Settings::setSummaryPrompt(const QString &v)
{
    if (v == m_summaryPrompt) return;
    m_summaryPrompt = v;
    save();
    emit summaryPromptChanged();
}

void Settings::setTranslationPrompt(const QString &v)
{
    if (v == m_translationPrompt) return;
    m_translationPrompt = v;
    save();
    emit translationPromptChanged();
}

void Settings::setTocPrompt(const QString &v)
{
    if (v == m_tocPrompt) return;
    m_tocPrompt = v;
    save();
    emit tocPromptChanged();
}

void Settings::setVisionPrompt(const QString &v)
{
    if (v == m_visionPrompt) return;
    m_visionPrompt = v;
    save();
    emit visionPromptChanged();
}

void Settings::setChatPrompt(const QString &v)
{
    if (v == m_chatPrompt) return;
    m_chatPrompt = v;
    save();
    emit chatPromptChanged();
}

void Settings::setChatIncludePaperText(bool v)
{
    if (v == m_chatIncludePaperText) return;
    m_chatIncludePaperText = v;
    save();
    emit chatIncludePaperTextChanged();
}

LlmClient *Settings::createClient(QObject *parent) const
{
    LlmClient *client = nullptr;
    const QString p = m_provider.toLower();
    if (p == QLatin1String("openai")
        || p == QLatin1String("openai-compatible")
        || p == QLatin1String("deepseek")) {
        client = new OpenAiClient(parent);
    } else {
        client = new AnthropicClient(parent);
    }
    client->setApiKey(m_apiKey);
    client->setModel(m_model);
    if (!m_baseUrl.isEmpty())
        client->setBaseUrl(QUrl(m_baseUrl));
    return client;
}

void Settings::fetchModels(const QString &provider,
                           const QString &baseUrl,
                           const QString &apiKey)
{
    if (m_modelsReply) {
        QNetworkReply *r = m_modelsReply;
        m_modelsReply.clear();
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }

    if (apiKey.isEmpty()) {
        setModelsError(tr("Enter an API key first."));
        return;
    }

    const QString providerLower = provider.toLower();
    QUrl base = baseUrl.trimmed().isEmpty()
                ? defaultBaseUrlFor(providerLower)
                : QUrl(baseUrl.trimmed());
    QString path = base.path();
    if (path.endsWith(QChar('/')))
        path.chop(1);
    base.setPath(path + QStringLiteral("/v1/models"));

    QNetworkRequest req(base);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    if (providerLower == QLatin1String("anthropic")) {
        req.setRawHeader("x-api-key", apiKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
    } else {
        req.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    }

    setModelsError({});
    setFetchingModels(true);

    QNetworkReply *reply = m_nam->get(req);
    m_modelsReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        setFetchingModels(false);
        if (m_modelsReply == reply)
            m_modelsReply.clear();

        const QByteArray body = reply->readAll();
        const auto err = reply->error();
        const QString netErr = reply->errorString();
        const int httpCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        if (err != QNetworkReply::NoError) {
            QString msg = QString::fromUtf8(body);
            if (msg.isEmpty())
                msg = netErr;
            if (httpCode > 0)
                msg = tr("HTTP %1: %2").arg(httpCode).arg(msg);
            setModelsError(msg);
            return;
        }

        QJsonParseError jerr{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &jerr);
        if (jerr.error != QJsonParseError::NoError) {
            setModelsError(tr("Invalid JSON: %1").arg(jerr.errorString()));
            return;
        }

        const QJsonArray data =
            doc.object().value(QStringLiteral("data")).toArray();
        QStringList ids;
        ids.reserve(data.size());
        for (const QJsonValue &v : data) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                ids.append(id);
        }
        ids.sort();
        if (ids.isEmpty()) {
            setModelsError(tr("Endpoint returned no models."));
            return;
        }
        setAvailableModels(std::move(ids));
    });
}

void Settings::setFetchingModels(bool v)
{
    if (v == m_fetchingModels) return;
    m_fetchingModels = v;
    emit fetchingModelsChanged();
}

void Settings::setModelsError(const QString &err)
{
    if (err == m_modelsError) return;
    m_modelsError = err;
    emit modelsErrorChanged();
}

void Settings::setAvailableModels(QStringList list)
{
    if (list == m_availableModels) return;
    m_availableModels = std::move(list);
    emit availableModelsChanged();
}

void Settings::load()
{
    m_provider      = m_qs.value(kKeyProvider,      QStringLiteral("anthropic")).toString();
    m_model         = m_qs.value(kKeyModel,         QStringLiteral("claude-opus-4-7")).toString();
    m_baseUrl       = m_qs.value(kKeyBaseUrl,       QString{}).toString();
    m_apiKey        = m_qs.value(kKeyApiKey,        QString{}).toString();
    m_temperature   = m_qs.value(kKeyTemperature,   0.2).toDouble();
    m_maxTokens     = m_qs.value(kKeyMaxTokens,     8192).toInt();
    m_contextWindow = m_qs.value(kKeyContextWindow, 128000).toInt();
    m_toolBudget    = qBound(1, m_qs.value(kKeyToolBudget, 30).toInt(), 100);
    m_targetLang    = m_qs.value(kKeyTargetLang,    QStringLiteral("zh-CN")).toString();
    m_summaryPrompt     = m_qs.value(kKeySummaryPrompt,     QString{}).toString();
    m_translationPrompt = m_qs.value(kKeyTranslationPrompt, QString{}).toString();
    m_tocPrompt         = m_qs.value(kKeyTocPrompt,         QString{}).toString();
    m_visionPrompt      = m_qs.value(kKeyVisionPrompt,      QString{}).toString();
    m_chatPrompt           = m_qs.value(kKeyChatPrompt,           QString{}).toString();
    m_chatIncludePaperText = m_qs.value(kKeyChatIncludePaperText, false).toBool();
}

void Settings::save()
{
    m_qs.setValue(kKeyProvider,      m_provider);
    m_qs.setValue(kKeyModel,         m_model);
    m_qs.setValue(kKeyBaseUrl,       m_baseUrl);
    m_qs.setValue(kKeyApiKey,        m_apiKey);
    m_qs.setValue(kKeyTemperature,   m_temperature);
    m_qs.setValue(kKeyMaxTokens,     m_maxTokens);
    m_qs.setValue(kKeyContextWindow, m_contextWindow);
    m_qs.setValue(kKeyToolBudget,    m_toolBudget);
    m_qs.setValue(kKeyTargetLang,    m_targetLang);
    m_qs.setValue(kKeySummaryPrompt,     m_summaryPrompt);
    m_qs.setValue(kKeyTranslationPrompt, m_translationPrompt);
    m_qs.setValue(kKeyTocPrompt,         m_tocPrompt);
    m_qs.setValue(kKeyVisionPrompt,      m_visionPrompt);
    m_qs.setValue(kKeyChatPrompt,           m_chatPrompt);
    m_qs.setValue(kKeyChatIncludePaperText, m_chatIncludePaperText);
    m_qs.sync();
}
