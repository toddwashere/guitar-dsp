#include "TTSClipPlayer.h"

#include <algorithm>
#include <cstring>

namespace guitar_dsp::audio {

TTSClipPlayer::TTSClipPlayer() = default;

void TTSClipPlayer::prepare(double sampleRate, int /*blockSize*/) {
    sampleRate_ = sampleRate;
    reset();
}

void TTSClipPlayer::reset() {
    playPos_ = 0;
}

void TTSClipPlayer::setClip(TTSClipPtr clip) {
    pendingClip_ = std::move(clip);
    newClipFlag_.store(true, std::memory_order_release);
}

void TTSClipPlayer::process(float* out, std::size_t numSamples) noexcept {
    // Pick up a pending clip if any. The shared_ptr move is allocation-
    // free; we deliberately move into `activeClip_` so the previous
    // active clip's destructor runs here (on the audio thread). The
    // previous clip's refcount is decremented; if this was the last
    // ref, the deallocation happens on the audio thread — that's a
    // soft real-time violation we accept for v1 (deallocations during
    // a hot scene change are rare and the cost is bounded). A future
    // hardening pass can swap in a deferred-deletion mechanism.
    if (newClipFlag_.exchange(false, std::memory_order_acquire)) {
        activeClip_ = std::move(pendingClip_);
        playPos_ = 0;
    }

    if (!activeClip_ || activeClip_->samples.empty()) {
        std::memset(out, 0, numSamples * sizeof(float));
        return;
    }

    const auto& samples = activeClip_->samples;
    const std::size_t remaining = (playPos_ < samples.size())
                                ? samples.size() - playPos_
                                : 0;
    const std::size_t toCopy = std::min(numSamples, remaining);

    if (toCopy > 0) {
        std::memcpy(out, samples.data() + playPos_, toCopy * sizeof(float));
        playPos_ += toCopy;
    }
    if (toCopy < numSamples) {
        std::memset(out + toCopy, 0, (numSamples - toCopy) * sizeof(float));
    }
}

} // namespace guitar_dsp::audio
