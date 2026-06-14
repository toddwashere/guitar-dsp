# Mic Talkbox (Scene 3)

**Status:** Design approved 2026-06-13. Implementation pending.

**Companion specs:**
- [Vocal Guitar — Clip Bank (Scene 2)](2026-06-13-vocal-guitar-clip-bank-design.md)
- [Auto-Vocal Formant (Scene 4)](2026-06-13-auto-vocal-formant-design.md)

## Problem

The most authentic way to make a guitar sound like Jack Black mouth-guitar is
to have the *user's mouth* drive the spectral character in real time — the
classic Peter-Frampton talkbox setup ("Livin' on a Prayer"). The user vocalizes
"weedly weedly weeeee" into the mic while playing guitar; the guitar's
spectrum tracks the vowels and consonants from the voice in real time.

This app already has `MicCapture` and a 24-band `ChannelVocoder`. The missing
piece is wiring the mic's audio as the vocoder modulator instead of a TTS clip,
exposed as a dedicated scene.

Scene 3 becomes the "mic talkbox" scene: when active, the mic feeds the vocoder
modulator continuously; the guitar is the carrier.

## Non-goals

- No global "Mod: Clip / Mic" pill on VocoderPanel. Mic-as-modulator lives
  only on Scene 3 for v1. The engine seam introduced here makes adding a
  global pill later a small change, but it is explicitly out of scope.
- No automatic mic gain control. The user sets mic level in the OS or hardware
  preamp; the in-app gate + makeup gain are fixed defaults.
- No mic echo cancellation. Use a directional mic and reasonable stage
  distance; consumer mics with proper placement are sufficient for the
  vocoder use case (which is more forgiving than speech recognition).
- No pitch tracking of the mic signal. The vocoder needs spectral envelope,
  not pitch; the guitar already carries pitch.

## Approach

Introduce a third entry in `AudioGraph::ModulatorSource`: `Mic`. When selected,
the modulator passed to the vocoder is the mic capture buffer (after a fixed
noise gate and makeup gain stage). The carrier is the clean guitar, same as
every other vocoder scene.

Scene 3's JSON declares `"source": "mic"` in the TTS block. `SceneEngine`
translates that into `audioGraph.setModulatorSource(Mic)` on activation. No
clip, no bank, no text, no word-sync — the modulator is a live audio stream.

The mic capture path today lives in `PluginProcessor`, not `AudioGraph`, and
runs on a request/response model: `MicCapture::beginCapture()` starts
accumulating mic samples until `endCapture()` returns the buffer (for whisper
in the conversational-AI path). For a continuous vocoder modulator we need a
different consumption pattern — a per-block forwarding of the latest mic
samples directly into `AudioGraph`.

The plumbing is established: `PluginProcessor::processBlock` already pulls the
mic block from the correct bus (sidechain bus 1 in the AU plugin, bus 0 in
standalone) and hands it to `MicCapture`. We extend that same pull to also
forward the block into `AudioGraph` via a new `setMicBlock()` setter when
`ModulatorSource::Mic` is active. The `MicShaper` and modulator routing run on
the audio thread inside `AudioGraph` as planned.

## Architecture

### Modified components

#### `src/audio/AudioGraph.{h,cpp}`

```cpp
enum class ModulatorSource {
    Linear,
    NoteStepped,
    ClipBank,    // from Phase A
    Mic,         // NEW (Phase B)
};

// NEW. Called from PluginProcessor::processBlock once per block, with the mic
// block already extracted from sidechain bus 1 (AU) or main input bus 0
// (standalone). Stored into an internal scratch buffer for use by process()
// in the same block. RT-safe; no allocation.
void setMicBlock(const float* mono, std::size_t numSamples) noexcept;
```

In `process()`, add a branch:

```cpp
case ModulatorSource::Mic: {
    // micScratchBuffer_ was populated by setMicBlock() earlier this block.
    micShaper_.process(micScratchBuffer_.data(),
                       modulatorBuffer_.data(),
                       numSamples);
    // modulatorBuffer_ feeds ChannelVocoder.modulator below.
    break;
}
```

The `micScratchBuffer_` and `micShaper_` are new members of `AudioGraph`.
`micScratchBuffer_` is sized to the maximum block size at `prepare()` time.

