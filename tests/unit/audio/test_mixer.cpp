#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/Mixer.h"
#include "harness/SyntheticGuitar.h"
#include "harness/RealtimeSentinel.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::Mixer;
using guitar_dsp::tests::SyntheticGuitar;
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("Mixer: dry=1 wet=0 is bit-exact passthrough", "[audio][mixer]") {
    Mixer mixer;
    mixer.prepare(48000.0, 512);
    mixer.setDryWet(0.0f);  // 0 = fully dry
    mixer.setMasterGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> dry(512), wet(512, 0.0f), out(512);
    gen.sine(440.0f, 0.4f, dry.data(), dry.size());

    mixer.process(dry.data(), wet.data(), out.data(), out.size());

    for (std::size_t i = 0; i < out.size(); ++i)
        REQUIRE(out[i] == dry[i]);
}

TEST_CASE("Mixer: dry=0 wet=1 is bit-exact wet", "[audio][mixer]") {
    Mixer mixer;
    mixer.prepare(48000.0, 512);
    mixer.setDryWet(1.0f);  // 1 = fully wet
    mixer.setMasterGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> dry(512, 0.0f), wet(512), out(512);
    gen.sine(440.0f, 0.4f, wet.data(), wet.size());

    mixer.process(dry.data(), wet.data(), out.data(), out.size());

    for (std::size_t i = 0; i < out.size(); ++i)
        REQUIRE(out[i] == wet[i]);
}

TEST_CASE("Mixer: master gain scales output", "[audio][mixer]") {
    Mixer mixer;
    mixer.prepare(48000.0, 512);
    mixer.setDryWet(0.0f);
    mixer.setMasterGainDb(-6.0f);  // ~0.5x

    SyntheticGuitar gen{48000.0};
    std::vector<float> dry(512), wet(512, 0.0f), out(512);
    gen.dc(1.0f, dry.data(), dry.size());

    mixer.process(dry.data(), wet.data(), out.data(), out.size());

    // After settling, output should be ~0.5.
    REQUIRE_THAT(out[511], WithinAbs(0.501f, 0.01f));
}

TEST_CASE("Mixer: zero allocations on audio thread", "[audio][mixer][realtime]") {
    Mixer mixer;
    mixer.prepare(48000.0, 512);
    mixer.setDryWet(0.5f);
    mixer.setMasterGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> dry(512), wet(512), out(512);
    gen.sine(440.0f, 0.4f, dry.data(), dry.size());
    gen.sine(220.0f, 0.4f, wet.data(), wet.size());

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 938; ++i)
        mixer.process(dry.data(), wet.data(), out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
