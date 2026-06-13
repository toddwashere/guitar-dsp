#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace guitar_dsp::ai {

class CancellationToken;

struct TranscriptionResult {
    std::string                text;
    std::string                language;
    std::chrono::milliseconds  latency {0};
    std::string                error;       // empty on success
};

class ITranscriber {
public:
    virtual ~ITranscriber() = default;

    virtual TranscriptionResult transcribe(const std::vector<float>& mono16k,
                                           CancellationToken* cancel = nullptr) = 0;
    virtual std::string modelName() const = 0;
};

} // namespace guitar_dsp::ai