#### `src/audio/MicShaper.{h,cpp}` (NEW)

```cpp
namespace guitar_dsp::audio {

// Fixed noise gate + makeup gain stage for routing the mic into the vocoder
// modulator input. Gate prevents room noise from gating the vocoder open
// during silence; makeup gain compensates for typical mic level (-30 dBFS
// peaks → -6 dBFS peaks) so the modulator excites the envelope follower
// reliably.
//
// Coefficients are not user-tunable in v1. If on-stage tuning is needed later,
// add atomic setters mirrored on VocoderPanel.
class MicShaper {
public:
    void prepare(double sampleRate);
    void reset();

    void process(const float* in, float* out, std::size_t numSamples) noexcept;

private:
    // Single-pole envelope for gate; samples below threshold for >holdMs are
    // attenuated. Threshold and times in implementation tuned against the
    // user's stage mic.
    float gateThreshold_ = 0.0025f;  // ~-52 dBFS, tunable in impl
    float gateAttackCoef_ = 0.0f;
    float gateReleaseCoef_ = 0.0f;
    float gateEnv_ = 0.0f;
    float gateGain_ = 0.0f;          // 0..1 smoothed gate state

    float makeupGainLinear_ = 4.0f;  // +12 dB; tunable in impl
};

} // namespace guitar_dsp::audio
```

A small, single-file module. Implementation is ~50 lines of straightforward
DSP. Output is hard-limited to ±1.0 to avoid feeding a clipped modulator into
the vocoder, which would smear envelopes across all bands.

#### `src/scenes/Scene.h` — `TtsConfig`

```cpp
struct TtsConfig {
    // existing fields…
    std::string source;  // "prebaked" | "apple" | "piper" | "clipBank" | "mic" | ""
    // existing fields…
};
```

No new fields; `"mic"` is just a new valid value for `source`. The `clip`,
`bank`, `text`, `voice`, `trigger`, `wordSync` fields are all ignored when
`source == "mic"`.

#### `src/scenes/SceneLibrary.cpp`

Accept `"mic"` as a valid source value (no parsing changes needed — `source`
is already free-form string).

#### `src/scenes/SceneEngine.cpp` (scene activation)

When activating a scene with `source == "mic"`:
1. `audioGraph.setModulatorSource(ModulatorSource::Mic)`.
2. Skip TTS clip loading (no clip).
3. (No bank rewind, no `setClip(nullptr)` needed — the modulator-source switch
   bypasses both the linear and note-stepped players.)

When deactivating Scene 3 (switching to another scene), the new scene's
activation sets a different `ModulatorSource`. No teardown needed for the mic
path — `micCapture_` keeps running continuously.

#### `src/app/WordReadout` (UI)

When `source == "mic"`, the readout displays a fixed string — "🎤 MIC" or
"Talkbox" (pick during implementation; whichever fits the existing UI
typography). Rewind pill, P/M/W shortcuts are disabled (no clip to step).

#### `src/app/PluginProcessor` (existing mic plumbing)

Extend the mic sidechain block in `processBlock` so that *whenever Scene 3 is
active* (or, more generally, whenever the active `ModulatorSource` is `Mic`),
the mic samples are forwarded to `AudioGraph::setMicBlock()` in addition to
the existing `MicCapture::appendFromAudioBlock()` (which only runs when
whisper is capturing). This means the mic bus is read every block when
Scene 3 is active, regardless of conversational-AI state.

In the AU plugin the mic comes from sidechain bus 1 — clean separation from
the guitar carrier. In standalone there is no sidechain bus, so the "mic"
samples are the same buffer as the guitar — the vocoder ends up
self-modulating on the guitar. This produces a sound (a kind of dynamic
spectral comb) but is not the talkbox effect this scene is designed for.
Scene 3 is therefore primarily a *plugin-host* scene; the standalone fallback
is documented but not the demo target.

#### `src/app/VocoderPanel` (UI) — mic level meter

Add a small horizontal level meter strip on the panel, fed by a single atomic
float `AudioGraph::micPeak()` updated once per audio block. The meter is
visible on every scene (visibility principle from the project memory), not
only Scene 3. Two states:

- Mic capture running → meter shows live peak.
- Mic capture missing/error → meter shows a grey "no mic" indicator.

