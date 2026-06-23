#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "audio/TTSClip.h"
#include "scenes/Scene.h"
#include "harness/SyntheticGuitar.h"
#include "harness/RealtimeSentinel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <limits>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::tests::SyntheticGuitar;
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("AudioGraph: produces non-silent output for sine input", "[audio][graph]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(512), out(512);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    graph.process(in.data(), out.data(), out.size());

    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    REQUIRE(peak > 0.05f);
}

TEST_CASE("AudioGraph: silence in -> silence out", "[audio][graph]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);

    std::vector<float> in(512, 0.0f), out(512);
    graph.process(in.data(), out.data(), out.size());

    for (float s : out) REQUIRE(std::abs(s) < 1e-3f);
}

TEST_CASE("AudioGraph: zero allocations on audio thread", "[audio][graph][realtime]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(512), out(512);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 938; ++i)
        graph.process(in.data(), out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}

TEST_CASE("AudioGraph: with TTS clip active + dryWet=1, output is vocoded",
          "[audio][graph][vocoder]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);
    graph.mixer().setDryWet(1.0f);
    graph.mixer().setMasterGainDb(0.0f);
    graph.mixer().reset();

    // Inject a clip: 0.5 s of 800 Hz modulator.
    auto clip = std::make_shared<guitar_dsp::audio::TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.resize(24000);
    for (int i = 0; i < 24000; ++i) {
        clip->samples[i] = 0.6f * std::sin(2.0 * 3.14159265 * 800.0 * i / 48000.0);
    }
    graph.ttsClipPlayer().setClip(clip);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000), out(48000);
    gen.sine(800.0f, 0.6f, in.data(), in.size());

    // Process the full second in 512-sample blocks.
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const auto n = std::min<std::size_t>(512, in.size() - i);
        graph.process(in.data() + i, out.data() + i, n);
    }

    // First half: vocoder active -> output non-silent.
    float peakFirst = 0.0f;
    for (int i = 6000; i < 12000; ++i) peakFirst = std::max(peakFirst, std::abs(out[i]));
    REQUIRE(peakFirst > 0.05f);

    // Second half: clip ended -> modulator silent -> output near silent.
    float peakSecond = 0.0f;
    for (int i = 40000; i < 48000; ++i) peakSecond = std::max(peakSecond, std::abs(out[i]));
    REQUIRE(peakSecond < 0.02f);
}

TEST_CASE("AudioGraph: carousel wet-source transforms the guitar",
          "[audio][graph][carousel]") {
    using guitar_dsp::scenes::CarouselConfig;

    AudioGraph g;
    g.prepare(48000.0, 512);
    g.mixer().setDryWet(1.0f);
    g.mixer().setMasterGainDb(0.0f);
    g.mixer().reset();

    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.shaper = CarouselConfig::Shaper::HardClip;
    cfg.drive = 24.0f;
    g.carousel().setConfig(cfg);
    g.setWetSource(AudioGraph::WetSource::Carousel);

    std::vector<float> in(512), out(512);
    for (int i = 0; i < 512; ++i)
        in[static_cast<size_t>(i)] = 0.9f * std::sin(2.0f*3.14159265f*110.0f*i/48000.0f);

    g.process(in.data(), out.data(), out.size());
    g.process(in.data(), out.data(), out.size());

    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak <= 1.05f);
    REQUIRE(peak > 0.3f);
}

