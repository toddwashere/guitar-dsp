# Phase 3 — LLM Clients

> Continuation of [2026-06-12-conversational-ai.md](2026-06-12-conversational-ai.md). Read it first for conventions and Phase 0–2 setup.

**Goal:** Implement `ILlmClient` interface and two concrete impls — `AnthropicClient` (cloud) and `OllamaClient` (local), both driven by `IHttpTransport`.

---

### Task 3.1: `ILlmClient` interface

**Files:**
- Create: `src/ai/ILlmClient.h`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include "ai/ConversationBuffer.h"
#include "ai/IHttpTransport.h"

#include <chrono>
#include <string>
#include <vector>

namespace guitar_dsp::ai {

class CancellationToken;

struct LlmRequest {
    std::vector<Message>      messages;       // system first, then turns
    int                       maxTokens = 80;
    std::chrono::milliseconds timeout {10000};
};

struct LlmReply {
    std::string text;
    std::string error;              // empty on success
    int         promptTokens     = 0;
    int         completionTokens = 0;
};

class ILlmClient {
public:
    virtual ~ILlmClient() = default;
    virtual LlmReply generate(const LlmRequest&,
                              CancellationToken* cancel = nullptr) = 0;
    virtual std::string displayName() const = 0;   // e.g. "Claude Haiku 4.5"
    virtual std::string statusText()  const = 0;   // e.g. "key set ✓"
    virtual std::string modelId()     const = 0;   // e.g. "claude-haiku-4-5"
};

} // namespace guitar_dsp::ai
```

- [ ] **Step 2: Verify compile + commit**

```bash
cmake --build build --target guitar_dsp_ai
git add src/ai/ILlmClient.h
git commit -m "feat(ai): add ILlmClient interface"
```

---

### Task 3.2: `AnthropicClient`

**Files:**
- Create: `src/ai/AnthropicClient.h`
- Create: `src/ai/AnthropicClient.cpp`
- Create: `tests/unit/ai/test_anthropic_client.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ai/AnthropicClient.h"
#include "FakeHttpTransport.h"
#include <juce_core/juce_core.h>
#include <fstream>
#include <sstream>

using guitar_dsp::ai::AnthropicClient;
using guitar_dsp::ai::LlmRequest;
using guitar_dsp::ai::Message;
using guitar_dsp::ai::test::FakeHttpTransport;
using guitar_dsp::ai::HttpResponse;

namespace {
std::string slurp(const char* path) {
    std::ifstream in(path); std::stringstream ss; ss << in.rdbuf(); return ss.str();
}
LlmRequest sampleRequest() {
    LlmRequest r;
    r.messages = {
        {Message::Role::System,    "you are an interviewer"},
        {Message::Role::User,      "tell me about yourself"},
    };
    return r;
}
}

TEST_CASE("AnthropicClient: POSTs to /v1/messages with required headers",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({200, slurp("tests/fixtures/ai/anthropic_response_200.json"), "", {}});
    AnthropicClient c{http, "sk-ant-test", "claude-haiku-4-5"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.error.empty());
    REQUIRE(reply.text == "I've been played by three guys.");
    REQUIRE(http.calls.size() == 1);
    REQUIRE(http.calls[0].url == "https://api.anthropic.com/v1/messages");
    REQUIRE(http.calls[0].headers.at("x-api-key") == "sk-ant-test");
    REQUIRE(http.calls[0].headers.at("anthropic-version") == "2023-06-01");
    REQUIRE(http.calls[0].headers.at("content-type") == "application/json");
}

TEST_CASE("AnthropicClient: body uses top-level system field, messages array has user/assistant only",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({200, slurp("tests/fixtures/ai/anthropic_response_200.json"), "", {}});
    AnthropicClient c{http, "sk-ant-test", "claude-haiku-4-5"};
    c.generate(sampleRequest());
    auto body = juce::JSON::parse(juce::String(http.calls[0].body));
    REQUIRE(body["model"].toString() == "claude-haiku-4-5");
    REQUIRE(int(body["max_tokens"]) == 80);
    REQUIRE(body["system"].toString() == "you are an interviewer");
    auto msgs = body["messages"].getArray();
    REQUIRE(msgs->size() == 1);
    REQUIRE((*msgs)[0]["role"].toString() == "user");
    REQUIRE((*msgs)[0]["content"].toString() == "tell me about yourself");
}

TEST_CASE("AnthropicClient: 401 maps to friendly error",
          "[ai][llm][anthropic]") {
    FakeHttpTransport http;
    http.replies.push({401, slurp("tests/fixtures/ai/anthropic_response_401.json"), "", {}});
    AnthropicClient c{http, "bad-key", "claude-haiku-4-5"};
    auto reply = c.generate(sampleRequest());
    REQUIRE(reply.text.empty());
    REQUIRE(reply.error.find("invalid") != std::string::npos);
}

