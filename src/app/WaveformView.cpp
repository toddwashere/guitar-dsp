#include "WaveformView.h"

#include "PluginProcessor.h"
#include "audio/Syllabifier.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp {

namespace {
constexpr int kTimerHz = 30;

// Dark base (matches Oscilloscope) so the waveform sits inside the same
// visual family as the rest of the bottom panel.
const juce::Colour kBackground   = juce::Colour::fromRGB(14, 16, 22);
const juce::Colour kAxisLine     = juce::Colour::fromRGB(40, 44, 52);
const juce::Colour kWaveformFill = juce::Colour::fromRGB(120, 220, 180);  // teal
const juce::Colour kBoundary     = juce::Colour::fromRGBA(220, 230, 240, 110);
const juce::Colour kActiveBand   = juce::Colour::fromRGBA(255, 170, 80, 50);
const juce::Colour kActiveBorder = juce::Colour::fromRGB(255, 170, 80);
const juce::Colour kPlayhead     = juce::Colour::fromRGB(255, 235, 90);
const juce::Colour kLabel        = juce::Colour::fromRGB(120, 130, 150);
}  // namespace

WaveformView::WaveformView(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(kTimerHz);
}

WaveformView::~WaveformView() { stopTimer(); }

void WaveformView::timerCallback() {
    bool changed = false;

    audio::TTSClipPtr newClip = processor_.activeSceneIsPhoneme()
        ? processor_.lastPhonemeClip()
        : processor_.lastV1Clip();
    if (newClip != clip_) { clip_ = std::move(newClip); changed = true; }

    const int newSyl = processor_.activeSceneIsPhoneme()
        ? processor_.currentSyllableIndex()
        : processor_.currentSpokenWordIndex();
    if (newSyl != activeSylIdx_) { activeSylIdx_ = newSyl; changed = true; }

    const int newPlay = processor_.currentPhonemePlaySample();
    if (newPlay != playSample_) { playSample_ = newPlay; changed = true; }

    if (changed) repaint();
}

