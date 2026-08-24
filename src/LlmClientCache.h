#pragma once

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
// only place that decides when that is safe.
class LlmClientCache
{
public:
    // `translation` picks the translation override; everything else runs on
    // the main configuration.
    LlmClientCache(Settings *settings, QObject *owner, bool translation = false);

    // The client to use now.
    //
    // `mayRebuild` is false for a caller that still has replies in flight:
    // every LlmReply is a child of its client, so replacing it would strand
    // them with neither a finished nor an error signal. Such a caller gets
    // the client it already had, and picks the new configuration up as soon
    // as it is idle.
    LlmClient *client(bool mayRebuild = true);

private:
    Settings *m_settings;
    QObject *m_owner;
    bool m_translation;
    QPointer<LlmClient> m_client;
    int m_revision = -1;
};
