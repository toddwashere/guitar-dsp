#include "ai/WhisperTranscriber.h"
#include "ai/CancellationToken.h"

#include <whisper.h>

namespace guitar_dsp::ai {

WhisperTranscriber::WhisperTranscriber(juce::File f)
    : modelFile_(std::move(f)),
      modelName_(modelFile_.getFileNameWithoutExtension().toStdString()) {
    if (! modelFile_.existsAsFile()) return;
    whisper_context_params cp = whisper_context_default_params();
    ctx_ = whisper_init_from_file_with_params(
        modelFile_.getFullPathName().toRawUTF8(), cp);
}

WhisperTranscriber::~WhisperTranscriber() {
    if (ctx_) whisper_free(ctx_);
}

TranscriptionResult WhisperTranscriber::transcribe(
    const std::vector<float>& mono16k, CancellationToken* cancel) {
    TranscriptionResult r;

    if (! ctx_) { r.error = "model not loaded"; return r; }
    if (cancel && cancel->isCancelled()) { r.error = "cancelled"; return r; }

    const auto start = std::chrono::steady_clock::now();
    std::lock_guard lk(mutex_);

    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.language         = "en";
    p.print_progress   = false;
    p.print_timestamps = false;
    p.print_special    = false;
    p.print_realtime   = false;
    p.no_context       = true;
    p.single_segment   = false;

    try {
        if (whisper_full(ctx_, p, mono16k.data(), static_cast<int>(mono16k.size())) != 0) {
            r.error = "whisper_full failed";
            return r;
        }
    } catch (...) {
        r.error = "whisper exception";
        return r;
    }

    const int n = whisper_full_n_segments(ctx_);
    std::string out;
    for (int i = 0; i < n; ++i) out += whisper_full_get_segment_text(ctx_, i);
    r.text     = std::move(out);
    r.language = "en";
    r.latency  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start);
    return r;
}

} // namespace guitar_dsp::ai
