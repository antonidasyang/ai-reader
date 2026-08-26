#pragma once

#include <QList>
#include <QPointer>

class LlmClient;
class QObject;
class Settings;

// The LLM client a service talks to, kept in step with the settings.
//
// Every service used to cache its own client and, on the next request, patch
// its fields: api key, model, base URL. That is wrong in two ways. Switching
// provider changes the *class* of client (Anthropic and OpenAI speak
// different protocols), which no amount of field-setting fixes. And the base
// URL it re-applied was the raw stored one, which for a named provider is
// not the endpoint at all -- so after switching provider the app kept
// talking to the previous one until it was restarted.
//
// This rebuilds the client whenever the configuration has moved, and is the
// only place that decides how that happens safely.
class LlmClientCache
{
public:
    // `translation` picks the translation override; everything else runs on
    // the main configuration.
    LlmClientCache(Settings *settings, QObject *owner, bool translation = false);

    // The client to use now, on the current configuration — always.
    //
    // Every LlmReply is a child of the client that made it, so the previous
    // client is not deleted out from under its replies: it is retired and
    // stays alive until the last of them is gone. New requests pick the new
    // configuration up immediately; the ones already in the air finish on
    // the endpoint they were sent to.
    LlmClient *client();

private:
    // Delete retired clients whose replies have all been destroyed.
    void sweepRetired();

    Settings *m_settings;
    QObject *m_owner;
    bool m_translation;
    QPointer<LlmClient> m_client;
    QList<QPointer<LlmClient>> m_retired;
    int m_revision = -1;
};
