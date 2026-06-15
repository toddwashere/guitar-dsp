#include <catch2/catch_test_macros.hpp>
#include "ai/IHttpTransport.h"
#include "FakeHttpTransport.h"

using guitar_dsp::ai::test::FakeHttpTransport;
using guitar_dsp::ai::HttpResponse;

TEST_CASE("FakeHttpTransport: post records call and returns scripted reply",
          "[ai][http][fake]") {
    FakeHttpTransport http;
    http.replies.push({200, "ok", "", {{"content-type", "text/plain"}}});
    auto r = http.post("https://example.test/x",
                       {{"x-key", "v"}},
                       "body",
                       std::chrono::seconds{1});
    REQUIRE(r.status == 200);
    REQUIRE(r.body == "ok");
    REQUIRE(http.calls.size() == 1);
    REQUIRE(http.calls[0].method == "POST");
    REQUIRE(http.calls[0].url == "https://example.test/x");
    REQUIRE(http.calls[0].headers.at("x-key") == "v");
    REQUIRE(http.calls[0].body == "body");
}

TEST_CASE("FakeHttpTransport: get records call and returns scripted reply",
          "[ai][http][fake]") {
    FakeHttpTransport http;
    http.replies.push({200, "hello", "", {}});
    auto r = http.get("https://example.test/y", std::chrono::seconds{1});
    REQUIRE(r.status == 200);
    REQUIRE(r.body == "hello");
    REQUIRE(http.calls[0].method == "GET");
}

TEST_CASE("FakeHttpTransport: empty queue returns transport-error reply",
          "[ai][http][fake]") {
    FakeHttpTransport http;
    auto r = http.post("https://example.test/z", {}, "", std::chrono::seconds{1});
    REQUIRE(r.status == 0);
    REQUIRE(r.error == "no scripted reply");
}
