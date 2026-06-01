#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"
#include "audio/TTSClip.h"

#include <cmath>
#include <memory>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::audio::TTSClip;

namespace {
void pluck(AudioGraph& g, int n) {
    std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        in[static_cast<size_t>(i)] = 0.8f * std::sin(2.0f*3.14159265f*110.0f*i/48000.0f);
    g.process(in.data(), out.data(), out.size());
}
void quiet(AudioGraph& g, int n) {
    std::vector<float> in(static_cast<size_t>(n), 0.0f), out(static_cast<size_t>(n), 0.0f);
    g.process(in.data(), out.data(), out.size());
}
}

TEST_CASE("integration: plucks step through words and loop",
          "[integration][notestep]") {
    AudioGraph g;
    // Prepare a block size large enough for the longest block fed below.
    // (AudioGraph truncates blocks larger than the prepared size, so the
    // 8000-sample silence gaps must fit to let the onset detector re-arm.)
    g.prepare(48000.0, 8192);
    g.mixer().setDryWet(1.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();

    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.assign(3000, 0.3f);
    clip->words = { {"a",0,1000}, {"b",1000,2000}, {"c",2000,3000} };
    g.noteSteppedPlayer().setClip(clip);
    g.setModulatorSource(AudioGraph::ModulatorSource::NoteStepped);

    REQUIRE(g.noteSteppedPlayer().currentWordIndex() == -1);

    pluck(g, 2048); REQUIRE(g.noteSteppedPlayer().currentWordIndex() == 0); quiet(g, 8000);
    pluck(g, 2048); REQUIRE(g.noteSteppedPlayer().currentWordIndex() == 1); quiet(g, 8000);
    pluck(g, 2048); REQUIRE(g.noteSteppedPlayer().currentWordIndex() == 2); quiet(g, 8000);
    pluck(g, 2048); REQUIRE(g.noteSteppedPlayer().currentWordIndex() == 0);  // looped
}
