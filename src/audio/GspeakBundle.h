#pragma once

#include "TTSClip.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>

namespace guitar_dsp::audio {

// Reads and writes .gspeak clip bundles — a zip file containing
// audio.wav (16-bit mono PCM) + manifest.json. Used to persist
// hand-tuned scene clips so every performance starts from the same
// known-good audio + boundary map. See
// docs/superpowers/specs/2026-06-17-gspeak-clip-bundle-design.md.
//
// All entry points are message-thread only (synchronous I/O).
class GspeakBundle {
public:
    struct Loaded {
        TTSClipPtr  clip;
        std::string text;
        bool        isV2 = true;
    };

    // Writes the clip to outFile as a .gspeak zip.
    // - v2 clips (sylsV2 populated): clipKind "v2", syllables + phonemes arrays.
    // - v1 clips (sylsV2 empty): clipKind "v1", wordsV1 + syllablesV1 arrays.
    // - text goes into manifest.text verbatim.
    // - Audio is mono 16-bit PCM at clip.sampleRate.
    // Returns true on success; logs to stderr on failure.
    static bool write(const juce::File& outFile,
                      const TTSClip& clip,
                      const std::string& text);

    // Reads inFile, validates manifest + audio. Resamples + scales
    // boundary indices if manifest sampleRate != engineSampleRate.
    // Returns nullopt on any failure (logs the reason).
    static std::optional<Loaded> read(const juce::File& inFile,
                                      double engineSampleRate);
};

} // namespace guitar_dsp::audio
