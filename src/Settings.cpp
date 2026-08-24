#include "Settings.h"

#include "AnthropicClient.h"
#include "OpenAiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

// qtkeychain is now built in-tree via FetchContent; its public header
// lives at the source root so we include it unprefixed (the system
// install would put it at qt6keychain/keychain.h, but we don't rely
// on a system install anymore).
#include <keychain.h>

using QKeychain::DeletePasswordJob;
using QKeychain::Error;
using QKeychain::Job;
using QKeychain::ReadPasswordJob;
using QKeychain::WritePasswordJob;

namespace {
constexpr auto kKeyProvider    = "llm/provider";
constexpr auto kKeyModel       = "llm/model";
constexpr auto kKeyBaseUrl     = "llm/baseUrl";
constexpr auto kKeyApiKey      = "llm/apiKey";  // legacy: for one-time migration
constexpr auto kKeyTemperature   = "llm/temperature";
constexpr auto kKeyMaxTokens     = "llm/maxTokens";
constexpr auto kKeyContextWindow = "llm/contextWindow";
constexpr auto kKeyToolBudget    = "chat/toolBudget";
constexpr auto kKeyTargetLang    = "translation/targetLang";
constexpr auto kKeyUiLanguage    = "ui/language";
constexpr auto kKeySummaryPrompt     = "prompts/summary";
constexpr auto kKeyTranslationPrompt = "prompts/translation";
constexpr auto kKeyTocPrompt         = "prompts/toc";
constexpr auto kKeyVisionPrompt      = "prompts/vision";
constexpr auto kKeyChatPrompt           = "prompts/chat";
constexpr auto kKeyChatIncludePaperText = "chat/includePaperText";
constexpr auto kKeyAutoCheckUpdates     = "updates/autoCheck";
constexpr auto kKeyUpdateManifestUrl    = "updates/manifestUrl";
constexpr auto kKeyGrobidEnabled        = "grobid/enabled";
constexpr auto kKeyGrobidUrl            = "grobid/url";
constexpr auto kKeyAutoSegment          = "paper/autoSegment";
constexpr auto kKeySharePaperData       = "paper/sharePaperData";
constexpr auto kKeyTranslationConcurrency = "translation/concurrency";
constexpr auto kKeyCrashReportsOptIn    = "privacy/crashReportsOptIn";
constexpr auto kKeyTocFontSize          = "fonts/toc";
constexpr auto kKeySummaryFontSize      = "fonts/summary";
constexpr auto kKeyParagraphFontSize    = "fonts/paragraph";
constexpr auto kKeyChatFontSize         = "fonts/chat";
constexpr auto kKeyChatSendKey          = "chat/sendKey";
constexpr auto kKeyChatInputHeight      = "chat/inputHeight";
constexpr auto kKeyTranslationProvider  = "translation/provider";
constexpr auto kKeyTranslationModel     = "translation/model";
constexpr auto kKeyTranslationBaseUrl   = "translation/baseUrl";
constexpr auto kKeyAnalysisMaxTokens    = "analysis/maxTokens";
constexpr auto kKeyAnalysisConcurrency  = "analysis/concurrency";
// Retired in 1.2.7: the per-feature override used to hang off interpretation
// and now hangs off translation. The old keys are cleared on load so a stale
// endpoint cannot quietly stay in the file.
constexpr auto kKeyRetiredAnalysisProvider = "analysis/provider";
constexpr auto kKeyRetiredAnalysisModel    = "analysis/model";
constexpr auto kKeyRetiredAnalysisBaseUrl  = "analysis/baseUrl";

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
    // If a legacy plaintext key was migrated out of QSettings during load(),
    // m_apiKey is already populated and we just need to push it into the
    // keychain. Otherwise an async read brings the secret back in.
    if (!m_apiKey.isEmpty())
        writeApiKeyToKeychain(m_apiKey);
    else
        readApiKeyFromKeychain();
    readTranslationKeyFromKeychain();
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
    emit apiKeyChanged();
    emit configurationChanged();
    if (m_apiKey.isEmpty()) {
        auto *job = new DeletePasswordJob(QStringLiteral("ai-reader"), this);
        job->setKey(QStringLiteral("llm/apiKey"));
        job->setInsecureFallback(true);
        connect(job, &Job::finished, this, [this](Job *j) {
            if (j->error() != QKeychain::NoError
                && j->error() != QKeychain::EntryNotFound)
                setKeychainStatus(tr("Keychain delete failed: %1").arg(j->errorString()));
            else
                setKeychainStatus(tr("API key removed from keyring."));
        });
        job->start();
    } else {
        writeApiKeyToKeychain(m_apiKey);
    }
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

void Settings::setUiLanguage(const QString &v)
{
    if (v == m_uiLanguage) return;
    m_uiLanguage = v;
    save();
    emit uiLanguageChanged();
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

void Settings::setChatSendKey(const QString &v)
{
    const QString norm = v == QLatin1String("ctrl-enter")
                             ? v
                             : QStringLiteral("enter");
    if (norm == m_chatSendKey) return;
    m_chatSendKey = norm;
    save();
    emit chatSendKeyChanged();
}

void Settings::setChatInputHeight(int v)
{
    v = qBound(36, v, 400);
    if (v == m_chatInputHeight) return;
    m_chatInputHeight = v;
    save();
    emit chatInputHeightChanged();
}

void Settings::setTranslationProvider(const QString &v)
{
    if (v == m_translationProvider) return;
    m_translationProvider = v;
    save();
    emit translationConfigChanged();
}

void Settings::setTranslationModel(const QString &v)
{
    if (v == m_translationModel) return;
    m_translationModel = v;
    save();
    emit translationConfigChanged();
}

void Settings::setTranslationBaseUrl(const QString &v)
{
    if (v == m_translationBaseUrl) return;
    m_translationBaseUrl = v;
    save();
    emit translationConfigChanged();
}

void Settings::setTranslationApiKey(const QString &v)
{
    if (v == m_translationApiKey) return;
    m_translationApiKey = v;
    emit translationConfigChanged();
    if (m_translationApiKey.isEmpty()) {
        auto *job = new DeletePasswordJob(QStringLiteral("ai-reader"), this);
        job->setKey(QStringLiteral("llm/translationApiKey"));
        job->setInsecureFallback(true);
        connect(job, &Job::finished, this, [this](Job *j) {
            if (j->error() != QKeychain::NoError
                && j->error() != QKeychain::EntryNotFound)
                setKeychainStatus(tr("Keychain delete failed: %1")
                                      .arg(j->errorString()));
        });
        job->start();
    } else {
        writeTranslationKeyToKeychain(m_translationApiKey);
    }
}

void Settings::setAnalysisMaxTokens(int v)
{
    v = qBound(512, v, 64000);
    if (v == m_analysisMaxTokens) return;
    m_analysisMaxTokens = v;
    save();
    emit analysisConfigChanged();
}

void Settings::setAnalysisConcurrency(int v)
{
    v = qBound(1, v, 8);
    if (v == m_analysisConcurrency) return;
    m_analysisConcurrency = v;
    save();
    emit analysisConfigChanged();
}

QString Settings::translationModelInUse() const
{
    return m_translationModel.isEmpty() ? m_model : m_translationModel;
}

QString Settings::translationProviderInUse() const
{
    return m_translationProvider.isEmpty() ? m_provider : m_translationProvider;
}

QString Settings::translationBaseUrlInUse() const
{
    return m_translationBaseUrl.isEmpty() ? m_baseUrl : m_translationBaseUrl;
}

QString Settings::translationApiKeyInUse() const
{
    // A key only travels with its own endpoint: reusing the main key against
    // someone else's base URL would leak it.
    if (!m_translationApiKey.isEmpty())
        return m_translationApiKey;
    if (m_translationBaseUrl.isEmpty() || m_translationBaseUrl == m_baseUrl)
        return m_apiKey;
    return {};
}

bool Settings::translationOverridden() const
{
    return !m_translationModel.isEmpty() || !m_translationProvider.isEmpty()
           || !m_translationBaseUrl.isEmpty() || !m_translationApiKey.isEmpty();
}

LlmClient *Settings::createTranslationClient(QObject *parent) const
{
    // Field by field: whatever the translation section leaves blank comes
    // from the main configuration, so pointing only the model name at a
    // cheaper model on the same gateway is a one-field change.
    const QString provider = translationProviderInUse();
    const QString model = translationModelInUse();
    const QString baseUrl = translationBaseUrlInUse();
    const QString apiKey = translationApiKeyInUse();

    LlmClient *client = nullptr;
    const QString p = provider.toLower();
    if (p == QLatin1String("openai")
        || p == QLatin1String("openai-compatible")
        || p == QLatin1String("deepseek")) {
        client = new OpenAiClient(parent);
    } else {
        client = new AnthropicClient(parent);
    }
    client->setApiKey(apiKey);
    client->setModel(model);
    if (!baseUrl.isEmpty())
        client->setBaseUrl(QUrl(baseUrl));
    return client;
}

void Settings::fetchModels(const QString &provider, const QString &baseUrl,
                           const QString &apiKey)
{
    fetchModelsInto(ModelSlot::Main, provider, baseUrl, apiKey);
}

void Settings::fetchTranslationModels(const QString &provider,
                                      const QString &baseUrl,
                                      const QString &apiKey)
{
    fetchModelsInto(ModelSlot::Translation, provider, baseUrl, apiKey);
}

void Settings::fetchModelsInto(ModelSlot slot, const QString &provider,
                               const QString &baseUrl, const QString &apiKey)
{
    const bool main = slot == ModelSlot::Main;
    QPointer<QNetworkReply> &pending =
        main ? m_modelsReply : m_translationModelsReply;
    auto setBusy = [this, main](bool v) {
        main ? setFetchingModels(v) : setFetchingTranslationModels(v);
    };
    auto setError = [this, main](const QString &e) {
        main ? setModelsError(e) : setTranslationModelsError(e);
    };
    auto setList = [this, main](QStringList l) {
        main ? setAvailableModels(std::move(l))
             : setAvailableTranslationModels(std::move(l));
    };

    if (pending) {
        QNetworkReply *r = pending;
        pending.clear();
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }

    if (apiKey.isEmpty()) {
        setError(tr("Enter an API key first."));
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

    setError({});
    setBusy(true);

    QNetworkReply *reply = m_nam->get(req);
    pending = reply;

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, main, setBusy, setError, setList]() {
        setBusy(false);
        QPointer<QNetworkReply> &slotReply =
            main ? m_modelsReply : m_translationModelsReply;
        if (slotReply == reply)
            slotReply.clear();

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
            setError(msg);
            return;
        }

        QJsonParseError jerr{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &jerr);
        if (jerr.error != QJsonParseError::NoError) {
            setError(tr("Invalid JSON: %1").arg(jerr.errorString()));
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
            setError(tr("Endpoint returned no models."));
            return;
        }
        setList(std::move(ids));
    });
}

