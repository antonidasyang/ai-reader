#include "Settings.h"

#include "AnthropicClient.h"
#include "OpenAiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

#include <limits>
#include <optional>

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
constexpr auto kKeyRemoteMode           = "ui/remoteMode";
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

// Factory defaults, named because two places need the same numbers: load(),
// and defaultAccountSettings() -- which is how the account sync tells a
// machine the user has configured apart from one nobody has touched. A
// default that drifted between the two would make a fresh install look
// configured and let it win a first-pull merge it should have lost.
constexpr auto kDefProvider              = "anthropic";
constexpr auto kDefModel                 = "claude-opus-4-7";
constexpr double kDefTemperature         = 0.2;
constexpr int  kDefMaxTokens             = 8192;
constexpr int  kDefContextWindow         = 128000;
constexpr int  kDefToolBudget            = 30;
constexpr auto kDefTargetLang            = "zh-CN";
constexpr bool kDefChatIncludePaperText  = false;
constexpr bool kDefAutoCheckUpdates      = true;
constexpr bool kDefGrobidEnabled         = true;
constexpr bool kDefAutoSegment           = false;
constexpr bool kDefSharePaperData        = true;
constexpr int  kDefTranslationConcurrency = 2;
constexpr int  kDefTocFontSize           = 12;
constexpr int  kDefSummaryFontSize       = 13;
constexpr int  kDefParagraphFontSize     = 12;
constexpr int  kDefChatFontSize          = 14;
constexpr auto kDefChatSendKey           = "enter";
constexpr int  kDefAnalysisMaxTokens     = 8192;
constexpr int  kDefAnalysisConcurrency   = 2;

// ── Reading an account payload ─────────────────────────────────────────
// Everything below treats the incoming object as untrusted: it was written
// by another machine, possibly by an older or newer build of this app, and
// nothing guarantees a key still holds the type it held when it was saved.
// A value we cannot read as the type we need is left out entirely, which
// means the local value survives -- always the safer of the two outcomes,
// since the local one at least came from this user on this machine.

std::optional<QString> jsonString(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QString::fromLatin1(key));
    if (!v.isString())
        return std::nullopt;
    return v.toString();
}

std::optional<double> jsonDouble(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QString::fromLatin1(key));
    if (v.isDouble())
        return v.toDouble();
    // A number that made the round trip through a string (some JSON
    // producers stringify everything) is still unambiguously a number.
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
            return d;
    }
    return std::nullopt;
}

std::optional<int> jsonInt(const QJsonObject &o, const char *key)
{
    const auto d = jsonDouble(o, key);
    if (!d)
        return std::nullopt;
    // Out of int range is not a number we can use for anything here; the
    // setters would clamp it, but rounding it first would be a lie.
    if (*d < double(std::numeric_limits<int>::min())
        || *d > double(std::numeric_limits<int>::max()))
        return std::nullopt;
    return int(qRound(*d));
}

std::optional<bool> jsonBool(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QString::fromLatin1(key));
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble() != 0.0;
    if (v.isString()) {
        const QString t = v.toString().trimmed().toLower();
        if (t == QLatin1String("true") || t == QLatin1String("1"))
            return true;
        if (t == QLatin1String("false") || t == QLatin1String("0"))
            return false;
    }
    return std::nullopt;
}

// The four provider codes the UI offers. An unknown one would leave the app
// silently talking to the wrong client class, so it is dropped instead.
bool isKnownProvider(const QString &p)
{
    return p == QLatin1String("anthropic") || p == QLatin1String("openai")
           || p == QLatin1String("deepseek")
           || p == QLatin1String("openai-compatible");
}

