#pragma once

#include <array>

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// FFT-based spectrum analyzer of the post-DSP mono signal. 1024-point
// transform, Hann window, log frequency axis (20 Hz - 20 kHz), magnitude
// in dB. Smooths between frames so the display is calm.
class SpectrumAnalyzer : public juce::Component,
                         private juce::Timer {
public:
    explicit SpectrumAnalyzer(PluginProcessor& p);
    ~SpectrumAnalyzer() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    static constexpr int kFftOrder = 10;
    static constexpr int kFftSize  = 1 << kFftOrder;  // 1024

    PluginProcessor& processor_;

    juce::dsp::FFT                   fft_{kFftOrder};
    juce::dsp::WindowingFunction<float> window_{
        kFftSize, juce::dsp::WindowingFunction<float>::hann};

    std::array<float, kFftSize>     timeDomain_{};
    std::array<float, kFftSize * 2> fftBuffer_{};  // FFT scratch (real-only)
    std::array<float, kFftSize / 2> displayDb_{};  // smoothed dB per bin
};

} // namespace guitar_dsp
