# While My Guitar Gently Speaks — Design

**Date**: 2026-05-29
**Status**: Approved for implementation planning
**Target platform**: macOS (Apple Silicon primary, Intel Mac fallback)
**Language / framework**: C++ with JUCE
**Form factor**: Standalone macOS application (not an AU plugin)

---

## 1. Overview

*While My Guitar Gently Speaks* is a standalone macOS audio application used as the live instrument for a tech-conference talk of the same name. A live guitar signal is processed in real time into two families of effect:

1. **Instrument Carousel** — multi-effect transformations that make the guitar sound like other instruments (Hammond organ, piano-ish, synth lead, 8-bit chiptune, choir / pad).
2. **Speaking Guitar** — a channel vocoder whose carrier is the live guitar and whose modulator is text-to-speech audio, so the guitar appears to "speak" predefined phrases in rhythm with the playing.

Scenes are switched live via a Behringer FCB1010 MIDI foot controller. A full-screen visualizer provides spectrogram and karaoke-style text feedback for the audience.

The system is designed to be **reliable across multiple live performances** — stability is the primary non-functional requirement.

## 2. Goals & non-goals

### Goals
- Crash-free across a 4-hour soak test and multiple live performances.
- Round-trip audio latency ≤ 10 ms at 64-sample / 48 kHz buffers.
- Speaking-guitar output intelligible to a back-row audience (≥ 90% word recognition in blind transcription).
- Scene transitions free of clicks, pops, or audible glitches.
- FCB1010 plug-and-play; full keyboard fallback so rehearsal and emergency operation are possible without the pedal.
- Three independent TTS sources (prebaked, Apple, Piper) so any one of them failing is recoverable mid-performance.
- A swappable vocoder block so a neural voice-conversion backend (RVC) can be introduced in v2 without restructuring.

### Non-goals (v1)
- Audience-text encore (web server, live audience input). Deferred to v2.
- Neural voice conversion (RVC / so-vits-svc). Deferred to v2.
- AU plugin packaging. Standalone-only in v1; AU export remains a free option via JUCE.
- Polyphonic pitch-to-MIDI conversion. Out of scope entirely.
- Cross-platform (Windows / Linux). macOS-only.
- Cloud / internet-dependent TTS. All processing is local.

## 3. Demo arc (the 15-minute show)

The design must support, and be tuned for, the following on-stage sequence:

