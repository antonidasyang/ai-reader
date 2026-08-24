#include "LlmClientCache.h"

#include "LlmClient.h"
#include "Settings.h"

LlmClientCache::LlmClientCache(Settings *settings, QObject *owner,
                               bool translation)
    : m_settings(settings)
    , m_owner(owner)
    , m_translation(translation)
{
}

LlmClient *LlmClientCache::client(bool mayRebuild)
{
    if (!m_settings)
        return nullptr;
    const int now = m_settings->configRevision();
    if (m_client && (!mayRebuild || m_revision == now))
        return m_client;

    if (m_client)
        m_client->deleteLater();
    m_client = m_translation ? m_settings->createTranslationClient(m_owner)
                             : m_settings->createClient(m_owner);
    m_revision = now;
    return m_client;
}