TEST_CASE("AudioGraph: noise-carrier diagnostic lets a silent guitar vocode",
          "[audio][graph][diag]") {
    using guitar_dsp::audio::TTSClip;

    auto runPeak = [](bool noiseCarrier) {
        AudioGraph g;
        g.prepare(48000.0, 512);
        g.mixer().setDryWet(1.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();

        auto clip = std::make_shared<TTSClip>();
        clip->sampleRate = 48000.0;
        clip->samples.resize(48000);
        for (int i = 0; i < 48000; ++i)
            clip->samples[static_cast<size_t>(i)] =
                0.6f * std::sin(2.0f * 3.14159265f * 800.0f * i / 48000.0f);
        g.ttsClipPlayer().setClip(clip);
        g.setDiagNoiseCarrier(noiseCarrier);
        // Isolate the *tonal* path so this measures the carrier's contribution
        // only: the sibilance/noise path emits output regardless of the carrier
        // (that's the very leak this diagnostic helps reveal).
        g.setDiagSibilanceOff(true);
        // Disable the built-in broadband carrier floor so the only carrier
        // energy comes from the diagnostic toggle under test.
        g.setVocoderCarrierNoise(0.0f);
        // Disable the speak-clearly blend — this test isolates the vocoder's
        // carrier contribution, and any clarity > 0 would bleed unvocoded
        // speech back in regardless of the carrier.
        g.setClarity(0.0f);

        std::vector<float> in(48000, 0.0f), out(48000, 0.0f);  // silent guitar
        for (std::size_t i = 0; i < in.size(); i += 512) {
            const auto n = std::min<std::size_t>(512, in.size() - i);
            g.process(in.data() + i, out.data() + i, n);
        }
        float peak = 0.0f;
        for (int i = 12000; i < 36000; ++i) peak = std::max(peak, std::fabs(out[static_cast<size_t>(i)]));
        return peak;
    };

    const float off = runPeak(false);
    const float on  = runPeak(true);
    REQUIRE(off < 0.01f);          // silent guitar carrier -> near-no tonal output
    REQUIRE(on  > 0.02f);          // noise carrier -> audible vocoded speech
    REQUIRE(on  > 4.0f * off);
}

TEST_CASE("AudioGraph: bypass-vocoder diagnostic routes the raw modulator",
          "[audio][graph][diag]") {
    using guitar_dsp::audio::TTSClip;

    AudioGraph g;
    g.prepare(48000.0, 512);
    g.mixer().setDryWet(1.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();

    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.resize(48000);
    for (int i = 0; i < 48000; ++i)
        clip->samples[static_cast<size_t>(i)] =
            0.5f * std::sin(2.0f * 3.14159265f * 300.0f * i / 48000.0f);
    g.ttsClipPlayer().setClip(clip);
    g.setDiagBypassVocoder(true);

    std::vector<float> in(48000, 0.0f), out(48000, 0.0f);  // silent guitar
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const auto n = std::min<std::size_t>(512, in.size() - i);
        g.process(in.data() + i, out.data() + i, n);
    }
    float peak = 0.0f;
    for (int i = 6000; i < 36000; ++i) peak = std::max(peak, std::fabs(out[static_cast<size_t>(i)]));
    REQUIRE(peak > 0.3f);  // raw ~0.5-amp modulator passes through (would be ~0 if vocoded)
    for (float x : out) REQUIRE(std::isfinite(x));
}

TEST_CASE("AudioGraph: clarity=1 plays the raw modulator over a silent carrier",
          "[audio][graph][clarity]") {
    using guitar_dsp::audio::TTSClip;

    AudioGraph g;
    g.prepare(48000.0, 512);
    g.mixer().setDryWet(1.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();
    g.setVocoderSibilance(0.0f);
    g.setVocoderCarrierNoise(0.0f);  // make the vocoded path near-silent for a silent carrier

    auto makeClip = []() {
        auto clip = std::make_shared<TTSClip>();
        clip->sampleRate = 48000.0;
        clip->samples.resize(24000);
        for (int i = 0; i < 24000; ++i)
            clip->samples[static_cast<size_t>(i)] =
                0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
        return clip;
    };

    auto peakFor = [&](float clarity) {
        g.ttsClipPlayer().setClip(makeClip());  // fresh clip per run (player consumes it)
        g.setClarity(clarity);
        std::vector<float> in(24000, 0.0f), out(24000, 0.0f);  // silent guitar
        for (std::size_t i = 0; i < in.size(); i += 512) {
            const auto n = std::min<std::size_t>(512, in.size() - i);
            g.process(in.data() + i, out.data() + i, n);
        }
        float peak = 0.0f;
        for (int i = 6000; i < 20000; ++i) peak = std::max(peak, std::fabs(out[static_cast<size_t>(i)]));
        return peak;
    };

    const float vocoded   = peakFor(0.0f);  // current behavior: nothing to vocode -> ~silent
    const float clarified = peakFor(1.0f);  // raw modulator passes through
    INFO("vocoded=" << vocoded << " clarified=" << clarified);
    REQUIRE(vocoded   < 0.05f);
    REQUIRE(clarified > 0.30f);  // modulator at 0.5 amp pushed through makeup+tanh
}

TEST_CASE("AudioGraph: diagnostic toggles are realtime-safe",
          "[audio][graph][diag][realtime]") {
    using guitar_dsp::audio::TTSClip;

    AudioGraph g;
    g.prepare(48000.0, 512);
    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.assign(48000, 0.2f);
    g.ttsClipPlayer().setClip(clip);
    g.setDiagNoiseCarrier(true);
    g.setDiagSibilanceOff(true);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(512), out(512);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 300; ++i)
        g.process(in.data(), out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}

TEST_CASE("AudioGraph: note-stepped modulator vocodes a word on a pluck",
          "[audio][graph][notestep]") {
    using guitar_dsp::audio::TTSClip;

    AudioGraph g;
    g.prepare(48000.0, 1024);
    g.mixer().setDryWet(1.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();

    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.assign(2000, 0.4f);
    clip->words = { {"word", 0, 2000} };
    g.noteSteppedPlayer().setClip(clip);
    g.setModulatorSource(AudioGraph::ModulatorSource::NoteStepped);

    std::vector<float> in(1024), out(1024);
    for (int i = 0; i < 1024; ++i)
        in[static_cast<size_t>(i)] = 0.8f * std::sin(2.0f*3.14159265f*110.0f*i/48000.0f);

    g.process(in.data(), out.data(), out.size());
    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak > 1e-3f);
    for (float x : out) REQUIRE(std::isfinite(x));
}

TEST_CASE("AudioGraph: pitchSinging toggle defaults to off",
          "[audio][graph][pitch_singing]") {
    AudioGraph graph;
    REQUIRE(graph.pitchSinging() == false);
}

TEST_CASE("AudioGraph: toggle-off output is deterministic across instances",
          "[audio][graph][pitch_singing][regression]") {
    // With pitchSinging off, two AudioGraph instances with identical config
    // and identical inputs must produce bit-identical outputs across blocks.
    // This catches accidental dependence on the new PitchTrackedCarrier's
    // internal state (e.g. if it leaked into a shared buffer).
    AudioGraph a, b;
    a.prepare(48000.0, 512);
    b.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(512);
    gen.sine(440.0f, 0.3f, in.data(), in.size());

    std::vector<float> outA(512), outB(512);
    for (int blk = 0; blk < 4; ++blk) {
        a.process(in.data(), outA.data(), in.size());
        b.process(in.data(), outB.data(), in.size());
        for (std::size_t i = 0; i < in.size(); ++i)
            REQUIRE(outA[i] == outB[i]);
    }
}

TEST_CASE("AudioGraph: pitchSinging toggle on -> detected note published",
          "[audio][graph][pitch_singing]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);
    graph.setPitchSinging(true);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    std::vector<float> out(512);
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        graph.process(in.data() + i, out.data(), n);
    }

    REQUIRE(graph.detectedNoteMidi() == 69);  // A4
    REQUIRE(std::abs(graph.detectedCents()) < 20.0f);
    REQUIRE(graph.detectedHz() > 430.0f);
    REQUIRE(graph.detectedHz() < 450.0f);
}

TEST_CASE("AudioGraph: ClipBank modulator source routes clip bank to vocoder",
          "[audio][graph][clip_bank]") {
    using guitar_dsp::audio::AudioGraph;
    using guitar_dsp::audio::TTSClip;

    AudioGraph g;
    g.prepare(48000.0, 512);

    // Put a single clip in the bank so onsets emit a known value.
    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.assign(2000, 0.4f);
    g.clipBankPlayer().setBank({ clip });
    g.setModulatorSource(AudioGraph::ModulatorSource::ClipBank);
    g.mixer().setDryWet(1.0f);  // route wet so vocoder output reaches the mix

    // Drive a pluck-shaped guitar input — both carrier AND onset for the
    // clip-bank player.
    std::vector<float> in(2000, 0.0f), out(2000, 0.0f);
    for (std::size_t i = 0; i < 64; ++i)
        in[i] = 0.9f * std::exp(-static_cast<float>(i) * 0.05f);

    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        g.process(in.data() + i, out.data() + i, n);
    }

    // The cursor must have advanced (proving the clip-bank branch was taken).
    REQUIRE(g.clipBankPlayer().currentClipIndex() == 0);
}

