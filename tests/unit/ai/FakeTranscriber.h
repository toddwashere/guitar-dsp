#pragma once
#include "ai/ITranscriber.h"
#include <chrono>
#include <thread>
#include <atomic>

namespace guitar_dsp::ai::test {
class FakeTranscriber : public ITranscriber {
public:
    std::string                scriptedText  {"hello there"};
    std::string                scriptedError;
    std::chrono::milliseconds  delay         {0};
    std::atomic<int>           callCount     {0};

    TranscriptionResult transcribe(const std::vector<float>&, CancellationToken*) override {
        ++callCount;
        if (delay.count() > 0) std::this_thread::sleep_for(delay);
        TranscriptionResult r;
        r.text     = scriptedText;
        r.language = "en";
        r.latency  = delay;
        r.error    = scriptedError;
        return r;
    }
    std::string modelName() const override { return "fake"; }
};
} // namespace guitar_dsp::ai::test
