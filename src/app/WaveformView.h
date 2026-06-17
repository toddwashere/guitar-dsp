#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

#include "audio/TTSClip.h"

namespace guitar_dsp {

class PluginProcessor;

// Stage-and-debug waveform display for phoneme-stepped (Scene 10+)
// speech scenes. Shows the active TTS clip's waveform with vertical
// lines at every syllable boundary, a tinted band over the active
// syllable, and a playhead at the current sample position.
//
// Reads from PluginProcessor at 30 Hz (Timer):
//   - audio::PhonemeSteppedTTSPlayer's active clip, syllable index,
//     and sample position (all published via atomics/shared_ptr).
//
// Intended audience: the performer (debug) and the audience (demo).
// Cuts in the wrong place are visible at a glance — the boundary
// lines either sit on real silences or they don't.
class WaveformView : public juce::Component,
                     private juce::Timer {
public:
    explicit WaveformView(PluginProcessor& p);
    ~WaveformView() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    PluginProcessor& processor_;

    // Snapshot of state polled from the audio thread, refreshed each
    // timer tick. Held here so paint() is a pure render off cached
    // values (no atomic loads inside the draw loop).
    audio::TTSClipPtr clip_;       // shared_ptr; cheap to compare/assign
    int activeSylIdx_  = -1;
    int playSample_    = -1;
};

} // namespace guitar_dsp