// A Base URL is only worth storing if a request could actually be sent to
// it. Anything else (a file:// path, a bare word, a URL with no host) turns
// every call into an obscure network error long after the sync that
// delivered it.
bool isUsableEndpoint(const QString &url)
{
    const QUrl u(url.trimmed(), QUrl::StrictMode);
    if (!u.isValid() || u.host().isEmpty())
        return false;
    const QString scheme = u.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

// Empty (follow the system) or something shaped like a locale code. We
// deliberately do not check it against the .qm files we ship: a newer build
// may know a language this one does not, and dropping the code here would
// quietly reset that machine's choice on every sync.
bool isUsableLanguageCode(const QString &code)
{
    if (code.isEmpty())
        return true;
    static const QRegularExpression re(
        QStringLiteral("^[A-Za-z]{2,3}([_-][A-Za-z0-9]{2,8})?$"));
    return re.match(code).hasMatch();
}

} // namespace

QString Settings::officialBaseUrl(const QString &provider)
{
    const QString p = provider.toLower();
    if (p == QLatin1String("anthropic"))
        return QStringLiteral("https://api.anthropic.com");
    if (p == QLatin1String("deepseek"))
        return QStringLiteral("https://api.deepseek.com");
    if (p == QLatin1String("openai"))
        return QStringLiteral("https://api.openai.com");
    return {};                     // openai-compatible has no home address
}

bool Settings::providerTakesCustomUrl(const QString &provider)
{
    return officialBaseUrl(provider).isEmpty();
}

QString Settings::resolveBaseUrl(const QString &provider,
                                 const QString &customUrl)
{
    if (providerTakesCustomUrl(provider))
        return customUrl.trimmed();
    return officialBaseUrl(provider);
}

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
    if (m_apiKey.isEmpty() || m_model.isEmpty() || m_provider.isEmpty())
        return false;
    // "openai-compatible" means "an endpoint I will name". Without the name
    // the client would fall back to its built-in default and talk to the
    // wrong server -- which is worse than saying it is not configured.
    if (providerTakesCustomUrl(m_provider) && m_baseUrl.trimmed().isEmpty())
        return false;
    return true;
}

void Settings::setProvider(const QString &v)
{
    if (v == m_provider) return;
    m_provider = v;
    ++m_configRevision;
    save();
    emit providerChanged();
    emit configurationChanged();
}

void Settings::setModel(const QString &v)
{
    if (v == m_model) return;
    m_model = v;
    ++m_configRevision;
    save();
    emit modelChanged();
    emit configurationChanged();
}

void Settings::setBaseUrl(const QString &v)
{
    if (v == m_baseUrl) return;
    m_baseUrl = v;
    ++m_configRevision;
    save();
    emit baseUrlChanged();
}

void Settings::setApiKey(const QString &v)
{
    if (v == m_apiKey) return;
    m_apiKey = v;
    ++m_configRevision;
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
    const QString base = resolveBaseUrl(m_provider, m_baseUrl);
    if (!base.isEmpty())
        client->setBaseUrl(QUrl(base));
    return client;
}

void Settings::setRemoteMode(const QString &v)
{
    const QString norm = (v == QLatin1String("on") || v == QLatin1String("off"))
                             ? v
                             : QStringLiteral("auto");
    if (norm == m_remoteMode) return;
    m_remoteMode = norm;
    save();
    emit remoteModeChanged();
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
    ++m_configRevision;
    save();
    emit translationConfigChanged();
}

void Settings::setTranslationModel(const QString &v)
{
    if (v == m_translationModel) return;
    m_translationModel = v;
    ++m_configRevision;
    save();
    emit translationConfigChanged();
}

void Settings::setTranslationBaseUrl(const QString &v)
{
    if (v == m_translationBaseUrl) return;
    m_translationBaseUrl = v;
    ++m_configRevision;
    save();
    emit translationConfigChanged();
}

