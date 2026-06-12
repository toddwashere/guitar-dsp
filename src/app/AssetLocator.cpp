#include "AssetLocator.h"

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>

namespace guitar_dsp {

namespace fs = std::filesystem;

std::string AssetLocator::assetsRoot() {
    if (const char* env = std::getenv("GUITAR_DSP_ASSETS_DIR")) {
        if (fs::exists(env)) return env;
    }

    // The bundle that CONTAINS THIS CODE — works for both the standalone .app
    // and the AU .component. currentApplicationFile would return the HOST app
    // (e.g. Logic) when we're loaded as a plugin, so use dladdr to find our own
    // binary, then its bundle's Resources/assets.
    //   dli_fname = <Bundle>/Contents/MacOS/<binary>
    //   -> <Bundle>/Contents/Resources/assets
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&AssetLocator::assetsRoot), &info) != 0
            && info.dli_fname != nullptr) {
        const fs::path bin(info.dli_fname);
        const auto resources = bin.parent_path().parent_path() / "Resources" / "assets";
        std::error_code ec;
        if (fs::is_directory(resources, ec))
            return resources.string();
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
