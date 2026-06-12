# Audio Unit (AU) Plugin Build — Design

**Status:** Approved design (brainstorming) — 2026-06-12

**Goal:** Ship the existing "While My Guitar Gently Speaks" app as an **Audio Unit
v2 plugin** alongside the current Standalone, so it can run inside Logic Pro —
processing the live guitar through the speaking/vocoder pipeline while Logic
plays back and records other tracks. The speaking path is the priority; the
carousel effects come along for free but are not a focus.

**Non-goal (separate future spec):** the full conversational AI (mic → STT →
local LLM → response text). This spec only documents it as a *north star* so the
packaging decisions don't preclude it.

**Standing design principle — visibility & tweakability.** Across this project,
surface *what the app is doing* wherever reasonable (status indicators,
diagnostics, meters) and expose controls to tweak behavior with immediate
audible/visible feedback. The diagnostic toggles and the live vocoder panel are
examples of this; the AU work continues it (e.g. a TTS-engine status readout so
you can *see* which speech engine is active and why — especially whether Piper is
available in-plugin).

---

## 1. North star (context, not built here)

The plugin's reason for being is a **conversational talking guitar**, decomposed
into two flows that alternate to form a back-and-forth conversation:

- **Flow A — "mic → AI":** mic → voice-activity detection → speech-to-text →
  local LLM (with conversation history) → response text. *Entirely new; future.*
- **Flow B — "AI → guitar speaks":** response text → TTS → note-stepped vocoder →
  the guitar talks as you play. **~80% already built today** — the only future
  change is that the spoken text becomes *dynamic* (from the LLM) instead of a
  fixed scene phrase.

A **conversation manager** (future) owns turn-taking and history; it produces
text that feeds the existing `ITTSSource → vocoder` seam. Flow B itself supports
**two text sources**, both already converging on that seam:

1. **Manual text** — the existing "type-and-say" panel (`SayPanel`): user types a
   phrase → TTS → vocoder. Works today; must keep working in the plugin.
2. **Dynamic text** — the live LLM source (future).

**Why this matters for *this* spec:** the only architectural seam we must protect
is `text → ITTSSource → ChannelVocoder`. It already exists. The packaging work
below changes nothing about it.

**One known future wrinkle (decided in the conversational spec, not here):** a
plugin only sees its own track's audio, but the conversation needs *two* signals
— the **mic** (for STT) and the **guitar** (the vocoder carrier). Options:
sidechain input for the mic, a separate mic track, or letting the **Standalone**
open both directly. This slightly favors the Standalone as the conversation host.

---

## 2. Approach: AUv2, one codebase, two formats

`juce_add_plugin(... FORMATS Standalone AU ...)` builds **both** targets from the
same source. The core `PluginProcessor` is already a `juce::AudioProcessor`, so
the DSP, scenes, and TTS are shared verbatim — no fork.

**Why AUv2 (not AUv3):** AUv2 loads **in-process** in Logic. That is what lets
`AVSpeechSynthesizer` (Apple TTS) and the Piper **subprocess** run. AUv3 runs in
a hardened app-extension **sandbox** that would almost certainly block both the
subprocess and parts of TTS. JUCE's `AU` format target is AUv2. *Rejected: AUv3.*

**Cost of AUv2:** in-process means a plugin crash can take **Logic down with it**.
This raises the stakes on the project's standing "cannot crash" requirement.
Mitigation is in the dev workflow (§7): stabilize in Standalone + `pluginval`
before trusting it in a live Logic session.

---

## 3. Components / changes

