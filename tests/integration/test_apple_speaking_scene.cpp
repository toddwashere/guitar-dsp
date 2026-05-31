#include <catch2/catch_test_macros.hpp>

#include "audio/AppleTTSSource.h"
#include "audio/AudioGraph.h"
#include "harness/SyntheticGuitar.h"
#include "scenes/SceneEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using guitar_dsp::audio::AppleTTSSource;
using guitar_dsp::audio::AudioGraph;
using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::tests::SyntheticGuitar;

namespace {
bool ttsTestEnabled() {
    const char* env = std::getenv("GUITAR_DSP_TEST_APPLE_TTS");
    return env && std::string(env) == "1";
}
}

TEST_CASE("integration: apple-source scene -> vocoder produces speech-shaped output",
          "[integration][speaking][apple][live]") {
    if (!ttsTestEnabled()) {
        SKIP("set GUITAR_DSP_TEST_APPLE_TTS=1 to run");
    }

    AppleTTSSource src;
    src.prepare(48000.0);
    auto clip = src.synthesize("hello hello hello hello hello hello");
    REQUIRE(clip);
    REQUIRE(clip->samples.size() > 24000);  // > 0.5 s

    AudioGraph graph;
    graph.prepare(48000.0, 512);
    graph.ttsClipPlayer().setClip(clip);
    graph.mixer().setDryWet(1.0f);
    graph.mixer().reset();

    SyntheticGuitar gen{48000.0};
    const int N = static_cast<int>(clip->samples.size()) + 12000;  // clip + 0.25 s tail
    std::vector<float> in(N), out(N);
    gen.sine(440.0f, 0.5f, in.data(), N);

    for (int i = 0; i < N; i += 512) {
        const int n = std::min(512, N - i);
        graph.process(in.data() + i, out.data() + i, static_cast<std::size_t>(n));
    }

    auto rms = [](const float* p, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += p[i] * p[i];
        return std::sqrt(s / n);
    };
    const int midIdx = static_cast<int>(clip->samples.size()) / 2;
    const double rmsDuring = rms(out.data() + midIdx, 2000);
    const double rmsAfter  = rms(out.data() + N - 4000, 4000);
    INFO("during=" << rmsDuring << " after=" << rmsAfter);
    REQUIRE(rmsDuring > 0.01);
    REQUIRE(rmsAfter  < rmsDuring * 0.3);
}
