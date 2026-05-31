#include <catch2/catch_test_macros.hpp>

#include "audio/TTSPrewarmer.h"
#include "audio/ITTSSource.h"

#include <atomic>
#include <chrono>
#include <thread>

using guitar_dsp::audio::ITTSSource;
using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::TTSClipPtr;
using guitar_dsp::audio::TTSPrewarmer;

namespace {

// Test double: deterministic, counts synthesize calls.
class FakeSource : public ITTSSource {
public:
    std::atomic<int> calls{0};
    std::string sourceName() const override { return "fake"; }
    TTSClipPtr synthesize(const std::string& key) override {
        ++calls;
        if (key == "fail") return nullptr;
        auto c = std::make_shared<TTSClip>();
        c->name = key;
        c->sampleRate = 48000.0;
        c->samples.assign(100, 0.5f);
        return c;
    }
};

} // namespace

TEST_CASE("TTSPrewarmer: enqueue + waitForKey returns the synthesized clip",
          "[audio][tts][prewarmer]") {
    FakeSource src;
    TTSPrewarmer pw(src);
    pw.enqueue("hello");

    auto clip = pw.waitForKey("hello", std::chrono::seconds(2));
    REQUIRE(clip);
    REQUIRE(clip->name == "hello");
    REQUIRE(src.calls.load() == 1);
}

TEST_CASE("TTSPrewarmer: takeIfReady returns nullptr when not yet synthesized",
          "[audio][tts][prewarmer]") {
    FakeSource src;
    TTSPrewarmer pw(src);
    REQUIRE_FALSE(pw.takeIfReady("not_enqueued"));
}

TEST_CASE("TTSPrewarmer: duplicate enqueue does not re-synthesize",
          "[audio][tts][prewarmer]") {
    FakeSource src;
    TTSPrewarmer pw(src);
    pw.enqueue("dup");
    REQUIRE(pw.waitForKey("dup", std::chrono::seconds(2)));
    pw.enqueue("dup");  // already cached; should be a no-op
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(src.calls.load() == 1);
}

TEST_CASE("TTSPrewarmer: synthesize failure caches nullptr (no retry loop)",
          "[audio][tts][prewarmer]") {
    FakeSource src;
    TTSPrewarmer pw(src);
    pw.enqueue("fail");
    auto clip = pw.waitForKey("fail", std::chrono::seconds(2));
    REQUIRE_FALSE(clip);
    pw.enqueue("fail");  // should NOT re-attempt
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(src.calls.load() == 1);
}
