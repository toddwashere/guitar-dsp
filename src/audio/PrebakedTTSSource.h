#pragma once

#include <string>

#include "ITTSSource.h"

namespace guitar_dsp::audio {

// Loads TTS clips from <rootDir>/<key>/audio.wav at synthesize() time.
// Resamples to the prepared sample rate. Returns nullptr on any
// failure (missing file, malformed WAV, wrong format).
class PrebakedTTSSource : public ITTSSource {
public:
    explicit PrebakedTTSSource(std::string rootDir);

    void prepare(double targetSampleRate);

    TTSClipPtr synthesize(const std::string& key) override;
    std::string sourceName() const override { return "prebaked"; }

private:
    std::string rootDir_;
    double      targetSampleRate_ = 48000.0;
};

} // namespace guitar_dsp::audio
