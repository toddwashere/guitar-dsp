#pragma once

#include <cstddef>
#include <vector>

#include "InputStage.h"
#include "Mixer.h"

namespace guitar_dsp::audio {

// The top-level audio processing graph. In Phase 1 this is just:
//   InputStage -> (passthrough as both dry and wet) -> Mixer -> Output
// Subsequent phases insert the Instrument Carousel and Vocoder branches
// between InputStage and Mixer. All buffers used internally are sized at
// prepare() time; processing is allocation-free.
class AudioGraph {
public:
    AudioGraph();

    void prepare(double sampleRate, int blockSize);
    void reset();

    void process(const float* in, float* out, std::size_t numSamples);

    InputStage& input() { return inputStage_; }
    Mixer& mixer() { return mixer_; }

private:
    InputStage inputStage_;
    Mixer mixer_;

    std::vector<float> postInputBuffer_;
    std::vector<float> wetBuffer_;
};

} // namespace guitar_dsp::audio
