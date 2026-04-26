#include "Settings.h"

#include "AnthropicClient.h"
#include "OpenAiClient.h"

namespace {
constexpr auto kKeyProvider    = "llm/provider";
constexpr auto kKeyModel       = "llm/model";
constexpr auto kKeyBaseUrl     = "llm/baseUrl";
constexpr auto kKeyApiKey      = "llm/apiKey";
constexpr auto kKeyTemperature = "llm/temperature";
constexpr auto kKeyTargetLang  = "translation/targetLang";
} // namespace

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    load();
}

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

void Settings::setTargetLang(const QString &v)
{
    if (v == m_targetLang) return;
    m_targetLang = v;
    save();
    emit targetLangChanged();
}

LlmClient *Settings::createClient(QObject *parent) const
{
    LlmClient *client = nullptr;
    if (m_provider.compare(QStringLiteral("openai"), Qt::CaseInsensitive) == 0
        || m_provider.compare(QStringLiteral("openai-compatible"), Qt::CaseInsensitive) == 0
        || m_provider.compare(QStringLiteral("deepseek"), Qt::CaseInsensitive) == 0) {
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

void Settings::load()
{
    m_provider    = m_qs.value(kKeyProvider,    QStringLiteral("anthropic")).toString();
    m_model       = m_qs.value(kKeyModel,       QStringLiteral("claude-opus-4-7")).toString();
    m_baseUrl     = m_qs.value(kKeyBaseUrl,     QString{}).toString();
    m_apiKey      = m_qs.value(kKeyApiKey,      QString{}).toString();
    m_temperature = m_qs.value(kKeyTemperature, 0.2).toDouble();
    m_targetLang  = m_qs.value(kKeyTargetLang,  QStringLiteral("zh-CN")).toString();
}

void Settings::save()
{
    m_qs.setValue(kKeyProvider,    m_provider);
    m_qs.setValue(kKeyModel,       m_model);
    m_qs.setValue(kKeyBaseUrl,     m_baseUrl);
    m_qs.setValue(kKeyApiKey,      m_apiKey);
    m_qs.setValue(kKeyTemperature, m_temperature);
    m_qs.setValue(kKeyTargetLang,  m_targetLang);
    m_qs.sync();
}
