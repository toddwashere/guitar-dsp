#pragma once
#include "ai/ITranscriber.h"
#include <juce_core/juce_core.h>

#include <mutex>

struct whisper_context;

namespace guitar_dsp::ai {

class WhisperTranscriber : public ITranscriber {
public:
    explicit WhisperTranscriber(juce::File modelFile);
    ~WhisperTranscriber() override;

    WhisperTranscriber(const WhisperTranscriber&) = delete;
    WhisperTranscriber& operator=(const WhisperTranscriber&) = delete;

    TranscriptionResult transcribe(const std::vector<float>& mono16k,
                                   CancellationToken* cancel = nullptr) override;
    std::string modelName() const override { return modelName_; }

private:
    juce::File         modelFile_;
    std::string        modelName_;
    whisper_context*   ctx_ {nullptr};
    std::mutex         mutex_;
};

} // namespace guitar_dsp::ai
