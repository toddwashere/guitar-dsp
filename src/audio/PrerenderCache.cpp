#include "PrerenderCache.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace guitar_dsp::audio {

namespace {

constexpr std::uint32_t kBakeMagic   = 0x454B4142u;  // 'B''A''K''E' little-endian
constexpr std::uint32_t kBakeVersion = 1u;

// Little-endian primitive write helpers (we don't ship to BE platforms today,
// but the explicit byte layout makes the file format unambiguous on disk).
template <typename T>
void writeLE(std::ofstream& s, T v) {
    static_assert(std::is_trivially_copyable_v<T>);
    char buf[sizeof(T)];
    std::memcpy(buf, &v, sizeof(T));
    s.write(buf, sizeof(T));
}

template <typename T>
bool readLE(std::ifstream& s, T& v) {
    char buf[sizeof(T)];
    if (! s.read(buf, sizeof(T))) return false;
    std::memcpy(&v, buf, sizeof(T));
    return true;
}

} // namespace

std::string PrerenderCache::hashBytes(const void* data, std::size_t numBytes) {
    // FNV-1a 64-bit — non-cryptographic but collision-free for our scale
    // (handful of bundles). Two passes for a 128-bit-ish digest so the
    // 32-hex-char filename keeps the same shape as MD5 had.
    constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ull;
    constexpr std::uint64_t kFnvPrime  = 0x100000001b3ull;
    auto* bytes = static_cast<const std::uint8_t*>(data);

    std::uint64_t h1 = kFnvOffset;
    for (std::size_t i = 0; i < numBytes; ++i) {
        h1 ^= bytes[i];
        h1 *= kFnvPrime;
    }
    // Second pass with a different seed for the lower 64 bits.
    std::uint64_t h2 = kFnvOffset ^ 0xa5a5a5a5a5a5a5a5ull;
    for (std::size_t i = 0; i < numBytes; ++i) {
        h2 ^= bytes[i] + (h1 & 0xFF);
        h2 *= kFnvPrime;
    }

    char hex[33];
    std::snprintf(hex, sizeof(hex), "%016llx%016llx",
                  static_cast<unsigned long long>(h1),
                  static_cast<unsigned long long>(h2));
    return std::string(hex);
}

juce::File PrerenderCache::pathForHash(const std::string& bundleHash) {
    const auto root = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory)
        .getChildFile("Guitar Speak")
        .getChildFile("prebake");
    root.createDirectory();
    return root.getChildFile(juce::String(bundleHash) + ".bake");
}

std::optional<std::vector<PrerenderCache::GrainEntry>>
PrerenderCache::read(const juce::File& bakeFile,
                     const std::string& expectedBundleHash,
                     int expectedSemitoneRange) {
    if (! bakeFile.existsAsFile()) return std::nullopt;
    std::ifstream s(bakeFile.getFullPathName().toRawUTF8(), std::ios::binary);
    if (! s) return std::nullopt;

    std::uint32_t magic = 0, version = 0, semitoneRange = 0, sampleRate = 0,
                  numGrains = 0;
    if (! readLE(s, magic) || magic != kBakeMagic)         return std::nullopt;
    if (! readLE(s, version) || version != kBakeVersion)   return std::nullopt;
    if (! readLE(s, semitoneRange)
        || semitoneRange != static_cast<std::uint32_t>(expectedSemitoneRange))
        return std::nullopt;
    if (! readLE(s, sampleRate)) return std::nullopt;
    if (! readLE(s, numGrains))  return std::nullopt;

    char hashBuf[32];
    if (! s.read(hashBuf, 32)) return std::nullopt;
    const std::string fileHash(hashBuf, 32);
    if (fileHash != expectedBundleHash) return std::nullopt;

    const int numRatiosExpected = 2 * expectedSemitoneRange + 1;

    std::vector<GrainEntry> out;
    out.reserve(numGrains);
    for (std::uint32_t g = 0; g < numGrains; ++g) {
        std::uint32_t phonemeIdx = 0, numRatios = 0, sampleCount = 0;
        float anchorPitchHz = 0.0f;
        if (! readLE(s, phonemeIdx))    return std::nullopt;
        if (! readLE(s, anchorPitchHz)) return std::nullopt;
        if (! readLE(s, numRatios)
            || numRatios != static_cast<std::uint32_t>(numRatiosExpected))
            return std::nullopt;
        if (! readLE(s, sampleCount))   return std::nullopt;

        auto grain = std::make_shared<ShifterGrain>();
        grain->sampleRate = static_cast<int>(sampleRate);
        grain->preRendered.reserve(numRatios);

        std::vector<std::int16_t> i16Buf(sampleCount);
        for (std::uint32_t r = 0; r < numRatios; ++r) {
            float ratio = 0.0f;
            if (! readLE(s, ratio)) return std::nullopt;
            if (! s.read(reinterpret_cast<char*>(i16Buf.data()),
                         static_cast<std::streamsize>(
                             sampleCount * sizeof(std::int16_t))))
                return std::nullopt;

            PreRenderedRatio pr;
            pr.ratio = ratio;
            pr.samples.resize(sampleCount);
            constexpr float kInvScale = 1.0f / 32767.0f;
            for (std::uint32_t i = 0; i < sampleCount; ++i)
                pr.samples[i] = static_cast<float>(i16Buf[i]) * kInvScale;
            grain->preRendered.push_back(std::move(pr));
        }

        GrainEntry e;
        e.phonemeIdx    = static_cast<int>(phonemeIdx);
        e.anchorPitchHz = anchorPitchHz;
        e.grain         = std::const_pointer_cast<const ShifterGrain>(grain);
        out.push_back(std::move(e));
    }

    return out;
}

