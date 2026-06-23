#include "SungDirectPath.h"

#include "PrerenderCache.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

SungDirectPath::~SungDirectPath() {
    cancelAndJoinPreRender_();
}

void SungDirectPath::prepare(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_  = std::max(64, blockSize);
    clipBank_.prepare(sampleRate, blockSize_);
    shifter_.prepare(sampleRate, blockSize_);
    vowelLoop_.prepare(sampleRate);
    grainOutBuf_.assign(static_cast<std::size_t>(blockSize_), 0.0f);
    smoothedRatio_   = 1.0f;
    lastSourceIdx_   = -2;
    currentAnchorHz_ = 0.0f;
}

void SungDirectPath::reset() {
    cancelAndJoinPreRender_();
    clipBank_.reset();
    shifter_.reset();
    vowelLoop_.reset();
    smoothedRatio_   = 1.0f;
    lastSourceIdx_   = -2;
    currentAnchorHz_ = 0.0f;
    // Clear any cached prerender — message-thread store, audio-thread loads.
    std::atomic_store(&activePrerendered_,
                      std::shared_ptr<PrerenderedMap>{});
    loadState_.store(static_cast<int>(LoadState::Idle),
                     std::memory_order_release);
    loadProgressPercent_.store(0, std::memory_order_relaxed);
}

void SungDirectPath::cancelAndJoinPreRender_() {
    cancelToken_.store(true, std::memory_order_release);
    if (preRenderThread_.joinable()) preRenderThread_.join();
    cancelToken_.store(false, std::memory_order_release);
}

void SungDirectPath::setGrainsForBank(const std::vector<TTSClipPtr>& bank,
                                      const std::string& bundleHash) {
    // 1. Tear down any in-flight pre-render.
    cancelAndJoinPreRender_();

    // 2. Clear active cache + shifter source so the audio thread emits
    //    silence while we re-render. ClipBankPlayer still gets the bank
    //    immediately so onset detection works the moment audio resumes
    //    (it just won't be backed by a pre-rendered grain until ready).
    std::atomic_store(&activePrerendered_,
                      std::shared_ptr<PrerenderedMap>{});
    shifter_.setSource(nullptr);
    clipBank_.setBank(bank);
    lastSourceIdx_   = -2;
    currentAnchorHz_ = 0.0f;

    if (bank.empty()) {
        loadState_.store(static_cast<int>(LoadState::Idle),
                         std::memory_order_release);
        loadProgressPercent_.store(0, std::memory_order_relaxed);
        return;
    }

    loadState_.store(static_cast<int>(LoadState::Loading),
                     std::memory_order_release);
    loadProgressPercent_.store(0, std::memory_order_relaxed);

    // 3. Spawn the worker. The lambda captures a copy of the bank so the
    //    TTSClipPtrs (shared_ptr) keep the source samples alive while we
    //    analyse + render even if the message thread tears down the bank.
    preRenderThread_ = std::thread([this, bank, bundleHash]() {
        const int total = static_cast<int>(bank.size());
        auto cache = std::make_shared<PrerenderedMap>();
        cache->reserve(static_cast<std::size_t>(total));

        // ---- Fast path: try the disk cache first ------------------------
        if (! bundleHash.empty()) {
            const auto bakeFile = PrerenderCache::pathForHash(bundleHash);
            if (auto cached = PrerenderCache::read(
                    bakeFile, bundleHash, FormantShifter::kSemitoneRange)) {
                // Map cache entries back to TTSClip pointers by phoneme index.
                for (const auto& e : *cached) {
                    if (e.phonemeIdx < 0 || e.phonemeIdx >= total) continue;
                    const auto& c = bank[static_cast<std::size_t>(e.phonemeIdx)];
                    if (!c || !e.grain) continue;
                    (*cache)[c.get()] = e.grain;
                }
                loadProgressPercent_.store(100, std::memory_order_relaxed);
                if (cancelToken_.load(std::memory_order_acquire)) return;
                std::atomic_store(&activePrerendered_, cache);
                for (const auto& c : bank) {
                    if (!c) continue;
                    auto it = cache->find(c.get());
                    if (it != cache->end()) {
                        shifter_.setSource(it->second);
                        currentAnchorHz_ = c->anchorPitchHz;
                        break;
                    }
                }
                loadState_.store(static_cast<int>(LoadState::Ready),
                                 std::memory_order_release);
                return;
            }
        }

        // ---- Slow path: render via WORLD, then write cache --------------
        std::vector<PrerenderCache::GrainEntry> writeQueue;
        writeQueue.reserve(static_cast<std::size_t>(total));

        for (int i = 0; i < total; ++i) {
            if (cancelToken_.load(std::memory_order_acquire)) return;

            const auto& c = bank[static_cast<std::size_t>(i)];
            if (!c || c->samples.empty()) continue;

            // WORLD analysis (Harvest + CheapTrick + D4C).
            auto raw = analyseGrain(c->samples.data(),
                                    static_cast<int>(c->samples.size()),
                                    static_cast<int>(c->sampleRate));
            if (!raw) continue;
            if (cancelToken_.load(std::memory_order_acquire)) return;

            // Pre-render kNumRatios variants via Synthesis.
            auto prerendered = FormantShifter::preRenderGrain(raw);
            if (prerendered) {
                (*cache)[c.get()] = prerendered;
                PrerenderCache::GrainEntry e;
                e.phonemeIdx    = i;
                e.anchorPitchHz = c->anchorPitchHz;
                e.grain         = prerendered;
                writeQueue.push_back(std::move(e));
            }

            loadProgressPercent_.store(
                (100 * (i + 1)) / total, std::memory_order_relaxed);
        }

        if (cancelToken_.load(std::memory_order_acquire)) return;

        // 4. Install the cache atomically.
        std::atomic_store(&activePrerendered_, cache);

        // 5. Prime the shifter with the first usable grain so process()
        //    has something to play before any onset selects a new clip.
        for (const auto& c : bank) {
            if (!c) continue;
            auto it = cache->find(c.get());
            if (it != cache->end()) {
                shifter_.setSource(it->second);
                currentAnchorHz_ = c->anchorPitchHz;
                break;
            }
        }

        // 6. Persist to disk for next activation.
        if (! bundleHash.empty() && ! writeQueue.empty()) {
            const auto bakeFile = PrerenderCache::pathForHash(bundleHash);
            const bool ok = PrerenderCache::write(
                bakeFile, bundleHash,
                static_cast<int>(bank.front() ? bank.front()->sampleRate : 48000),
                FormantShifter::kSemitoneRange, writeQueue);
            (void) ok;  // best-effort; failures are logged in PrerenderCache.
        }

        loadProgressPercent_.store(100, std::memory_order_relaxed);
        loadState_.store(static_cast<int>(LoadState::Ready),
                         std::memory_order_release);
    });
}

