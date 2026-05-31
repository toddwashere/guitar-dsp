#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

#include "Carousel.h"
#include "ChannelVocoder.h"
#include "InputStage.h"
#include "Mixer.h"
#include "TTSClipPlayer.h"

namespace guitar_dsp::audio {

// The top-level audio processing graph.
//   InputStage -> dry path -> Mixer -> Output
//                          -> TTSClipPlayer -> ChannelVocoder -> wet path -> Mixer
// All buffers used internally are sized at prepare() time; processing is
// allocation-free.
class AudioGraph {
public:
    AudioGraph();

    void prepare(double sampleRate, int blockSize);
    void reset();

    void process(const float* in, float* out, std::size_t numSamples);

    InputStage& input() { return inputStage_; }
    Mixer& mixer() { return mixer_; }
    TTSClipPlayer& ttsClipPlayer() { return ttsClipPlayer_; }
    ChannelVocoder& vocoder() { return vocoder_; }
    Carousel& carousel() { return carousel_; }

    enum class WetSource { Vocoder, Carousel };
    // Message-thread: choose which branch feeds the Mixer's wet input.
    void setWetSource(WetSource s) noexcept {
        wetSource_.store(static_cast<int>(s), std::memory_order_relaxed);
    }

private:
    InputStage inputStage_;
    Mixer mixer_;
    TTSClipPlayer ttsClipPlayer_;
    ChannelVocoder vocoder_;
    Carousel carousel_;

    std::atomic<int> wetSource_ {static_cast<int>(WetSource::Vocoder)};

    std::vector<float> postInputBuffer_;
    std::vector<float> wetBuffer_;
};

} // namespace guitar_dsp::audio
