#pragma once
#include "ai/ILlmClient.h"
#include "ai/IHttpTransport.h"

namespace guitar_dsp::ai {

class AnthropicClient : public ILlmClient {
public:
    AnthropicClient(IHttpTransport& http,
                    std::string     apiKey,
                    std::string     modelId);

    LlmReply    generate(const LlmRequest&,
                         CancellationToken* cancel = nullptr) override;
    std::string displayName() const override;
    std::string statusText()  const override;
    std::string modelId()     const override { return modelId_; }

private:
    IHttpTransport& http_;
    std::string     apiKey_;
    std::string     modelId_;
};

} // namespace guitar_dsp::ai
