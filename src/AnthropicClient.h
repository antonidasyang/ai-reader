#pragma once

#include "LlmClient.h"

class AnthropicClient : public LlmClient
{
    Q_OBJECT
public:
    explicit AnthropicClient(QObject *parent = nullptr);

    LlmReply *send(const Request &req) override;
};