void WaveformView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.fillAll(kBackground);

    // Label.
    g.setColour(kLabel);
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText("Waveform + slice boundaries", bounds.reduced(8, 4),
               juce::Justification::topLeft);

    const auto plot = bounds.reduced(8, 18).toFloat();

    // Center axis line — visible even when there's no clip yet.
    g.setColour(kAxisLine);
    g.drawHorizontalLine(static_cast<int>(plot.getCentreY()),
                         plot.getX(), plot.getRight());

    if (!clip_ || clip_->samples.empty()) {
        g.setColour(kLabel);
        g.drawText("(no clip loaded)", plot.toNearestInt(),
                   juce::Justification::centred);
        return;
    }

    const auto&  samples    = clip_->samples;
    const auto&  syls       = clip_->sylsV2;   // may be empty for v1 clips
    const float  totalSamps = static_cast<float>(samples.size());

    // Lambdas that present a uniform interface over v2 syllables, v1
    // syllables (hyphenated text), and v1 words — in priority order.
    const bool useV2Syls   = !syls.empty();
    const bool useV1Syls   = !useV2Syls && !clip_->syllables.empty();
    const bool useV1Words  = !useV2Syls && !useV1Syls && !clip_->words.empty();

    auto numBoundaries = [&]() -> std::size_t {
        if (useV2Syls)  return syls.size();
        if (useV1Syls)  return clip_->syllables.size();
        if (useV1Words) return clip_->words.size();
        return 0;
    };
    auto startSampleAt = [&](std::size_t i) -> std::size_t {
        if (useV2Syls)  return syls[i].startSample;
        if (useV1Syls)  return clip_->syllables[i].startSample;
        return clip_->words[i].startSample;
    };
    auto endSampleAt = [&](std::size_t i) -> std::size_t {
        if (useV2Syls)  return syls[i].endSample;
        if (useV1Syls)  return clip_->syllables[i].endSample;
        return clip_->words[i].endSample;
    };
    const float  yMid       = plot.getCentreY();
    const float  yScale     = plot.getHeight() * 0.45f;
    const int    pxLeft     = static_cast<int>(plot.getX());
    const int    pxRight    = static_cast<int>(plot.getRight());
    const int    pxWidth    = std::max(1, pxRight - pxLeft);

    // Cache geometry for mouse handlers.
    plotGeom_.xLeft      = plot.getX();
    plotGeom_.xRight     = plot.getRight();
    plotGeom_.totalSamps = totalSamps;
    plotGeom_.valid      = true;

    // 1. Active-segment highlight band (under the waveform).
    //    Uses the same priority-ordered accessors so v1 and v2 clips both work.
    if (activeSylIdx_ >= 0 &&
        activeSylIdx_ < static_cast<int>(numBoundaries())) {
        const auto idx = static_cast<std::size_t>(activeSylIdx_);
        const float x0 = plot.getX()
                       + (static_cast<float>(startSampleAt(idx)) / totalSamps)
                             * plot.getWidth();
        const float x1 = plot.getX()
                       + (static_cast<float>(endSampleAt(idx)) / totalSamps)
                             * plot.getWidth();
        g.setColour(kActiveBand);
        g.fillRect(juce::Rectangle<float>(x0, plot.getY(),
                                          x1 - x0, plot.getHeight()));
        g.setColour(kActiveBorder);
        g.drawRect(juce::Rectangle<float>(x0, plot.getY(),
                                          x1 - x0, plot.getHeight()), 1.0f);
    }

    // 2. Waveform — peak-per-pixel-column from the clip samples.
    //    Mirrored above + below center to draw a symmetric envelope shape.
    g.setColour(kWaveformFill);
    juce::Path waveform;
    waveform.preallocateSpace(pxWidth * 4 + 16);
    waveform.startNewSubPath(plot.getX(), yMid);
    for (int px = 0; px < pxWidth; ++px) {
        const std::size_t s0 = static_cast<std::size_t>(
            (px       / static_cast<float>(pxWidth)) * totalSamps);
        const std::size_t s1 = static_cast<std::size_t>(
            ((px + 1) / static_cast<float>(pxWidth)) * totalSamps);
        const std::size_t end = std::min(s1, samples.size());
        float peak = 0.0f;
        for (std::size_t s = s0; s < end; ++s)
            peak = std::max(peak, std::fabs(samples[s]));
        const float x = plot.getX() + static_cast<float>(px);
        waveform.lineTo(x, yMid - peak * yScale);
    }
    // Close the top half, then walk back along the bottom to mirror.
    for (int px = pxWidth - 1; px >= 0; --px) {
        const std::size_t s0 = static_cast<std::size_t>(
            (px       / static_cast<float>(pxWidth)) * totalSamps);
        const std::size_t s1 = static_cast<std::size_t>(
            ((px + 1) / static_cast<float>(pxWidth)) * totalSamps);
        const std::size_t end = std::min(s1, samples.size());
        float peak = 0.0f;
        for (std::size_t s = s0; s < end; ++s)
            peak = std::max(peak, std::fabs(samples[s]));
        const float x = plot.getX() + static_cast<float>(px);
        waveform.lineTo(x, yMid + peak * yScale);
    }
    waveform.closeSubPath();
    g.fillPath(waveform);

    // 3. Segment boundary lines — full height, ~55% alpha.
    //    Draws for v2 syllables, v1 syllables, and v1 words in priority order.
    g.setColour(kBoundary);
    for (std::size_t i = 0; i < numBoundaries(); ++i) {
        const float x = plot.getX()
                      + (static_cast<float>(startSampleAt(i)) / totalSamps)
                            * plot.getWidth();
        g.drawVerticalLine(static_cast<int>(x),
                           plot.getY(), plot.getBottom());
    }

    // 4. Playhead — 2 px bright vertical, only when something is playing.
    if (playSample_ >= 0 && playSample_ < static_cast<int>(samples.size())) {
        const float x = plot.getX()
                      + (static_cast<float>(playSample_) / totalSamps)
                            * plot.getWidth();
        g.setColour(kPlayhead);
        g.fillRect(juce::Rectangle<float>(x - 1.0f, plot.getY(),
                                          2.0f, plot.getHeight()));
    }
}

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

