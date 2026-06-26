#include "audio/RaveSynthesizer.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstring>

namespace guitar_dsp::audio {

RaveSynthesizer::RaveSynthesizer() = default;

RaveSynthesizer::~RaveSynthesizer() {
    releaseResources();
}

void RaveSynthesizer::prepare(double sampleRate, int samplesPerBlock) {
    sr_ = sampleRate;
    blockSize_ = samplesPerBlock;
    frontEnd_.prepare(sr_);
    limiter_.prepare(sr_);
    limiter_.setCeilingDb(-3.0f);
    limiter_.setReleaseMs(60.0f);
    scratch_.assign(std::size_t(samplesPerBlock), 0.0f); // pre-allocate; processBlock never resizes
    inRing_.clear();
    outRing_.clear();
    guard_.reset();
    const auto t0 = nowMs_();
    lastOutputReadMsSinceEpoch_.store(t0, std::memory_order_relaxed);
    lastInputPushMsSinceEpoch_.store(t0, std::memory_order_relaxed);
}

void RaveSynthesizer::releaseResources() {
    stop_.store(true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
}

void RaveSynthesizer::loadModel(const std::string& path) {
    if (worker_.joinable()) {
        juce::Logger::writeToLog("RaveSynthesizer::loadModel called twice — ignoring subsequent call. "
                                  "Use swapModel() for runtime model changes.");
        return;
    }
    spawnWorker_(path);
}

void RaveSynthesizer::swapModel(const std::string& path) {
    // Explicit user-initiated swap. Join the current worker, then respawn
    // with the new model. The join is bounded by the worker's current
    // inference call (~ms for stub, tens of ms for real RAVE) — acceptable
    // for a message-thread UI action.
    releaseResources();
    spawnWorker_(path);
}

void RaveSynthesizer::spawnWorker_(const std::string& path) {
    stop_.store(false, std::memory_order_release);
    status_.store(RaveBranchStatus::Loading, std::memory_order_release);
    // Clear the rings so stale audio from the previous model doesn't bleed
    // into the new model's first inference window.
    inRing_.clear();
    outRing_.clear();
    worker_ = std::thread([this, path]() {
        inference_.loadModel(path);
        if (inference_.status() != ml::RaveStatus::Loaded) {
            status_.store(RaveBranchStatus::Unavailable, std::memory_order_release);
            return;
        }
        status_.store(RaveBranchStatus::Loaded, std::memory_order_release);
        backgroundLoop_();
    });
}

void RaveSynthesizer::backgroundLoop_() {
    std::vector<float> winIn(kModelHop), winOut(kModelHop);
    while (!stop_.load(std::memory_order_acquire)) {
        if (inRing_.available() >= kModelHop) {
            inRing_.read(winIn.data(), kModelHop);
            if (!inference_.process(winIn.data(), winOut.data(), kModelHop)) {
                std::fill(winOut.begin(), winOut.end(), 0.0f);
            }
            inferenceMs_.store(inference_.lastInferenceMs(), std::memory_order_relaxed);
            // Write to outRing (may block briefly if consumer is slow — drop excess).
            std::size_t off = 0;
            while (off < kModelHop && !stop_.load(std::memory_order_acquire)) {
                const auto w = outRing_.write(winOut.data() + off, kModelHop - off);
                if (w == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
                off += w;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void RaveSynthesizer::applyParamsIfChanged_() {
    const float g = paramGateDb_.load(std::memory_order_relaxed);
    const float p = paramPresence_.load(std::memory_order_relaxed);
    const float d = paramDriveDb_.load(std::memory_order_relaxed);
    if (g != currentGateDb_)  { frontEnd_.setGateDb(g);  currentGateDb_ = g; }
    if (p != currentPresence_){ frontEnd_.setPresence(p);currentPresence_ = p; }
    if (d != currentDriveDb_) { frontEnd_.setDriveDb(d); currentDriveDb_ = d; }
}

void RaveSynthesizer::processBlock(const float* in, float* wetOut, std::size_t n) noexcept {
    const auto s = status_.load(std::memory_order_acquire);
    if (s == RaveBranchStatus::Unavailable) {
        std::memset(wetOut, 0, n * sizeof(float));
        return;
    }

    // Scene-activation watchdog reset: if input has been quiet for >500 ms,
    // treat this as a fresh activation and reset the output-read timestamp
    // so the watchdog doesn't fire before the inference pipeline has time
    // to produce its first window.
    const auto nowMs = nowMs_();
    if (nowMs - lastInputPushMsSinceEpoch_.load(std::memory_order_relaxed) > 500) {
        lastOutputReadMsSinceEpoch_.store(nowMs, std::memory_order_relaxed);
    }

    applyParamsIfChanged_();

    // Front-end conditioning into pre-allocated scratch (no audio-thread alloc).
    // Defensive: if host gave us a larger block than prepare() expected, fall back
    // to per-block bookkeeping but never realloc — clamp n down.
    const std::size_t nn = std::min(n, scratch_.size());
    std::memcpy(scratch_.data(), in, nn * sizeof(float));
    frontEnd_.processBlock(scratch_.data(), nn);
    inputRms_.store(frontEnd_.postRms(), std::memory_order_relaxed);
    inRing_.write(scratch_.data(), nn);
    lastInputPushMsSinceEpoch_.store(nowMs, std::memory_order_relaxed);

    // Pull whatever is available from outRing. If we get nothing for >100 ms while Loaded, flip to Stalled.
    const auto got = outRing_.read(wetOut, n);
    if (got > 0) {
        lastOutputReadMsSinceEpoch_.store(nowMs_(), std::memory_order_relaxed);
        if (s == RaveBranchStatus::Stalled) status_.store(RaveBranchStatus::Loaded, std::memory_order_release);
    }
    if (got < n) std::memset(wetOut + got, 0, (n - got) * sizeof(float));

    if (s == RaveBranchStatus::Loaded) {
        const auto silent = nowMs_() - lastOutputReadMsSinceEpoch_.load(std::memory_order_relaxed);
        if (silent > 100) status_.store(RaveBranchStatus::Stalled, std::memory_order_release);
    }

    // NaN/Inf guard. guard_.processBlock already zero-fills wetOut when it
    // detects bad samples, so no separate memset is needed here.
    if (!guard_.processBlock(wetOut, n) && guard_.stalled() &&
        s == RaveBranchStatus::Loaded) {
        status_.store(RaveBranchStatus::Stalled, std::memory_order_release);
    }

    // Branch peak limiter.
    limiter_.processBlock(wetOut, n);

    // Output RMS.
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) sum += double(wetOut[i]) * wetOut[i];
    outputRms_.store(std::sqrt(float(sum / double(n))), std::memory_order_relaxed);
}

} // namespace guitar_dsp::audio