TEST_CASE("AudioGraph: Mic modulator source routes mic samples to vocoder",
          "[audio][graph][mic]") {
    using guitar_dsp::audio::AudioGraph;

    AudioGraph g;
    g.prepare(48000.0, 512);

    // Build a sine-ish "mic" input (well above the gate threshold).
    constexpr std::size_t N = 512;
    std::vector<float> mic(N);
    for (std::size_t i = 0; i < N; ++i)
        mic[i] = 0.3f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);

    g.setModulatorSource(AudioGraph::ModulatorSource::Mic);
    g.setMicBlock(mic.data(), N);
    g.mixer().setDryWet(1.0f);

    // Build a guitar input — sustained sine to act as carrier.
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = 0.4f * std::sin(2.0f * 3.14159265f * 110.0f * i / 48000.0f);

    g.process(in.data(), out.data(), N);

    // The vocoder produces output only when both carrier and modulator are
    // present. With mic silent the output is near-zero; with this mic feed,
    // some output energy must reach the wet bus.
    float peakOut = 0.0f;
    for (float v : out) peakOut = std::max(peakOut, std::fabs(v));
    REQUIRE(peakOut > 0.001f);
}

TEST_CASE("AudioGraph: pitchSinging on with TTS modulator -> wet path peak at guitar's F0",
          "[audio][graph][pitch_singing][vocoder]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);
    graph.setPitchSinging(true);
    graph.mixer().setDryWet(1.0f);
    graph.mixer().setMasterGainDb(0.0f);
    graph.mixer().reset();

    // TTS clip: 1 s of 800 Hz tone — broadband-enough modulator to let
    // multiple vocoder bands pass.
    auto clip = std::make_shared<guitar_dsp::audio::TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.resize(48000);
    for (int i = 0; i < 48000; ++i)
        clip->samples[i] = 0.6f * std::sin(2.0 * 3.14159265 * 800.0 * i / 48000.0);
    graph.ttsClipPlayer().setClip(clip);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(220.0f, 0.5f, in.data(), in.size());

    std::vector<float> out(48000), blockOut(512);
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        graph.process(in.data() + i, blockOut.data(), n);
        std::copy(blockOut.begin(), blockOut.begin() + n, out.begin() + i);
    }

    // Goertzel @ 220 Hz vs 313 Hz over the last 0.5 s.
    auto goertzel = [&](float hz) {
        const float omega = 2.0f * 3.14159265f * hz / 48000.0f;
        const float coef  = 2.0f * std::cos(omega);
        float s1 = 0.0f, s2 = 0.0f;
        for (std::size_t i = 24000; i < out.size(); ++i) {
            const float s = out[i] + coef * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        return s1 * s1 + s2 * s2 - coef * s1 * s2;
    };
    REQUIRE(goertzel(220.0f) > 2.0f * goertzel(313.0f));
}

