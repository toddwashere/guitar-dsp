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
    return apiKey_.empty() ? "Anthropic: key missing"
                           : "Anthropic: key set";
}

LlmReply AnthropicClient::generate(const LlmRequest& req, CancellationToken* cancel) {
    if (apiKey_.empty()) {
        LlmReply out;
        out.error = "Anthropic: key missing";
        return out;
    }

    // Build request body
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("model", juce::String(modelId_));
    root->setProperty("max_tokens", req.maxTokens);

    juce::Array<juce::var> msgs;
    std::string systemText;
    for (auto& m : req.messages) {
        if (m.role == Message::Role::System) {
            systemText = m.text;
            continue;
        }
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("role", m.role == Message::Role::User ? "user" : "assistant");
        o->setProperty("content", juce::String(m.text));
        msgs.add(juce::var(o.get()));
    }
    root->setProperty("system", juce::String(systemText));
    root->setProperty("messages", msgs);
    auto body = juce::JSON::toString(juce::var(root.get())).toStdString();

    std::map<std::string, std::string> headers{
        {"x-api-key",         apiKey_},
        {"anthropic-version", "2023-06-01"},
        {"content-type",      "application/json"},
    };

    auto resp = http_.post("https://api.anthropic.com/v1/messages",
                           headers, body, req.timeout, cancel);

    LlmReply out;

    // Map status codes to friendly errors
    if (resp.status == 0) {
        out.error = "timed out";
        return out;
    }
    if (resp.status == 401) {
        out.error = "API key invalid";
        return out;
    }
    if (resp.status == 429) {
        auto it = resp.headers.find("retry-after");
        const auto secs = it != resp.headers.end() ? it->second : std::string{"?"};
        out.error = "rate limited (retry after " + secs + "s)";
        return out;
    }
    if (resp.status >= 500) {
        out.error = "Anthropic service error";
        return out;
    }
    if (resp.status != 200) {
        out.error = "unexpected status " + std::to_string(resp.status);
        return out;
    }

    // Parse 200 response body
    auto parsed = juce::JSON::parse(juce::String(resp.body));
    auto* content = parsed["content"].getArray();
    if (! content || content->isEmpty()) {
        out.error = "malformed response";
        return out;
    }
    out.text             = (*content)[0]["text"].toString().toStdString();
    out.promptTokens     = int(parsed["usage"]["input_tokens"]);
    out.completionTokens = int(parsed["usage"]["output_tokens"]);
    return out;
}

} // namespace guitar_dsp::ai
