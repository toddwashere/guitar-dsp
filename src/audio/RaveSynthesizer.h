#pragma once
#include "audio/LockFreeSPSCRing.h"
#include "audio/NaNInfGuard.h"
#include "audio/BranchLimiter.h"
#include "audio/RaveFrontEnd.h"
#include "ml/RaveInference.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace guitar_dsp::audio {

enum class RaveBranchStatus { Loading, Loaded, Unavailable, Stalled };

class RaveSynthesizer {
public:
    RaveSynthesizer();
    ~RaveSynthesizer();

    void loadModel(const std::string& onnxPath);
    void prepare(double sampleRate, int samplesPerBlock);
    void releaseResources();

    void setGateDb(float db) noexcept     { paramGateDb_.store(db, std::memory_order_relaxed); }
    void setPresence(float a) noexcept    { paramPresence_.store(a, std::memory_order_relaxed); }
    void setDriveDb(float db) noexcept    { paramDriveDb_.store(db, std::memory_order_relaxed); }

    void processBlock(const float* in, float* wetOut, std::size_t n) noexcept;

    RaveBranchStatus status() const noexcept { return status_.load(std::memory_order_acquire); }
    float inputRms() const noexcept       { return inputRms_.load(std::memory_order_relaxed); }
    float outputRms() const noexcept      { return outputRms_.load(std::memory_order_relaxed); }
    float inferenceMs() const noexcept    { return inferenceMs_.load(std::memory_order_relaxed); }

private:
    static constexpr std::size_t kModelHop = 2048;
    static constexpr std::size_t kRingCap  = 8192;

    void backgroundLoop_();
    void applyParamsIfChanged_();

    double sr_ = 48000.0;
    int    blockSize_ = 512;

    RaveFrontEnd frontEnd_;
    NaNInfGuard guard_;
    BranchLimiter limiter_;
    ml::RaveInference inference_;

    LockFreeSPSCRing<float> inRing_{kRingCap};
    LockFreeSPSCRing<float> outRing_{kRingCap};

    // Knob state (audio thread reads, message thread writes)
    std::atomic<float> paramGateDb_{-40.0f};
    std::atomic<float> paramPresence_{0.5f};
    std::atomic<float> paramDriveDb_{0.0f};
    float currentGateDb_ = 0.0f, currentPresence_ = -1.0f, currentDriveDb_ = 0.0f;

    // Status / readouts
    std::atomic<RaveBranchStatus> status_{RaveBranchStatus::Loading};
    std::atomic<float> inputRms_{0.0f};
    std::atomic<float> outputRms_{0.0f};
    std::atomic<float> inferenceMs_{0.0f};

    // Pre-allocated audio-thread scratch (no per-block allocations).
    std::vector<float> scratch_;

    // Watchdog: last time audio thread successfully read from outRing.
    std::atomic<int64_t> lastOutputReadMsSinceEpoch_{0};
    static int64_t nowMs_() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Background inference thread
    std::thread worker_;
    std::atomic<bool> stop_{false};
};

} // namespace guitar_dsp::audio
