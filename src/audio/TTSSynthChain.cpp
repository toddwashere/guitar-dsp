#include "TTSSynthChain.h"

#include "scenes/Scene.h"

namespace guitar_dsp::audio {

TTSClipPtr synthesizeWithFallback(
    const scenes::TtsConfig& cfg,
    const TTSSourceRegistry& registry,
    const std::function<std::string(const std::string&)>& keyFor,
    int maxDepth)
{
    std::string sourceName = cfg.source;
    std::string fallback   = cfg.fallback;

    for (int depth = 0; depth < maxDepth; ++depth) {
        if (sourceName.empty()) break;
        auto it = registry.find(sourceName);
        if (it != registry.end() && it->second != nullptr) {
            const std::string key = keyFor(sourceName);
            if (!key.empty()) {
                if (auto clip = it->second->synthesize(key)) return clip;
            }
        }
        if (fallback == sourceName) break;  // self-loop guard
        sourceName = fallback;
        fallback.clear();
    }
    return nullptr;
}

} // namespace guitar_dsp::audio
