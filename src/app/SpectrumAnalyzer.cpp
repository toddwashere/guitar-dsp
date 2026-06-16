#include "SpectrumAnalyzer.h"

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp {

namespace {
constexpr int   kTimerHz       = 30;
constexpr float kMinDb         = -80.0f;
constexpr float kMaxDb         =   0.0f;
constexpr float kMinFreq       =  20.0f;
constexpr float kMaxFreq       = 20000.0f;
constexpr float kSmoothing     = 0.6f;   // 0 = no smoothing, 1 = frozen

float linearToDb(float mag) {
    return mag > 1.0e-9f ? 20.0f * std::log10(mag) : kMinDb;
}

float dbToNorm(float db) {
    return juce::jlimit(0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
}

float freqToNormX(float hz) {
    if (hz <= 0.0f) return 0.0f;
    const float logHz   = std::log10(hz);
    const float logMin  = std::log10(kMinFreq);
    const float logMax  = std::log10(kMaxFreq);
    return juce::jlimit(0.0f, 1.0f, (logHz - logMin) / (logMax - logMin));
}
} // namespace

SpectrumAnalyzer::SpectrumAnalyzer(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    std::fill(displayDb_.begin(), displayDb_.end(), kMinDb);
    startTimerHz(kTimerHz);
}

SpectrumAnalyzer::~SpectrumAnalyzer() { stopTimer(); }

void SpectrumAnalyzer::timerCallback() {
    processor_.snapshotRecentSamples(timeDomain_.data(), kFftSize);

    // Apply Hann window in-place on timeDomain_, then copy to fftBuffer_.
    window_.multiplyWithWindowingTable(timeDomain_.data(), kFftSize);

    std::fill(fftBuffer_.begin(), fftBuffer_.end(), 0.0f);
    std::copy(timeDomain_.begin(), timeDomain_.end(), fftBuffer_.begin());

    // Magnitudes (no phase). Output fills the first kFftSize entries.
    fft_.performFrequencyOnlyForwardTransform(fftBuffer_.data());

    // Bin amplitude correction for the Hann window: factor of 2.0 to account
    // for window gain loss, then normalize by FFT size for unity gain on a
    // pure sine. Convert to dB and smooth.
    constexpr float windowGain = 2.0f;
    constexpr float norm = windowGain / static_cast<float>(kFftSize);
    for (int bin = 0; bin < kFftSize / 2; ++bin) {
        const float magDb = linearToDb(fftBuffer_[static_cast<std::size_t>(bin)] * norm);
        displayDb_[static_cast<std::size_t>(bin)] =
            kSmoothing * displayDb_[static_cast<std::size_t>(bin)]
            + (1.0f - kSmoothing) * magDb;
    }

    repaint();
}

void SpectrumAnalyzer::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour::fromRGB(14, 16, 22));

    g.setColour(juce::Colour::fromRGB(120, 130, 150));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText("Spectrum (20 Hz - 20 kHz, log; -80 dBFS - 0 dBFS)",
               bounds.reduced(8, 4),
               juce::Justification::topLeft);

    const auto plot = bounds.reduced(8, 18).toFloat();

    // Frequency grid lines (octaves).
    g.setColour(juce::Colour::fromRGB(40, 44, 52));
    for (const float f : {100.0f, 1000.0f, 10000.0f}) {
        const float x = plot.getX() + freqToNormX(f) * plot.getWidth();
        g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());
    }
    // dB grid lines.
    for (const float db : {-20.0f, -40.0f, -60.0f}) {
        const float y = plot.getBottom() - dbToNorm(db) * plot.getHeight();
        g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
    }

    // Spectrum polyline. Sample one column per pixel by mapping pixel-x
    // back to a frequency, then to an FFT bin (with linear-interp).
    const double sampleRate = processor_.getSampleRate();
    if (sampleRate <= 0.0) return;

    const float binHz = static_cast<float>(sampleRate) / static_cast<float>(kFftSize);
    const int pixelCols = juce::jmax(2, static_cast<int>(plot.getWidth()));

    juce::Path path;
    path.preallocateSpace(pixelCols * 3);
    bool started = false;
    for (int px = 0; px < pixelCols; ++px) {
        const float nx = static_cast<float>(px) / static_cast<float>(pixelCols - 1);
        const float logMin = std::log10(kMinFreq);
        const float logMax = std::log10(kMaxFreq);
        const float hz = std::pow(10.0f, logMin + nx * (logMax - logMin));
        const float binF = hz / binHz;
        const int bin = juce::jlimit(0, kFftSize / 2 - 1, static_cast<int>(binF));

        const float db = displayDb_[static_cast<std::size_t>(bin)];
        const float y  = plot.getBottom() - dbToNorm(db) * plot.getHeight();
        const float x  = plot.getX() + nx * plot.getWidth();
        if (!started) { path.startNewSubPath(x, y); started = true; }
        else            path.lineTo(x, y);
    }

    g.setColour(juce::Colour::fromRGB(180, 140, 230));
    g.strokePath(path, juce::PathStrokeType{1.5f});
}

} // namespace guitar_dsp
