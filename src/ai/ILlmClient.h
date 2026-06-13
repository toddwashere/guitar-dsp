#pragma once
#include "ai/ConversationBuffer.h"

#include <chrono>
#include <string>
#include <vector>

namespace guitar_dsp::ai {

class CancellationToken;

struct LlmRequest {
    std::vector<Message>       messages;            // system first, then user/assistant turns
    int                        maxTokens = 80;
    std::chrono::milliseconds  timeout {10000};
};

struct LlmReply {
    std::string text;
    std::string error;                  // empty on success
    int         promptTokens     = 0;
    int         completionTokens = 0;
};

class ILlmClient {
public:
    virtual ~ILlmClient() = default;

    virtual LlmReply    generate(const LlmRequest&,
                                 CancellationToken* cancel = nullptr) = 0;
    virtual std::string displayName() const = 0;   // for UI dropdown
    virtual std::string statusText()  const = 0;   // for status pill
    virtual std::string modelId()     const = 0;   // for state persistence
};

} // namespace guitar_dsp::ai
