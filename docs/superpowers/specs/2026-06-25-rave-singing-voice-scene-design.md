# Scene 5 — RAVE Singing Voice (Neural Voice Synthesis from Guitar) — Design

**Status:** Draft for review
**Author:** Todd + Claude
**Date:** 2026-06-25
**Branch target:** TBD (per [feedback_commit_often.md](../../../../.claude/projects/-Users-user-GIT-guitar-dsp/memory/feedback_commit_often.md), expect granular commits per logical chunk)

## 1. Goal

Add a new scene to the carousel that drives a pretrained neural voice model (RAVE) in real time from guitar input, producing voice-like output without pre-recorded clips or pitch-shifting. The scene is positioned as a distinct "ML showpiece" — a deliberate stage moment to contrast with the clip-bank and vocoder approaches already in the demo, not a replacement for any existing scene.

## 2. Why now (and why this shape)

The current voice scenes (2 talkbox-clipbank, 3 mic-talkbox, 4 auto-vocal, 12 sung-direct) all work by either pitch-shifting pre-recorded clips or vocoding a fixed source. They are stable and rehearsed, but they all share the same fundamental approach: a static voice asset becomes the wet signal.

A neural voice model takes a different approach — the voice is *generated* from features (or from audio passed through an encoder/decoder), so the audience hears the guitar driving voice synthesis at the algorithm level, not sample playback. For a conference demo this is a meaningful "next level" moment.

Two non-negotiables shaped the design:

1. **Stability across performances.** The existing demo must not regress. Scene 5 must fail gracefully on stage and the rest of the app must be untouchable by its failure modes. (See [project_while_my_guitar_gently_speaks.md](../../../../.claude/projects/-Users-user-GIT-guitar-dsp/memory/project_while_my_guitar_gently_speaks.md).)
2. **Visibility into what the model is doing.** The "maximize visibility + tweakable controls + see-the-result feedback" principle ([feedback_visibility_principle.md](../../../../.claude/projects/-Users-user-GIT-guitar-dsp/memory/feedback_visibility_principle.md)) means the operator and the audience can see what RAVE is receiving and producing.

## 3. Design decisions (resolved during brainstorming)

| # | Decision | Choice | Rejected |
|---|---|---|---|
| 1 | Which model | Pretrained Acids-IRCAM RAVE voice checkpoint | Fine-tune (B), train from scratch (C) — both deferred |
| 2 | Scene positioning | New scene 5, coexists with all existing scenes | Replace scene 12 (B); parallel blend (C) |
| 3 | Input strategy | Audio-domain conditioning, RAVE in forward (encode→decode) mode | Latent synthesis from features (B); hybrid prior + nudges (C) |
| 4 | Inference runtime | ONNX Runtime, CPU | CoreML (deferred); libtorch (binary size non-starter) |
| 5 | Failure behavior | Startup: status pill + dry passthrough. Runtime: latency watchdog + NaN/Inf guard + branch peak limiter | Hide scene; silent failover to clip-bank |
| 6 | UI | New `RavePanel`: status pill, latency readout, input/output meters, three knobs (Gate, Presence, Drive) | Minimal (status only); maximal (with latent visualization) |

## 4. Architecture

### 4.1 New modules

| Module | Path | Responsibility |
|---|---|---|
| `RaveInference` | `src/ml/RaveInference.{h,cpp}` | Wraps ONNX Runtime session. Loads bundled `.onnx` checkpoint. Synchronous `process(in, out, frames)`. Reports load state, last inference latency, errors. ML-runtime-agnostic shape — swappable for CoreML later without touching callers. |
| `RaveFrontEnd` | `src/audio/RaveFrontEnd.{h,cpp}` | Pure DSP audio-domain conditioning before the encoder. Noise gate → voice-shaped EQ (HPF ~100 Hz + presence boost ~2–4 kHz + gentle high-shelf cut) → drive + soft limiter. Parameters: `gateDb`, `presence`, `drive`. Reports post-conditioning RMS for input meter. |
| `RaveSynthesizer` | `src/audio/RaveSynthesizer.{h,cpp}` | Audio node owning the full path. Front-end → background inference (lock-free ring) → NaN/Inf guard → independent branch peak limiter. Atomic `Status` enum: `Loaded`, `Loading`, `Unavailable`, `Stalled`. Exposes input RMS, output RMS, last inference latency. Background thread pattern follows `SungDirectPath`. |
| `RavePanel` | `src/app/RavePanel.{h,cpp}` | UI panel. Three sliders, status pill, two meters, latency readout. Visible when `scene.showRave == true`. |
| Scene JSON | `assets/scenes/05_rave_voice.json` | New scene file. `raveConfig.enabled=true`, sensible default knob positions, `showRave=true`, vocoder/carousel disabled. |
| Model asset | `assets/models/rave-voice.onnx` | Bundled Acids-IRCAM checkpoint. Resolved via existing asset locator pattern. |

