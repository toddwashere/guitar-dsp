#pragma once
#include "ai/ILlmClient.h"
#include "ai/IHttpTransport.h"

#include <vector>

namespace guitar_dsp::ai {

class OllamaClient : public ILlmClient {
public:
    OllamaClient(IHttpTransport& http,
                 std::string     endpoint,
                 std::string     modelTag);

    LlmReply    generate(const LlmRequest&,
                         CancellationToken* cancel = nullptr) override;
    std::string displayName() const override;
    std::string statusText()  const override;
    std::string modelId()     const override { return "ollama:" + modelTag_; }

    static bool                     isRunning(IHttpTransport& http,
                                              const std::string& endpoint);
    static std::vector<std::string> listInstalledModels(IHttpTransport& http,
                                                        const std::string& endpoint);

private:
    IHttpTransport& http_;
    std::string     endpoint_;
    std::string     modelTag_;
};

} // namespace guitar_dsp::ai
