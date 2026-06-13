#include "ai/OllamaClient.h"
#include "ai/CancellationToken.h"
#include <juce_core/juce_core.h>

namespace guitar_dsp::ai {

OllamaClient::OllamaClient(IHttpTransport& h, std::string e, std::string m)
    : http_(h), endpoint_(std::move(e)), modelTag_(std::move(m)) {}

std::string OllamaClient::displayName() const {
    return modelTag_ + " (local — Ollama)";
}

std::string OllamaClient::statusText() const {
    return "Ollama: " + modelTag_;
}

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
        const char* role = m.role == Message::Role::System    ? "system"
                         : m.role == Message::Role::User      ? "user"
                                                              : "assistant";
        o->setProperty("role", role);
        o->setProperty("content", juce::String(m.text));
        msgs.add(juce::var(o.get()));
    }
    root->setProperty("messages", msgs);

    auto body = juce::JSON::toString(juce::var(root.get())).toStdString();

    std::map<std::string, std::string> headers{{"content-type", "application/json"}};
    auto resp = http_.post(endpoint_ + "/api/chat",
                           headers, body, req.timeout, cancel);

    LlmReply out;
    if (resp.status == 0)   { out.error = "Ollama: not running"; return out; }
    if (resp.status == 404) {
        out.error = "model not found locally — run: ollama pull " + modelTag_;
        return out;
    }
    if (resp.status >= 500) { out.error = "Ollama service error"; return out; }
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
    std::vector<std::string> out;
    auto r = http.get(endpoint + "/api/tags", std::chrono::milliseconds{1000});
    if (r.status != 200) return out;

    auto parsed = juce::JSON::parse(juce::String(r.body));
    if (auto* arr = parsed["models"].getArray()) {
        out.reserve(static_cast<size_t>(arr->size()));
        for (auto& m : *arr) out.push_back(m["name"].toString().toStdString());
    }
    return out;
}

} // namespace guitar_dsp::ai
