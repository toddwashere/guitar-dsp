# Phase 5 — Mic Capture + AU Sidechain

> Continuation of [2026-06-12-conversational-ai.md](2026-06-12-conversational-ai.md).

**Goal:** A `MicCapture` component that accumulates audio between `beginCapture()` and `endCapture()`, RT-safe in `processBlock`, with resample to 16 kHz mono. Plus AU sidechain bus declaration.

---

### Task 5.1: `MicCapture` core (FIFO + resample, no audio I/O)

**Files:**
- Create: `src/audio/MicCapture.h`
- Create: `src/audio/MicCapture.cpp`
- Create: `tests/unit/audio/test_mic_capture.cpp`

- [ ] **Step 1: Tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/MicCapture.h"
#include "harness/RealtimeSentinel.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::MicCapture;

namespace {
std::vector<float> sine(int n, double rate, double hz, float amp = 0.5f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = amp * std::sin(2 * M_PI * hz * i / rate);
    return s;
}
}

TEST_CASE("MicCapture: end after begin returns resampled mono 16k",
          "[audio][mic]") {
    MicCapture m;
    m.prepare(48000.0, 1);
    m.beginCapture();
    auto in = sine(48000, 48000.0, 440.0);     // 1 second at 48 k
    m.appendFromAudioBlock(in.data(), (int)in.size());
    auto out = m.endCapture();
    REQUIRE(out.size() >= 15800);              // ~1 s at 16 k, allow 1% jitter
    REQUIRE(out.size() <= 16200);
}

TEST_CASE("MicCapture: passthrough when sample rate already 16k",
          "[audio][mic]") {
    MicCapture m;
    m.prepare(16000.0, 1);
    m.beginCapture();
    auto in = sine(16000, 16000.0, 440.0);
    m.appendFromAudioBlock(in.data(), (int)in.size());
    auto out = m.endCapture();
    REQUIRE(out.size() == in.size());
}

TEST_CASE("MicCapture: too-short capture flagged",
          "[audio][mic]") {
    MicCapture m;
    m.prepare(48000.0, 1);
    m.beginCapture();
    auto in = sine(1000, 48000.0, 440.0);      // ~21 ms
    m.appendFromAudioBlock(in.data(), (int)in.size());
    auto out = m.endCapture();
    REQUIRE(out.size() < 1600);                // <100 ms
    REQUIRE(m.lastResultWasTooShort());
}

TEST_CASE("MicCapture: too-long capture is auto-truncated to 30s",
          "[audio][mic]") {
    MicCapture m;
    m.prepare(16000.0, 1);
    m.beginCapture();
    std::vector<float> chunk(1600, 0.5f);
    for (int s = 0; s < 60; ++s)               // 60 seconds of pushes
        m.appendFromAudioBlock(chunk.data(), (int)chunk.size()); // missing — sim 100ms/iter
    auto out = m.endCapture();
    REQUIRE(out.size() <= 16000 * 30);
    REQUIRE(m.lastResultWasTruncated());
}

TEST_CASE("MicCapture: appendFromAudioBlock is allocation-free",
          "[audio][mic][rt]") {
    MicCapture m;
    m.prepare(48000.0, 1);
    m.beginCapture();
    std::vector<float> chunk(512, 0.1f);
    guitar_dsp::tests::RealtimeSentinel rt;
    rt.run([&]{
        for (int i = 0; i < 100; ++i)
            m.appendFromAudioBlock(chunk.data(), (int)chunk.size());
    });
    REQUIRE(rt.allocations() == 0);
}
```

- [ ] **Step 2: Implement**

`src/audio/MicCapture.h`:
```cpp
#pragma once
#include <atomic>
#include <vector>

namespace guitar_dsp::audio {

class MicCapture {
public:
    static constexpr int    kTargetRate    = 16000;
    static constexpr double kMaxDurationSec = 30.0;
    static constexpr double kMinDurationSec = 0.2;

    void prepare(double sampleRate, int numChannels);
    void beginCapture();
    void appendFromAudioBlock(const float* mono, int n);  // RT-safe
    std::vector<float> endCapture();                      // 16 kHz mono

    bool  isCapturing() const noexcept { return capturing_.load(); }
    float currentPeakDbfs() const noexcept;
    bool  lastResultWasTooShort()  const noexcept { return tooShort_; }
    bool  lastResultWasTruncated() const noexcept { return truncated_; }

private:
    double             sampleRate_   {48000.0};
    std::vector<float> staging_;                // grown only outside capture
    std::atomic<bool>  capturing_    {false};
    std::atomic<int>   writeIdx_     {0};       // index into staging_
    std::atomic<float> peakLin_      {0.0f};
    bool               tooShort_     {false};
    bool               truncated_    {false};
};

} // namespace
```

`src/audio/MicCapture.cpp`:
```cpp
#include "audio/MicCapture.h"
#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void MicCapture::prepare(double sr, int) {
    sampleRate_ = sr;
    const int cap = (int)std::ceil(sr * kMaxDurationSec) + 1024;
    staging_.assign(static_cast<size_t>(cap), 0.0f);
    writeIdx_.store(0);
}

