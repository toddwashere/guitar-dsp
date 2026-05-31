#pragma once

#include <atomic>
#include <cstddef>

#include "TTSClip.h"

namespace guitar_dsp::audio {

// Plays a TTSClip on the audio thread. setClip() is called from the
// message thread; the audio thread sees the new clip on its next
// process() call (atomic shared_ptr swap). Emits silence when no clip
// is active or after the active clip has finished.
class TTSClipPlayer {
public:
    TTSClipPlayer();
    ~TTSClipPlayer() = default;

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message-thread API. Pass nullptr to clear.
    void setClip(TTSClipPtr clip);

    // Audio-thread API. Writes numSamples mono samples into `out`.
    void process(float* out, std::size_t numSamples) noexcept;

private:
    double sampleRate_ = 48000.0;

    std::atomic<bool>   newClipFlag_ {false};
    TTSClipPtr          pendingClip_;     // touched only on message thread
    TTSClipPtr          activeClip_;      // touched only on audio thread
    std::size_t         playPos_ = 0;     // audio thread
};

} // namespace guitar_dsp::audio