void Settings::readApiKeyFromKeychain()
{
    auto *job = new ReadPasswordJob(QStringLiteral("ai-reader"), this);
    job->setKey(QStringLiteral("llm/apiKey"));
    job->setInsecureFallback(true);
    connect(job, &Job::finished, this, [this](Job *j) {
        auto *r = static_cast<ReadPasswordJob *>(j);
        const Error err = r->error();
        if (err == QKeychain::NoError) {
            const QString text = r->textData();
            if (text != m_apiKey) {
                m_apiKey = text;
                emit apiKeyChanged();
                emit configurationChanged();
            }
            setKeychainStatus(tr("API key loaded from keyring."));
        } else if (err == QKeychain::EntryNotFound) {
            setKeychainStatus(tr("No API key in keyring yet."));
        } else {
            setKeychainStatus(tr("Keychain read failed: %1").arg(r->errorString()));
        }
    });
    job->start();
}

void Settings::writeApiKeyToKeychain(const QString &value)
{
    auto *job = new WritePasswordJob(QStringLiteral("ai-reader"), this);
    job->setKey(QStringLiteral("llm/apiKey"));
    job->setTextData(value);
    job->setInsecureFallback(true);
    connect(job, &Job::finished, this, [this](Job *j) {
        if (j->error() == QKeychain::NoError)
            setKeychainStatus(tr("API key saved to keyring."));
        else
            setKeychainStatus(tr("Keychain write failed: %1").arg(j->errorString()));
    });
    job->start();
}