void Settings::setTranslationApiKey(const QString &v)
{
    if (v == m_translationApiKey) return;
    m_translationApiKey = v;
    ++m_configRevision;
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
    const QString provider = translationProviderInUse();
    // Only a provider that takes an address of its own can be pointed at
    // one; for the rest the endpoint follows the provider.
    if (!providerTakesCustomUrl(provider))
        return officialBaseUrl(provider);
    return m_translationBaseUrl.isEmpty() ? m_baseUrl.trimmed()
                                          : m_translationBaseUrl.trimmed();
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
    const QString resolved = resolveBaseUrl(provider, baseUrl);
    if (resolved.isEmpty()) {
        setError(tr("Enter the endpoint's Base URL first."));
        setBusy(false);
        return;
    }
    QUrl base(resolved);
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
                ++m_configRevision;
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
            ++m_configRevision;
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
    m_provider      = m_qs.value(kKeyProvider,      QString::fromLatin1(kDefProvider)).toString();
    m_model         = m_qs.value(kKeyModel,         QString::fromLatin1(kDefModel)).toString();
    m_baseUrl       = m_qs.value(kKeyBaseUrl,       QString{}).toString();
    // Legacy plaintext API key — picked up here only so the constructor
    // can migrate it into the keychain. Erase from QSettings to avoid
    // leaving a plaintext copy on disk.
    m_apiKey        = m_qs.value(kKeyApiKey,        QString{}).toString();
    if (!m_apiKey.isEmpty())
        m_qs.remove(kKeyApiKey);
    m_temperature   = m_qs.value(kKeyTemperature,   kDefTemperature).toDouble();
    m_maxTokens     = m_qs.value(kKeyMaxTokens,     kDefMaxTokens).toInt();
    m_contextWindow = m_qs.value(kKeyContextWindow, kDefContextWindow).toInt();
    m_toolBudget    = qBound(1, m_qs.value(kKeyToolBudget, kDefToolBudget).toInt(), 100);
    m_targetLang    = m_qs.value(kKeyTargetLang,    QString::fromLatin1(kDefTargetLang)).toString();
    m_uiLanguage    = m_qs.value(kKeyUiLanguage,    QString{}).toString();
    m_translationPrompt = m_qs.value(kKeyTranslationPrompt, QString{}).toString();
    m_tocPrompt         = m_qs.value(kKeyTocPrompt,         QString{}).toString();
    m_visionPrompt      = m_qs.value(kKeyVisionPrompt,      QString{}).toString();
    m_chatPrompt           = m_qs.value(kKeyChatPrompt,           QString{}).toString();
    m_chatIncludePaperText = m_qs.value(kKeyChatIncludePaperText, kDefChatIncludePaperText).toBool();
    m_autoCheckUpdates     = m_qs.value(kKeyAutoCheckUpdates,     kDefAutoCheckUpdates).toBool();
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
    m_grobidEnabled        = m_qs.value(kKeyGrobidEnabled,        kDefGrobidEnabled).toBool();
    m_grobidUrl            = m_qs.value(kKeyGrobidUrl,
                                 QStringLiteral("https://aireader.d2ssoft.com/grobid")).toString();
    m_crashReportsOptIn    = m_qs.value(kKeyCrashReportsOptIn,    false).toBool();
    m_autoSegment          = m_qs.value(kKeyAutoSegment,          kDefAutoSegment).toBool();
    m_sharePaperData       = m_qs.value(kKeySharePaperData,       kDefSharePaperData).toBool();
    m_translationConcurrency =
        qBound(1, m_qs.value(kKeyTranslationConcurrency, kDefTranslationConcurrency).toInt(), 16);
    m_tocFontSize          = qBound(8, m_qs.value(kKeyTocFontSize,       kDefTocFontSize).toInt(), 32);
    m_summaryFontSize      = qBound(8, m_qs.value(kKeySummaryFontSize,   kDefSummaryFontSize).toInt(), 32);
    m_paragraphFontSize    = qBound(8, m_qs.value(kKeyParagraphFontSize, kDefParagraphFontSize).toInt(), 32);
    m_chatFontSize         = qBound(8, m_qs.value(kKeyChatFontSize,      kDefChatFontSize).toInt(), 32);
    m_remoteMode           = m_qs.value(kKeyRemoteMode,
                                        QStringLiteral("auto")).toString();
    if (m_remoteMode != QLatin1String("on") && m_remoteMode != QLatin1String("off"))
        m_remoteMode = QStringLiteral("auto");
    m_chatSendKey          = m_qs.value(kKeyChatSendKey,
                                        QString::fromLatin1(kDefChatSendKey)).toString();
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
        qBound(512, m_qs.value(kKeyAnalysisMaxTokens, kDefAnalysisMaxTokens).toInt(), 64000);
    m_analysisConcurrency  =
        qBound(1, m_qs.value(kKeyAnalysisConcurrency, kDefAnalysisConcurrency).toInt(), 8);
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
    m_qs.setValue(kKeyRemoteMode,           m_remoteMode);
    m_qs.setValue(kKeyChatSendKey,          m_chatSendKey);
    m_qs.setValue(kKeyChatInputHeight,      m_chatInputHeight);
    m_qs.setValue(kKeyTranslationProvider,  m_translationProvider);
    m_qs.setValue(kKeyTranslationModel,     m_translationModel);
    m_qs.setValue(kKeyTranslationBaseUrl,   m_translationBaseUrl);
    m_qs.setValue(kKeyAnalysisMaxTokens,    m_analysisMaxTokens);
    m_qs.setValue(kKeyAnalysisConcurrency,  m_analysisConcurrency);
    m_qs.sync();
}

