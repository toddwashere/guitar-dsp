#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace guitar_dsp::app {

// On-disk store for named lyric/song text snippets.
// One .txt file per song, name = sanitized filename minus ".txt".
//
// Sanitization strips path separators, ".." substrings, and clamps to
// 100 chars — so a malicious name like "../escape" can never write
// outside the store directory.
class SongStore {
public:
    explicit SongStore(juce::File directory);

    bool save(const std::string& name, const std::string& text);
    std::optional<std::string> load(const std::string& name) const;
    bool remove(const std::string& name);

    // Returns the sanitised names of saved songs, sorted ascending.
    std::vector<std::string> list() const;

    // Exposed for testing / UI preview of what name will actually be used.
    static std::string sanitiseName(const std::string& name);

private:
    juce::File dir_;
    juce::File fileFor(const std::string& sanitisedName) const;
};

} // namespace guitar_dsp::app