void SungDirectPath::process(const float* guitarIn, float detectedHz,
                             float* wetOut, std::size_t numSamples) noexcept {
    // Drive ClipBankPlayer for onset + grain-index tracking regardless of
    // pre-render state (so the user's note timing is captured even while
    // we're still loading; audio is silent until the cache lands).
    clipBank_.setDetectedPitchHz(detectedHz);
    if (numSamples > grainOutBuf_.size())
        numSamples = grainOutBuf_.size();
    clipBank_.process(guitarIn, grainOutBuf_.data(), numSamples);

    // Atomic snapshot of the active pre-render cache. If it hasn't been
    // installed yet, silence.
    auto cache = std::atomic_load(&activePrerendered_);
    if (!cache) {
        std::fill(wetOut, wetOut + numSamples, 0.0f);
        return;
    }

    // Source change → look up the matching prerendered grain via the
    // active cache (not a member map any more — the message thread can
    // swap it out from under us between blocks).
    const int idx = clipBank_.currentClipIndex();
    if (idx >= 0 && idx != lastSourceIdx_) {
        const auto clip = clipBank_.activeBankAt(idx);
        if (clip) {
            auto it = cache->find(clip.get());
            if (it != cache->end()) shifter_.setSource(it->second);
            currentAnchorHz_ = clip->anchorPitchHz;
        }
        lastSourceIdx_ = idx;
    }

    // Ratio computation (I1 / I2 fixes from final review).
    const float portMs = portamentoMs_.load(std::memory_order_relaxed);
    float targetRatio  = 1.0f;
    if (detectedHz > 0.0f && currentAnchorHz_ > 0.0f) {
        targetRatio = detectedHz / currentAnchorHz_;
    } else if (detectedHz > 0.0f) {
        targetRatio = detectedHz / 220.0f;
    }
    if (targetRatio < 0.25f) targetRatio = 0.25f;
    if (targetRatio > 4.0f)  targetRatio = 4.0f;

    const float alphaBlock = (portMs <= 0.0f || numSamples == 0)
        ? 0.0f
        : std::exp(-1.0f / (static_cast<float>(sampleRate_) * portMs * 0.001f
                            / static_cast<float>(numSamples)));
    smoothedRatio_ = alphaBlock * smoothedRatio_ + (1.0f - alphaBlock) * targetRatio;

    shifter_.setRatio(smoothedRatio_);
    shifter_.process(wetOut, static_cast<int>(numSamples));

    // Note-off gate mirrors ClipBankPlayer's envelope onto the shifter.
    const float gate = clipBank_.currentGateGain();
    if (gate < 1.0f) {
        for (std::size_t i = 0; i < numSamples; ++i) wetOut[i] *= gate;
    }
}

} // namespace guitar_dsp::audio