// ── Settings that follow the user's account ────────────────────────
//
// Three functions, one list. accountSettingKeys() names the keys; export
// and import move them in and out of a JSON object; defaultAccountSettings()
// answers "what would a machine nobody has configured say?". Everything
// about which settings are account-class and which are local lives here, so
// UserPrefsSync never has to know a single QSettings key by name.

const QStringList &Settings::accountSettingKeys()
{
    // Grouped the way the settings dialog groups them. The order carries no
    // meaning for the protocol -- the payload is a JSON object -- but a
    // stable one keeps a diff of two accounts readable.
    static const QStringList keys = {
        QString::fromLatin1(kKeyProvider),
        QString::fromLatin1(kKeyModel),
        QString::fromLatin1(kKeyBaseUrl),
        QString::fromLatin1(kKeyTemperature),
        QString::fromLatin1(kKeyMaxTokens),
        QString::fromLatin1(kKeyContextWindow),

        QString::fromLatin1(kKeyTranslationProvider),
        QString::fromLatin1(kKeyTranslationModel),
        QString::fromLatin1(kKeyTranslationBaseUrl),
        QString::fromLatin1(kKeyTargetLang),
        QString::fromLatin1(kKeyTranslationConcurrency),

        QString::fromLatin1(kKeyTranslationPrompt),
        QString::fromLatin1(kKeyTocPrompt),
        QString::fromLatin1(kKeyVisionPrompt),
        QString::fromLatin1(kKeyChatPrompt),

        QString::fromLatin1(kKeyToolBudget),
        QString::fromLatin1(kKeyChatIncludePaperText),
        QString::fromLatin1(kKeyChatSendKey),

        QString::fromLatin1(kKeyAnalysisMaxTokens),
        QString::fromLatin1(kKeyAnalysisConcurrency),

        QString::fromLatin1(kKeyTocFontSize),
        QString::fromLatin1(kKeySummaryFontSize),
        QString::fromLatin1(kKeyParagraphFontSize),
        QString::fromLatin1(kKeyChatFontSize),

        QString::fromLatin1(kKeyUiLanguage),
        QString::fromLatin1(kKeyAutoSegment),
        QString::fromLatin1(kKeySharePaperData),
        QString::fromLatin1(kKeyGrobidEnabled),
        QString::fromLatin1(kKeyAutoCheckUpdates),
    };
    return keys;
}

