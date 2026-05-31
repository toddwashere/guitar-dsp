#pragma once

#include <string>

#include "TTSClip.h"

namespace guitar_dsp::audio {

// Source of TTS clips for the vocoder's modulator input. Three concrete
// implementations are planned per spec §6.2:
//   - PrebakedTTSSource (Phase 3, this plan)
//   - AppleTTSSource    (Phase 3.5)
//   - PiperTTSSource    (Phase 3.6)
//
// Synthesize() is called from the message thread (scene activation,
// asset load). It returns a TTSClipPtr that's then handed to a
// TTSClipPlayer for audio-thread playback.
class ITTSSource {
public:
    virtual ~ITTSSource() = default;

    // Returns the clip for `key` (an opaque identifier — for the
    // prebaked source this is a clip directory name; for live sources
    // it's the text to synthesize). May return nullptr on failure;
    // caller falls back to a sibling source per the spec §6.5
    // fallback chain.
    virtual TTSClipPtr synthesize(const std::string& key) = 0;

    // For diagnostics / logging.
    virtual std::string sourceName() const = 0;
};

} // namespace guitar_dsp::audio
