#include "Crusher.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

float Crusher::processSample(float x) noexcept {
    // Sample-and-hold downsampling.
    if (downsample_ > 1) {
        if (holdCounter_ == 0) held_ = x;
        x = held_;
        if (++holdCounter_ >= downsample_) holdCounter_ = 0;
    }
    // Bit-depth quantization.
    if (bits_ > 0 && bits_ < 16) {
        // Clamp to [-1, 1] first.
        x = std::clamp(x, -1.0f, 1.0f);
        // Quantize to 2^(bits-1) levels (symmetric around 0).
        const float levels = static_cast<float>(1 << (bits_ - 1));
        x = std::round(x * levels) / levels;
    }
    return x;
}

} // namespace guitar_dsp::audio