### 4.2 Modified existing modules

| File | Change |
|---|---|
| `src/scenes/Scene.h` | Add `RaveConfig` substruct (`enabled`, `gateDb`, `presence`, `drive`, `dryWet`). Add `showRave` visibility flag. |
| `src/scenes/SceneLibrary.cpp` | Parse new fields from scene JSON; default-fill missing values; clamp out-of-range knob values. |
| `src/audio/AudioGraph.{h,cpp}` | Wire `RaveSynthesizer` as a new wet branch alongside vocoder/carousel. Mutual exclusion with vocoder and carousel on the wet bus (follows existing pattern). Setters for the three knobs (message thread → atomic params on audio thread). |
| `src/app/PluginEditor.cpp` | Mount `RavePanel`; visibility gated on `scene.showRave`. |
| `CMakeLists.txt` | Add ONNX Runtime via `FetchContent` (mirrors WORLD/Piper pattern). Link to `guitar_dsp_audio`. Bundle model file as resource. |

### 4.3 Why this shape

- One audio module (`RaveSynthesizer`) owns *everything* about the RAVE branch — front-end, inference, safety. The rest of `AudioGraph` only sees "another wet branch."
- `RaveInference` is deliberately ML-runtime-agnostic — swapping ONNX for CoreML later touches only that file.
- `RaveFrontEnd` is unit-testable as a pure DSP module (no model dependency, no threading).
- Scene JSON pattern is unchanged — adding a new scene is still "drop a JSON file."

## 5. Data flow & threading

### 5.1 Wet branch in the audio graph

```
Guitar → InputStage (gate, mic)
       ├── Dry path ───────────────────────────────────────────┐
       └── [if scene.raveConfig.enabled]                       │
             RaveSynthesizer:                                  │
               [audio thread]                                  │
                 RaveFrontEnd (gate → EQ → drive)              │
                 write samples → inputRing                     │
                 read samples ← outputRing                     │
                 NaN/Inf guard                                 │
                 branch peak limiter ─────────────────────► Mixer (dry/wet)
               [background thread]                             │
                 pull blocks from inputRing                    │
                 RaveInference.process()  (~10–25 ms)          │
                 write blocks → outputRing                     │
       └── [vocoder / carousel branches, when their scenes]    │
                                                            Master limiter → out
```

### 5.2 Block size & framing

- RAVE models from Acids-IRCAM operate at a model-defined hop (typically 2048 samples @ 44.1 kHz, ~46 ms). The background thread reads 2048-sample windows from `inputRing`, runs one forward pass, writes 2048 samples to `outputRing`.
- Audio thread runs at JUCE's host block size (typically 256–512). It reads/writes partial blocks from the rings; the rings decouple the rates.
- `inputRing` and `outputRing` are lock-free SPSC ring buffers (one writer, one reader). ~8192 samples each — ~185 ms at 44.1 kHz, enough headroom.

### 5.3 Latency budget

| Stage | Latency |
|---|---|
| Front-end conditioning | ~0 ms (in-place DSP) |
| `inputRing` buffering | ~46 ms (one model window) |
| ONNX inference | ~10–25 ms (CPU, M-series) |
| `outputRing` buffering | ~5 ms steady-state |
| **Total branch latency** | **~60–75 ms** |

Acceptable for a "scene-change effect." The audience perceives a delayed vocal response rather than the guitar speaking in real time — which is consistent with how other scenes in the app already feel.

### 5.4 Scene transitions

- When scene 5 deactivates: `RaveSynthesizer` stops writing to `inputRing`. Background thread drains and idles. Atomic flag, no thread teardown.
- When scene 5 activates: background thread starts pulling. Model already loaded at startup, so no warm-up time.
- Hot-swap is cheap; no allocations, no deallocations on the audio thread.

### 5.5 Parameter updates (UI → audio thread)

