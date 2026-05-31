#include "SceneLibrary.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace guitar_dsp::scenes {

namespace {

std::optional<std::uint32_t> parseColor(const juce::String& s) {
    if (s.isEmpty() || s[0] != '#' || s.length() != 7) return std::nullopt;
    const auto hex = s.substring(1).getHexValue32();
    return static_cast<std::uint32_t>(hex & 0xFFFFFFu);
}

} // namespace

std::optional<Scene> SceneLibrary::loadOne(const std::string& path) {
    juce::File file(path);
    if (!file.existsAsFile()) {
        std::cerr << "[SceneLibrary] missing file: " << path << '\n';
        return std::nullopt;
    }
    const auto text = file.loadFileAsString();
    auto parsed = juce::JSON::parse(text);
    if (!parsed.isObject()) {
        std::cerr << "[SceneLibrary] not a JSON object: " << path << '\n';
        return std::nullopt;
    }

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        std::cerr << "[SceneLibrary] empty object: " << path << '\n';
        return std::nullopt;
    }

    Scene s;
    if (! obj->hasProperty("id") || ! obj->hasProperty("name")) {
        std::cerr << "[SceneLibrary] missing 'id' or 'name': " << path << '\n';
        return std::nullopt;
    }
    s.id = static_cast<int>(obj->getProperty("id"));
    s.name = obj->getProperty("name").toString().toStdString();

    if (auto colorOpt = parseColor(obj->getProperty("color").toString())) {
        s.colorRgb = *colorOpt;
    }

    if (obj->hasProperty("mixer")) {
        if (auto* m = obj->getProperty("mixer").getDynamicObject()) {
            if (m->hasProperty("masterGainDb"))
                s.mixer.masterGainDb = static_cast<float>(static_cast<double>(m->getProperty("masterGainDb")));
            if (m->hasProperty("dryWet"))
                s.mixer.dryWet = static_cast<float>(static_cast<double>(m->getProperty("dryWet")));
            if (m->hasProperty("transitionMs"))
                s.mixer.transitionMs = static_cast<float>(static_cast<double>(m->getProperty("transitionMs")));
        }
    }

    if (obj->hasProperty("tts")) {
        if (auto* t = obj->getProperty("tts").getDynamicObject()) {
            if (t->hasProperty("source"))
                s.tts.source = t->getProperty("source").toString().toStdString();
            if (t->hasProperty("clip"))
                s.tts.clip = t->getProperty("clip").toString().toStdString();
            if (t->hasProperty("text"))
                s.tts.text = t->getProperty("text").toString().toStdString();
            if (t->hasProperty("voice"))
                s.tts.voice = t->getProperty("voice").toString().toStdString();
        }
    }

    return s;
}

std::vector<Scene> SceneLibrary::loadDirectory(const std::string& directory) {
    std::vector<Scene> out;
    namespace fs = std::filesystem;
    if (! fs::exists(directory) || ! fs::is_directory(directory)) {
        std::cerr << "[SceneLibrary] not a directory: " << directory << '\n';
        return out;
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (! entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        if (auto s = loadOne(entry.path().string())) out.push_back(*s);
    }

    std::sort(out.begin(), out.end(),
              [](const Scene& a, const Scene& b) { return a.id < b.id; });
    return out;
}

} // namespace guitar_dsp::scenes
