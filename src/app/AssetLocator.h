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

    // Returns <assetsRoot>/clips/vocal-guitar/ — the root for the Phase A
    // "vocal guitar" clip bank (Scene 2). Each subdir is one clip key, with
    // an `audio.wav` inside. No `meta.json` needed.
    static std::string vocalGuitarClipsDirectory();

    // Returns the path to the bundled Piper binary, or empty if not bundled.
    // Path: <assetsRoot>/piper/piper
    static std::string piperBinaryPath();

    // Returns the path to the bundled espeak-ng binary, or empty if not bundled.
    // Path: <assetsRoot>/piper/espeak-ng (lives next to the Piper binary).
    static std::string espeakBinaryPath();

    // Returns the path to the default Piper voice .onnx model, or empty if
    // not bundled. Path: <assetsRoot>/piper/voices/en_US-amy-medium.onnx
    // (matching the model the prebake tool already uses).
    static std::string defaultPiperVoicePath();

    // Returns the path to the bundled Whisper model binary, or empty if not
    // bundled. Path: <assetsRoot>/whisper/ggml-base.en.bin
    static std::string whisperModelPath();

    // Resolves a relative path (e.g. "clips/gspeak/scene0.gspeak") against the
    // assets root. Returns empty string if the assets root can't be found.
    // Does NOT check whether the resolved file exists.
    // If relPath starts with "assets/", the prefix is stripped before joining
    // (scene JSONs write "assets/clips/gspeak/foo.gspeak" for human readability,
    // but assetsRoot() already includes the "assets/" segment).
    static std::string resolveRelativePath(const std::string& relPath);

    // Like resolveRelativePath, but targets the SOURCE assets dir (the repo's
    // working tree), not the runtime/bundle dir. Returns empty if the binary
    // doesn't appear to live inside a development build tree (e.g. an
    // installed AU in Logic). Used by Save handlers so hand-tuned files
    // persist across rebuilds — the bundle copy gets refreshed from source
    // each build, so saving INTO the bundle is wiped on next build.
    static std::string resolveSourceRelativePath(const std::string& relPath);

    // Read-side resolver: prefer the source assets dir when the source file
    // exists, else fall back to the runtime/bundle dir. This is what
    // dev-build read paths use so saves done in the current session are
    // visible on the very next read without waiting for a rebuild's
    // POST_BUILD asset copy. Installed AU / standalone .app builds have no
    // source-tree sibling, so this transparently equals resolveRelativePath
    // for them.
    static std::string resolveForRead(const std::string& relPath);

private:
    static std::string assetsRoot();
    static std::string sourceAssetsRoot();
};

} // namespace guitar_dsp
