#include <catch2/catch_test_macros.hpp>
#include "ai/AnthropicClient.h"
#include "FakeHttpTransport.h"
#include <juce_core/juce_core.h>
#include <fstream>
#include <sstream>

using guitar_dsp::ai::AnthropicClient;
using guitar_dsp::ai::LlmRequest;
using guitar_dsp::ai::Message;
using guitar_dsp::ai::HttpResponse;
using guitar_dsp::ai::test::FakeHttpTransport;

namespace {
std::string slurp(const char* path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
LlmRequest sampleRequest() {
    LlmRequest r;
    r.messages = {
        {Message::Role::System, "you are an interviewer"},
        {Message::Role::User,   "tell me about yourself"},
    };
    return r;
}
}

TEST_CASE("AnthropicClient: POST to /v1/messages with required headers",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({200, slurp("tests/fixtures/ai/anthropic_response_200.json"), "", {}});
    AnthropicClient c{http, "sk-ant-test", "claude-haiku-4-5"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.empty());
    REQUIRE(reply.text == "I've been played by three guys.");
    REQUIRE(reply.promptTokens == 12);
    REQUIRE(reply.completionTokens == 8);
    REQUIRE(http.calls.size() == 1);
    REQUIRE(http.calls[0].method == "POST");
    REQUIRE(http.calls[0].url == "https://api.anthropic.com/v1/messages");
    REQUIRE(http.calls[0].headers.at("x-api-key") == "sk-ant-test");
    REQUIRE(http.calls[0].headers.at("anthropic-version") == "2023-06-01");
    REQUIRE(http.calls[0].headers.at("content-type") == "application/json");
}

TEST_CASE("AnthropicClient: body uses top-level system; messages contain only user/assistant",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({200, slurp("tests/fixtures/ai/anthropic_response_200.json"), "", {}});
    AnthropicClient c{http, "sk-ant-test", "claude-haiku-4-5"};
    c.generate(sampleRequest());

    auto body = juce::JSON::parse(juce::String(http.calls[0].body));
    REQUIRE(body["model"].toString() == "claude-haiku-4-5");
    REQUIRE(int(body["max_tokens"]) == 80);
    REQUIRE(body["system"].toString() == "you are an interviewer");
    auto* msgs = body["messages"].getArray();
    REQUIRE(msgs != nullptr);
    REQUIRE(msgs->size() == 1);   // System filtered out, only user left
    REQUIRE((*msgs)[0]["role"].toString() == "user");
    REQUIRE((*msgs)[0]["content"].toString() == "tell me about yourself");
}

TEST_CASE("AnthropicClient: 401 maps to 'API key invalid'",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({401, slurp("tests/fixtures/ai/anthropic_response_401.json"), "", {}});
    AnthropicClient c{http, "bad-key", "claude-haiku-4-5"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.text.empty());
    REQUIRE(reply.error.find("invalid") != std::string::npos);
}

TEST_CASE("AnthropicClient: 429 with retry-after maps to 'rate limited (retry after Ns)'",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({429, "{}", "", {{"retry-after", "5"}}});
    AnthropicClient c{http, "k", "claude-haiku-4-5"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.find("rate limited") != std::string::npos);
    REQUIRE(reply.error.find("5") != std::string::npos);
}

TEST_CASE("AnthropicClient: 5xx maps to service error",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({502, "{}", "", {}});
    AnthropicClient c{http, "k", "claude-haiku-4-5"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.find("service error") != std::string::npos);
}

TEST_CASE("AnthropicClient: transport error (status=0) maps to 'timed out'",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({0, "", "connect failed", {}});
    AnthropicClient c{http, "k", "claude-haiku-4-5"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.find("timed out") != std::string::npos);
}

TEST_CASE("AnthropicClient: statusText reports key state",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    AnthropicClient missing{http, "", "claude-haiku-4-5"};
    REQUIRE(missing.statusText().find("key missing") != std::string::npos);
    AnthropicClient set{http, "sk-ant-x", "claude-haiku-4-5"};
    REQUIRE(set.statusText().find("key set") != std::string::npos);
}

TEST_CASE("AnthropicClient: displayName reflects model id",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    AnthropicClient haiku{http, "k", "claude-haiku-4-5"};
    REQUIRE(haiku.displayName().find("Haiku") != std::string::npos);
    AnthropicClient sonnet{http, "k", "claude-sonnet-4-6"};
    REQUIRE(sonnet.displayName().find("Sonnet") != std::string::npos);
}

TEST_CASE("AnthropicClient: modelId returns the configured model",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    AnthropicClient c{http, "k", "claude-haiku-4-5"};
    REQUIRE(c.modelId() == "claude-haiku-4-5");
}

TEST_CASE("AnthropicClient: empty API key returns pre-flight error without HTTP call",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    AnthropicClient c{http, "", "claude-haiku-4-5"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.find("key missing") != std::string::npos);
    REQUIRE(http.calls.empty());   // never reached HTTP
}

TEST_CASE("AnthropicClient: non-special status maps to 'unexpected status N'",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({418, "{}", "", {}});  // I'm a teapot
    AnthropicClient c{http, "k", "claude-haiku-4-5"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.find("unexpected status") != std::string::npos);
    REQUIRE(reply.error.find("418") != std::string::npos);
}