The foundation is done (it's a `juce::AudioProcessor`). Only a handful of
plugin-specific changes are needed.

### 3.1 Build (`src/app/CMakeLists.txt`)
- Add `AU` to `FORMATS` (currently `Standalone`).
- Set `COPY_PLUGIN_AFTER_BUILD TRUE` so each build installs the `.component` to
  `~/Library/Audio/Plug-Ins/Components/` for Logic to re-scan on launch.
- Keep the existing `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` (AU type/subtype/
  manufacturer 4-char codes) stable so Logic's plugin identity doesn't churn.

### 3.2 Host MIDI → scenes (`PluginProcessor::processBlock`)
Today `processBlock(buffer, midiMessages)` **ignores** `midiMessages`; the
Standalone gets MIDI via the direct-CoreMIDI `MidiRouter`. For the plugin, the
FCB1010 is routed through Logic, so MIDI arrives in `midiMessages`.

- In `processBlock`, iterate `midiMessages`; for each **Program Change** (and any
  other mapped message), run it through the existing `FCB1010Mapping` to produce a
  `SceneCommand`, then `sceneEngine_.activateScene(...)`.
- `activateScene` is **message-thread** API and must **not** be called from the
  audio thread (`processBlock`). RT-safe approach:
  - In `processBlock` (audio thread): scan `midiMessages`; on a Program Change,
    store the raw PC value into an `std::atomic<int> pendingHostProgramChange_`
    (a plain atomic store — no allocation, no mapping work on the audio thread).
  - On the **message thread**: a `juce::Timer` (in `PluginProcessor`) polls the
    atomic; when it changes, run the value through `FCB1010Mapping` →
    `sceneEngine_.activateScene(...)`. This reuses the existing scene-change
    handling (the `processBlock` "active scene id changed → callAsync builds the
    TTS clip" path already runs once a scene is activated).
  - Sentinel value (e.g. `-1`) means "nothing pending"; the timer resets it after
    applying so repeated identical PCs still re-trigger if needed.
- The Standalone's `MidiRouter` path stays unchanged. Both coexist; the plugin
  simply also honors host MIDI.

### 3.3 Asset bundling (`src/app/CMakeLists.txt`)
The asset-copy custom command currently targets the Standalone `.app` Resources.
Extend it so `assets/` is also copied into the **AU `.component`** bundle's
`Contents/Resources/assets/`, so `AssetLocator` resolves scenes, prebaked clips,
and the Piper binary/voice when running as a plugin.
- `AssetLocator` already resolves runtime paths relative to the bundle; verify it
  picks the `.component` Resources when loaded as an AU (it uses the executing
  bundle, which differs between `.app` and `.component`).

### 3.4 State persistence (`PluginProcessor::get/setStateInformation`)
Currently empty no-ops. Persist a **small** amount of state so reopening a Logic
project restores the user's setup:
- active scene id,
- the three vocoder knobs (makeup, carrier-noise, sibilance).
Serialize as a tiny JSON/`ValueTree` blob. On `setStateInformation`, apply on the
message thread (activate scene, set vocoder params). Keep it minimal and
forward-compatible (ignore unknown keys).

### 3.5 Editor in plugin mode (`PluginEditor`)
Hide the controls that are meaningless when Logic owns I/O:
- the **MIDI device picker** (`MidiDevicePicker`) — host provides MIDI,
- any audio-device UI (the Standalone audio settings come from JUCE's
  `StandalonePluginHolder`, not our editor, so likely nothing to remove there).
Gate via `juce::JUCEApplicationBase::isStandaloneApp()` (or
`wrapperType != wrapperType_Standalone`). Everything else (scene indicator, word
readout, diagnostics, vocoder panel, say panel, scopes) stays.

### 3.6 TTS in-plugin (no engine change expected, but the key risk)
Apple TTS and Piper are already routed off the audio thread (prewarmer threads /
the recently-fixed shared-state synthesis). The open question is whether they run
correctly when hosted **inside Logic** (in-process AUv2). The Piper subprocess is
the riskiest. **Graceful degradation already exists:** if Piper is unavailable the
chain falls back to prebaked (fixed earlier), and prebaked is pure file playback
that will always work. Apple TTS is very likely fine in-process.

### 3.7 TTS-engine status readout (visibility) — `TtsStatusBar`
Per the standing visibility principle, add a small always-on UI strip showing the
state of each speech engine so the operator can *see* what's happening (vs.
guessing why a scene is silent or robotic):
- **Apple:** available (always, on macOS).
- **Piper:** ready (binary + voice present and last synth succeeded) /
  unavailable (with the short reason: "binary not fetched" or "failed in host").
- **Prebaked:** available.
- **Active source for the current scene** and, when a fallback fired, an explicit
  "fell back: piper → prebaked" note.
Data comes from the existing source registry + `PiperTTSSource::isReady()` plus an
atomic "last synth outcome" the chain already computes. Message-thread polled
(like the other readouts). This directly answers the "tell me what's going on with
Piper in the plugin" requirement and is equally useful in the standalone.

---

## 4. Milestone 1 — de-risk FIRST

Before any MIDI/state/editor polish, prove the risky parts:
1. Add `AU` to `FORMATS`, build the `.component`.
2. Run **`auval -v aufx <subtype> <manufacturer>`** — Apple's validator must pass.
3. Run **`pluginval`** at a high strictness level (catches threading/lifecycle
   crashes outside Logic).
4. Load in **Logic** (or AU Lab); on a guitar track, switch to a speaking scene,
   play notes, and confirm: (a) audio processes, (b) **Apple TTS speaks**,
   (c) **Piper speaks or cleanly falls back to prebaked**.

If TTS works in-plugin → proceed to the rest. If Piper is blocked in-plugin →
prebaked fallback already covers it; note the limitation and continue.

---

## 5. Recording in Logic (no code — native Logic)

Document for the user:
- Insert the AU on the guitar track. **Bounce in Place** renders the track through
  the plugin to a new audio track (captures the processed/spoken take), or
- route the track's output to a **bus** and record that bus live onto another
  track for a real-time capture.

---

## 6. Error handling / stability

- **Audio thread stays allocation/lock-free** — unchanged; the host-MIDI parsing
  in `processBlock` must also be allocation-free (iterate the buffer, push an
  atomic pending-scene; no `activateScene` on the audio thread).
- **No throwing on the audio thread or in host callbacks.** (The AppleTTSSource
  UAF — a `std::mutex::lock()` that threw and aborted — is the cautionary tale;
  already fixed.)
- **Multi-instance:** if the user loads two AU instances, each gets its own
  `PluginProcessor` (TTS sources, prewarmers). Functionally fine, resource-heavy;
  one instance is the expected case. No special handling now (YAGNI).

---

## 7. Dev workflow (important — bake into the build/docs)

Iterating *inside Logic* is slow (Logic caches the AU in-process; a rebuilt binary
needs a **Logic restart** or remove+re-insert to load). So:
- **Do most development in the Standalone** (rebuild + relaunch in seconds) and the
  **Catch2 test suite** — same core code.
- **Validate the AU without Logic** via `auval`, `pluginval`, and **AU Lab**.
- **`COPY_PLUGIN_AFTER_BUILD`** installs the `.component` automatically; Logic
  re-scans on launch (no manual re-import).
- **Only open Logic at milestones** for host-MIDI, recording, and in-Logic TTS.
- Because AUv2 is in-process, **`pluginval` (high strictness) before Logic** is the
  guardrail against taking down a live session.

---

## 8. Testing

- **Unit/integration (Catch2):** host-MIDI → scene mapping (feed a `MidiBuffer`
  with a Program Change to a small harness around the scene-command path; assert
  the resulting scene id) and state round-trip (`getStateInformation` →
  `setStateInformation` restores scene + vocoder knobs).
- **`pluginval`** at strictness 8–10 as a CI/local gate for the AU.
- **`auval`** pass as a build acceptance check.
- **Manual:** the Milestone-1 Logic checklist (§4).

---

## 9. Out of scope (this spec)

- The full conversational AI (Flow A: mic → VAD → STT → LLM; dynamic Flow B text
  source). Its own spec next.
- Mic / sidechain input routing (decided in the conversational spec).
- Any new/changed carousel effects (kept as-is; not a plugin focus).
- **Pitch-matched "singing"** — detect the guitar's pitch and feed the vocoder a
  pitch-tracked harmonic oscillator (sawtooth) carrier so the spoken/AI voice
  sings in the guitar's note. A future audio-engine feature (its own spec):
  format-agnostic, slots into the existing carrier-mix path in
  `AudioGraph`/`ChannelVocoder`, and would also resolve the intelligibility-vs-
  pitch tension (a pitched-but-harmonic carrier is both intelligible *and* in
  tune, unlike the broadband noise floor). Not in this spec.
- AUv3 / app-extension packaging.
- Windows/VST3 (macOS + AU only for now).

---

## 10. File-touch summary

- `src/app/CMakeLists.txt` — add `AU` to FORMATS, `COPY_PLUGIN_AFTER_BUILD`,
  extend asset copy to the `.component`.
- `src/app/PluginProcessor.{h,cpp}` — host-MIDI → scene in `processBlock`
  (allocation-free, via pending-scene + existing callAsync apply path);
  `get/setStateInformation` persistence.
- `src/app/PluginEditor.{h,cpp}` — host the new `TtsStatusBar`; hide MIDI/audio
  device pickers when not standalone.
- `src/app/TtsStatusBar.{h,cpp}` — new: per-engine availability + active-source +
  fallback readout (visibility principle).
- `src/app/PluginProcessor.{h,cpp}` — expose engine-status accessors (Piper
  ready?, last synth outcome, active source) for the readout.
- `tests/` — host-MIDI→scene mapping test; state round-trip test.
- Docs — dev-workflow + recording notes (README or a dedicated doc).
