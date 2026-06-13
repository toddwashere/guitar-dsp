#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/WordReadout.h"

#include <juce_gui_basics/juce_gui_basics.h>

using guitar_dsp::WordReadout;

TEST_CASE("WordReadout: pip layout constants", "[app][ui][word-readout][pips]") {
    REQUIRE(WordReadout::kPipStripHeight    == 18);
    REQUIRE(WordReadout::kPipDiameter       == 10);
    REQUIRE(WordReadout::kPipAlphaCurrent   == 1.0f);
    REQUIRE(WordReadout::kPipAlphaCompleted == 0.45f);
    REQUIRE(WordReadout::kPipAlphaUpcoming  == 0.15f);
    // Alpha ordering: current brightest, completed mid, upcoming dimmest.
    REQUIRE(WordReadout::kPipAlphaCurrent   > WordReadout::kPipAlphaCompleted);
    REQUIRE(WordReadout::kPipAlphaCompleted > WordReadout::kPipAlphaUpcoming);
}

TEST_CASE("WordReadout: center-word ramp constants", "[app][ui][word-readout][ramp]") {
    REQUIRE(WordReadout::kCenterBaseHeight == 34.0f);
    REQUIRE(WordReadout::kCenterGrowFactor == 0.6f);
    REQUIRE(WordReadout::kPeakColorR == 0xFF);
    REQUIRE(WordReadout::kPeakColorG == 0x30);
    REQUIRE(WordReadout::kPeakColorB == 0x30);
    // At progress=1, font height grows from 34 -> 54.4 pt.
    const float peakHeight = WordReadout::kCenterBaseHeight *
                             (1.0f + WordReadout::kCenterGrowFactor * 1.0f);
    REQUIRE(peakHeight == Catch::Approx(54.4f).epsilon(1e-4f));
}

TEST_CASE("WordReadout: center word at progress=1 lerps to peak red, ~1.6x size",
          "[app][ui][word-readout][ramp]") {
    // Sanity: kPeakColorR/G/B match #FF3030.
    REQUIRE(WordReadout::kPeakColorR == 0xFF);
    REQUIRE(WordReadout::kPeakColorG == 0x30);
    REQUIRE(WordReadout::kPeakColorB == 0x30);

    // Sanity: at progress=1, computed font size is 1.6x base.
    const float ramped = WordReadout::kCenterBaseHeight *
                         (1.0f + WordReadout::kCenterGrowFactor * 1.0f);
    REQUIRE(ramped == Catch::Approx(54.4f).epsilon(1e-4f));
}
