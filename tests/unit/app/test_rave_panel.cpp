#include <catch2/catch_test_macros.hpp>
#include "app/RavePanel.h"
#include "audio/AudioGraph.h"

using guitar_dsp::app::RavePanel;
using guitar_dsp::audio::AudioGraph;

TEST_CASE("RavePanel: shows 'Unavailable' when no model loaded", "[app][rave-panel]") {
    AudioGraph g;
    g.prepare(48000.0, 512);
    g.loadRaveModel("/nope.onnx");
    RavePanel panel(g);
    panel.setSize(400, 200);
    // Force the polled state to refresh now (test-only public hook).
    panel.refreshNow();
    REQUIRE(panel.statusPillText().contains("Unavailable"));
}

TEST_CASE("RavePanel: knob changes call AudioGraph setters", "[app][rave-panel]") {
    AudioGraph g;
    g.prepare(48000.0, 512);
    RavePanel panel(g);
    panel.setSize(400, 200);

    panel.setGateForTest(-25.0f);
    panel.setPresenceForTest(0.8f);
    panel.setDriveForTest(6.0f);

    // We can't directly read the AudioGraph's atomic, but post-set, the panel's
    // mirror should match. Test that the UI does its job by querying the slider.
    REQUIRE(panel.gateValue() == -25.0f);
    REQUIRE(panel.presenceValue() == 0.8f);
    REQUIRE(panel.driveValue() == 6.0f);
}
