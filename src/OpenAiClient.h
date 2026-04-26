#pragma once

#include "LlmClient.h"

class OpenAiClient : public LlmClient
{
    Q_OBJECT
public:
    explicit OpenAiClient(QObject *parent = nullptr);

    LlmReply *send(const Request &req) override;
};