float WaveformView::boundaryToPx(std::size_t sampleIdx) const {
    if (!plotGeom_.valid || plotGeom_.totalSamps <= 0.0f) return -1.0f;
    return plotGeom_.xLeft +
        (plotGeom_.xRight - plotGeom_.xLeft) *
        static_cast<float>(sampleIdx) / plotGeom_.totalSamps;
}

std::size_t WaveformView::pxToSample(float px) const {
    if (!plotGeom_.valid || plotGeom_.xRight <= plotGeom_.xLeft) return 0;
    const float t       = (px - plotGeom_.xLeft) /
                          (plotGeom_.xRight - plotGeom_.xLeft);
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return static_cast<std::size_t>(clamped * plotGeom_.totalSamps);
}

// ---------------------------------------------------------------------------
// Hit test — returns interior boundary index 1..syls.size()-1, or -1.
// ---------------------------------------------------------------------------

int WaveformView::hitBoundary_(float px) const {
    if (!clip_ || clip_->sylsV2.size() < 2) return -1;
    const auto& syls = clip_->sylsV2;
    // Interior boundaries: index i corresponds to syls[i].startSample
    // (= syls[i-1].endSample). i ranges from 1 to syls.size()-1.
    int bestIdx  = -1;
    float bestDist = static_cast<float>(kBoundaryHitPx) + 1.0f;
    for (std::size_t i = 1; i < syls.size(); ++i) {
        const float bx   = boundaryToPx(syls[i].startSample);
        const float dist = std::fabs(px - bx);
        if (dist <= static_cast<float>(kBoundaryHitPx) && dist < bestDist) {
            bestDist = dist;
            bestIdx  = static_cast<int>(i);
        }
    }
    return bestIdx;
}

// ---------------------------------------------------------------------------
// Mouse handlers
// ---------------------------------------------------------------------------

void WaveformView::mouseMove(const juce::MouseEvent& e) {
    if (!clip_ || clip_->sylsV2.empty()) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (hitBoundary_(e.position.x) >= 0)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown(const juce::MouseEvent& e) {
    if (!clip_ || clip_->sylsV2.empty()) return;
    const int idx = hitBoundary_(e.position.x);

    if (e.mods.isRightButtonDown()) {
        if (idx >= 1) {
            juce::PopupMenu menu;
            menu.addItem(1, "Delete this boundary");
            menu.showMenuAsync(juce::PopupMenu::Options{},
                [this, idx](int chosen) {
                    if (chosen == 1)
                        deleteBoundary_(static_cast<std::size_t>(idx));
                });
        }
        return;
    }

    dragBoundaryIndex_ = idx;  // -1 if not on a boundary
}

void WaveformView::mouseDrag(const juce::MouseEvent& e) {
    if (dragBoundaryIndex_ < 1) return;
    if (!clip_) return;
    const std::size_t newSample = pxToSample(e.position.x);
    moveBoundary_(static_cast<std::size_t>(dragBoundaryIndex_), newSample);
}

void WaveformView::mouseUp(const juce::MouseEvent&) {
    dragBoundaryIndex_ = -1;
}

void WaveformView::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!clip_ || clip_->sylsV2.empty()) return;
    const std::size_t at = pxToSample(e.position.x);
    auto edited = std::make_shared<audio::TTSClip>(*clip_);
    if (audio::addBoundary(edited->sylsV2, at, edited->samples,
                            edited->sampleRate)) {
        processor_.installEditedPhonemeClip(edited);
    }
}

// ---------------------------------------------------------------------------
// Edit helpers
// ---------------------------------------------------------------------------

void WaveformView::moveBoundary_(std::size_t idx, std::size_t newSample) {
    if (!clip_) return;
    auto edited = std::make_shared<audio::TTSClip>(*clip_);
    audio::moveBoundary(edited->sylsV2, idx, newSample,
                        edited->samples, edited->sampleRate);
    processor_.installEditedPhonemeClip(edited);
}

void WaveformView::deleteBoundary_(std::size_t idx) {
    if (!clip_) return;
    auto edited = std::make_shared<audio::TTSClip>(*clip_);
    if (audio::removeBoundary(edited->sylsV2, idx,
                               edited->samples, edited->sampleRate)) {
        processor_.installEditedPhonemeClip(edited);
    }
}

} // namespace guitar_dsp
