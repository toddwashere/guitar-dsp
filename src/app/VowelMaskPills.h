#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>

namespace guitar_dsp::app {

// Row of five toggle pills — Ah / Eh / Ee / Oh / Oo — that drive the
// per-vowel enable mask for the sung-vowel scenes (11 + 12). Bit i of
// the mask corresponds to the i-th pill (left → right).
class VowelMaskPills : public juce::Component {
public:
    VowelMaskPills();
    ~VowelMaskPills() override = default;

    void setMask(std::uint32_t mask);
    std::uint32_t mask() const noexcept { return currentMask_; }

    // Fired when the user toggles a pill. The argument is the full new mask.
    std::function<void(std::uint32_t)> onMaskChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    static constexpr int kNumPills = 5;
    static const char* const kLabels[kNumPills];

    std::array<juce::TextButton, kNumPills> pills_;
    std::uint32_t currentMask_ = 0x1Fu;

    void applyVisualState_();
};

} // namespace guitar_dsp::app
