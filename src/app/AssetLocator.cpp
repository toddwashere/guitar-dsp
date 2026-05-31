#include "AssetLocator.h"

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <filesystem>

namespace guitar_dsp {

namespace fs = std::filesystem;

std::string AssetLocator::assetsRoot() {
    if (const char* env = std::getenv("GUITAR_DSP_ASSETS_DIR")) {
        if (fs::exists(env)) return env;
    }

    auto appFile = juce::File::getSpecialLocation(
        juce::File::currentApplicationFile);
    auto bundleResources = appFile.getChildFile("Contents/Resources/assets");
    if (bundleResources.isDirectory())
        return bundleResources.getFullPathName().toStdString();

    // Walk up from CWD looking for an `assets/` directory (tests, dev runs).
    auto p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        auto candidate = p / "assets";
        if (fs::exists(candidate)) return candidate.string();
        p = p.parent_path();
    }
    return {};
}

std::string AssetLocator::scenesDirectory() {
    const auto root = assetsRoot();
    if (root.empty()) return {};
    return (fs::path(root) / "scenes").string();
}

std::string AssetLocator::midiDirectory() {
    const auto root = assetsRoot();
    if (root.empty()) return {};
    return (fs::path(root) / "midi").string();
}

std::string AssetLocator::ttsDirectory() {
    const auto root = assetsRoot();
    if (root.empty()) return {};
    return (fs::path(root) / "tts").string();
}

std::string AssetLocator::piperBinaryPath() {
    const auto root = assetsRoot();
    if (root.empty()) return {};
    const auto p = (fs::path(root) / "piper" / "piper").string();
    return fs::exists(p) ? p : std::string{};
}

std::string AssetLocator::defaultPiperVoicePath() {
    const auto root = assetsRoot();
    if (root.empty()) return {};
    const auto p = (fs::path(root) / "piper" / "voices" / "en_US-amy-medium.onnx").string();
    return fs::exists(p) ? p : std::string{};
}

} // namespace guitar_dsp
