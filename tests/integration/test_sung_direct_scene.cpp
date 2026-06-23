#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "audio/GspeakBundle.h"
#include "app/AssetLocator.h"
#include <cmath>
#include <vector>

using namespace guitar_dsp::audio;

TEST_CASE("Scene 12 SungDirect produces non-silent wet output on a guitar burst",
          "[integration][scene-12]") {
    const double sr = 48000.0;
    const int blk  = 256;
    AudioGraph g;
    g.prepare(sr, blk);

    // Resolve the bundle via AssetLocator so it works in both dev-build
    // and worktree contexts (source-tree path takes precedence).
    const auto bundlePath = guitar_dsp::AssetLocator::resolveForRead(
        "assets/clips/gspeak/scene11_sung_m1.gspeak");
    REQUIRE_FALSE(bundlePath.empty());
    juce::File bundle(bundlePath);
    REQUIRE(bundle.existsAsFile());
    auto loaded = GspeakBundle::read(bundle, sr);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip);

    // Build the per-grain bank using each phoneme's sample range and label.
    // Per-phoneme anchorPitchHz is populated by Task 6 (GspeakBundle v2 reader).
    std::vector<TTSClipPtr> bank;
    for (const auto& p : loaded->clip->phonemes) {
        auto sub = std::make_shared<TTSClip>();
        sub->sampleRate = loaded->clip->sampleRate;
        sub->samples.assign(
            loaded->clip->samples.begin() + static_cast<std::ptrdiff_t>(p.startSample),
            loaded->clip->samples.begin() + static_cast<std::ptrdiff_t>(p.endSample));

        // Map phoneme labels to canonical bankKey strings.
        if (p.label == "a") sub->bankKey = "sung_ah";
        else if (p.label == "e") sub->bankKey = "sung_eh";
        else if (p.label == "i") sub->bankKey = "sung_ee";
        else if (p.label == "o") sub->bankKey = "sung_oh";
        else if (p.label == "u") sub->bankKey = "sung_oo";
        else if (!p.label.empty()) sub->bankKey = std::string("sung_") + p.label;

        sub->anchorPitchHz = p.anchorPitchHz;  // per-phoneme metadata wired in Task 6

        bank.push_back(sub);  // shared_ptr<TTSClip> converts to shared_ptr<const TTSClip>
    }

    // Fall back: if the bundle has no phoneme entries, use the whole clip as
    // a single grain (ensures the test does not trivially pass with an empty bank).
    if (bank.empty()) {
        auto sub = std::make_shared<TTSClip>(*loaded->clip);
        sub->bankKey = "sung_ah";
        bank.push_back(sub);
    }

    REQUIRE_FALSE(bank.empty());

    // Route grains through SungDirectPath (scene 12 routing: no vocoder).
    g.sungDirectPath().setGrainsForBank(bank);
    g.setWetSource(AudioGraph::WetSource::SungDirect);

    // Drain the routing-flag latency.
    std::vector<float> in(blk, 0.0f), out(blk, 0.0f);
    g.process(in.data(), out.data(), blk);

    // 220 Hz guitar tone + periodic transients to drive the onset detector.
    double energy = 0.0;
    for (int b = 0; b < 30; ++b) {
        for (int i = 0; i < blk; ++i) {
            const double t = (b * blk + i) / sr;
            in[i] = 0.4f * static_cast<float>(std::sin(2.0 * M_PI * 220.0 * t));
        }
        if (b % 5 == 0) in[0] += 0.8f;  // periodic onset / note strike
        g.process(in.data(), out.data(), blk);
        for (float v : out) {
            REQUIRE(!std::isnan(v));
            REQUIRE(!std::isinf(v));
            energy += static_cast<double>(v) * v;
        }
    }
    INFO("scene-12 energy = " << energy);
    CHECK(energy > 1e-3);
}