bool PrerenderCache::write(const juce::File& bakeFile,
                           const std::string& bundleHash,
                           int sampleRate,
                           int semitoneRange,
                           const std::vector<GrainEntry>& grains) {
    bakeFile.getParentDirectory().createDirectory();
    bakeFile.deleteFile();

    std::ofstream s(bakeFile.getFullPathName().toRawUTF8(), std::ios::binary);
    if (! s) {
        std::cerr << "[PrerenderCache] cannot open for write: "
                  << bakeFile.getFullPathName() << '\n';
        return false;
    }

    writeLE(s, kBakeMagic);
    writeLE(s, kBakeVersion);
    writeLE(s, static_cast<std::uint32_t>(semitoneRange));
    writeLE(s, static_cast<std::uint32_t>(sampleRate));
    writeLE(s, static_cast<std::uint32_t>(grains.size()));

    // Write the hash as 32 ASCII hex characters (caller already provides
    // it in hex form). Pad with zeros if it's shorter than expected.
    char hashFixed[32] = {0};
    for (std::size_t i = 0; i < std::min<std::size_t>(32, bundleHash.size()); ++i)
        hashFixed[i] = bundleHash[i];
    s.write(hashFixed, 32);

    const int numRatios = 2 * semitoneRange + 1;
    for (const auto& g : grains) {
        if (! g.grain || g.grain->preRendered.empty()) {
            std::cerr << "[PrerenderCache] empty grain — skipping write\n";
            return false;
        }
        const auto& pr0 = g.grain->preRendered[0];
        const std::uint32_t sampleCount = static_cast<std::uint32_t>(pr0.samples.size());

        writeLE(s, static_cast<std::uint32_t>(g.phonemeIdx));
        writeLE(s, g.anchorPitchHz);
        writeLE(s, static_cast<std::uint32_t>(numRatios));
        writeLE(s, sampleCount);

        std::vector<std::int16_t> i16Buf(sampleCount);
        for (const auto& pr : g.grain->preRendered) {
            writeLE(s, pr.ratio);
            for (std::uint32_t i = 0; i < sampleCount; ++i) {
                float v = pr.samples[i];
                if (v >  0.999f) v =  0.999f;
                if (v < -0.999f) v = -0.999f;
                i16Buf[i] = static_cast<std::int16_t>(v * 32767.0f);
            }
            s.write(reinterpret_cast<const char*>(i16Buf.data()),
                    static_cast<std::streamsize>(
                        sampleCount * sizeof(std::int16_t)));
        }
    }
    return static_cast<bool>(s);
}

} // namespace guitar_dsp::audio
