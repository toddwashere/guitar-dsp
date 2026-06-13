#pragma once

#include <string>

namespace guitar_dsp {

// Returns the directory containing scene/MIDI/etc. assets at runtime.
// Precedence:
//   1. $GUITAR_DSP_ASSETS_DIR (if set and exists)
//   2. <app bundle>/Contents/Resources/assets/
//   3. The repo's `assets/` directory walked from CWD (for unit tests)
class AssetLocator {
public:
    static std::string scenesDirectory();
    static std::string midiDirectory();
    static std::string ttsDirectory();

    // Returns the path to the bundled Piper binary, or empty if not bundled.
    // Path: <assetsRoot>/piper/piper
    static std::string piperBinaryPath();

    // Returns the path to the default Piper voice .onnx model, or empty if
    // not bundled. Path: <assetsRoot>/piper/voices/en_US-amy-medium.onnx
    // (matching the model the prebake tool already uses).
    static std::string defaultPiperVoicePath();

    // Returns the path to the bundled Whisper model binary, or empty if not
    // bundled. Path: <assetsRoot>/whisper/ggml-base.en.bin
    static std::string whisperModelPath();

private:
    static std::string assetsRoot();
};

} // namespace guitar_dsp