void MicCapture::beginCapture() {
    writeIdx_.store(0);
    peakLin_.store(0.0f);
    tooShort_ = truncated_ = false;
    capturing_.store(true);
}

void MicCapture::appendFromAudioBlock(const float* mono, int n) {
    if (! capturing_.load()) return;
    int idx = writeIdx_.load(std::memory_order_relaxed);
    const int cap = (int)staging_.size();
    if (idx >= cap) return;                       // full, drop (we'll flag truncated)
    const int writeN = std::min(n, cap - idx);
    float peak = peakLin_.load(std::memory_order_relaxed);
    for (int i = 0; i < writeN; ++i) {
        const float s = mono[i];
        staging_[(size_t)(idx + i)] = s;
        const float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    peakLin_.store(peak, std::memory_order_relaxed);
    writeIdx_.store(idx + writeN, std::memory_order_release);
    if (writeN < n) truncated_ = true;
}

float MicCapture::currentPeakDbfs() const noexcept {
    const float p = peakLin_.load();
    if (p <= 1e-6f) return -120.0f;
    return 20.0f * std::log10(p);
}

namespace {
// Simple linear resampler — good enough for STT and predictable.
std::vector<float> resampleLinear(const float* in, int n, double srcRate, double dstRate) {
    if (srcRate == dstRate) return std::vector<float>(in, in + n);
    const double ratio = dstRate / srcRate;
    const int out_n = (int)std::round(n * ratio);
    std::vector<float> out(out_n);
    for (int i = 0; i < out_n; ++i) {
        const double t = i / ratio;
        const int    j = (int)t;
        const double f = t - j;
        const float  a = j     < n ? in[j]     : 0.0f;
        const float  b = j + 1 < n ? in[j + 1] : 0.0f;
        out[(size_t)i] = (float)(a + (b - a) * f);
    }
    return out;
}
}

std::vector<float> MicCapture::endCapture() {
    capturing_.store(false);
    const int n = writeIdx_.load();
    auto resampled = resampleLinear(staging_.data(), n, sampleRate_, (double)kTargetRate);
    const double seconds = (double)resampled.size() / (double)kTargetRate;
    if (seconds < kMinDurationSec) tooShort_ = true;
    return resampled;
}

} // namespace
```

- [ ] **Step 3: Add `MicCapture.cpp` to `guitar_dsp_audio` in `src/CMakeLists.txt`**

Add the line to the `add_library(guitar_dsp_audio STATIC ...)` source list:
```
audio/MicCapture.cpp
```

- [ ] **Step 4: Run tests + commit**

```bash
./build/tests/guitar_dsp_tests "[audio][mic]"
git add src/audio/MicCapture.{h,cpp} tests/unit/audio/test_mic_capture.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(audio): MicCapture with RT-safe append and 16k resample"
```

---

### Task 5.2: AU sidechain bus declaration

**Files:**
- Modify: `src/app/PluginProcessor.cpp` (BusesProperties + isBusesLayoutSupported)
- Modify: `src/app/PluginProcessor.h` (any new accessors)
- Create: `tests/integration/test_sidechain_routing.cpp`

- [ ] **Step 1: Tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

using guitar_dsp::PluginProcessor;

TEST_CASE("PluginProcessor: accepts layout with no sidechain (main mono)",
          "[app][buses][integration]") {
    PluginProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p.isBusesLayoutSupported(l));
}

TEST_CASE("PluginProcessor: accepts layout with stereo main + mono sidechain",
          "[app][buses][integration]") {
    PluginProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::stereo());
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(p.isBusesLayoutSupported(l));
}

TEST_CASE("PluginProcessor: micBusIsActive false when sidechain absent",
          "[app][buses][integration]") {
    PluginProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    p.setBusesLayout(l);
    REQUIRE_FALSE(p.micBusIsActive());
}

TEST_CASE("PluginProcessor: micBusIsActive true when sidechain mono",
          "[app][buses][integration]") {
    PluginProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::stereo());
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    p.setBusesLayout(l);
    REQUIRE(p.micBusIsActive());
}
```

- [ ] **Step 2: Modify `PluginProcessor` constructor `BusesProperties`**

Find the existing constructor BusesProperties and extend:
```cpp
PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withInput ("Mic",    juce::AudioChannelSet::mono(),   false)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) { ... }
```