TEST_CASE("AnthropicClient: 429 maps with retry-after",
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

TEST_CASE("AnthropicClient: transport error (status=0) maps to timed out",
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
```

- [ ] **Step 2: Implement**

`src/ai/AnthropicClient.h`:
```cpp
#pragma once
#include "ai/ILlmClient.h"
namespace guitar_dsp::ai {

class AnthropicClient : public ILlmClient {
public:
    AnthropicClient(IHttpTransport&, std::string apiKey, std::string modelId);
    LlmReply    generate(const LlmRequest&, CancellationToken*) override;
    std::string displayName() const override;
    std::string statusText()  const override;
    std::string modelId()     const override { return modelId_; }
private:
    IHttpTransport& http_;
    std::string     apiKey_;
    std::string     modelId_;
};

} // namespace
```

`src/ai/AnthropicClient.cpp`:
```cpp
#include "ai/AnthropicClient.h"
#include <juce_core/juce_core.h>

namespace guitar_dsp::ai {

AnthropicClient::AnthropicClient(IHttpTransport& h, std::string k, std::string m)
    : http_(h), apiKey_(std::move(k)), modelId_(std::move(m)) {}

std::string AnthropicClient::displayName() const {
    if (modelId_ == "claude-haiku-4-5")  return "Claude Haiku 4.5 (cloud)";
    if (modelId_ == "claude-sonnet-4-6") return "Claude Sonnet 4.6 (cloud)";
    return "Anthropic " + modelId_;
}

std::string AnthropicClient::statusText() const {
    return apiKey_.empty() ? "Anthropic: key missing" : "Anthropic: key set ✓";
}

LlmReply AnthropicClient::generate(const LlmRequest& req, CancellationToken* cancel) {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("model", juce::String(modelId_));
    root->setProperty("max_tokens", req.maxTokens);

    juce::Array<juce::var> msgs;
    std::string systemText;
    for (auto& m : req.messages) {
        if (m.role == Message::Role::System) { systemText = m.text; continue; }
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("role", m.role == Message::Role::User ? "user" : "assistant");
        o->setProperty("content", juce::String(m.text));
        msgs.add(juce::var(o.get()));
    }
    root->setProperty("system", juce::String(systemText));
    root->setProperty("messages", msgs);
    auto body = juce::JSON::toString(juce::var(root.get())).toStdString();

    std::map<std::string,std::string> headers{
        {"x-api-key", apiKey_},
        {"anthropic-version", "2023-06-01"},
        {"content-type", "application/json"},
    };
    auto resp = http_.post("https://api.anthropic.com/v1/messages",
                           headers, body, req.timeout, cancel);

    LlmReply out;
    if (resp.status == 0) { out.error = "timed out"; return out; }
    if (resp.status == 401) { out.error = "API key invalid"; return out; }
    if (resp.status == 429) {
        auto it = resp.headers.find("retry-after");
        auto secs = it != resp.headers.end() ? it->second : "?";
        out.error = "rate limited (retry after " + secs + "s)";
        return out;
    }
    if (resp.status >= 500) { out.error = "Anthropic service error"; return out; }
    if (resp.status != 200) { out.error = "unexpected status " + std::to_string(resp.status); return out; }

    auto parsed = juce::JSON::parse(juce::String(resp.body));
    auto content = parsed["content"].getArray();
    if (! content || content->isEmpty()) { out.error = "malformed response"; return out; }
    out.text = (*content)[0]["text"].toString().toStdString();
    out.promptTokens     = int(parsed["usage"]["input_tokens"]);
    out.completionTokens = int(parsed["usage"]["output_tokens"]);
    return out;
}

} // namespace
```

- [ ] **Step 3: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[ai][llm][anthropic]"
git add src/ai/AnthropicClient.{h,cpp} tests/unit/ai/test_anthropic_client.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ai): add AnthropicClient with FakeHttpTransport-tested error mapping"
```

---

### Task 3.3: `OllamaClient`

**Files:**
- Create: `src/ai/OllamaClient.h`
- Create: `src/ai/OllamaClient.cpp`
- Create: `tests/unit/ai/test_ollama_client.cpp`

- [ ] **Step 1: Tests**

```cpp
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
}

TEST_CASE("OllamaClient: POSTs /api/chat with messages array (system included)",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({200, slurp("tests/fixtures/ai/ollama_chat_response_200.json"), "", {}});
    OllamaClient c{http, "http://localhost:11434", "llama3.2:3b"};
    LlmRequest req;
    req.messages = {
        {Message::Role::System,    "you are an interviewer"},
        {Message::Role::User,      "tell me about yourself"},
    };
    auto reply = c.generate(req);
    REQUIRE(reply.error.empty());
    REQUIRE(reply.text == "I've been played by three guys.");
    REQUIRE(http.calls[0].url == "http://localhost:11434/api/chat");

    auto body = juce::JSON::parse(juce::String(http.calls[0].body));
    REQUIRE(body["model"].toString() == "llama3.2:3b");
    REQUIRE(bool(body["stream"]) == false);
    auto msgs = body["messages"].getArray();
    REQUIRE(msgs->size() == 2);
    REQUIRE((*msgs)[0]["role"].toString() == "system");
}

TEST_CASE("OllamaClient: isRunning true on 200, false on transport error",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({200, "{\"models\":[]}", "", {}});
    REQUIRE(OllamaClient::isRunning(http, "http://localhost:11434"));

    http.replies.push({0, "", "connect refused", {}});
    REQUIRE_FALSE(OllamaClient::isRunning(http, "http://localhost:11434"));
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

TEST_CASE("OllamaClient: connection refused maps to friendly error",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({0, "", "connect failed", {}});
    OllamaClient c{http, "http://localhost:11434", "llama3.2:3b"};
    auto reply = c.generate({});
    REQUIRE(reply.error.find("not running") != std::string::npos);
}

TEST_CASE("OllamaClient: 404 maps to model-not-pulled",
          "[ai][llm][ollama]") {
    FakeHttpTransport http;
    http.replies.push({404, "{}", "", {}});
    OllamaClient c{http, "http://localhost:11434", "llama3.2:3b"};
    auto reply = c.generate({});
    REQUIRE(reply.error.find("not found") != std::string::npos);
    REQUIRE(reply.error.find("ollama pull llama3.2:3b") != std::string::npos);
}
```

- [ ] **Step 2: Implement**

`src/ai/OllamaClient.h`:
```cpp
#pragma once
#include "ai/ILlmClient.h"
namespace guitar_dsp::ai {

class OllamaClient : public ILlmClient {
public:
    OllamaClient(IHttpTransport&, std::string endpoint, std::string modelTag);
    LlmReply    generate(const LlmRequest&, CancellationToken*) override;
    std::string displayName() const override;
    std::string statusText()  const override;
    std::string modelId()     const override { return "ollama:" + modelTag_; }

    static bool                     isRunning(IHttpTransport&, const std::string& endpoint);
    static std::vector<std::string> listInstalledModels(IHttpTransport&, const std::string& endpoint);

private:
    IHttpTransport& http_;
    std::string     endpoint_;
    std::string     modelTag_;
};

} // namespace
```

`src/ai/OllamaClient.cpp`:
```cpp
#include "ai/OllamaClient.h"
#include "ai/CancellationToken.h"
#include <juce_core/juce_core.h>

namespace guitar_dsp::ai {

OllamaClient::OllamaClient(IHttpTransport& h, std::string e, std::string m)
    : http_(h), endpoint_(std::move(e)), modelTag_(std::move(m)) {}

std::string OllamaClient::displayName() const { return modelTag_ + " (local — Ollama)"; }
std::string OllamaClient::statusText()  const { return "Ollama: " + modelTag_; }

LlmReply OllamaClient::generate(const LlmRequest& req, CancellationToken* cancel) {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("model", juce::String(modelTag_));
    root->setProperty("stream", false);
    juce::DynamicObject::Ptr opts = new juce::DynamicObject();
    opts->setProperty("num_predict", req.maxTokens);
    root->setProperty("options", juce::var(opts.get()));

    juce::Array<juce::var> msgs;
    for (auto& m : req.messages) {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        const char* role = m.role == Message::Role::System ? "system"
                        : m.role == Message::Role::User   ? "user" : "assistant";
        o->setProperty("role", role);
        o->setProperty("content", juce::String(m.text));
        msgs.add(juce::var(o.get()));
    }
    root->setProperty("messages", msgs);
    auto body = juce::JSON::toString(juce::var(root.get())).toStdString();

    auto resp = http_.post(endpoint_ + "/api/chat",
                           {{"content-type", "application/json"}},
                           body, req.timeout, cancel);

    LlmReply out;
    if (resp.status == 0)   { out.error = "Ollama: not running"; return out; }
    if (resp.status == 404) {
        out.error = "model not found locally — run: ollama pull " + modelTag_;
        return out;
    }
    if (resp.status != 200) { out.error = "Ollama error " + std::to_string(resp.status); return out; }

    auto parsed = juce::JSON::parse(juce::String(resp.body));
    out.text = parsed["message"]["content"].toString().toStdString();
    return out;
}

bool OllamaClient::isRunning(IHttpTransport& http, const std::string& endpoint) {
    auto r = http.get(endpoint + "/api/tags", std::chrono::milliseconds{500});
    return r.status == 200;
}

std::vector<std::string> OllamaClient::listInstalledModels(
    IHttpTransport& http, const std::string& endpoint) {
    auto r = http.get(endpoint + "/api/tags", std::chrono::milliseconds{1000});
    std::vector<std::string> out;
    if (r.status != 200) return out;
    auto parsed = juce::JSON::parse(juce::String(r.body));
    if (auto* arr = parsed["models"].getArray())
        for (auto& m : *arr) out.push_back(m["name"].toString().toStdString());
    return out;
}

} // namespace
```

- [ ] **Step 3: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[ai][llm][ollama]"
git add src/ai/OllamaClient.{h,cpp} tests/unit/ai/test_ollama_client.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ai): add OllamaClient + isRunning/listInstalledModels"
```

---

## Phase 3 checkpoint — green.
