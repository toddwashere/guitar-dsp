#include "MidiRouter.h"

namespace guitar_dsp::midi {

MidiRouter::MidiRouter(MessageCallback onMessage)
    : callback_(std::move(onMessage)) {
    refresh();
}

MidiRouter::~MidiRouter() {
    for (auto& in : openInputs_) in->stop();
    openInputs_.clear();
}

void MidiRouter::refresh() {
    for (auto& in : openInputs_) in->stop();
    openInputs_.clear();

    const auto infos = juce::MidiInput::getAvailableDevices();

    // Prefer FCB1010 if present.
    std::vector<juce::MidiDeviceInfo> chosen;
    for (const auto& info : infos) {
        if (info.name.containsIgnoreCase("FCB1010")) chosen.push_back(info);
    }
    if (chosen.empty()) {
        for (const auto& info : infos) chosen.push_back(info);
    }

    for (const auto& info : chosen) {
        if (auto in = juce::MidiInput::openDevice(info.identifier, this)) {
            in->start();
            openInputs_.push_back(std::move(in));
        }
    }
}

std::vector<juce::String> MidiRouter::openDeviceNames() const {
    std::vector<juce::String> names;
    names.reserve(openInputs_.size());
    for (const auto& in : openInputs_) names.push_back(in->getName());
    return names;
}

void MidiRouter::handleIncomingMidiMessage(juce::MidiInput*,
                                           const juce::MidiMessage& message) {
    // JUCE delivers callbacks on the high-priority MIDI thread. Hop to the
    // message thread before touching scene state to keep things simple.
    auto cb = callback_;
    juce::MessageManager::callAsync([cb, message] {
        cb(message);
    });
}

} // namespace guitar_dsp::midi
