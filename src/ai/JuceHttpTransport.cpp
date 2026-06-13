#include "ai/JuceHttpTransport.h"
#include "ai/CancellationToken.h"
#include <juce_core/juce_core.h>

namespace guitar_dsp::ai {

namespace {

HttpResponse runRequest(const juce::URL& url, bool isPost,
                        const std::map<std::string, std::string>& headers,
                        std::chrono::milliseconds timeout,
                        CancellationToken* cancel) {
    if (cancel && cancel->isCancelled())
        return {0, "", "cancelled", {}};

    juce::String headerStr;
    for (auto& [k, v] : headers)
        headerStr << juce::String(k) << ": " << juce::String(v) << "\r\n";

    juce::StringPairArray respHeaders;
    int statusCode = 0;

    auto opts = juce::URL::InputStreamOptions(
                    isPost ? juce::URL::ParameterHandling::inPostData
                           : juce::URL::ParameterHandling::inAddress)
                  .withExtraHeaders(headerStr)
                  .withConnectionTimeoutMs(static_cast<int>(timeout.count()))
                  .withResponseHeaders(&respHeaders)
                  .withStatusCode(&statusCode);

    auto stream = url.createInputStream(opts);
    if (! stream)
        return {0, "", "connect failed", {}};

    juce::MemoryBlock buf;
    constexpr int chunkSize = 4096;
    char chunk[chunkSize];
    while (! stream->isExhausted()) {
        if (cancel && cancel->isCancelled())
            return {0, "", "cancelled", {}};
        const int n = stream->read(chunk, chunkSize);
        if (n <= 0) break;
        buf.append(chunk, static_cast<size_t>(n));
    }

    std::map<std::string, std::string> outHeaders;
    for (const auto& k : respHeaders.getAllKeys()) {
        // Lowercase response header names so case-insensitive lookups by clients
        // (e.g. AnthropicClient looking for "retry-after") work regardless of
        // what casing the server actually returns.
        auto lower = k.toLowerCase().toStdString();
        outHeaders[lower] = respHeaders[k].toStdString();
    }

    return HttpResponse{
        statusCode,
        std::string(static_cast<const char*>(buf.getData()), buf.getSize()),
        "",
        std::move(outHeaders)
    };
}

} // anonymous

HttpResponse JuceHttpTransport::get(const std::string& url,
                                    std::chrono::milliseconds timeout,
                                    CancellationToken* cancel) {
    juce::URL u{juce::String(url)};
    return runRequest(u, /*isPost=*/false, {}, timeout, cancel);
}

HttpResponse JuceHttpTransport::post(const std::string& url,
                                     const std::map<std::string, std::string>& headers,
                                     const std::string& body,
                                     std::chrono::milliseconds timeout,
                                     CancellationToken* cancel) {
    juce::URL u = juce::URL{juce::String(url)}.withPOSTData(juce::String(body));
    return runRequest(u, /*isPost=*/true, headers, timeout, cancel);
}

} // namespace guitar_dsp::ai
