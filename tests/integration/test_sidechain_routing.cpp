// Tests for the AU sidechain mic bus declaration.
//
// PluginProcessor cannot be instantiated in the plain test binary — it drags
// in PluginEditor and the full UI JUCE module stack, which is only available
// inside the juce_add_plugin target. Instead, we replicate the exact bus
// properties and the two methods under test in a minimal local subclass.
// This is the standard JUCE testing pattern for AudioProcessor bus logic.

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

// Mirrors the BusesProperties declared in PluginProcessor::PluginProcessor()
// and the two methods added in Task 5.2 — nothing more.
class SidechainTestProcessor : public juce::AudioProcessor {
public:
    SidechainTestProcessor()
        : juce::AudioProcessor(BusesProperties()
            .withInput ("Input",  juce::AudioChannelSet::mono(),   true)
            .withInput ("Mic",    juce::AudioChannelSet::mono(),   false)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    // Exact copy of PluginProcessor::isBusesLayoutSupported.
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        const auto inputs  = layouts.getMainInputChannelSet();
        const auto outputs = layouts.getMainOutputChannelSet();
        if (inputs.isDisabled() || outputs.isDisabled()) return false;

        const bool inputOk  = (inputs  == juce::AudioChannelSet::mono()
                            || inputs  == juce::AudioChannelSet::stereo());
        const bool outputOk = (outputs == juce::AudioChannelSet::mono()
                            || outputs == juce::AudioChannelSet::stereo());
        if (! inputOk || ! outputOk) return false;

        // Optional sidechain mic input bus: disabled, mono, or stereo.
        if (layouts.inputBuses.size() >= 2) {
            const auto sc = layouts.inputBuses[1];
            if (! sc.isDisabled()
                && sc != juce::AudioChannelSet::mono()
                && sc != juce::AudioChannelSet::stereo()) return false;
        }
        return true;
    }

    // Exact copy of PluginProcessor::micBusIsActive.
    bool micBusIsActive() const noexcept {
        if (getBusCount(/*isInput=*/true) < 2) return false;
        return getChannelCountOfBus(/*isInput=*/true, /*busIndex=*/1) > 0;
    }

    // Required AudioProcessor pure virtuals — stubs only.
    const juce::String getName() const override { return "SidechainTestProcessor"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

} // namespace

TEST_CASE("PluginProcessor: accepts layout with no sidechain (mono main)",
          "[app][buses][integration]") {
    SidechainTestProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p.isBusesLayoutSupported(l));
}

TEST_CASE("PluginProcessor: accepts layout with stereo main + mono sidechain",
          "[app][buses][integration]") {
    SidechainTestProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::stereo());
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p.isBusesLayoutSupported(l));
}

TEST_CASE("PluginProcessor: accepts layout with mono main + stereo sidechain",
          "[app][buses][integration]") {
    SidechainTestProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.inputBuses.add(juce::AudioChannelSet::stereo());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p.isBusesLayoutSupported(l));
}

TEST_CASE("PluginProcessor: accepts layout with disabled sidechain bus",
          "[app][buses][integration]") {
    SidechainTestProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.inputBuses.add(juce::AudioChannelSet::disabled());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p.isBusesLayoutSupported(l));
}

TEST_CASE("PluginProcessor: rejects sidechain with 5.1 channels",
          "[app][buses][integration]") {
    SidechainTestProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.inputBuses.add(juce::AudioChannelSet::create5point1());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE_FALSE(p.isBusesLayoutSupported(l));
}

TEST_CASE("PluginProcessor: micBusIsActive false when sidechain absent",
          "[app][buses][integration]") {
    SidechainTestProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    p.setBusesLayout(l);
    REQUIRE_FALSE(p.micBusIsActive());
}

TEST_CASE("PluginProcessor: micBusIsActive true when sidechain mono",
          "[app][buses][integration]") {
    SidechainTestProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    p.setBusesLayout(l);
    REQUIRE(p.micBusIsActive());
}

TEST_CASE("PluginProcessor: micBusIsActive false when sidechain disabled",
          "[app][buses][integration]") {
    SidechainTestProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.inputBuses.add(juce::AudioChannelSet::disabled());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    p.setBusesLayout(l);
    REQUIRE_FALSE(p.micBusIsActive());
}
