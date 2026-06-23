#pragma once

#include "FormantShifter.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace guitar_dsp::audio {

// On-disk cache of FormantShifter::preRenderGrain() output. One file per
// voice bundle, keyed by an MD5 hash of the bundle's audio so a bundle
// rebuild silently invalidates stale caches. The first scene-12 activation
// for each voice writes the file (~40 MB / voice with kSemitoneRange = 3);
// every subsequent activation memory-maps it instead of re-running WORLD's
// Synthesis() per ratio per grain.
//
// File layout (little-endian binary, no compression):
//
//     uint32  magic            = 'B' 'A' 'K' 'E'  (0x454B4142)
//     uint32  version          = 1
//     uint32  semitoneRange    = FormantShifter::kSemitoneRange when written
//     uint32  sampleRate
//     uint32  numGrains
//     char[32] bundleHashHexAscii   (matches PrerenderCache::hashBytes output)
//
//     repeat numGrains times:
//       uint32  phonemeIdx     (matches bundle phoneme[] position)
//       float32 anchorPitchHz
//       uint32  numRatios      (= 2*semitoneRange + 1)
//       uint32  sampleCount    (per-ratio length)
//       repeat numRatios times:
//         float32  ratio
//         int16[sampleCount]   samples (peak-normalised to ±0.999)
//
// All offline / message-thread; never touched from the audio thread.
class PrerenderCache {
public:
    struct GrainEntry {
        int   phonemeIdx     = -1;
        float anchorPitchHz  = 0.0f;
        std::shared_ptr<const ShifterGrain> grain;  // only preRendered populated
    };

    // MD5-hex of an arbitrary byte range. Used to key cache files to the
    // bundle that produced them — a re-built bundle gets a different hash.
    static std::string hashBytes(const void* data, std::size_t numBytes);

    // Cache file path for a given bundle hash, under the user's app-data
    // directory. Creates the parent directory if missing.
    static juce::File pathForHash(const std::string& bundleHash);

    // Try to load a cache file. Returns nullopt if missing, header mismatch,
    // semitone-range mismatch, or any I/O error.
    static std::optional<std::vector<GrainEntry>> read(
        const juce::File& bakeFile,
        const std::string& expectedBundleHash,
        int expectedSemitoneRange);

    // Serialise the given grains to disk. Returns true on success.
    static bool write(const juce::File& bakeFile,
                      const std::string& bundleHash,
                      int sampleRate,
                      int semitoneRange,
                      const std::vector<GrainEntry>& grains);
};

} // namespace guitar_dsp::audio
