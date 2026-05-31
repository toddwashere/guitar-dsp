#include "SceneEngine.h"

#include <algorithm>

namespace guitar_dsp::scenes {

SceneEngine::SceneEngine() = default;

void SceneEngine::loadScenes(std::vector<Scene> scenes) {
    scenes_ = std::move(scenes);
    std::sort(scenes_.begin(), scenes_.end(),
              [](const Scene& a, const Scene& b) { return a.id < b.id; });
    if (scenes_.empty()) {
        activeIndex_ = -1;
        publishSnapshot(MixerParams{});
    } else {
        activeIndex_ = 0;
        publishSnapshot(scenes_[0].mixer);
    }
}

bool SceneEngine::activateScene(int id) {
    auto it = std::find_if(scenes_.begin(), scenes_.end(),
                           [id](const Scene& s) { return s.id == id; });
    if (it == scenes_.end()) return false;
    activeIndex_ = static_cast<int>(std::distance(scenes_.begin(), it));
    publishSnapshot(it->mixer);
    return true;
}

int SceneEngine::getActiveSceneId() const {
    if (activeIndex_ < 0) return -1;
    return scenes_[static_cast<std::size_t>(activeIndex_)].id;
}

int SceneEngine::getSceneCount() const {
    return static_cast<int>(scenes_.size());
}

const Scene& SceneEngine::getActiveScene() const {
    if (activeIndex_ < 0) return emptyScene_;
    return scenes_[static_cast<std::size_t>(activeIndex_)];
}

MixerParams SceneEngine::currentMixerParams() const noexcept {
    MixerParams m;
    m.masterGainDb = snapMasterGainDb_.load(std::memory_order_relaxed);
    m.dryWet       = snapDryWet_.load(std::memory_order_relaxed);
    m.transitionMs = snapTransitionMs_.load(std::memory_order_relaxed);
    return m;
}

void SceneEngine::publishSnapshot(const MixerParams& m) noexcept {
    snapMasterGainDb_.store(m.masterGainDb, std::memory_order_relaxed);
    snapDryWet_.store(m.dryWet, std::memory_order_relaxed);
    snapTransitionMs_.store(m.transitionMs, std::memory_order_relaxed);
}

} // namespace guitar_dsp::scenes
