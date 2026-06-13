#pragma once
#include <atomic>
#include <vector>

namespace guitar_dsp::audio {

class MicCapture {
public:
    static constexpr int    kTargetRate      = 16000;
    static constexpr double kMaxDurationSec  = 30.0;
    static constexpr double kMinDurationSec  = 0.2;

    void prepare(double sampleRate, int numChannels);
    void beginCapture();
    void appendFromAudioBlock(const float* mono, int n);   // RT-safe
    std::vector<float> endCapture();                       // returns 16k mono

    bool  isCapturing()            const noexcept { return capturing_.load(); }
    float currentPeakDbfs()        const noexcept;
    bool  lastResultWasTooShort()  const noexcept { return tooShort_; }
    bool  lastResultWasTruncated() const noexcept { return truncated_; }

private:
    double             sampleRate_   {48000.0};
    int                maxWriteIdx_  {0};              // logical 30 s cap in samples
    std::vector<float> staging_;                       // grown only outside capture
    std::atomic<bool>  capturing_ {false};
    std::atomic<int>   writeIdx_  {0};
    std::atomic<float> peakLin_   {0.0f};
    bool               tooShort_  {false};
    bool               truncated_ {false};
};

} // namespace guitar_dsp::audio
