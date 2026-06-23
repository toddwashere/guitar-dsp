#include "ClipBankPlayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace guitar_dsp::audio {

ClipBankPlayer::ClipBankPlayer() = default;

void ClipBankPlayer::prepare(double sampleRate, int /*blockSize*/) {
    onset_.prepare(sampleRate);
    // Note-off gate coefficients.
    //   Release time = 20 ms  → coef = exp(-1 / (sr * 0.020))
    //   Hangover     = 100 ms → wait before fade starts after dipping silent
    //   Fade-out     = 10 ms  → smooth click-free stop
    gateReleaseCoef_     = std::exp(-1.0f / static_cast<float>(sampleRate * 0.020));
    gateHangoverSamples_ = static_cast<int>(0.100 * sampleRate);
    gateFadeStep_        = 1.0f / static_cast<float>(sampleRate * 0.010);
    reset();
}

void ClipBankPlayer::reset() {
    onset_.reset();
    cursor_  = -1;
    playPos_ = 0;
    playing_ = false;
    currentClipIndex_.store(-1, std::memory_order_relaxed);

    // Reset gate state — start with envelope at zero, no in-flight fade.
    gateEnv_            = 0.0f;
    gateSilenceCounter_ = 0;
    gateFadeGain_       = 1.0f;
    gateFadingOut_      = false;
}

void ClipBankPlayer::setBank(std::vector<TTSClipPtr> clips) {
    pendingBank_ = std::move(clips);
    bankSize_.store(static_cast<int>(pendingBank_.size()),
                    std::memory_order_relaxed);
    newBankFlag_.store(true, std::memory_order_release);
}

void ClipBankPlayer::rewind() noexcept {
    pendingRewind_.store(true, std::memory_order_release);
}

void ClipBankPlayer::process(const float* onsetSrc, float* modOut,
                             std::size_t numSamples) noexcept {
    // Drain pending bank swap (audio-thread-safe pointer move).
    if (newBankFlag_.exchange(false, std::memory_order_acquire)) {
        activeBank_ = std::move(pendingBank_);
        cursor_  = -1;
        playPos_ = 0;
        playing_ = false;
        onset_.reset();
        currentClipIndex_.store(-1, std::memory_order_relaxed);

        // Recompute anchor-mode + unique key list.
        anchorMode_  = false;
        uniqueKeys_.clear();
        keyCursor_   = -1;
        if (! activeBank_.empty() && activeBank_[0] &&
            ! activeBank_[0]->bankKey.empty()) {
            anchorMode_ = true;
            for (const auto& c : activeBank_) {
                if (! c) continue;
                const auto& k = c->bankKey;
                if (std::find(uniqueKeys_.begin(), uniqueKeys_.end(), k) ==
                    uniqueKeys_.end()) {
                    uniqueKeys_.push_back(k);
                }
            }
        }
    }

    // Drain pending rewind.
    if (pendingRewind_.exchange(false, std::memory_order_acquire)) {
        cursor_    = -1;
        playPos_   = 0;
        playing_   = false;
        keyCursor_ = -1;   // I8: reset anchor-mode key cursor so next onset starts at key 0
        onset_.reset();
        currentClipIndex_.store(-1, std::memory_order_relaxed);
    }

    const bool haveBank = !activeBank_.empty();

    for (std::size_t i = 0; i < numSamples; ++i) {
        // ---- Note-off gate (envelope follower on the guitar input) -------
        // Attack instant, release smoothed — peak-follower style.
        const float absG = std::fabs(onsetSrc[i]);
        if (absG > gateEnv_) {
            gateEnv_ = absG;
        } else {
            gateEnv_ = gateReleaseCoef_ * gateEnv_
                     + (1.0f - gateReleaseCoef_) * absG;
        }
        if (gateEnv_ < kGateSilenceThreshold) {
            if (gateSilenceCounter_ < gateHangoverSamples_) {
                ++gateSilenceCounter_;
            } else if (playing_ && !gateFadingOut_) {
                // Guitar has been silent past the hangover — fade the
                // current grain out.
                gateFadingOut_ = true;
                gateFadeGain_  = 1.0f;
            }
        } else {
            gateSilenceCounter_ = 0;
            // Guitar is back — cancel any in-flight fade.
            if (gateFadingOut_) {
                gateFadingOut_ = false;
                gateFadeGain_  = 1.0f;
            }
        }

        if (haveBank && onset_.processSample(onsetSrc[i])) {
            const int n = static_cast<int>(activeBank_.size());
            int next = -1;
            if (anchorMode_ && ! uniqueKeys_.empty()) {
                // Cycle keys; pick closest anchor within the new key.
                keyCursor_ = (keyCursor_ + 1) % static_cast<int>(uniqueKeys_.size());
                const auto& wantKey = uniqueKeys_[(std::size_t) keyCursor_];
                const float detected = detectedPitchHz_.load(std::memory_order_relaxed);
                if (detected <= 0.0f) {
                    // Pitch unknown — pick first grain of the key.
                    for (int j = 0; j < n; ++j) {
                        const auto& c = activeBank_[(std::size_t) j];
                        if (c && c->bankKey == wantKey) { next = j; break; }
                    }
                } else {
                    float bestDist = std::numeric_limits<float>::infinity();
                    for (int j = 0; j < n; ++j) {
                        const auto& c = activeBank_[(std::size_t) j];
                        if (! c || c->bankKey != wantKey) continue;
                        const float d = std::fabs(c->anchorPitchHz - detected);
                        if (d < bestDist) { bestDist = d; next = j; }
                    }
                }
            }
            if (next < 0) {
                next = (cursor_ + 1) % n;  // legacy round-robin
            }
            cursor_  = next;
            playPos_ = 0;
            playing_ = true;
            // Fresh attack overrides any in-progress gate fade.
            gateFadingOut_      = false;
            gateFadeGain_       = 1.0f;
            gateSilenceCounter_ = 0;
            currentClipIndex_.store(cursor_, std::memory_order_relaxed);
        }

        float s = 0.0f;
        if (playing_) {
            const auto& clip = activeBank_[static_cast<std::size_t>(cursor_)];
            if (clip && playPos_ < clip->samples.size()) {
                s = clip->samples[playPos_++];
            } else {
                playing_ = false;
            }
        }
        // Apply note-off fade if active.
        if (gateFadingOut_) {
            s *= gateFadeGain_;
            gateFadeGain_ -= gateFadeStep_;
            if (gateFadeGain_ <= 0.0f) {
                gateFadingOut_ = false;
                gateFadeGain_  = 0.0f;
                playing_       = false;
            }
        }
        modOut[i] = s;
    }
}

} // namespace guitar_dsp::audio
