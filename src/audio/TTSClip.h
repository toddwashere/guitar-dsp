#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace guitar_dsp::audio {

// In-memory mono TTS clip. Owned by an `ITTSSource`, consumed by
// `TTSClipPlayer` on the audio thread. The samples vector is resized
// only off the audio thread.
struct TTSClip {
    std::string  name;             // for debug/UI
    double       sampleRate = 48000.0;
    std::vector<float> samples;    // mono float32, at sampleRate

    bool empty() const noexcept { return samples.empty(); }
    std::size_t lengthSamples() const noexcept { return samples.size(); }
    double durationSeconds() const noexcept {
        return sampleRate > 0.0 ? samples.size() / sampleRate : 0.0;
    }
};

using TTSClipPtr = std::shared_ptr<const TTSClip>;

} // namespace guitar_dsp::audio
