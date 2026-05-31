#include <catch2/catch_test_macros.hpp>

#include "audio/TTSSynthChain.h"
#include "scenes/Scene.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>

using guitar_dsp::audio::ITTSSource;
using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::TTSClipPtr;
using guitar_dsp::audio::TTSSourceRegistry;
using guitar_dsp::audio::synthesizeWithFallback;
using guitar_dsp::scenes::TtsConfig;

namespace {

class AlwaysFailSource : public ITTSSource {
public:
    std::atomic<int> calls{0};
    std::string name_;
    explicit AlwaysFailSource(std::string n) : name_(std::move(n)) {}
    std::string sourceName() const override { return name_; }
    TTSClipPtr synthesize(const std::string&) override { ++calls; return nullptr; }
};

class AlwaysSucceedSource : public ITTSSource {
public:
    std::atomic<int> calls{0};
    std::string name_;
    explicit AlwaysSucceedSource(std::string n) : name_(std::move(n)) {}
    std::string sourceName() const override { return name_; }
    TTSClipPtr synthesize(const std::string& key) override {
        ++calls;
        auto c = std::make_shared<TTSClip>();
        c->name = name_ + ":" + key;
        c->sampleRate = 48000.0;
        c->samples.assign(100, 0.1f);
        return c;
    }
};

auto identityKeyFor = [](const std::string&) {
    return std::string{"text"};
};

} // namespace

TEST_CASE("fallback chain: primary succeeds; fallback untouched",
          "[integration][fallback]") {
    AlwaysSucceedSource piper{"piper"};
    AlwaysSucceedSource apple{"apple"};
    TTSSourceRegistry reg{{"piper", &piper}, {"apple", &apple}};

    TtsConfig cfg;
    cfg.source = "piper";
    cfg.fallback = "apple";
    cfg.text = "hello";

    auto clip = synthesizeWithFallback(cfg, reg, identityKeyFor);
    REQUIRE(clip);
    REQUIRE(clip->name == "piper:text");
    REQUIRE(piper.calls.load() == 1);
    REQUIRE(apple.calls.load() == 0);
}

TEST_CASE("fallback chain: primary fails; fallback runs",
          "[integration][fallback]") {
    AlwaysFailSource piper{"piper"};
    AlwaysSucceedSource apple{"apple"};
    TTSSourceRegistry reg{{"piper", &piper}, {"apple", &apple}};

    TtsConfig cfg;
    cfg.source = "piper";
    cfg.fallback = "apple";
    cfg.text = "hello";

    auto clip = synthesizeWithFallback(cfg, reg, identityKeyFor);
    REQUIRE(clip);
    REQUIRE(clip->name == "apple:text");
    REQUIRE(piper.calls.load() == 1);
    REQUIRE(apple.calls.load() == 1);
}

TEST_CASE("fallback chain: both fail; returns nullptr",
          "[integration][fallback]") {
    AlwaysFailSource piper{"piper"};
    AlwaysFailSource apple{"apple"};
    TTSSourceRegistry reg{{"piper", &piper}, {"apple", &apple}};

    TtsConfig cfg;
    cfg.source = "piper";
    cfg.fallback = "apple";
    cfg.text = "hello";

    auto clip = synthesizeWithFallback(cfg, reg, identityKeyFor);
    REQUIRE_FALSE(clip);
    REQUIRE(piper.calls.load() == 1);
    REQUIRE(apple.calls.load() == 1);
}

TEST_CASE("fallback chain: unknown primary skipped to fallback",
          "[integration][fallback]") {
    AlwaysSucceedSource apple{"apple"};
    TTSSourceRegistry reg{{"apple", &apple}};  // piper missing

    TtsConfig cfg;
    cfg.source = "piper";
    cfg.fallback = "apple";
    cfg.text = "hello";

    auto clip = synthesizeWithFallback(cfg, reg, identityKeyFor);
    REQUIRE(clip);
    REQUIRE(clip->name == "apple:text");
}

TEST_CASE("fallback chain: empty source returns nullptr",
          "[integration][fallback]") {
    AlwaysSucceedSource apple{"apple"};
    TTSSourceRegistry reg{{"apple", &apple}};

    TtsConfig cfg;
    cfg.source = "";
    cfg.fallback = "";

    auto clip = synthesizeWithFallback(cfg, reg, identityKeyFor);
    REQUIRE_FALSE(clip);
}

TEST_CASE("fallback chain: empty key for source returns nullptr",
          "[integration][fallback]") {
    AlwaysSucceedSource apple{"apple"};
    TTSSourceRegistry reg{{"apple", &apple}};

    TtsConfig cfg;
    cfg.source = "apple";
    cfg.text = "";

    auto emptyKey = [](const std::string&) { return std::string{}; };

    auto clip = synthesizeWithFallback(cfg, reg, emptyKey);
    REQUIRE_FALSE(clip);
}

TEST_CASE("fallback chain: primary and fallback both unknown -> nullptr",
          "[integration][fallback]") {
    TTSSourceRegistry reg;  // empty registry

    TtsConfig cfg;
    cfg.source = "piper";
    cfg.fallback = "apple";
    cfg.text = "hello";

    auto clip = synthesizeWithFallback(cfg, reg, identityKeyFor);
    REQUIRE_FALSE(clip);
}

TEST_CASE("fallback chain: primary fails with no fallback -> nullptr",
          "[integration][fallback]") {
    AlwaysFailSource piper{"piper"};
    AlwaysSucceedSource apple{"apple"};  // present but not in chain
    TTSSourceRegistry reg{{"piper", &piper}, {"apple", &apple}};

    TtsConfig cfg;
    cfg.source = "piper";
    cfg.fallback = "";  // explicitly no fallback
    cfg.text = "hello";

    auto clip = synthesizeWithFallback(cfg, reg, identityKeyFor);
    REQUIRE_FALSE(clip);
    REQUIRE(piper.calls.load() == 1);
    REQUIRE(apple.calls.load() == 0);
}
