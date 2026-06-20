#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>
#include <string>
#include <vector>

namespace guitar_dsp::audio {

// Decodes an audio file from disk to mono PCM at a requested sample rate.
// Backed by JUCE's basic-format manager (WAV / AIFF / FLAC / OGG / MP3
// where the platform provides it). Multichannel files are downmixed by
// averaging channels.
//
// Pure function (no state, no audio-thread concerns). Safe to call from
// any thread, but expected to run off the message thread because file
// decode + resample can take tens of milliseconds for multi-second clips.
class AudioFileDecoder {
public:
    struct Result {
        std::vector<float> samples;       // mono float32 at requestedSampleRate
        double             sampleRate = 0.0;
        std::string        formatName;    // e.g. "WAV", "MP3" — informational
    };

    // Returns nullopt on:
    //   - missing or unreadable file
    //   - unsupported format (no registered reader)
    //   - decode error
    //   - empty audio (zero samples after decode)
    static std::optional<Result> decodeMono(const juce::File& file,
                                            double requestedSampleRate);
};

} // namespace guitar_dsp::audio
