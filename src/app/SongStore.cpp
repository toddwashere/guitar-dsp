#include "SongStore.h"

#include <algorithm>
#include <cctype>

namespace guitar_dsp::app {

SongStore::SongStore(juce::File directory) : dir_(std::move(directory)) {
    dir_.createDirectory();
}

std::string SongStore::sanitiseName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        // Strip ".." pairs explicitly so "..foo" -> "foo" rather than ".foo".
        if (c == '.' && i + 1 < name.size() && name[i + 1] == '.') {
            ++i;
            continue;
        }
        // Keep letters, digits, space, hyphen, underscore. Drop everything
        // else (slashes, colons, quotes, control chars).
        if (std::isalnum(static_cast<unsigned char>(c))
            || c == ' ' || c == '-' || c == '_') {
            out += c;
        }
    }
    // Trim leading/trailing whitespace.
    auto notSpace = [](char c) { return c != ' '; };
    out.erase(out.begin(), std::find_if(out.begin(), out.end(), notSpace));
    out.erase(std::find_if(out.rbegin(), out.rend(), notSpace).base(), out.end());
    if (out.size() > 100) out.resize(100);
    return out;
}

juce::File SongStore::fileFor(const std::string& sanitisedName) const {
    return dir_.getChildFile(juce::String(sanitisedName) + ".txt");
}

bool SongStore::save(const std::string& name, const std::string& text) {
    const auto safe = sanitiseName(name);
    if (safe.empty()) return false;
    auto file = fileFor(safe);
    file.deleteFile();
    juce::FileOutputStream out(file);
    if (!out.openedOk()) return false;
    return out.write(text.data(), text.size());
}

std::optional<std::string> SongStore::load(const std::string& name) const {
    const auto safe = sanitiseName(name);
    if (safe.empty()) return std::nullopt;
    auto file = fileFor(safe);
    if (!file.existsAsFile()) return std::nullopt;
    juce::MemoryBlock block;
    if (!file.loadFileAsData(block)) return std::nullopt;
    return std::string(static_cast<const char*>(block.getData()), block.getSize());
}

bool SongStore::remove(const std::string& name) {
    const auto safe = sanitiseName(name);
    if (safe.empty()) return false;
    return fileFor(safe).deleteFile();
}

std::vector<std::string> SongStore::list() const {
    std::vector<std::string> names;
    juce::Array<juce::File> files;
    dir_.findChildFiles(files, juce::File::findFiles, /*recursive*/ false, "*.txt");
    for (const auto& f : files) {
        names.push_back(f.getFileNameWithoutExtension().toStdString());
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace guitar_dsp::app
