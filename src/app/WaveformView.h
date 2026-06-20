#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
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
//
// Interactive slice editing (message thread only):
//   Drag a boundary line to move it, double-click to insert a new
//   boundary, right-click a boundary for a Delete context menu.
//   Edits are pushed into the processor via installEditedPhonemeClip()
//   and wiped when Say fires a new bake.
class WaveformView : public juce::Component,
                     private juce::Timer {
public:
    explicit WaveformView(PluginProcessor& p);
    ~WaveformView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Mouse interaction for boundary editing.
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    juce::TextButton saveButton_      {"Save"};
    juce::TextButton loadButton_      {"Load"};
    juce::TextButton importButton_    {"Import"};
    juce::TextButton autoSliceButton_ {"Auto-slice"};

    void onSavePressed_();
    void onLoadPressed_();
    void onImportPressed_();
    void onAutoSlicePressed_();

    // Splits SayPanel text into (unhyphenated words, hyphenated words)
    // pairs for WordAligner::alignSyllables. Whitespace-split; ASCII
    // punctuation stripped from token ends; interior hyphens preserved
    // in `hyphenatedWords`. Same size on success.
    struct ParsedTranscript {
        std::vector<std::string> words;
        std::vector<std::string> hyphenatedWords;
    };
    static ParsedTranscript parseTranscript_(const juce::String& text);

    // Helpers for boundary editing.
    // Returns the interior boundary index (1 .. syls.size()-1) nearest
    // to px within kBoundaryHitPx, or -1 if none.
    int  hitBoundary_(float px) const;
    void moveBoundary_(std::size_t idx, std::size_t newSample);
    void deleteBoundary_(std::size_t idx);

    PluginProcessor& processor_;

    // Snapshot of state polled from the audio thread, refreshed each
    // timer tick. Held here so paint() is a pure render off cached
    // values (no atomic loads inside the draw loop).
    audio::TTSClipPtr clip_;       // shared_ptr; cheap to compare/assign
    int activeSylIdx_  = -1;
    int playSample_    = -1;

    // True while a file decode is running off the message thread.
    // Re-clicks are ignored; button is disabled in the meantime.
    std::atomic<bool> importInFlight_ { false };

    // Drag state.
    int dragBoundaryIndex_ = -1;  // -1 = not dragging

    // Geometry cached during paint() so mouse handlers share the same
    // coordinate transform without recomputing.
    struct PlotGeom {
        float xLeft      = 0.0f;
        float xRight     = 0.0f;
        float totalSamps = 0.0f;
        bool  valid      = false;
    } plotGeom_;

    static constexpr int kBoundaryHitPx = 5;

    enum class ClipKind { None, V2, V1Syl };
    ClipKind activeClipKind_() const;

    // Coordinate helpers.
    float       boundaryToPx(std::size_t sampleIdx) const;
    std::size_t pxToSample(float px) const;
};

} // namespace guitar_dsp
