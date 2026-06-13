#pragma once
#include "ai/ILlmClient.h"
#include <chrono>
#include <thread>
#include <atomic>

namespace guitar_dsp::ai::test {
class FakeLlmClient : public ILlmClient {
public:
    std::string                scriptedText  {"I'm a fake."};
    std::string                scriptedError;
    std::chrono::milliseconds  delay         {0};
    std::atomic<int>           callCount     {0};
    LlmRequest                 lastRequest;

    LlmReply generate(const LlmRequest& req, CancellationToken*) override {
        ++callCount;
        lastRequest = req;
        if (delay.count() > 0) std::this_thread::sleep_for(delay);
        return LlmReply{scriptedText, scriptedError, 0, 0};
    }
    std::string displayName() const override { return "Fake LLM"; }
    std::string statusText()  const override { return "Fake: ready"; }
    std::string modelId()     const override { return "fake"; }
};
} // namespace guitar_dsp::ai::test