This makes mic problems immediately visible during a live demo (the user's
canonical concern about demo stability), regardless of which scene is active.

### Scene 3 JSON

Replace `assets/scenes/03_carousel_piano.json` with:

```json
{
  "id": 3,
  "name": "Mic Talkbox",
  "color": "#22a2c0",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": {
    "source": "mic",
    "clarity": 0.0
  },
  "carousel": {
    "enabled": true,
    "drive": 6.0,
    "waveshaper": { "type": "tanh", "amount": 1.0 },
    "harmonizer": { "intervals": [12], "detuneCents": [0], "mix": 0.25 },
    "reverb": { "roomSize": 0.2, "wet": 0.10 },
    "outputTrimDb": -3.0
  }
}
```

Carousel preset is a milder rock voicing than Scene 2 — talkbox tradition is
"clean-ish with sustain," not "shredding." Tunable during implementation polish.

## Data flow (per audio block)

```
clean guitar ────► ChannelVocoder.carrier ──► Carousel chain ──► output
                                ▲
                                │
mic capture ──► MicShaper ──► ChannelVocoder.modulator
   │
   └──► micPeak_ (atomic, for VocoderPanel meter)
```

No clip loading, no segment indexing, no onset detection on this path. The
mic is a continuous modulator stream; vocoder envelope followers handle the
temporal shaping automatically (their ~15 ms time constant is well-matched to
spoken/vocalized audio).

## Testing

**Unit tests** (`tests/`):
- `MicShaper_GateBelowThresholdAttenuates` — feed near-silence, assert output
  amplitude < gate threshold.
- `MicShaper_PassesAboveThreshold` — feed a sine above threshold, assert
  output ≈ input × makeupGain.
- `MicShaper_OutputHardLimited` — feed full-scale input, assert |output| ≤ 1.0.
- `AudioGraph_MicModulatorRoutes` — set `ModulatorSource::Mic`, feed mic
  buffer, assert vocoder receives the gated/gained mic stream.

**Manual / demo verification:**
- Activate Scene 3 with no mic connected: vocoder modulator is silent → output
  is dry carrier through carousel. No crash. VocoderPanel mic meter shows "no
  mic" state.
- Connect mic, activate Scene 3, hum into mic while picking: hear vowel
  character on the guitar that tracks the hummed vowel.
- Speak a sentence into mic without playing guitar: silence (no carrier).
- Sustain a guitar note, vocalize "weeeeeee" → "ahhhh" → "ohhh": hear the
  guitar vowel shift smoothly with the voice.
- Switch from Scene 2 (clip bank) to Scene 3 (mic): no audio glitch, no
  feedback squeal. Mic meter remains active across scene change.
- Disconnect mic mid-scene: meter goes "no mic," guitar continues dry through
  carousel. Reconnect: meter returns, vocoder character returns.

## Risk / open questions

- **Mic placement and bleed.** A live-stage scenario has the mic near the
  guitar amp; bleed-back can cause the guitar to modulate itself through the
  mic ("self-vocoder feedback"). Mitigation in v1: rely on directional mic
  placement and the user's stage discipline. If this turns out to be a
  blocker, a later revision can add a notch/gate keyed off the guitar signal
  (echo cancellation). Document the risk; do not pre-build the mitigation.
- **Mic gate threshold.** The fixed default (-52 dBFS) is a reasonable
  starting point but stage noise varies. If the gate is consistently wrong
  for the user's setup, add a single "Mic gate" knob to VocoderPanel post-v1.
- **Standalone mic/guitar separation.** The standalone build has no sidechain
  bus, so the "mic" samples are the same buffer as the guitar. The vocoder
  ends up self-modulating; not the talkbox effect. The AU plugin in a host
  with sidechain routing (e.g. Logic with a separate mic track wired to the
  plugin's sidechain) is the designed surface for Scene 3. This is the
  reverse of the usual "standalone is the demo target" pattern for this
  project — flag it in `docs/superpowers/specs/2026-06-12-au-plugin-design.md`
  and confirm Logic sidechain routing during implementation testing.

## Deletes

- `assets/scenes/03_carousel_piano.json` (replaced by the new Scene 3 JSON
  above). No C++ deletions — the pitch shifter and comb filter used by the
  old Scene 3 are still used by other scenes.
