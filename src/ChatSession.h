#pragma once

#include "ChatModel.h"
#include "LlmClient.h"

#include <QDateTime>
#include <QString>
#include <QVector>

// One conversation thread for a paper. ChatService keeps a list of these
// per paper; each carries its own user-visible message log AND the parallel
// API history (which holds tool_use/tool_result blocks not surfaced in the
// UI). `autoNamed` is true while the title was derived by the auto-namer
// — it flips to false once the user manually renames the session, so a
// later auto-name pass won't clobber a hand-picked title.
struct ChatSession {
    QString id;
    QString name;
    bool autoNamed = true;
    QDateTime createdAt;
    QDateTime updatedAt;
    QVector<ChatMessage> messages;
    QVector<LlmClient::Message> apiMessages;
};
