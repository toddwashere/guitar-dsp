#pragma once

#include <atomic>
#include <vector>

#include "Scene.h"

namespace guitar_dsp::scenes {

// Owns the loaded scene set and the active-scene state. Designed for one
// message-thread writer (UI / MIDI) and one audio-thread reader.
//
// Cross-thread contract:
//   - loadScenes / activateScene: message thread only
//   - currentMixerParams: audio thread only (lock-free, never allocates)
//   - getActiveSceneId / getActiveScene / getSceneCount: message thread
class SceneEngine {
public:
    SceneEngine();

    // Message-thread API
    void loadScenes(std::vector<Scene> scenes);
    bool activateScene(int id);
    int  getActiveSceneId() const;
    int  getSceneCount() const;
    const Scene& getActiveScene() const;

    // Audio-thread API
    MixerParams currentMixerParams() const noexcept;

private:
    std::vector<Scene>  scenes_;
    int                 activeIndex_ = -1;
    Scene               emptyScene_ {Scene::defaults(-1)};

    std::atomic<float> snapMasterGainDb_ {0.0f};
    std::atomic<float> snapDryWet_       {0.0f};
    std::atomic<float> snapTransitionMs_ {20.0f};

    void publishSnapshot(const MixerParams& m) noexcept;
};

} // namespace guitar_dsp::scenes
