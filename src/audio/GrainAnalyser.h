#pragma once

#include "FormantShifter.h"

#include <memory>

namespace guitar_dsp::audio {

// Offline (message thread). Run WORLD's analysis stack on a mono
// float32 buffer and return a fully populated ShifterGrain.
// Allocates freely; caller invokes this once per grain at bundle load.
std::shared_ptr<ShifterGrain>
analyseGrain(const float* samples, int numSamples, int sampleRate);

} // namespace guitar_dsp::audio