- `RavePanel` slider callbacks call `AudioGraph::setRaveGateDb(float)` etc., which write to atomics owned by `RaveSynthesizer`.
- Audio thread reads atomics each block. Same pattern as existing knob plumbing.

### 5.6 Diagnostic readouts (audio/inference → UI)

- `RaveSynthesizer` writes input RMS, output RMS, and last-inference-ms to atomics.
- `RavePanel` polls at ~30 Hz via JUCE timer (existing pattern, see `NoteReadout`).

## 6. Error handling & safety net

| Failure | Detection | Audio behavior | UI state | Recovery |
|---|---|---|---|---|
| Model file missing in bundle | `RaveInference::load()` returns `false` at startup | Scene 5 still selectable; wet branch outputs silence; mixer's dry path carries the guitar | Status pill: **"RAVE: Unavailable"** | None at runtime; operator sees the pill, checks the bundle |
| ONNX session init fails (corrupt model, op not supported) | `try/catch` around session creation | Same as above | Status pill: **"RAVE: Unavailable"** with log entry naming the ONNX error | None at runtime |
| Inference watchdog: no output for >100 ms | Audio thread sees `outputRing` starvation > 100 ms while scene 5 active | Wet branch mutes; dry guitar flows unaffected; background thread keeps trying | Status pill: **"RAVE: Stalled"** | Auto-recovers when `outputRing` refills; pill flips back to **"Loaded"** |
| Inference produces NaN/Inf | Per-output-block `std::isfinite` scan on the audio thread | Affected block muted to zero; subsequent blocks resume normally | After 3 consecutive bad blocks: status pill → **"Stalled"**, wet branch mutes until clean blocks resume | Auto |
| Output spike (valid but loud) | Independent branch peak limiter, -3 dBFS ceiling, on RAVE output before mixer | Limiter catches transparently; audience hears clean output | Output meter shows limiting | Continuous |
| Background thread crash | Thread wrapper catches exception, sets status to Unavailable | Wet branch mutes | Status pill: **"RAVE: Crashed (see log)"** | Manual app restart |

**Two invariants this guarantees:**

1. **Worst case on stage is dry guitar through scene 5 + a clear status pill.** Never silence, never noise burst, never a crashed app.
2. **The dry path is never touched by the RAVE branch.** No matter what happens in `RaveSynthesizer`, the dry guitar always reaches the mixer. This is structural, not a defensive check — `RaveSynthesizer` writes only to the wet bus.

## 7. UI — `RavePanel`

Three sliders, three readouts, mounted in the existing panel area when `scene.showRave == true`.

| Element | Type | Range / Behavior |
|---|---|---|
| **Gate** slider | Knob | -60 dBFS … -20 dBFS. Controls `RaveFrontEnd` gate threshold. Aggressively kills pick noise and silence before the encoder. |
| **Presence** slider | Knob | 0 … 1 (dimensionless). Scales the voice-shaped EQ tilt intensity. Gentle → aggressive. |
| **Drive** slider | Knob | -12 dB … +12 dB. Input gain into the encoder (with soft limiter). Lets the operator sit the guitar in the model's sweet spot. |
| **Status pill** | Read-only | "RAVE: Loaded" / "Loading" / "Unavailable" / "Stalled". Color: green / amber / red / amber. |
| **Inference latency readout** | Read-only | Live ms, updated ~5 Hz. Tells operator (and audience, if pointed at) whether RAVE is keeping up. |
| **Input meter** | Bar meter | Post-conditioning RMS. The visibility win: shows what the encoder is actually seeing. |
| **Output meter** | Bar meter | RAVE wet output RMS, post-limiter, pre-mixer. |

The three knobs map directly to the "constrain RAVE's input" narrative for the demo: tighter gate, more voice-like EQ, optimum drive — the operator can demonstrate the principle on stage by tweaking them in real time.

## 8. Testing strategy

Tests are TDD-driven where possible. Pure DSP modules (`RaveFrontEnd`, `NaNInfGuard`, `BranchLimiter`, `LockFreeSPSCRing`) get tests written first; integration with the real model is a final validation step.

### 8.1 Unit tests (fast, no model dependency)

