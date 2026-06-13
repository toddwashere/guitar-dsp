#pragma once
#include "audio/IMicCapture.h"
#include <atomic>

namespace guitar_dsp::ai::test {
class FakeMicCapture : public guitar_dsp::audio::IMicCapture {
public:
    std::vector<float>  scriptedSamples {std::vector<float>(16000, 0.5f)};
    bool                tooShort  {false};
    bool                truncated {false};
    std::atomic<bool>   capturing {false};

    void beginCapture()                              override { capturing = true; }
    std::vector<float> endCapture()                  override { capturing = false; return scriptedSamples; }
    bool  isCapturing()          const noexcept override { return capturing.load(); }
    float currentPeakDbfs()      const noexcept override { return -20.0f; }
    bool  lastResultWasTooShort()const noexcept override { return tooShort; }
    bool  lastResultWasTruncated()const noexcept override { return truncated; }
};
} // namespace guitar_dsp::ai::test