QJsonObject Settings::exportAccountSettings() const
{
    QJsonObject o;
    o.insert(QString::fromLatin1(kKeyProvider),      m_provider);
    o.insert(QString::fromLatin1(kKeyModel),         m_model);
    o.insert(QString::fromLatin1(kKeyBaseUrl),       m_baseUrl);
    o.insert(QString::fromLatin1(kKeyTemperature),   m_temperature);
    o.insert(QString::fromLatin1(kKeyMaxTokens),     m_maxTokens);
    o.insert(QString::fromLatin1(kKeyContextWindow), m_contextWindow);

    o.insert(QString::fromLatin1(kKeyTranslationProvider), m_translationProvider);
    o.insert(QString::fromLatin1(kKeyTranslationModel),    m_translationModel);
    o.insert(QString::fromLatin1(kKeyTranslationBaseUrl),  m_translationBaseUrl);
    o.insert(QString::fromLatin1(kKeyTargetLang),          m_targetLang);
    o.insert(QString::fromLatin1(kKeyTranslationConcurrency), m_translationConcurrency);

    o.insert(QString::fromLatin1(kKeyTranslationPrompt), m_translationPrompt);
    o.insert(QString::fromLatin1(kKeyTocPrompt),         m_tocPrompt);
    o.insert(QString::fromLatin1(kKeyVisionPrompt),      m_visionPrompt);
    o.insert(QString::fromLatin1(kKeyChatPrompt),        m_chatPrompt);

    o.insert(QString::fromLatin1(kKeyToolBudget),            m_toolBudget);
    o.insert(QString::fromLatin1(kKeyChatIncludePaperText),  m_chatIncludePaperText);
    o.insert(QString::fromLatin1(kKeyChatSendKey),           m_chatSendKey);

    o.insert(QString::fromLatin1(kKeyAnalysisMaxTokens),   m_analysisMaxTokens);
    o.insert(QString::fromLatin1(kKeyAnalysisConcurrency), m_analysisConcurrency);

    o.insert(QString::fromLatin1(kKeyTocFontSize),       m_tocFontSize);
    o.insert(QString::fromLatin1(kKeySummaryFontSize),   m_summaryFontSize);
    o.insert(QString::fromLatin1(kKeyParagraphFontSize), m_paragraphFontSize);
    o.insert(QString::fromLatin1(kKeyChatFontSize),      m_chatFontSize);

    o.insert(QString::fromLatin1(kKeyUiLanguage),      m_uiLanguage);
    o.insert(QString::fromLatin1(kKeyAutoSegment),     m_autoSegment);
    o.insert(QString::fromLatin1(kKeySharePaperData),  m_sharePaperData);
    o.insert(QString::fromLatin1(kKeyGrobidEnabled),   m_grobidEnabled);
    o.insert(QString::fromLatin1(kKeyAutoCheckUpdates), m_autoCheckUpdates);

    // No API keys, no tokens: those live in the OS keychain and the whole
    // point of keeping them there is that they never travel.
    return o;
}

