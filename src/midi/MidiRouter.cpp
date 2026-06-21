#include "MidiRouter.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace guitar_dsp::midi {

class MidiRouter::RescanTimer : public juce::Timer {
public:
    explicit RescanTimer(MidiRouter& owner) : owner_(owner) {}
    void timerCallback() override { owner_.refresh(); }
private:
    MidiRouter& owner_;
};

MidiRouter::MidiRouter(MessageCallback onMessage)
    : callback_(std::move(onMessage)) {
    refresh();
    rescanTimer_ = std::make_unique<RescanTimer>(*this);
    rescanTimer_->startTimer(2000);  // 2 s hot-plug poll
}

MidiRouter::~MidiRouter() {
    if (rescanTimer_) rescanTimer_->stopTimer();
    for (auto& in : openInputs_) in->stop();
    openInputs_.clear();
}

void MidiRouter::setPreferredDeviceName(juce::String name) {
    preferredName_ = std::move(name);
    refresh();
}

std::vector<juce::MidiDeviceInfo> MidiRouter::availableDevices() {
    const auto infos = juce::MidiInput::getAvailableDevices();
    return {infos.begin(), infos.end()};
}

std::vector<juce::MidiDeviceInfo>
MidiRouter::selectWantedDevices(const std::vector<juce::MidiDeviceInfo>& available,
                                const juce::String& preferredName) {
    std::vector<juce::MidiDeviceInfo> wanted;
    const juce::String match =
        preferredName.isNotEmpty() ? preferredName : juce::String("FCB1010");
    for (const auto& info : available) {
        if (info.name.containsIgnoreCase(match)) wanted.push_back(info);
    }
    return wanted;
}

void MidiRouter::refresh() {
    const auto juceInfos = juce::MidiInput::getAvailableDevices();
    const std::vector<juce::MidiDeviceInfo> infos(juceInfos.begin(), juceInfos.end());
    auto wanted = selectWantedDevices(infos, preferredName_);

    // One-line stderr log of what we ended up opening so we can diagnose
    // the next "Logic ate my audio" report without guessing.
    {
        std::string names;
        for (const auto& w : wanted) {
            if (!names.empty()) names += ", ";
            names += w.name.toStdString();
        }
        std::fprintf(stderr, "[MidiRouter] wanted=[%s] (of %d available)\n",
                     names.c_str(), (int)infos.size());
    }

    // Close any open inputs that aren't in `wanted`.
    auto stillWanted = [&wanted](const juce::String& id) {
        return std::any_of(wanted.begin(), wanted.end(),
                           [&id](const auto& w) { return w.identifier == id; });
    };
    openInputs_.erase(
        std::remove_if(openInputs_.begin(), openInputs_.end(),
                       [&](std::unique_ptr<juce::MidiInput>& in) {
                           if (!stillWanted(in->getIdentifier())) {
                               in->stop();
                               return true;
                           }
                           return false;
                       }),
        openInputs_.end());

    // Open any wanted devices that aren't already open.
    auto alreadyOpen = [this](const juce::String& id) {
        return std::any_of(openInputs_.begin(), openInputs_.end(),
                           [&id](const auto& in) { return in->getIdentifier() == id; });
    };
    for (const auto& info : wanted) {
        if (alreadyOpen(info.identifier)) continue;
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
