#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: generate_sine_440 <output.wav>\n";
        return 1;
    }

    constexpr double sr = 48000.0;
    constexpr double freq = 440.0;
    constexpr double durSec = 2.0;
    const int numSamples = static_cast<int>(sr * durSec);

    juce::AudioBuffer<float> buffer(1, numSamples);
    float* data = buffer.getWritePointer(0);
    const double inc = 2.0 * 3.14159265358979323846 * freq / sr;
    double phase = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        data[i] = static_cast<float>(0.5 * std::sin(phase));
        phase += inc;
    }

    juce::File outFile(argv[1]);
    outFile.deleteFile();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream().release());
    if (!stream) {
        std::cerr << "Failed to open output file\n";
        return 1;
    }
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        format.createWriterFor(stream.get(), sr, 1, 24, {}, 0));
    if (!writer) {
        std::cerr << "Failed to create writer\n";
        return 1;
    }
    stream.release();  // writer takes ownership
    writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    return 0;
}