TEST_CASE("AudioGraph WetSource::SungDirect is selectable and routes wet bus",
          "[audio-graph][sung-direct]") {
    using guitar_dsp::audio::AudioGraph;
    AudioGraph g; g.prepare(48000.0, 256);
    g.setWetSource(AudioGraph::WetSource::SungDirect);
    std::vector<float> in(256, 0.0f), out(256, 0.0f);
    // No bank set → silent output is acceptable; the test only asserts
    // no crash + no NaN/Inf under the new routing.
    g.process(in.data(), out.data(), 256);
    for (float v : out) {
        REQUIRE(!std::isnan(v));
        REQUIRE(!std::isinf(v));
    }
}

TEST_CASE("AudioGraph: rewindSpoken resets the phoneme-stepped player when v2 is active",
          "[audio][graph][rewind][regression]") {
    using guitar_dsp::audio::TTSClip;
    using guitar_dsp::audio::SyllableSpan;
    using guitar_dsp::audio::PhonemeSteppedTTSPlayer;

    AudioGraph g;
    g.prepare(48000.0, 256);
    g.setActiveSpeechPlayer(AudioGraph::ActiveSpeechPlayer::PhonemeStepped);

    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.assign(9000, 0.0f);
    for (int i = 0; i < 9000; ++i)
        clip->samples[i] = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
    auto mk = [](std::size_t s, std::size_t e) {
        SyllableSpan sp;
        sp.startSample = s; sp.endSample = e;
        sp.vowelNucleusSample  = (s + e) / 2;
        sp.attackEndSample     = s + (e - s) / 3;
        sp.codaStartSample     = s + 2 * (e - s) / 3;
        sp.nucleusIsFricative  = false;
        return sp;
    };
    clip->sylsV2 = { mk(0, 3000), mk(3000, 6000), mk(6000, 9000) };

    auto& php = g.phonemeSteppedPlayer();
    php.setClip(clip);

    // Drive the phoneme player past syllable 0 with a synthetic pluck.
    std::vector<float> onset(256, 0.0f), out(256, 0.0f);
    for (std::size_t i = 0; i < 240; ++i) onset[i] = 0.8f;
    php.process(onset.data(), out.data(), out.size());
    REQUIRE(php.currentSyllableIndex() == 0);

    // Now rewind via AudioGraph's API (the path the Rewind pill uses).
    g.rewindSpoken();

    // Consume the pending flag with one quiet process block.
    std::fill(onset.begin(), onset.end(), 0.0f);
    php.process(onset.data(), out.data(), out.size());

    // Pre-fix: stayed at 0 because AudioGraph::rewindSpoken() only rewound
    // the v1 note-stepped player. Post-fix: back to idle (-1).
    REQUIRE(php.currentSyllableIndex() == -1);
}