1. **Open clean.** Switch 1 (PC 0). A clean lick to establish the baseline guitar tone.
2. **Instrument Carousel.** Switches 2–6 (PC 1–5). Same recognizable riff (Smoke on the Water, Mario theme, Star Wars — performer's choice) played through five instrument timbres in turn.
3. **The pivot.** Performer addresses audience: "what if it could speak?"
4. **Speaking guitar — short clips.** Switches 7–8 (PC 6–7). Two short pre-baked phrases spoken by the guitar over chord work.
5. **Finale.** Switch 9 (PC 8). The chord progression of *While My Guitar Gently Weeps*; the guitar sings the lyrics in time with the chord changes.
6. **Panic.** Switch 10 (PC 9) — always available, returns to dry clean guitar in ≤ 30 ms.

## 4. Top-level architecture

### Threading model
- **Audio thread** (JUCE-hosted, real-time): runs the audio graph. No allocations, no locks, no I/O. Standard JUCE real-time discipline applies.
- **Message thread**: GUI, MIDI input, scene transitions, asset loading. Communicates with the audio thread via lock-free FIFOs and atomic parameter snapshots.
- **Prewarm worker** (background): synthesizes live-TTS clips ahead of time to hide TTS engine latency.
- **Offline (separate binary)**: Python TTS pre-bake CLI. Runs only at design time, never at performance time.

### Audio graph

```
            ┌──────────────────────────────────────────────┐
Guitar in ─►│ InputStage (DC block, noise gate, gain)      │
            └──────────────────────────────────────────────┘
                  │                              │
                  ▼                              ▼
        ┌─────────────────────┐         ┌──────────────────────┐
        │ InstrumentCarousel  │         │ Vocoder (IVocoder)   │◄── TTSClipPlayer ◄── active TTSClip
        │ (per-scene chain)   │         │   v1: ChannelVocoder │       (modulator)
        │                     │         │   v2: NeuralVocoder  │
        └─────────────────────┘         └──────────────────────┘
                  │                              │
                  └──────────┬───────────────────┘
                             ▼
                  ┌────────────────────────┐
                  │ Mixer (per-branch wet, │ ──► Output
                  │  master gain)          │
                  └────────────────────────┘
```

Per-scene config decides which branch is active and at what mix level — the inactive branch is a no-op pass.

## 5. DSP design

### 5.1 Instrument Carousel

A parametric effects chain whose stages are reused across scenes with different parameter sets:

- Pitch shifter / octaver (PSOLA or phase-vocoder, ≤ 5 ms latency)
- Multi-voice harmonizer (formant-preserving)
- Formant shifter (LPC-based)
- Resonant multimode filter (LP / BP / HP, with envelope follower modulation)
- Waveshaper / bit crusher / sample-rate reducer
- Comb filter (short-decay envelope for "piano" attack)
- Tube saturation
- Chorus / Leslie / vibrato
- Plate reverb

Each scene's `carouselPreset` (JSON) specifies which stages are enabled and their parameters. Stages run in fixed order; disabled stages are bypassed at near-zero cost.

### 5.2 Channel vocoder (v1)

Classic design, real-time-safe, no FFT, no blocks:

1. Split modulator (TTS audio) into **N = 24** log-spaced bandpass filters (Bark scale, 80 Hz – 10 kHz). Filters are biquad 2nd-order or state-variable.
2. Per band: one-pole envelope follower, time constant ~15 ms.
3. Split carrier (guitar) into the same N bands with the same filters.
4. Multiply each carrier band by its modulator envelope.
5. Sum bands.
6. **Sibilance channel**: detect high-frequency unvoiced energy in the modulator; mix in a noise generator shaped by the high bands so fricatives ("s", "sh", "t") stay intelligible.

Parameters exposed per scene: number-of-bands hint, envelope smoothing, sibilance blend (0–100%), output gain.

Latency: a few ms of filter group delay; safely under the 10 ms budget.

### 5.3 IVocoder — the v2 swap point

```cpp
class IVocoder {
public:
    virtual ~IVocoder() = default;
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(const float* carrier,
                         const float* modulator,
                         float* output,
                         int numSamples) = 0;
    virtual void setParameter(ParamId id, float value) = 0;
    virtual void reset() = 0;
};
```

v1 ships `ChannelVocoder : IVocoder`. v2 will introduce `NeuralVocoder : IVocoder` wrapping ONNX Runtime + an RVC model. The audio graph and scene system are unchanged across the swap.

## 6. TTS sources

The vocoder needs a modulator audio stream per active scene. Three interchangeable sources, all local, all selected per-scene:

### 6.1 ITTSSource interface

```cpp
class ITTSSource {
public:
    virtual ~ITTSSource() = default;
    virtual std::shared_ptr<TTSClip> synthesize(const std::string& text,
                                                const VoiceConfig& voice) = 0;
    virtual bool isReady() const = 0;
    virtual std::string sourceName() const = 0;
};
```

A `TTSClip` is an immutable struct containing the synthesized audio buffer (mono 48 kHz float), optional alignment metadata (word/phoneme timings for karaoke), and an optional precomputed formant track (unused in v1, reserved for v1.5 formant-tracking mode).

### 6.2 The three sources

| Source class | Backed by | Latency on activation | Voice quality | Primary use |
|---|---|---|---|---|
| `PrebakedTTSSource` | Loads `assets/tts/<clip>/audio.wav` baked offline by `tools/tts_prebake/` (XTTS v2 or StyleTTS 2) | 0 ms | Highest | Default for demo scenes 6, 7, 8 |
| `LiveTTSSource` (Apple) | `AVSpeechSynthesizer` (Obj-C++ wrapper) | ~0.3–1 s | Good, OS-managed neural voices | Live testing path; v2 audience encore primary |
| `PiperTTSSource` | Spawns bundled [Piper](https://github.com/rhasspy/piper) CLI binary; pipes text to stdin, reads PCM from stdout | ~0.3–0.8 s | Good, ONNX-model-managed | Alternate live engine; fallback if Apple voices fail |

The three sources are independent — a failure or absence of one does not affect the others. Defense-in-depth is the explicit goal: any one source can be lost and the show continues.

### 6.3 TTS pre-bake pipeline (offline)

`tools/tts_prebake/prebake.py` (Python, run by hand or via Makefile target):

```
text + voice config
       │
       ▼
  TTS engine (XTTS v2 / StyleTTS 2, PyTorch + MPS)
       │
       ▼
  mono 48 kHz WAV
       │
       ├─► whisperX forced alignment → word/phoneme timings
       ├─► LPC analysis → F1/F2/F3 trajectories (10 ms hop)
       └─► Voiced/unvoiced/silence segmentation
       │
       ▼
  assets/tts/<clip_name>/
    audio.wav        ← v1 vocoder consumes only this
    alignment.json   ← karaoke text timing
    formants.json    ← reserved for v1.5 / v2
    meta.json
```

The pipeline is idempotent and cached: re-running with unchanged text leaves outputs untouched.

`audio.wav` is always baked at 48 kHz mono float. At app load time, `PrebakedTTSSource` resamples to the current audio device sample rate (44.1 / 48 / 96 kHz) using a high-quality offline resampler so the audio thread always consumes native-rate buffers. Live TTS sources produce buffers at the device sample rate directly.

### 6.4 Prewarming

On app startup, every live-TTS scene (any scene whose `tts.source` is `apple` or `piper`) is queued for synthesis on the prewarm worker thread. Synthesis runs sequentially in scene-id order; results are cached as `TTSClip`s in memory keyed by scene id. By the time any pedal is stepped, the clip is normally already in cache.

If a clip has not been prewarmed by activation time, the visualizer briefly shows a "…" indicator and audio starts when the clip is ready. The vocoder smoothly emits silence in the meantime (envelopes at 0). At no point does the audio thread stall waiting for TTS.

### 6.5 Failure handling

If a `LiveTTSSource` or `PiperTTSSource` reports a synthesis error on the message thread:

1. The error is logged to `~/Library/Logs/guitar-dsp/`.
2. `SceneEngine` looks up a fallback for the scene (declared in scene JSON, e.g. `"fallback": "prebaked:hello_cleveland"`).
3. If a fallback exists, the fallback `TTSClip` is used silently.
4. If no fallback, the scene activates with the vocoder receiving silence — equivalent to a clean dry guitar at the mixer.

The audio thread is never informed of the failure; it just sees the `TTSClip` it gets.

## 7. Scene system

### 7.1 Default scene set (v1)

```
0  Clean intro             — input stage only, both branches bypassed
1  Carousel: Hammond organ — multi-octaver + tube saturation + slow Leslie
2  Carousel: piano-ish     — short-decay enveloper + comb filter + plate verb
3  Carousel: synth lead    — pitch-tracked saw oscillator + LP filter + chorus
4  Carousel: 8-bit         — bit crusher + sample-rate reducer + square waveshape
5  Carousel: choir / pad   — multi-voice formant-shifted harmonizer + big reverb
6  Speaking A              — prebaked "hello cleveland" (or performer choice)
7  Speaking B              — prebaked mid-talk line
8  Speaking finale         — prebaked Gently Weeps lyrics, tightly aligned
9  Panic — bypass to clean — kill TTS, ramp dry/wet to 100% dry over 30 ms
```

### 7.2 Scene file format

One JSON per scene under `assets/scenes/`, e.g. `assets/scenes/08_gently_weeps.json`:

```json
{
  "id": 8,
  "name": "Speaking — gently weeps finale",
  "color": "#a64dff",
  "carousel": { "enabled": false },
  "vocoder": {
    "enabled": true,
    "mix": 0.9,
    "bands": 24,
    "envelopeMs": 15,
    "sibilance": 0.6
  },
  "tts": {
    "source": "prebaked",
    "clip": "gently_weeps_lyrics",
    "fallback": null
  },
  "transitionMs": 20
}
```

Live-TTS scene example:

```json
{
  "id": 11,
  "name": "Live TTS — hello-world test",
  "vocoder": { "enabled": true, "mix": 0.85, "sibilance": 0.6 },
  "tts": {
    "source": "apple",
    "text": "Hello, conference. I am a guitar.",
    "voice": "com.apple.voice.enhanced.en-US.Ava",
    "fallback": "prebaked:hello_cleveland"
  }
}
```

### 7.3 Scene transition behavior

- Continuous parameters (filter cutoff, mix, gain) ramp over `transitionMs` (default 20 ms) on scene change to prevent zipper noise / clicks.
- On activating a speaking scene: vocoder envelope followers reset; TTS clip starts from sample 0.
- On deactivating: envelopes ramp to zero over 50 ms; clip playback ceases.
- **Panic** (scene 9) cuts TTS immediately, ramps all wet sends to 0 over 30 ms.

### 7.4 Hot reload

Scene JSON files are watched while the app is running (debug builds and a hidden hotkey in release builds). A modified file triggers a re-load on the message thread; the audio thread receives the new parameters via the same lock-free path used for live transitions. Useful for rehearsal-time tweaking without restarts.

## 8. MIDI / FCB1010 integration

### 8.1 Default mapping

The stock FCB1010 sends Program Change per footswitch and CC 27 / CC 7 on the two expression pedals. No firmware reprogramming required.

| FCB1010 Switch | Program Change # | Scene |
|---|---|---|
| 1 | 0 | Clean intro |
| 2 | 1 | Carousel: organ |
| 3 | 2 | Carousel: piano-ish |
| 4 | 3 | Carousel: synth lead |
| 5 | 4 | Carousel: 8-bit |
| 6 | 5 | Carousel: choir / pad |
| 7 | 6 | Speaking A |
| 8 | 7 | Speaking B |
| 9 | 8 | Speaking finale (Gently Weeps) |
| 10 | 9 | Panic |

Expression pedal 1 (CC 27) → wet/dry mix of current scene's active branch.
Expression pedal 2 (CC 7) → master output gain.

### 8.2 Remap support

The mapping lives in `assets/midi/fcb1010.json`. Users can:
- Rebind any PC# to any scene id.
- Remap expression-pedal CCs.
- Add additional bindings (e.g., a CC for tap-tempo) without recompiling.

### 8.3 Keyboard fallback

Number keys `1`–`0` fire scene events identical to PC 0–9. Available always — rehearsal, emergency, or "I forgot the pedal" recovery.

### 8.4 MIDI device handling

- App auto-connects on startup to any MIDI input device whose name contains `FCB1010` (case-insensitive).
- If absent, a menu allows manual device selection.
- Hot-plug supported via `MidiInput::Listener`; reconnect within 1 second of cable re-insertion.
- A MIDI activity LED in the HUD blinks on every incoming message — instant diagnostic if a switch isn't reaching the app.

## 9. Visualization

Full-screen JUCE OpenGL component, 60 fps, three layered elements:

1. **Background — spectrogram** of the live output, scrolling left-to-right. Colormap tinted by current scene (`scene.color`). Provides constant visual motion even during silence.
2. **Mid — karaoke text** (speaking scenes only). Word stream sourced from `TTSClip.alignment`. The active word at the current playback cursor is highlighted larger / brighter; past/future words dimmed. Makes the speaking-guitar intelligible to the back row.
3. **Top-right HUD** — scene number + name, FCB1010 expression-pedal meters, MIDI activity LED, input/output VU meters, TTS prewarm status (READY / WARMING / ERROR).

The visualizer reads scene state, playback position, and audio levels via lock-free atomics from the audio thread. It never blocks the audio thread.

## 10. Module / file layout

```
/Users/user/GIT/guitar-dsp/
├── CMakeLists.txt
├── README.md
├── docs/superpowers/specs/
│   └── 2026-05-29-while-my-guitar-gently-speaks-design.md
├── external/
│   ├── JUCE/                                  (git submodule)
│   └── Catch2/                                (git submodule, v3.x)
├── src/
│   ├── app/
│   │   └── Main.cpp
│   ├── audio/
│   │   ├── PluginProcessor.{h,cpp}
│   │   ├── InputStage.{h,cpp}
│   │   ├── InstrumentCarousel.{h,cpp}
│   │   ├── IVocoder.h
│   │   ├── ChannelVocoder.{h,cpp}
│   │   ├── TTSClip.{h,cpp}
│   │   ├── TTSClipPlayer.{h,cpp}
│   │   ├── ITTSSource.h
│   │   ├── PrebakedTTSSource.{h,cpp}
│   │   ├── LiveTTSSource.{h,mm}              (Objective-C++)
│   │   ├── PiperTTSSource.{h,cpp}
│   │   ├── TTSPrewarmer.{h,cpp}
│   │   ├── Mixer.{h,cpp}
│   │   └── AudioGraph.{h,cpp}
│   ├── midi/
│   │   ├── MidiRouter.{h,cpp}
│   │   └── FCB1010Mapping.{h,cpp}
│   ├── scenes/
│   │   ├── Scene.h
│   │   ├── SceneEngine.{h,cpp}
│   │   └── SceneLibrary.{h,cpp}
│   ├── assets/
│   │   └── AssetLoader.{h,cpp}
│   └── ui/
│       ├── MainComponent.{h,cpp}
│       ├── VisualizerView.{h,cpp}
│       └── HUDView.{h,cpp}
├── tools/
│   └── tts_prebake/
│       ├── prebake.py
│       ├── formant_extract.py
│       ├── alignment.py
│       ├── requirements.txt
│       └── tests/
│           └── test_prebake.py                (pytest)
├── tests/
│   ├── unit/
│   │   ├── audio/                             (one .cpp per audio module)
│   │   ├── midi/
│   │   ├── scenes/
│   │   └── tts/
│   ├── integration/
│   │   ├── test_render_scene.cpp              (golden-file scene renders)
│   │   ├── test_realtime_safety.cpp           (allocation/lock sentinel)
│   │   └── test_mini_soak.cpp                 (10-min randomized run)
│   ├── fixtures/
│   │   ├── inputs/                            (WAV inputs: sine, pluck, strum)
│   │   ├── expected/                          (golden reference WAVs)
│   │   └── midi/                              (captured MIDI sequences)
│   └── harness/
│       ├── RealtimeSentinel.{h,cpp}           (malloc/mutex interception)
│       ├── GoldenFile.{h,cpp}                 (WAV diff with tolerance)
│       └── SyntheticGuitar.{h,cpp}            (input generators)
├── .github/workflows/
│   └── ci.yml                                 (macos-latest build + tests)
├── assets/
│   ├── scenes/                                (0-9 JSON files)
│   ├── tts/                                   (prebaked clip directories)
│   └── midi/fcb1010.json
└── Resources/
    └── piper/
        ├── piper                              (universal2 binary)
        └── voices/
            └── en_US-amy-medium.{onnx,json}
```

## 11. Risks & mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Audio dropouts / xruns | Medium | High | Strict audio-thread discipline. 64-sample buffers @ 48 kHz; fall back to 128 if xruns observed. Soak test pre-flight. |
| Live TTS synthesis lag at activation | Medium | Medium | Prewarm thread for next-likely scenes. Visualizer "…" indicator if not ready. Panic always available. |
| FCB1010 not detected / cable yank | Low | High | Auto-reconnect on hotplug; keyboard fallback (1–0); HUD MIDI status indicator. |
| Audio device disconnect | Low | High | JUCE device-change handling; modal recovery dialog; scene state preserved. |
| OS sample-rate change | Low | Medium | All DSP recomputes coefficients on `prepareToPlay`; tested at 44.1 / 48 / 96 kHz. |
| TTS source failure (Apple voice missing, Piper subprocess crash, model corruption) | Low | Medium | Per-scene `fallback` field; cross-source defense in depth. |
| Crash mid-performance | Very low | Catastrophic | (a) Run under `launchd` with restart-on-crash on performance days. (b) 4-hour soak test in CI before any conference. (c) ASan/UBSan debug builds during development. (d) Crash logs auto-saved. |
| Visualization stutter | Low | Low | Visualizer is fully decoupled from audio thread; stutter would be cosmetic, not audible. |

## 12. Testing strategy

Stability across repeated live performance is the top non-functional requirement, so automated testing is a first-class concern, not a bolt-on. The test suite is organized by the question *"would this fail catastrophically on stage?"* — Tier 1 prevents on-stage failures and is required for v1 merge; Tier 2 is required before the first conference; Tier 3 is required to call v1 "done."

### 12.1 Framework

**Primary**: [Catch2 v3](https://github.com/catchorg/Catch2), vendored as a git submodule under `external/Catch2`. Chosen for:
- `WithinRel` / `WithinAbs` floating-point matchers (essential for DSP correctness assertions)
- `GENERATE` parameterized tests (sweep across sample rates, buffer sizes, scene ids)
- Clean CMake integration
- Active maintenance, modern C++

**Python**: pytest for the `tools/tts_prebake/` pipeline.

**Not used**: JUCE's built-in `UnitTest` (insufficient matchers and parameterization), Google Test (heavier integration, no offsetting advantages here). rapidcheck (true property-based testing) may be added later but is not required for v1.

### 12.2 Custom test harness

Three reusable components live under `tests/harness/`:

- **`RealtimeSentinel`** — overrides `operator new` / `operator delete` / `pthread_mutex_lock` during test runs; aborts with a stack trace if any is called from a thread registered as the audio thread. Used by `test_realtime_safety.cpp` to run the full audio graph through 60 s of synthetic input with zero tolerance for allocations/locks in the callback.
- **`GoldenFile`** — loads a reference WAV, compares against a freshly-rendered WAV with configurable tolerance (bit-exact in release, small `WithinRel` tolerance allowed for SIMD/compiler variation). Provides a "regenerate goldens" command-line flag for intentional updates.
- **`SyntheticGuitar`** — generates deterministic test inputs: sine sweeps, plucked notes (Karplus-Strong), chord strums. Used by both unit tests (per-module) and golden-file integration tests.

### 12.3 Tier 1 — required for v1 merge

| # | Target | Test | Why |
|---|---|---|---|
| 1 | Audio-thread real-time safety | `RealtimeSentinel` wraps the full audio graph through 60 s of synthetic input. Zero allocations, zero lock acquisitions tolerated in the audio thread. | Allocation/locking in the callback is the #1 cause of live audio crashes. |
| 2 | Channel vocoder DSP correctness | Sine carrier + sine modulator → assert output envelope tracks modulator within 0.5 dB. Per-band isolation: feed band-center frequencies, assert correct band activates. Sibilance noise injection: feed unvoiced burst, assert noise channel engages. | A broken vocoder ruins the talk's centerpiece. |
| 3 | Filter stability under extreme parameters | Sweep cutoff / resonance / Q across full ranges at 44.1 / 48 / 96 kHz. Assert no `nan`, no `inf`, output never exceeds `+24 dB` peak. | Footswitch + expression pedal can drive a filter to instability; an exploding filter is loud and unrecoverable. |
| 4 | Scene transition glitch-freeness | Capture output across each scene switch with continuous sine input. Assert no sample-to-sample delta exceeds threshold (no clicks). | The whole show is scene switches; each must be silent at the seams. |
| 5 | MIDI mapping | Synthetic PC 0–9 messages → assert correct scene activation event. CC 27 / CC 7 → correct param events. Malformed MIDI (truncated, wrong channel, running status edge cases) → no crash. | A wrong scene fire is confusing; a MIDI-induced crash is catastrophic. |
| 6 | Scene JSON loader | Valid scene JSON → correct `Scene` struct. Missing fields → clear error. Malformed JSON → clear error, no crash. Reference to missing TTS clip → fallback chain engages, scene still loads. | A typo in scene JSON cannot brick the app on a performance day. |
| 7 | `ITTSSource` contracts | Mock-driven: `PrebakedTTSSource` returns correct duration / channel layout. `PiperTTSSource` survives subprocess crash (mock subprocess that exits nonzero). `LiveTTSSource` survives missing voice id. All three trigger their declared fallback when failing. | Failover is the entire point of having three sources; untested failover is no failover. |
| 8 | Sample-rate handling | Resample 48 kHz prebaked TTS to 44.1 / 96 kHz; assert correct length (within ±1 sample) and pitch (FFT-based, within 1 cent). Simulate device sample-rate change mid-run; assert `prepareToPlay` recomputes filter coefficients. | Plugging into a different audio interface on the road will hit this. |

### 12.4 Tier 2 — required before first conference

| # | Target | Test |
|---|---|---|
| 9 | `InstrumentCarousel` stages | Bypass mode = identity (bit-exact). Octaver octaves are octaves (within 5 cents). Bit crusher quantization is correct (step count). Reverb RT60 matches configured value within ±10%. |
| 10 | `TTSClipPlayer` | Plays exactly N samples then silence. Reset returns to sample 0. Two starts in a row don't double-trigger. End-of-clip transition is silent. |
| 11 | Parameter ramping | Set parameter from 0 to 1 in one block; assert ramp is smooth (linear or exp as configured), no instant jumps. Verify no zipper noise in spectrum. |
| 12 | `Mixer` | Dry=1/wet=0 → exact input passthrough (bit-exact). Dry=0/wet=1 → wet input only. Master gain is linear in linear-amplitude units. |
| 13 | Scene hot reload | Modify a scene JSON file during simulated playback; assert reload happens within 200 ms, no audio drop, no allocations on audio thread. |
| 14 | `FCB1010Mapping` JSON | Default mapping loads. Custom remap loads. Invalid remap → falls back to default with logged warning, never crashes. |

### 12.5 Tier 3 — required to call v1 "done"

| # | Target | Approach |
|---|---|---|
| 15 | Golden-file scene renders | For each of the 10 scenes, feed `fixtures/inputs/strum_clean.wav` and `fixtures/inputs/sine_sweep.wav`, render to WAV, diff against `fixtures/expected/scene_<n>_<input>.wav` via `GoldenFile` harness. **The highest-leverage test in the suite** — catches any unintentional DSP regression for the life of the project. |
| 16 | Audio-thread benchmark | Process 1 s of audio per scene; assert wall-clock time < real-time budget × 0.4 (i.e., < 40% CPU per scene on the reference machine). Regression-guards against accidental performance tanking. |
| 17 | Mini soak (in CI) | 10-minute randomized scene-switching run with synthetic input; assert zero xruns, zero log errors, memory growth < 50 MB. The full 4-hour soak stays manual / pre-conference. |
| 18 | TTS pre-bake pipeline | pytest: run `prebake.py` on a one-word input; assert outputs exist; `audio.wav` is valid 48 kHz mono float; `alignment.json` has one word entry with sensible timing; pipeline is idempotent (second run is a no-op). |

### 12.6 CI

GitHub Actions workflow at `.github/workflows/ci.yml`, runs on `macos-latest`:

- **Build matrix**: Debug + Release; Debug build also runs with AddressSanitizer + UndefinedBehaviorSanitizer.
- **Run on every PR**: all Tier 1 + Tier 2 unit/integration tests, the mini soak (test #17), the Python pre-bake pytest.
- **Target wall-clock**: full suite under 5 minutes (CI tests stay short; full soak runs locally).
- **Failed test = blocked merge.** No exceptions for v1 work.

### 12.7 Manual / interactive tests (not automated)

The following remain manual because they're hardware-dependent or subjective. They are part of the pre-conference checklist, not the CI gate:

1. **Latency measurement**: loopback test through a real USB audio interface; round-trip target ≤ 10 ms at 64-sample / 48 kHz.
2. **Vocoder intelligibility**: record each speaking scene at performance volume; blind human transcription must hit ≥ 90% word recognition.
3. **Full 4-hour soak**: continuous run with random scene switches every 5–30 s; zero crashes, zero xruns, monitored manually.
4. **FCB1010 + audio interface hotplug**: yank/reconnect during playback; confirm recovery within 1 s (MIDI) / 5 s (audio).
5. **Full 15-minute dress rehearsal**: end-to-end show, three times, before any conference.

### 12.8 Coverage expectations

No hard line-coverage target — for DSP code, line coverage is a poor proxy for correctness. Instead:

- **Every audio module** under `src/audio/` has a corresponding `tests/unit/audio/test_<module>.cpp`.
- **Every public method** of `IVocoder` and `ITTSSource` is exercised by at least one test against each implementation.
- **Every scene** has a golden-file render in Tier 3.
- New DSP code without tests blocks PR merge; new UI code is exempt.

## 13. v2 roadmap (out of scope for this spec)

- **Audience-text encore**: small embedded HTTP server + QR code; audience submits text; guitar speaks it via live TTS path that already exists in v1.
- **Neural voice conversion**: `NeuralVocoder : IVocoder` wrapping ONNX Runtime + RVC (or so-vits-svc) model; selectable per scene.
- **AU plugin packaging**: same JUCE codebase, alternative target.
- **Performer-defined scenes** via a GUI editor rather than hand-written JSON.

Each v2 feature is purely additive — none requires reworking v1 modules.
