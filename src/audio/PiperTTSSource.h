#pragma once

#include <string>

#include "ITTSSource.h"

namespace guitar_dsp::audio {

// Live TTS source using the bundled Piper CLI binary. `synthesize(text)`
// spawns the binary as a subprocess, pipes text to stdin, reads raw
// mono Int16 PCM from stdout, and packages the result as a TTSClipPtr.
//
// Latency is typically 300-800 ms on Apple Silicon — slower than
// AppleTTSSource for short phrases, faster for long ones; voice quality
// is generally crisper than Apple's compact voices.
//
// Returns nullptr if:
//   - the binary doesn't exist or isn't executable
//   - the voice model doesn't exist
//   - the subprocess exits non-zero or times out
//   - the stdout buffer is empty
//
// Call from the message thread, NOT the audio thread. Pair with
// TTSPrewarmer to hide latency at scene activation.
class PiperTTSSource : public ITTSSource {
public:
    // binaryPath: full path to the `piper` executable.
    // voicePath:  full path to the .onnx voice model. The matching
    //             .onnx.json must sit next to it (Piper's convention).
    PiperTTSSource(std::string binaryPath, std::string voicePath);

    void prepare(double targetSampleRate);

    // For PiperTTSSource, `key` is the text to synthesize.
    TTSClipPtr synthesize(const std::string& key) override;
    std::string sourceName() const override { return "piper"; }

    // Returns true if Piper looks runnable: binary + voice exist, binary
    // is executable, and the @rpath-resolved runtime dylibs are present
    // alongside the binary. Use this for diagnostic UI / startup checks.
    bool isReady() const;

    // Empty when isReady() — otherwise a human-readable explanation of
    // why Piper can't run (e.g. "libespeak-ng.1.dylib missing next to
    // piper binary — the upstream macOS tarball ships incomplete; build
    // Piper from source or copy the dylib from a working install").
    // Suitable for surfacing in the UI.
    std::string statusDetail() const;

private:
    std::string binaryPath_;
    std::string voicePath_;
    double      targetSampleRate_ = 48000.0;
};

} // namespace guitar_dsp::audio
