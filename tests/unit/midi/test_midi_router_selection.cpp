#include <catch2/catch_test_macros.hpp>

#include "midi/MidiRouter.h"

#include <juce_audio_basics/juce_audio_basics.h>

using guitar_dsp::midi::MidiRouter;

namespace {
juce::MidiDeviceInfo makeInfo(const juce::String& name) {
    juce::MidiDeviceInfo info;
    info.name = name;
    info.identifier = name + ":id";
    return info;
}
}

// REGRESSION: with no FCB1010 plugged in and no user preference, the router
// MUST open zero devices. Opening "everything available" grabs IAC Bus /
// Network MIDI / MTC sources, which when running as an AU inside Logic
// breaks the host's Audio/MIDI sync (Logic emits "Sample Rate XXXX
// recognized" and halts transport playback). See PluginProcessor / Logic
// session 2026-06-20.
TEST_CASE("MidiRouter: no preference + no FCB1010 -> opens nothing", "[midi][router][regression]") {
    const std::vector<juce::MidiDeviceInfo> available = {
        makeInfo("IAC Driver Bus 1"),
        makeInfo("Network Session 1"),
        makeInfo("Some Random Controller"),
    };
    const auto wanted = MidiRouter::selectWantedDevices(available, /*preferredName=*/{});
    REQUIRE(wanted.empty());
}

TEST_CASE("MidiRouter: no preference + FCB1010 present -> opens FCB1010 only", "[midi][router]") {
    const std::vector<juce::MidiDeviceInfo> available = {
        makeInfo("IAC Driver Bus 1"),
        makeInfo("Behringer FCB1010"),
        makeInfo("Network Session 1"),
    };
    const auto wanted = MidiRouter::selectWantedDevices(available, /*preferredName=*/{});
    REQUIRE(wanted.size() == 1);
    REQUIRE(wanted[0].name == "Behringer FCB1010");
}

TEST_CASE("MidiRouter: FCB1010 auto-pick is case-insensitive", "[midi][router]") {
    const std::vector<juce::MidiDeviceInfo> available = { makeInfo("fcb1010 USB") };
    const auto wanted = MidiRouter::selectWantedDevices(available, /*preferredName=*/{});
    REQUIRE(wanted.size() == 1);
}

TEST_CASE("MidiRouter: explicit preferred name overrides FCB1010 auto-pick", "[midi][router]") {
    const std::vector<juce::MidiDeviceInfo> available = {
        makeInfo("Behringer FCB1010"),
        makeInfo("Korg nanoKONTROL"),
    };
    const auto wanted = MidiRouter::selectWantedDevices(available, "Korg");
    REQUIRE(wanted.size() == 1);
    REQUIRE(wanted[0].name == "Korg nanoKONTROL");
}

TEST_CASE("MidiRouter: preferred-name substring matches multiple devices", "[midi][router]") {
    const std::vector<juce::MidiDeviceInfo> available = {
        makeInfo("Korg nanoKONTROL"),
        makeInfo("Korg nanoPAD"),
        makeInfo("Akai LPK25"),
    };
    const auto wanted = MidiRouter::selectWantedDevices(available, "Korg");
    REQUIRE(wanted.size() == 2);
}

TEST_CASE("MidiRouter: preferred name with no match -> opens nothing", "[midi][router]") {
    const std::vector<juce::MidiDeviceInfo> available = { makeInfo("Behringer FCB1010") };
    const auto wanted = MidiRouter::selectWantedDevices(available, "Korg");
    REQUIRE(wanted.empty());
}

TEST_CASE("MidiRouter: empty available list -> opens nothing", "[midi][router]") {
    REQUIRE(MidiRouter::selectWantedDevices({}, /*preferredName=*/{}).empty());
    REQUIRE(MidiRouter::selectWantedDevices({}, "Anything").empty());
}