| Module | Tests |
|---|---|
| `RaveFrontEnd::Gate` | Below threshold → silence. Above threshold → unchanged after attack. Hysteresis: doesn't chatter on signals near threshold. |
| `RaveFrontEnd::VoiceEQ` | Impulse response → expected magnitude at 100 Hz (HPF), 2 kHz (presence boost), 8 kHz (high-shelf cut), within ±1 dB tolerance. |
| `RaveFrontEnd::Drive` | 0 dB drive → bit-exact passthrough. High drive → soft-clipped, output bounded to ±1.0. |
| `RaveFrontEnd` (whole) | `setGateDb / setPresence / setDrive` are allocation-free and safe to call from the message thread while the audio thread runs (verified with TSAN). |
| `NaNInfGuard` | All-finite block → passthrough. Block with one NaN → muted. Block with one Inf → muted. 3 consecutive bad blocks → status = Stalled. |
| `BranchLimiter` | Input under ceiling → unchanged. Input over ceiling → clamped exactly at ceiling. Release time within tolerance. |
| `LockFreeSPSCRing` | Producer/consumer ordering. Read returns 0 on empty. Write returns 0 on full. Stress test: 1M push/pop pairs in parallel, no data loss. |
| `RaveSynthesizer::StatusMachine` | Mock `RaveInference`. State transitions: Loading → Loaded on success; Loading → Unavailable on failure; Loaded → Stalled on >100 ms ring starvation; Stalled → Loaded on clean blocks resuming. |
| `Scene` JSON parsing | Round-trip: struct → JSON → parse → match. Missing `raveConfig` → default values. Out-of-range `gateDb` → clamped to valid range. |

### 8.2 Integration tests (automated, slower)

| Test | What it proves |
|---|---|
| **Stub ONNX passthrough model** | Audio roundtrips through full path: front-end → ring → ONNX → ring → guard → limiter → mixer. Verifies wiring without depending on the real checkpoint. |
| **Real Acids-IRCAM model** | Marked `[slow]`. Loads the real checkpoint, runs 5 s of sustained tone through, verifies output is non-silent and not-NaN. Catches regressions in inference setup. |

### 8.3 Manual / soak tests (pre-demo checklist)

- **Rapid scene switching:** cycle in/out of scene 5 for 60 s; expect zero crashes, zero stuck wet branches.
- **Long-run stability:** scene 5 active for 30 min with intermittent playing; status pill stays Loaded.
- **Failure injection:** rename `assets/models/rave-voice.onnx` before launch; verify graceful Unavailable state and dry passthrough.
- **Conference demo dry-run:** full setlist with scene 5 in position; operator validates feel, latency, and that no other scene regressed.

### 8.4 Test framework

Default to **`juce::UnitTest`** for new tests — zero new dependencies, JUCE-idiomatic, integrates with the existing build. The implementation plan will add a `tests/` target if the project does not already have one; if GoogleTest is preferred for richer assertions, that decision is open during planning.

## 9. Scope / non-goals

- ❌ Not training or fine-tuning a model. The pretrained Acids-IRCAM checkpoint is the v1 model.
- ❌ Not building a latent-space synthesizer (option B from Q3 — deferred to a future spec).
- ❌ Not building latent visualization (option C from Q6 — deferred).
- ❌ Not replacing scene 12. Scene 12 stays as-is; scene 5 is additive.
- ❌ Not adding CoreML acceleration. ONNX Runtime CPU is the v1 target. CoreML is a phase-2 perf optimization.
- ❌ Not adding Windows/Linux support for the RAVE path. ONNX Runtime keeps that door open, but it is not in scope here.

## 10. Phase-2 follow-ups (out of scope, captured for later)

- **CoreML conversion** for Apple Silicon Neural Engine acceleration (~2–5 ms inference vs ~10–25 ms ONNX CPU).
- **Latent-space synthesis** (option B): replace audio-domain conditioning with feature → latent MLP, gives finer per-feature control.
- **Latent visualization** in `RavePanel`: tiny scrolling display of the latent vector for "look how it works" demo moments.
- **Fine-tuned voice model**: train on a specific singer's corpus to give the scene a distinctive timbre.
- **Voice-blend scene**: parallel RAVE + clip-bank with crossfade (option C from Q2).

## 11. Open items for the implementation plan

- Which specific Acids-IRCAM checkpoint to bundle (several voice models exist with different timbres). Decision deferred to plan — likely tied to listening tests during implementation.
- Whether to add a `tests/` directory to the project or extend an existing one — depends on what the implementation plan finds.
- Exact bundle path for the model file inside the AU vs Standalone targets — uses the existing asset locator, but the specific resource declaration syntax in CMake needs verification against the WORLD/Piper precedent.
- ONNX Runtime version and operator set — will be pinned during the planning step.
