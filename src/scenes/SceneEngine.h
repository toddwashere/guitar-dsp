#pragma once

#include <atomic>
#include <functional>
#include <string>
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
//
// Snapshot publication is deliberately *not* atomic across the three
// MixerParams fields: each is stored to an independent std::atomic<float>
// with relaxed ordering. An audio-thread read straddling a publishSnapshot
// call can therefore briefly see a mix of scene N's masterGainDb with
// scene N+1's dryWet (a "torn" snapshot). This is tolerated because the
// audio::Mixer downstream applies per-sample exponential smoothing to both
// gain and dryWet, so a one-block torn read is indistinguishable from the
// normal cross-fade between scenes. Avoiding the tear would require a
// seqlock or RCU pattern; the smoothing makes that complexity unnecessary.
class SceneEngine {
public:
    SceneEngine();

    // Message-thread API
    void loadScenes(std::vector<Scene> scenes);
    // Re-load scenes from `directory`. If the previously active scene id
    // still exists, it stays active; otherwise the lowest id is activated.
    void reloadFrom(const std::string& directory);
    bool activateScene(int id);
    int  getActiveSceneId() const;
    int  getSceneCount() const;
    const Scene& getActiveScene() const;
    // Returns the active scene's TTS clip key, or empty string if none.
    // Message-thread only.
    std::string activeTtsKey() const;
    // Returns the active scene's full TTS config (copy). Empty struct if
    // no active scene. Message-thread only.
    TtsConfig activeTtsConfig() const;
    // Returns the active scene's carousel config (copy). Disabled-default
    // struct if no active scene. Message-thread only.
    CarouselConfig activeCarouselConfig() const;
    // Visit every loaded scene (message-thread only). Order is by scene id.
    void forEachScene(const std::function<void(const Scene&)>& fn) const;

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
