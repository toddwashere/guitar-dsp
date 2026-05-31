#pragma once

#include <memory>
#include <string>

#include "ITTSSource.h"

namespace guitar_dsp::audio {

// Live TTS source using macOS AVSpeechSynthesizer. `synthesize(text)`
// returns a TTSClipPtr containing PCM audio at the prepared sample rate.
// `text` is the literal text to synthesize (not a clip-directory key
// like PrebakedTTSSource uses).
//
// Synthesis is blocking from the caller's perspective — typical latency
// is 300 ms to 1 s for short phrases. Call from the message thread, NOT
// the audio thread. Use TTSPrewarmer to hide latency at scene activation.
//
// macOS-only. On non-macOS builds, the implementation is a stub that
// returns nullptr.
class AppleTTSSource : public ITTSSource {
public:
    AppleTTSSource();
    ~AppleTTSSource() override;

    void prepare(double targetSampleRate);

    // For AppleTTSSource, `key` is the text to synthesize.
    TTSClipPtr synthesize(const std::string& key) override;
    std::string sourceName() const override { return "apple"; }

    // Optional voice override; pass empty string for the system default.
    // Voice identifiers follow Apple's scheme, e.g.
    // "com.apple.voice.enhanced.en-US.Ava" or "com.apple.voice.compact.en-US.Samantha".
    void setVoice(std::string voiceIdentifier);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    double      targetSampleRate_ = 48000.0;
    std::string voiceIdentifier_;  // empty = system default
};

} // namespace guitar_dsp::audio
