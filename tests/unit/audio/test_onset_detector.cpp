#include <catch2/catch_test_macros.hpp>
#include "audio/OnsetDetector.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::OnsetDetector;

namespace {
void pluck(std::vector<float>& buf, int burstLen, int gapLen, float amp, double sr) {
    const int start = static_cast<int>(buf.size());
    for (int i = 0; i < burstLen; ++i) {
        const float env = amp * std::exp(-i / (0.05f * static_cast<float>(sr)));
        buf.push_back(env * std::sin(2.0f*3.14159265f*220.0f*(start+i)/sr));
    }
    for (int i = 0; i < gapLen; ++i) buf.push_back(0.0f);
}
int countOnsets(OnsetDetector& d, const std::vector<float>& buf) {
    int n = 0;
    for (float x : buf) if (d.processSample(x)) ++n;
    return n;
}
}

TEST_CASE("OnsetDetector: one onset per pluck", "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    std::vector<float> buf;
    for (int k = 0; k < 5; ++k) pluck(buf, 4000, 4000, 0.8f, 48000.0);
    REQUIRE(countOnsets(d, buf) == 5);
}

TEST_CASE("OnsetDetector: sustained note fires once", "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    std::vector<float> buf(48000);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = 0.8f * std::sin(2.0f*3.14159265f*220.0f*i/48000.0f);
    REQUIRE(countOnsets(d, buf) == 1);
}

TEST_CASE("OnsetDetector: silence produces no onsets", "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    std::vector<float> buf(48000, 0.0f);
    REQUIRE(countOnsets(d, buf) == 0);
}

TEST_CASE("OnsetDetector: debounce suppresses a rapid double-hit",
          "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    d.setDebounceMs(80.0f);
    std::vector<float> buf;
    pluck(buf, 400, 100, 0.8f, 48000.0);
    pluck(buf, 400, 4000, 0.8f, 48000.0);
    REQUIRE(countOnsets(d, buf) == 1);
}

TEST_CASE("OnsetDetector: 80 ms default debounce blocks a second pulse at 60 ms",
          "[audio][onset_detector][debounce]") {
    // Pulse #1 amp 0.12 so env decays below rearm threshold (0.02) by ~50 ms
    // (release time 30 ms): 0.12 * exp(-50/30) ~= 0.0227, 0.12 * exp(-60/30)
    // ~= 0.0162 < 0.02. That isolates the debounce window from arm/rearm
    // hysteresis: by 60 ms the detector is armed again.
    //
    // Pulse #2 amp 0.08 is just above attack threshold (0.05) -- it would
    // fire instantly IF debounce were <=60 ms (count==2). At the 80 ms
    // default, the gate is closed at t=60 ms; by the time the gate opens at
    // t=80 ms the pulse has decayed to 0.08 * exp(-20/30) ~= 0.041 < attack,
    // so no late-fire either. count==1 pins the 80 ms default.
    using guitar_dsp::audio::OnsetDetector;
    OnsetDetector det;
    det.prepare(48000.0);

    constexpr int kSamples = 48000 / 5;        // 200 ms
    std::vector<float> buf(kSamples, 0.0f);
    buf[0]    = 0.12f;                          // pulse #1 at t=0 (fires)
    buf[2880] = 0.08f;                          // pulse #2 at t=60 ms

    int onsetCount = 0;
    for (float s : buf)
        if (det.processSample(s)) ++onsetCount;
    REQUIRE(onsetCount == 1);
}