QJsonObject Settings::defaultAccountSettings()
{
    QJsonObject o;
    o.insert(QString::fromLatin1(kKeyProvider),      QString::fromLatin1(kDefProvider));
    o.insert(QString::fromLatin1(kKeyModel),         QString::fromLatin1(kDefModel));
    o.insert(QString::fromLatin1(kKeyBaseUrl),       QString{});
    o.insert(QString::fromLatin1(kKeyTemperature),   kDefTemperature);
    o.insert(QString::fromLatin1(kKeyMaxTokens),     kDefMaxTokens);
    o.insert(QString::fromLatin1(kKeyContextWindow), kDefContextWindow);

    o.insert(QString::fromLatin1(kKeyTranslationProvider), QString{});
    o.insert(QString::fromLatin1(kKeyTranslationModel),    QString{});
    o.insert(QString::fromLatin1(kKeyTranslationBaseUrl),  QString{});
    o.insert(QString::fromLatin1(kKeyTargetLang),          QString::fromLatin1(kDefTargetLang));
    o.insert(QString::fromLatin1(kKeyTranslationConcurrency), kDefTranslationConcurrency);

    o.insert(QString::fromLatin1(kKeyTranslationPrompt), QString{});
    o.insert(QString::fromLatin1(kKeyTocPrompt),         QString{});
    o.insert(QString::fromLatin1(kKeyVisionPrompt),      QString{});
    o.insert(QString::fromLatin1(kKeyChatPrompt),        QString{});

    o.insert(QString::fromLatin1(kKeyToolBudget),           kDefToolBudget);
    o.insert(QString::fromLatin1(kKeyChatIncludePaperText), kDefChatIncludePaperText);
    o.insert(QString::fromLatin1(kKeyChatSendKey),          QString::fromLatin1(kDefChatSendKey));

    o.insert(QString::fromLatin1(kKeyAnalysisMaxTokens),   kDefAnalysisMaxTokens);
    o.insert(QString::fromLatin1(kKeyAnalysisConcurrency), kDefAnalysisConcurrency);

    o.insert(QString::fromLatin1(kKeyTocFontSize),       kDefTocFontSize);
    o.insert(QString::fromLatin1(kKeySummaryFontSize),   kDefSummaryFontSize);
    o.insert(QString::fromLatin1(kKeyParagraphFontSize), kDefParagraphFontSize);
    o.insert(QString::fromLatin1(kKeyChatFontSize),      kDefChatFontSize);

    o.insert(QString::fromLatin1(kKeyUiLanguage),       QString{});
    o.insert(QString::fromLatin1(kKeyAutoSegment),      kDefAutoSegment);
    o.insert(QString::fromLatin1(kKeySharePaperData),   kDefSharePaperData);
    o.insert(QString::fromLatin1(kKeyGrobidEnabled),    kDefGrobidEnabled);
    o.insert(QString::fromLatin1(kKeyAutoCheckUpdates), kDefAutoCheckUpdates);
    return o;
}

