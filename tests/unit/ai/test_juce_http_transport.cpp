#include <catch2/catch_test_macros.hpp>
#include "ai/JuceHttpTransport.h"
#include "ai/CancellationToken.h"

using guitar_dsp::ai::JuceHttpTransport;
using guitar_dsp::ai::CancellationToken;

TEST_CASE("JuceHttpTransport: unreachable host returns transport error, no crash",
          "[ai][http][juce]") {
    JuceHttpTransport t;
    auto r = t.get("http://127.0.0.1:1/", std::chrono::milliseconds{500});
    REQUIRE(r.status == 0);
    REQUIRE_FALSE(r.error.empty());
}

TEST_CASE("JuceHttpTransport: pre-cancelled token returns immediately",
          "[ai][http][juce]") {
    JuceHttpTransport t;
    CancellationToken c;
    c.cancel();
    auto r = t.get("http://example.com/", std::chrono::seconds{10}, &c);
    REQUIRE(r.status == 0);
    REQUIRE(r.error == "cancelled");
}

TEST_CASE("JuceHttpTransport: pre-cancelled token on POST returns immediately",
          "[ai][http][juce]") {
    JuceHttpTransport t;
    CancellationToken c;
    c.cancel();
    auto r = t.post("http://example.com/", {}, "{}", std::chrono::seconds{10}, &c);
    REQUIRE(r.status == 0);
    REQUIRE(r.error == "cancelled");
}
