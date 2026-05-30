#include "AudioGraph.h"

#include <algorithm>

namespace guitar_dsp::audio {

AudioGraph::AudioGraph() = default;

void AudioGraph::prepare(double sampleRate, int blockSize) {
    inputStage_.prepare(sampleRate, blockSize);
    mixer_.prepare(sampleRate, blockSize);

    postInputBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    wetBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);

    // Default mixer to fully dry until later phases install branches.
    mixer_.setDryWet(0.0f);
    mixer_.setMasterGainDb(0.0f);
    // Snap to desired initial state without ramping.
    mixer_.reset();
}

void AudioGraph::reset() {
    inputStage_.reset();
    mixer_.reset();
    std::fill(postInputBuffer_.begin(), postInputBuffer_.end(), 0.0f);
    std::fill(wetBuffer_.begin(), wetBuffer_.end(), 0.0f);
}

void AudioGraph::process(const float* in, float* out, std::size_t numSamples) {
    // Guard against caller passing a larger block than we prepared for.
    // Truncate rather than allocate. Tests should always call prepare()
    // with the expected max block size.
    numSamples = std::min(numSamples, postInputBuffer_.size());

    inputStage_.process(in, postInputBuffer_.data(), numSamples);
    // Wet path is silent in Phase 1; subsequent phases populate it.
    mixer_.process(postInputBuffer_.data(), wetBuffer_.data(), out, numSamples);
}

} // namespace guitar_dsp::audio
