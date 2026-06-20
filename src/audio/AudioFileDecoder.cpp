#include "AudioFileDecoder.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace guitar_dsp::audio {

std::optional<AudioFileDecoder::Result>
AudioFileDecoder::decodeMono(const juce::File& file,
                             double requestedSampleRate) {
    if (!file.existsAsFile() || requestedSampleRate <= 0.0)
        return std::nullopt;

    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(file));
    if (!reader) return std::nullopt;

    const auto srcLen = static_cast<int>(reader->lengthInSamples);
    if (srcLen <= 0) return std::nullopt;

    const auto srcRate    = reader->sampleRate;
    const auto srcChans   = (int) reader->numChannels;
    if (srcChans <= 0) return std::nullopt;

    juce::AudioBuffer<float> raw(srcChans, srcLen);
    if (!reader->read(&raw, 0, srcLen, 0, true, srcChans > 1))
        return std::nullopt;

    // Downmix: average all channels into a single mono buffer.
    std::vector<float> mono(static_cast<std::size_t>(srcLen), 0.0f);
    for (int ch = 0; ch < srcChans; ++ch) {
        const float* p = raw.getReadPointer(ch);
        for (int i = 0; i < srcLen; ++i)
            mono[(std::size_t) i] += p[i];
    }
    const float inv = 1.0f / (float) srcChans;
    for (auto& s : mono) s *= inv;

    Result out;
    out.sampleRate = requestedSampleRate;
    out.formatName = reader->getFormatName().toStdString();

    if (std::abs(srcRate - requestedSampleRate) < 0.5) {
        out.samples = std::move(mono);
    } else {
        // Linear resample — same loop as PrebakedTTSSource.cpp:50-64.
        const double ratio  = srcRate / requestedSampleRate;
        const int    outLen = static_cast<int>(srcLen / ratio);
        out.samples.resize(static_cast<std::size_t>(outLen));
        for (int i = 0; i < outLen; ++i) {
            const double srcIdx = i * ratio;
            const int    i0     = static_cast<int>(srcIdx);
            const float  frac   = static_cast<float>(srcIdx - i0);
            const int    i1     = std::min(i0 + 1, srcLen - 1);
            out.samples[(std::size_t) i] =
                (1.0f - frac) * mono[(std::size_t) i0]
                + frac       * mono[(std::size_t) i1];
        }
    }

    if (out.samples.empty()) return std::nullopt;
    return out;
}

} // namespace guitar_dsp::audio
