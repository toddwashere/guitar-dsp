#pragma once

#include <string>

namespace guitar_dsp::audio {

// Defines how NoteSteppedTTSPlayer treats incoming onsets while a
// segment is already playing.
enum class WordSyncMode {
    Latch,       // ignore onsets while a segment is playing (recommended)
    Advance,     // every onset advances + restarts the next segment
    Syllable,    // step through TTSClip::syllables instead of words
};

inline const char* toString(WordSyncMode m) noexcept {
    switch (m) {
        case WordSyncMode::Latch:    return "latch";
        case WordSyncMode::Advance:  return "advance";
        case WordSyncMode::Syllable: return "syllable";
    }
    return "latch";
}

inline WordSyncMode wordSyncModeFromString(const std::string& s) noexcept {
    if (s == "advance")  return WordSyncMode::Advance;
    if (s == "syllable") return WordSyncMode::Syllable;
    return WordSyncMode::Latch;
}

} // namespace guitar_dsp::audio
