#pragma once

namespace guitar_dsp::audio {

// Bit-depth quantizer + sample-and-hold downsampler ("8-bit" character).
// Per-sample, allocation-free. bits==0 bypasses quantization;
// downsample<=1 bypasses the hold.
class Crusher {
public:
    void setBits(int bits) noexcept { bits_ = bits; }
    void setDownsample(int factor) noexcept { downsample_ = factor < 1 ? 1 : factor; }
    void reset() noexcept { holdCounter_ = 0; held_ = 0.0f; }

    float processSample(float x) noexcept;

private:
    int   bits_       = 0;
    int   downsample_ = 1;
    int   holdCounter_= 0;
    float held_       = 0.0f;
};

} // namespace guitar_dsp::audio
