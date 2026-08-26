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

LlmClient *LlmClientCache::client()
{
    if (!m_settings)
        return nullptr;
    sweepRetired();
    const int now = m_settings->configRevision();
    if (m_client && m_revision == now)
        return m_client;

    if (m_client)
        m_retired.append(m_client);
    m_client = m_translation ? m_settings->createTranslationClient(m_owner)
                             : m_settings->createClient(m_owner);
    m_revision = now;
    return m_client;
}

void LlmClientCache::sweepRetired()
{
    for (int i = m_retired.size() - 1; i >= 0; --i) {
        LlmClient *c = m_retired.at(i);
        if (!c) {
            m_retired.removeAt(i);
            continue;
        }
        // A reply that is merely deleteLater'd is still a child this round;
        // it gets swept on a later call. Retired clients are parented to the
        // owning service either way, so the worst case is living until the
        // service does.
        if (!c->findChild<LlmReply *>(QString(), Qt::FindDirectChildrenOnly)) {
            c->deleteLater();
            m_retired.removeAt(i);
        }
    }
}