(Match existing main-bus default — stereo or mono — in your repo.)

- [ ] **Step 3: Extend `isBusesLayoutSupported`**

```cpp
bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto& main = l.getMainInputChannelSet();
    if (main != juce::AudioChannelSet::mono() &&
        main != juce::AudioChannelSet::stereo()) return false;

    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() &&
        out != juce::AudioChannelSet::stereo()) return false;

    if (l.inputBuses.size() >= 2) {
        const auto sc = l.inputBuses[1];
        if (sc != juce::AudioChannelSet::disabled() &&
            sc != juce::AudioChannelSet::mono() &&
            sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

bool PluginProcessor::micBusIsActive() const noexcept {
    if (getBusCount(true) < 2) return false;
    return getChannelCountOfBus(true, 1) > 0;
}
```

Declare `micBusIsActive()` in the header.

- [ ] **Step 4: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[app][buses]"
git add src/app/PluginProcessor.{h,cpp} tests/integration/test_sidechain_routing.cpp \
        tests/CMakeLists.txt
git commit -m "feat(au): declare optional mono sidechain mic bus, expose micBusIsActive"
```

---

### Task 5.3: Wire mic input through `MicCapture` in `processBlock`

**Files:**
- Modify: `src/app/PluginProcessor.h` (own a `MicCapture`)
- Modify: `src/app/PluginProcessor.cpp`

- [ ] **Step 1: Add member + accessor**

In `PluginProcessor.h`:
```cpp
#include "audio/MicCapture.h"

class PluginProcessor : public juce::AudioProcessor {
public:
    audio::MicCapture& micCapture() noexcept { return micCapture_; }
    // ...
private:
    audio::MicCapture micCapture_;
};
```

- [ ] **Step 2: Prepare in `prepareToPlay`**

```cpp
void PluginProcessor::prepareToPlay(double sr, int /*bs*/) {
    // ... existing ...
    micCapture_.prepare(sr, 1);
}
```

- [ ] **Step 3: Push mic samples in `processBlock`**

Inside `processBlock`, after the existing guitar processing:
```cpp
if (micCapture_.isCapturing() && getBusCount(true) >= 2) {
    auto micBus = getBusBuffer(buffer, /*isInput=*/true, /*busIdx=*/1);
    if (micBus.getNumChannels() >= 1) {
        // Downmix multi-channel to mono if needed
        if (micBus.getNumChannels() == 1) {
            micCapture_.appendFromAudioBlock(
                micBus.getReadPointer(0), micBus.getNumSamples());
        } else {
            const int n = micBus.getNumSamples();
            // Allocate-free: use a fixed-size local stack buffer (small block size)
            constexpr int kMaxBlock = 4096;
            float tmp[kMaxBlock];
            const int copyN = std::min(n, kMaxBlock);
            const auto* L = micBus.getReadPointer(0);
            const auto* R = micBus.getReadPointer(1);
            for (int i = 0; i < copyN; ++i) tmp[i] = 0.5f * (L[i] + R[i]);
            micCapture_.appendFromAudioBlock(tmp, copyN);
        }
    }
}
```

For the standalone path: a separate `setStandaloneInputDevice` route will be added in Phase 8 — for now the AU pipeline is functional, and standalone uses `JuceHttpTransport`/audio device wiring later.

- [ ] **Step 4: Add an RT-safety integration test**

`tests/integration/test_realtime_safety_ai.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/PluginProcessor.h"
#include "harness/RealtimeSentinel.h"
#include <juce_audio_basics/juce_audio_basics.h>

using guitar_dsp::PluginProcessor;

TEST_CASE("PluginProcessor: processBlock with mic capture active is allocation-free",
          "[app][rt][integration]") {
    PluginProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::stereo());
    l.inputBuses.add(juce::AudioChannelSet::mono());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    p.setBusesLayout(l);
    p.prepareToPlay(48000.0, 512);
    p.micCapture().beginCapture();

    juce::AudioBuffer<float> buf(3, 512);     // 2 main + 1 sidechain
    buf.clear();
    juce::MidiBuffer midi;

    guitar_dsp::tests::RealtimeSentinel rt;
    rt.run([&]{ for (int i = 0; i < 100; ++i) p.processBlock(buf, midi); });
    REQUIRE(rt.allocations() == 0);
    p.micCapture().endCapture();
}
```

- [ ] **Step 5: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[app][rt]"
git add src/app/PluginProcessor.{h,cpp} tests/integration/test_realtime_safety_ai.cpp \
        tests/CMakeLists.txt
git commit -m "feat(au): pipe sidechain mic into MicCapture from processBlock (RT-safe)"
```

---

## Phase 5 checkpoint — green.