void Settings::readTranslationKeyFromKeychain()
{
    auto *job = new ReadPasswordJob(QStringLiteral("ai-reader"), this);
    job->setKey(QStringLiteral("llm/translationApiKey"));
    job->setInsecureFallback(true);
    connect(job, &Job::finished, this, [this](Job *j) {
        auto *r = static_cast<ReadPasswordJob *>(j);
        if (r->error() != QKeychain::NoError)
            return;                 // absent is the normal case
        const QString text = r->textData();
        if (text != m_translationApiKey) {
            m_translationApiKey = text;
            emit translationConfigChanged();
        }
    });
    job->start();
}

void Settings::writeTranslationKeyToKeychain(const QString &value)
{
    auto *job = new WritePasswordJob(QStringLiteral("ai-reader"), this);
    job->setKey(QStringLiteral("llm/translationApiKey"));
    job->setTextData(value);
    job->setInsecureFallback(true);
    connect(job, &Job::finished, this, [this](Job *j) {
        if (j->error() != QKeychain::NoError)
            setKeychainStatus(tr("Keychain write failed: %1")
                                  .arg(j->errorString()));
    });
    job->start();
}

void Settings::setKeychainStatus(const QString &s)
{
    if (s == m_keychainStatus) return;
    m_keychainStatus = s;
    emit keychainStatusChanged();
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

void Settings::setFetchingTranslationModels(bool v)
{
    if (v == m_fetchingTranslationModels) return;
    m_fetchingTranslationModels = v;
    emit fetchingTranslationModelsChanged();
}

void Settings::setTranslationModelsError(const QString &err)
{
    if (err == m_translationModelsError) return;
    m_translationModelsError = err;
    emit translationModelsErrorChanged();
}

void Settings::setAvailableTranslationModels(QStringList list)
{
    if (list == m_availableTranslationModels) return;
    m_availableTranslationModels = std::move(list);
    emit availableTranslationModelsChanged();
}

void Settings::load()
{
    m_provider      = m_qs.value(kKeyProvider,      QStringLiteral("anthropic")).toString();
    m_model         = m_qs.value(kKeyModel,         QStringLiteral("claude-opus-4-7")).toString();
    m_baseUrl       = m_qs.value(kKeyBaseUrl,       QString{}).toString();
    // Legacy plaintext API key — picked up here only so the constructor
    // can migrate it into the keychain. Erase from QSettings to avoid
    // leaving a plaintext copy on disk.
    m_apiKey        = m_qs.value(kKeyApiKey,        QString{}).toString();
    if (!m_apiKey.isEmpty())
        m_qs.remove(kKeyApiKey);
    m_temperature   = m_qs.value(kKeyTemperature,   0.2).toDouble();
    m_maxTokens     = m_qs.value(kKeyMaxTokens,     8192).toInt();
    m_contextWindow = m_qs.value(kKeyContextWindow, 128000).toInt();
    m_toolBudget    = qBound(1, m_qs.value(kKeyToolBudget, 30).toInt(), 100);
    m_targetLang    = m_qs.value(kKeyTargetLang,    QStringLiteral("zh-CN")).toString();
    m_uiLanguage    = m_qs.value(kKeyUiLanguage,    QString{}).toString();
    m_summaryPrompt     = m_qs.value(kKeySummaryPrompt,     QString{}).toString();
    m_translationPrompt = m_qs.value(kKeyTranslationPrompt, QString{}).toString();
    m_tocPrompt         = m_qs.value(kKeyTocPrompt,         QString{}).toString();
    m_visionPrompt      = m_qs.value(kKeyVisionPrompt,      QString{}).toString();
    m_chatPrompt           = m_qs.value(kKeyChatPrompt,           QString{}).toString();
    m_chatIncludePaperText = m_qs.value(kKeyChatIncludePaperText, false).toBool();
    m_autoCheckUpdates     = m_qs.value(kKeyAutoCheckUpdates,     true).toBool();
    m_updateManifestUrl    = m_qs.value(kKeyUpdateManifestUrl,    QString{}).toString();
    // Migration: old installs saved the retired GitHub manifest URL
    // (raw.githubusercontent is unreachable for most users in China,
    // so update checks silently never worked). Clear it so the value
    // falls through to UpdateChecker's server-side default.
    if (m_updateManifestUrl.contains(
            QLatin1String("raw.githubusercontent.com"))) {
        m_updateManifestUrl.clear();
        m_qs.setValue(kKeyUpdateManifestUrl, QString());
        m_qs.sync();
    }
    m_grobidEnabled        = m_qs.value(kKeyGrobidEnabled,        true).toBool();
    m_grobidUrl            = m_qs.value(kKeyGrobidUrl,
                                 QStringLiteral("https://aireader.d2ssoft.com/grobid")).toString();
    m_crashReportsOptIn    = m_qs.value(kKeyCrashReportsOptIn,    false).toBool();
    m_autoSegment          = m_qs.value(kKeyAutoSegment,          false).toBool();
    m_sharePaperData       = m_qs.value(kKeySharePaperData,       true).toBool();
    m_translationConcurrency =
        qBound(1, m_qs.value(kKeyTranslationConcurrency, 2).toInt(), 16);
    m_tocFontSize          = qBound(8, m_qs.value(kKeyTocFontSize,       12).toInt(), 32);
    m_summaryFontSize      = qBound(8, m_qs.value(kKeySummaryFontSize,   13).toInt(), 32);
    m_paragraphFontSize    = qBound(8, m_qs.value(kKeyParagraphFontSize, 12).toInt(), 32);
    m_chatFontSize         = qBound(8, m_qs.value(kKeyChatFontSize,      14).toInt(), 32);
    m_chatSendKey          = m_qs.value(kKeyChatSendKey,
                                        QStringLiteral("enter")).toString();
    if (m_chatSendKey != QLatin1String("ctrl-enter"))
        m_chatSendKey = QStringLiteral("enter");
    m_chatInputHeight      =
        qBound(36, m_qs.value(kKeyChatInputHeight, 88).toInt(), 400);
    m_translationProvider  = m_qs.value(kKeyTranslationProvider, QString{}).toString();
    m_translationModel     = m_qs.value(kKeyTranslationModel,    QString{}).toString();
    m_translationBaseUrl   = m_qs.value(kKeyTranslationBaseUrl,  QString{}).toString();
    // Drop the retired interpretation override rather than leaving a model
    // name in the file that nothing reads any more.
    for (const char *k : {kKeyRetiredAnalysisProvider, kKeyRetiredAnalysisModel,
                          kKeyRetiredAnalysisBaseUrl}) {
        if (m_qs.contains(QString::fromLatin1(k)))
            m_qs.remove(QString::fromLatin1(k));
    }
    m_analysisMaxTokens    =
        qBound(512, m_qs.value(kKeyAnalysisMaxTokens, 8192).toInt(), 64000);
    m_analysisConcurrency  =
        qBound(1, m_qs.value(kKeyAnalysisConcurrency, 2).toInt(), 8);
}

void Settings::save()
{
    m_qs.setValue(kKeyProvider,      m_provider);
    m_qs.setValue(kKeyModel,         m_model);
    m_qs.setValue(kKeyBaseUrl,       m_baseUrl);
    // apiKey lives in the OS keychain, not QSettings.
    m_qs.setValue(kKeyTemperature,   m_temperature);
    m_qs.setValue(kKeyMaxTokens,     m_maxTokens);
    m_qs.setValue(kKeyContextWindow, m_contextWindow);
    m_qs.setValue(kKeyToolBudget,    m_toolBudget);
    m_qs.setValue(kKeyTargetLang,    m_targetLang);
    m_qs.setValue(kKeyUiLanguage,    m_uiLanguage);
    m_qs.setValue(kKeySummaryPrompt,     m_summaryPrompt);
    m_qs.setValue(kKeyTranslationPrompt, m_translationPrompt);
    m_qs.setValue(kKeyTocPrompt,         m_tocPrompt);
    m_qs.setValue(kKeyVisionPrompt,      m_visionPrompt);
    m_qs.setValue(kKeyChatPrompt,           m_chatPrompt);
    m_qs.setValue(kKeyChatIncludePaperText, m_chatIncludePaperText);
    m_qs.setValue(kKeyAutoCheckUpdates,     m_autoCheckUpdates);
    m_qs.setValue(kKeyUpdateManifestUrl,    m_updateManifestUrl);
    m_qs.setValue(kKeyGrobidEnabled,        m_grobidEnabled);
    m_qs.setValue(kKeyGrobidUrl,            m_grobidUrl);
    m_qs.setValue(kKeyCrashReportsOptIn,    m_crashReportsOptIn);
    m_qs.setValue(kKeyAutoSegment,          m_autoSegment);
    m_qs.setValue(kKeySharePaperData,       m_sharePaperData);
    m_qs.setValue(kKeyTranslationConcurrency, m_translationConcurrency);
    m_qs.setValue(kKeyTocFontSize,          m_tocFontSize);
    m_qs.setValue(kKeySummaryFontSize,      m_summaryFontSize);
    m_qs.setValue(kKeyParagraphFontSize,    m_paragraphFontSize);
    m_qs.setValue(kKeyChatFontSize,         m_chatFontSize);
    m_qs.setValue(kKeyChatSendKey,          m_chatSendKey);
    m_qs.setValue(kKeyChatInputHeight,      m_chatInputHeight);
    m_qs.setValue(kKeyTranslationProvider,  m_translationProvider);
    m_qs.setValue(kKeyTranslationModel,     m_translationModel);
    m_qs.setValue(kKeyTranslationBaseUrl,   m_translationBaseUrl);
    m_qs.setValue(kKeyAnalysisMaxTokens,    m_analysisMaxTokens);
    m_qs.setValue(kKeyAnalysisConcurrency,  m_analysisConcurrency);
    m_qs.sync();
}

// ── Updates / privacy setters ──────────────────────────────────────

void Settings::setAutoCheckUpdates(bool v)
{
    if (v == m_autoCheckUpdates) return;
    m_autoCheckUpdates = v;
    save();
    emit autoCheckUpdatesChanged();
}

void Settings::setUpdateManifestUrl(const QString &v)
{
    if (v == m_updateManifestUrl) return;
    m_updateManifestUrl = v;
    save();
    emit updateManifestUrlChanged();
}

void Settings::setCrashReportsOptIn(bool v)
{
    if (v == m_crashReportsOptIn) return;
    m_crashReportsOptIn = v;
    save();
    emit crashReportsOptInChanged();
}

void Settings::setGrobidEnabled(bool v)
{
    if (v == m_grobidEnabled) return;
    m_grobidEnabled = v;
    save();
    emit grobidEnabledChanged();
}

void Settings::setGrobidUrl(const QString &v)
{
    if (v == m_grobidUrl) return;
    m_grobidUrl = v;
    save();
    emit grobidUrlChanged();
}

void Settings::setAutoSegment(bool v)
{
    if (v == m_autoSegment) return;
    m_autoSegment = v;
    save();
    emit autoSegmentChanged();
}

void Settings::setSharePaperData(bool v)
{
    if (v == m_sharePaperData) return;
    m_sharePaperData = v;
    save();
    emit sharePaperDataChanged();
}

void Settings::setTranslationConcurrency(int v)
{
    v = qBound(1, v, 16);
    if (v == m_translationConcurrency) return;
    m_translationConcurrency = v;
    save();
    emit translationConcurrencyChanged();
}

void Settings::setTocFontSize(int v)
{
    v = qBound(8, v, 32);
    if (v == m_tocFontSize) return;
    m_tocFontSize = v;
    save();
    emit tocFontSizeChanged();
}

void Settings::setSummaryFontSize(int v)
{
    v = qBound(8, v, 32);
    if (v == m_summaryFontSize) return;
    m_summaryFontSize = v;
    save();
    emit summaryFontSizeChanged();
}

void Settings::setParagraphFontSize(int v)
{
    v = qBound(8, v, 32);
    if (v == m_paragraphFontSize) return;
    m_paragraphFontSize = v;
    save();
    emit paragraphFontSizeChanged();
}

void Settings::setChatFontSize(int v)
{
    v = qBound(8, v, 32);
    if (v == m_chatFontSize) return;
    m_chatFontSize = v;
    save();
    emit chatFontSizeChanged();
}
