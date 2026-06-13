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

    // Optional: hand-authored syllable timings in meta.json override the
    // WordAligner's energy-gap heuristic, which is unreliable on prebaked
    // clips with continuous prosody (no clean silences between words).
    // Schema:
    //   {
    //     "text": "...hyphenated...",
    //     "syllableTimingsMs": [[start, end], [start, end], ...]
    //   }
    // When present AND the length matches the syllable count from the
    // hyphen-split text, populate clip->syllables directly.
    const fs::path metaPath = fs::path(rootDir_) / key / "meta.json";
    if (fs::exists(metaPath)) {
        const auto json = juce::JSON::parse(
            juce::File(metaPath.string()).loadFileAsString());
        if (auto* obj = json.getDynamicObject()) {
            if (obj->hasProperty("syllableTimingsMs")
                    && obj->hasProperty("text")) {
                const auto text = obj->getProperty("text").toString().toStdString();
                std::vector<std::string> labels;
                {
                    std::string cur;
                    for (char c : text) {
                        if (c == ' ' || c == '\t' || c == '-' || c == '\n') {
                            if (!cur.empty()) { labels.push_back(cur); cur.clear(); }
                        } else cur += c;
                    }
                    if (!cur.empty()) labels.push_back(cur);
                }
                const auto* arr = obj->getProperty("syllableTimingsMs").getArray();
                if (arr && static_cast<std::size_t>(arr->size()) == labels.size()) {
                    for (std::size_t i = 0; i < labels.size(); ++i) {
                        const auto* pair = (*arr)[static_cast<int>(i)].getArray();
                        if (!pair || pair->size() < 2) continue;
                        const double startMs = (double)(*pair)[0];
                        const double endMs   = (double)(*pair)[1];
                        const auto startSample = static_cast<std::size_t>(
                            startMs / 1000.0 * clip->sampleRate);
                        const auto endSample = static_cast<std::size_t>(
                            endMs / 1000.0 * clip->sampleRate);
                        clip->syllables.push_back(
                            WordSegment{labels[i], startSample, endSample});
                    }
                }
            }
        }
    }

    return clip;
}

} // namespace guitar_dsp::audio
