#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace guitar_dsp::audio {

// Pitch-tracked sawtooth carrier for the vocoder. Detects the guitar's
// fundamental with YIN, synthesizes a PolyBLEP-anti-aliased sawtooth at
// that pitch, holds the last detected pitch for a configurable window
// when the guitar goes silent, then fades. Allocation-free in process();
// all buffers sized at prepare().
class PitchTrackedCarrier {
public:
    PitchTrackedCarrier();

    void prepare(double sampleRate, int blockSize);
    void reset();

    struct State {
        float freqHz   = 0.0f;   // 0 when unvoiced AND hold expired
        int   midiNote = -1;     // -1 when unvoiced AND hold expired
        float cents    = 0.0f;   // fine offset from midiNote, in [-50, +50]
        bool  voiced   = false;  // current detection (not hold state)
    };

    // Writes pitched carrier samples into `out`. Returns the latest
    // published state. `guitarIn` and `out` must not alias.
    State process(const float* guitarIn, float* out, std::size_t numSamples);

    // Tunable parameters (set once after construction; not message-thread
    // safe to change while audio is running).
    void setHoldMs(float ms)  noexcept;   // default 1000.0
    void setDecayMs(float ms) noexcept;   // default 200.0

    // Frequency range clamp. Detections outside [minHz, maxHz] are
    // treated as unvoiced. Defaults: 40 Hz .. 2000 Hz.
    void setFrequencyRange(float minHz, float maxHz) noexcept;

private:
    // ---- YIN detector parameters ---------------------------------------
    static constexpr int kWindowSize = 2048;   // ~46 ms @ 44.1 kHz
    static constexpr int kHopSize    = 256;    // ~5.8 ms @ 44.1 kHz
    static constexpr float kYinThreshold = 0.15f;

    // ---- Ring buffer of recent input samples ---------------------------
    std::vector<float> ring_;
    int                ringWriteIdx_ = 0;
    int                samplesUntilNextHop_ = kHopSize;

    // ---- YIN scratch ---------------------------------------------------
    std::vector<float> diff_;   // sized kWindowSize/2

    // ---- Saw oscillator ------------------------------------------------
    double sampleRate_ = 48000.0;
    double sawPhase_   = 0.0;   // [0, 1)
    float  currentFreqHz_ = 0.0f;

    // ---- Hold/decay state ----------------------------------------------
    float holdMs_       = 1000.0f;
    float decayMs_      = 200.0f;
    float minHz_        = 40.0f;
    float maxHz_        = 2000.0f;
    float lastVoicedFreqHz_ = 0.0f;
    int   holdSamplesRemaining_  = 0;   // counts down while in hold
    int   decaySamplesRemaining_ = 0;   // counts down during fade
    float decayGain_    = 0.0f;         // updated per sample during decay
    bool  currentlyVoiced_ = false;

    // ---- Cached for State output ---------------------------------------
    int   currentMidiNote_ = -1;
    float currentCents_    = 0.0f;

    // Run one YIN frame on the last kWindowSize samples ending at ringWriteIdx_.
    // Returns frequency in Hz or 0.0f if unvoiced.
    float runYin() noexcept;

    // PolyBLEP-corrected sawtooth: increments sawPhase_ by freq/sampleRate,
    // applies BLEP at the wraparound.
    float nextSawSample(float freqHz) noexcept;
};

} // namespace guitar_dsp::audio
