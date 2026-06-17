#include <catch2/catch_test_macros.hpp>
#include "audio/PhonemeSteppedTTSPlayer.h"
#include "audio/TTSClip.h"
#include "harness/RealtimeSentinel.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace guitar_dsp::audio;
using guitar_dsp::tests::RealtimeSentinel;

namespace {
std::shared_ptr<const TTSClip> threeSylClip() {
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    c->samples.assign(9000, 0.0f);
    for (int i = 0; i < 9000; ++i)
        c->samples[i] = 0.5f * std::sin(2*3.14159265f*220.0f*i/48000.0f);

    auto mk = [](std::size_t s, std::size_t e) {
        SyllableSpan sp;
        sp.startSample = s;
        sp.endSample = e;
        sp.vowelNucleusSample = (s + e) / 2;
        sp.attackEndSample = s + (e - s) / 3;
        sp.codaStartSample = s + 2 * (e - s) / 3;
        sp.nucleusIsFricative = false;
        return sp;
    };
    c->sylsV2 = { mk(0, 3000), mk(3000, 6000), mk(6000, 9000) };
    return c;
}

void runBlock(PhonemeSteppedTTSPlayer& p, std::size_t n,
              bool onsetAtStart, std::vector<float>& out) {
    std::vector<float> onset(n, 0.0f);
    if (onsetAtStart) {
        const std::size_t onsetLen = std::min(n, std::size_t{480});
        for (std::size_t i = 0; i < onsetLen; ++i) onset[i] = 0.8f;
    }
    out.assign(n, 0.0f);
    p.process(onset.data(), out.data(), out.size());
}
}

TEST_CASE("PhonemeSteppedTTSPlayer: idle until first onset",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    runBlock(p, 1000, false, out);
    for (float v : out) REQUIRE(v == 0.0f);
}

TEST_CASE("PhonemeSteppedTTSPlayer: first onset starts syllable 0 Attack",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    runBlock(p, 800, true, out);  // less than attackEnd (1000)
    REQUIRE(p.currentSyllableIndex() == 0);
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Attack);
    bool sawNonZero = false;
    for (float v : out) if (v != 0.0f) { sawNonZero = true; break; }
    REQUIRE(sawNonZero);
}

TEST_CASE("PhonemeSteppedTTSPlayer: held note enters Sustain",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    runBlock(p, 4000, true, out);  // long enough to enter Sustain
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Sustain);
}

TEST_CASE("PhonemeSteppedTTSPlayer: second onset during Sustain → Coda then advance",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    runBlock(p, 4000, true, out);   // syl 0 Attack→Sustain
    // The OnsetDetector requires silence to re-arm after the first onset
    // (release time ~30 ms → ~1440 samples; give ≥2000 samples of silence
    // so env decays below rearmThreshold before the second onset block).
    runBlock(p, 2000, false, out);  // silence → re-arm the detector
    runBlock(p, 100, true, out);    // second onset → Sustain→Coda
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Coda);
    runBlock(p, 4000, false, out);
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Idle);
    runBlock(p, 2000, false, out);  // silence → re-arm for third onset
    runBlock(p, 100, true, out);    // third onset → Idle→advanceToNext
    REQUIRE(p.currentSyllableIndex() == 1);
}

TEST_CASE("PhonemeSteppedTTSPlayer: rewind resets to start",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    runBlock(p, 800, true, out);
    p.rewind();
    runBlock(p, 100, true, out);
    REQUIRE(p.currentSyllableIndex() == 0);
}

TEST_CASE("PhonemeSteppedTTSPlayer: maxSustainMs=0 plays syllable linearly to end",
          "[audio][phstep]") {
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    // 3000-sample syllable. Audio is a ramp 0.0 -> 1.0 so we can tell
    // exactly which samples were played (output value == input value).
    c->samples.assign(3000, 0.0f);
    for (int i = 0; i < 3000; ++i) c->samples[i] = float(i) / 3000.0f;
    // Syllable spans the whole clip. attackEnd=1000 (1/3), codaStart=2000 (2/3).
    SyllableSpan s;
    s.startSample = 0;
    s.endSample = 3000;
    s.vowelNucleusSample = 1500;
    s.attackEndSample = 1000;
    s.codaStartSample = 2000;
    s.nucleusIsFricative = false;
    c->sylsV2 = { s };

    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(c);
    p.setMaxSustainMs(0.0);   // disable Sustain

    // First 480 samples: onset pulse; the rest: silence to drain Attack.
    std::vector<float> mixed(3500, 0.0f);
    for (std::size_t i = 0; i < 480; ++i) mixed[i] = 0.8f;
    std::vector<float> out(3500, 0.0f);
    p.process(mixed.data(), out.data(), out.size());

    // The middle of the ramp (sample 1500 -> value ~0.5) MUST appear in
    // output. With the old jump-to-codaStartSample bug, value ~0.4..0.6
    // would be entirely missing (the vowel body was skipped).
    bool sawMiddle = false;
    for (float v : out) if (v > 0.45f && v < 0.55f) { sawMiddle = true; break; }
    REQUIRE(sawMiddle);

    // End of ramp (value > 0.95) must also appear (played all the way through).
    bool sawEnd = false;
    for (float v : out) if (v > 0.95f) { sawEnd = true; break; }
    REQUIRE(sawEnd);
}

TEST_CASE("PhonemeSteppedTTSPlayer: process() is allocation/lock-free",
          "[audio][phstep][rt]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> onset(1000, 0.4f), mod(1000, 0.0f);
    RealtimeSentinel rts;
    rts.markCurrentThreadAsRealtime();
    p.process(onset.data(), mod.data(), mod.size());
    rts.unmarkCurrentThreadAsRealtime();
    REQUIRE(rts.violations() == 0);
}

TEST_CASE("PhonemeSteppedTTSPlayer: currentPlaySample published for the visualizer",
          "[audio][phstep][visualizer]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());     // syls at 0-3000, 3000-6000, 6000-9000

    // Idle: nothing playing yet.
    REQUIRE(p.currentPlaySample() == -1);

    // First pluck → Attack on syllable 0. After ~800 samples consumed,
    // playhead should be inside syllable 0's range and > 0.
    std::vector<float> out;
    runBlock(p, 800, true, out);
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Attack);
    REQUIRE(p.currentPlaySample() >= 0);
    REQUIRE(p.currentPlaySample() < 3000);

    // Hold long enough to enter Sustain — playhead should report the
    // vowel-nucleus sample of syllable 0 (parked, not the grain offset).
    runBlock(p, 4000, true, out);
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Sustain);
    REQUIRE(p.currentPlaySample() == 1500);  // (0 + 3000) / 2 per threeSylClip()

    // Rewind → playhead returns to -1.
    p.rewind();
    runBlock(p, 100, false, out);
    REQUIRE(p.currentPlaySample() == -1);
}
