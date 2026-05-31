#include "PrebakedTTSSource.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>

namespace guitar_dsp::audio {

PrebakedTTSSource::PrebakedTTSSource(std::string rootDir)
    : rootDir_(std::move(rootDir)) {}

void PrebakedTTSSource::prepare(double targetSampleRate) {
    targetSampleRate_ = targetSampleRate;
}

TTSClipPtr PrebakedTTSSource::synthesize(const std::string& key) {
    namespace fs = std::filesystem;
    const fs::path audioPath = fs::path(rootDir_) / key / "audio.wav";
    if (!fs::exists(audioPath)) {
        std::cerr << "[PrebakedTTSSource] missing: " << audioPath << '\n';
        return nullptr;
    }

    juce::File inFile(audioPath.string());
    juce::WavAudioFormat format;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        format.createReaderFor(inFile.createInputStream().release(), true));
    if (!reader) {
        std::cerr << "[PrebakedTTSSource] cannot read: " << audioPath << '\n';
        return nullptr;
    }

    const int srcLen = static_cast<int>(reader->lengthInSamples);
    const double srcRate = reader->sampleRate;

    juce::AudioBuffer<float> srcBuf(1, srcLen);
    reader->read(&srcBuf, 0, srcLen, 0, true, false);

    auto clip = std::make_shared<TTSClip>();
    clip->name = key;
    clip->sampleRate = targetSampleRate_;

    if (std::abs(srcRate - targetSampleRate_) < 0.5) {
        // No resample needed.
        clip->samples.assign(srcBuf.getReadPointer(0),
                             srcBuf.getReadPointer(0) + srcLen);
    } else {
        // Linear resample (good enough for vocoder modulator).
        const double ratio = srcRate / targetSampleRate_;
        const int outLen = static_cast<int>(srcLen / ratio);
        clip->samples.resize(static_cast<std::size_t>(outLen));
        const float* src = srcBuf.getReadPointer(0);
        for (int i = 0; i < outLen; ++i) {
            const double srcIdx = i * ratio;
            const int    i0     = static_cast<int>(srcIdx);
            const float  frac   = static_cast<float>(srcIdx - i0);
            const int    i1     = std::min(i0 + 1, srcLen - 1);
            clip->samples[static_cast<std::size_t>(i)] =
                (1.0f - frac) * src[i0] + frac * src[i1];
        }
    }

    return clip;
}

} // namespace guitar_dsp::audio
