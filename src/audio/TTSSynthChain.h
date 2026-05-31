#pragma once

#include <functional>
#include <map>
#include <string>

#include "ITTSSource.h"
#include "TTSClip.h"

namespace guitar_dsp::scenes { struct TtsConfig; }

namespace guitar_dsp::audio {

using TTSSourceRegistry = std::map<std::string, ITTSSource*>;

// Synthesize cfg.source's clip; on nullptr, try cfg.fallback once.
// Returns nullptr if both fail. `keyFor(sourceName)` returns the right
// key for that source (prebaked wants the clip name; live sources want
// the text). Chain depth is capped at `maxDepth` to prevent loops.
TTSClipPtr synthesizeWithFallback(
    const scenes::TtsConfig& cfg,
    const TTSSourceRegistry& registry,
    const std::function<std::string(const std::string&)>& keyFor,
    int maxDepth = 4);

} // namespace guitar_dsp::audio
