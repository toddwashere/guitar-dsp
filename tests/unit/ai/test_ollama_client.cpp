#include <catch2/catch_test_macros.hpp>
#include "ai/OllamaClient.h"
#include "FakeHttpTransport.h"
#include <juce_core/juce_core.h>
#include <fstream>
#include <sstream>

using guitar_dsp::ai::OllamaClient;
using guitar_dsp::ai::LlmRequest;
using guitar_dsp::ai::Message;
using guitar_dsp::ai::test::FakeHttpTransport;

namespace {
std::string slurp(const char* p) {
    std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str();
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

TEST_CASE("OllamaClient: POST /api/chat with system role IN messages array",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({200, slurp("tests/fixtures/ai/ollama_chat_response_200.json"), "", {}});
    OllamaClient c{http, "http://localhost:11434", "llama3.2:3b"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.empty());
    REQUIRE(reply.text == "I've been played by three guys.");
    REQUIRE(http.calls.size() == 1);
    REQUIRE(http.calls[0].method == "POST");
    REQUIRE(http.calls[0].url == "http://localhost:11434/api/chat");

    auto body = juce::JSON::parse(juce::String(http.calls[0].body));
    REQUIRE(body["model"].toString() == "llama3.2:3b");
    REQUIRE(bool(body["stream"]) == false);
    auto* msgs = body["messages"].getArray();
    REQUIRE(msgs != nullptr);
    REQUIRE(msgs->size() == 2);
    REQUIRE((*msgs)[0]["role"].toString() == "system");
    REQUIRE((*msgs)[1]["role"].toString() == "user");
}

TEST_CASE("OllamaClient: includes num_predict in options",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({200, slurp("tests/fixtures/ai/ollama_chat_response_200.json"), "", {}});
    OllamaClient c{http, "http://localhost:11434", "llama3.2:3b"};
    LlmRequest req;
    req.messages = { {Message::Role::User, "hi"} };
    req.maxTokens = 42;
    c.generate(req);
    auto body = juce::JSON::parse(juce::String(http.calls[0].body));
    REQUIRE(int(body["options"]["num_predict"]) == 42);
}

TEST_CASE("OllamaClient: isRunning true on 200, false on transport error",
          "[ai][llm][ollama]") {
    {
        FakeHttpTransport http;
        http.replies.push({200, "{\"models\":[]}", "", {}});
        REQUIRE(OllamaClient::isRunning(http, "http://localhost:11434"));
    }
    {
        FakeHttpTransport http;
        http.replies.push({0, "", "connect refused", {}});
        REQUIRE_FALSE(OllamaClient::isRunning(http, "http://localhost:11434"));
    }
}

TEST_CASE("OllamaClient: listInstalledModels parses tags response",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({200, slurp("tests/fixtures/ai/ollama_tags_response.json"), "", {}});
    auto m = OllamaClient::listInstalledModels(http, "http://localhost:11434");
    REQUIRE(m.size() == 2);
    REQUIRE(m[0] == "llama3.2:3b");
    REQUIRE(m[1] == "qwen2.5:3b");
}

TEST_CASE("OllamaClient: listInstalledModels returns empty on transport error",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({0, "", "connect refused", {}});
    auto m = OllamaClient::listInstalledModels(http, "http://localhost:11434");
    REQUIRE(m.empty());
}

TEST_CASE("OllamaClient: connection refused maps to 'Ollama: not running'",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({0, "", "connect failed", {}});
    OllamaClient c{http, "http://localhost:11434", "llama3.2:3b"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.find("not running") != std::string::npos);
}

TEST_CASE("OllamaClient: 404 maps to 'model not found — run: ollama pull <tag>'",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({404, "{}", "", {}});
    OllamaClient c{http, "http://localhost:11434", "llama3.2:3b"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.find("not found") != std::string::npos);
    REQUIRE(reply.error.find("ollama pull llama3.2:3b") != std::string::npos);
}

TEST_CASE("OllamaClient: 5xx maps to friendly error",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({500, "{}", "", {}});
    OllamaClient c{http, "http://localhost:11434", "llama3.2:3b"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.find("error") != std::string::npos);
}

TEST_CASE("OllamaClient: displayName + statusText + modelId",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    OllamaClient c{http, "http://localhost:11434", "llama3.2:3b"};
    REQUIRE(c.displayName().find("llama3.2:3b") != std::string::npos);
    REQUIRE(c.displayName().find("local") != std::string::npos);
    REQUIRE(c.statusText().find("Ollama") != std::string::npos);
    REQUIRE(c.modelId() == "ollama:llama3.2:3b");
}