void Settings::importAccountSettings(const QJsonObject &obj)
{
    // Every branch below calls the ordinary setter. That is deliberate and
    // it is the whole safety story of this function: the setters are where
    // the ranges live (8-32 px fonts, 1-8 interpretations at once, 1-16
    // translations), where configRevision is bumped so LlmClientCache
    // rebuilds its clients, and where the change signals are emitted so the
    // panes on screen re-lay out without a restart. Reimplementing any of
    // that here would mean a value from another machine could reach places
    // a value typed into the dialog cannot.

    // The provider goes first: it decides whether a Base URL is meaningful
    // at all, and the guard below reads the provider that is by then in
    // effect. An unrecognised code is dropped rather than stored -- it would
    // silently select the Anthropic client in createClient().
    if (const auto v = jsonString(obj, kKeyProvider)) {
        const QString p = v->trimmed().toLower();
        if (isKnownProvider(p))
            setProvider(p);
    }
    if (const auto v = jsonString(obj, kKeyModel))
        setModel(v->trimmed());
    if (const auto v = jsonString(obj, kKeyBaseUrl)) {
        // Stored whatever the provider is, and deliberately so. The rule that
        // only "openai-compatible" has an address of its own is enforced
        // where it matters -- resolveBaseUrl(), which every client goes
        // through, ignores this field for the three named providers -- and
        // not by refusing to remember it. Refusing would also make the two
        // sides permanently disagree about this key: this machine would keep
        // exporting the gateway it declined to overwrite, the account would
        // keep sending back the one it holds, and every launch would spend a
        // pointless write on the argument. What is rejected is a URL nothing
        // could ever be sent to.
        const QString url = v->trimmed();
        if (url.isEmpty() || isUsableEndpoint(url))
            setBaseUrl(url);
    }
    if (const auto v = jsonDouble(obj, kKeyTemperature)) {
        // setTemperature() takes anything; the dialog's slider is the only
        // thing that has ever bounded it. A value from elsewhere gets the
        // widest range any of our providers accepts, because an out-of-range
        // temperature does not degrade -- it turns every request into a 400
        // long after the sync that delivered it.
        setTemperature(qBound(0.0, *v, 2.0));
    }
    if (const auto v = jsonInt(obj, kKeyMaxTokens))
        setMaxTokens(*v);
    if (const auto v = jsonInt(obj, kKeyContextWindow))
        setContextWindow(*v);

    // Translation override. Blank means "use the main configuration", so
    // unlike the main provider an empty string is a legal value here.
    if (const auto v = jsonString(obj, kKeyTranslationProvider)) {
        const QString p = v->trimmed().toLower();
        if (p.isEmpty() || isKnownProvider(p))
            setTranslationProvider(p);
    }
    if (const auto v = jsonString(obj, kKeyTranslationModel))
        setTranslationModel(v->trimmed());
    if (const auto v = jsonString(obj, kKeyTranslationBaseUrl)) {
        // Same reasoning as the main Base URL above; translationBaseUrlInUse()
        // is what decides whether this address is ever dialled.
        const QString url = v->trimmed();
        if (url.isEmpty() || isUsableEndpoint(url))
            setTranslationBaseUrl(url);
    }
    if (const auto v = jsonString(obj, kKeyTargetLang))
        setTargetLang(v->trimmed());
    if (const auto v = jsonInt(obj, kKeyTranslationConcurrency))
        setTranslationConcurrency(*v);

    if (const auto v = jsonString(obj, kKeyTranslationPrompt))
        setTranslationPrompt(*v);
    if (const auto v = jsonString(obj, kKeyTocPrompt))
        setTocPrompt(*v);
    if (const auto v = jsonString(obj, kKeyVisionPrompt))
        setVisionPrompt(*v);
    if (const auto v = jsonString(obj, kKeyChatPrompt))
        setChatPrompt(*v);

    if (const auto v = jsonInt(obj, kKeyToolBudget))
        setToolBudget(*v);
    if (const auto v = jsonBool(obj, kKeyChatIncludePaperText))
        setChatIncludePaperText(*v);
    if (const auto v = jsonString(obj, kKeyChatSendKey)) {
        // setChatSendKey() normalises anything unknown to "enter". Here we
        // skip it instead: normalising would let a junk value silently undo
        // a deliberate local choice, which is not what the setter's rule is
        // for -- it exists to make a stale QSettings file safe, not to
        // arbitrate between two machines.
        const QString k = v->trimmed().toLower();
        if (k == QLatin1String("enter") || k == QLatin1String("ctrl-enter"))
            setChatSendKey(k);
    }

    if (const auto v = jsonInt(obj, kKeyAnalysisMaxTokens))
        setAnalysisMaxTokens(*v);
    if (const auto v = jsonInt(obj, kKeyAnalysisConcurrency))
        setAnalysisConcurrency(*v);

    if (const auto v = jsonInt(obj, kKeyTocFontSize))
        setTocFontSize(*v);
    if (const auto v = jsonInt(obj, kKeySummaryFontSize))
        setSummaryFontSize(*v);
    if (const auto v = jsonInt(obj, kKeyParagraphFontSize))
        setParagraphFontSize(*v);
    if (const auto v = jsonInt(obj, kKeyChatFontSize))
        setChatFontSize(*v);

    if (const auto v = jsonString(obj, kKeyUiLanguage)) {
        const QString code = v->trimmed();
        if (isUsableLanguageCode(code))
            setUiLanguage(code);
    }
    if (const auto v = jsonBool(obj, kKeyAutoSegment))
        setAutoSegment(*v);
    if (const auto v = jsonBool(obj, kKeySharePaperData))
        setSharePaperData(*v);
    if (const auto v = jsonBool(obj, kKeyGrobidEnabled))
        setGrobidEnabled(*v);
    if (const auto v = jsonBool(obj, kKeyAutoCheckUpdates))
        setAutoCheckUpdates(*v);

    // Anything else in obj is ignored on purpose: a newer build may sync a
    // key this one has never heard of, and guessing at it is worse than
    // leaving it alone. UserPrefsSync carries such keys back untouched so
    // an older client cannot erase a newer one's settings.
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
