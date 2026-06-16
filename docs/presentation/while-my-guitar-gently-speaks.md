# While My Guitar Gently Speaks — Presentation Notes

A talk-companion document covering what the app is, the technical decisions
behind it, and the bug-driven evolution of the idea. Aimed at a software-
engineering audience comfortable with C++/audio terminology, but with a glossary
for the DSP-specific jargon.

Source material: project README, the design specs under
`docs/superpowers/specs/`, memory notes, and the git history on `main`.

---

## 1. What the project actually is

**While My Guitar Gently Speaks** is a standalone macOS audio app (now also an
AUv2 Logic plugin) used as the live instrument for a conference talk of the same
name. A live guitar signal is transformed in real time, on stage, into two
families of effect:

1. **Instrument Carousel** — pedal-style timbre transforms (organ → distortion →
   piano-ish → 8-bit → auto-wah → choir/pad).
2. **Speaking guitar** — a channel vocoder driven by text-to-speech audio, so
   the guitar appears to *speak* predefined text in rhythm with playing.

Scenes are switched live via a **Behringer FCB1010** MIDI foot controller; a
full-screen visualizer shows what the audience is hearing.

The single non-functional requirement that dominates every design decision is
**stability across repeated live performances** — the app cannot crash on stage.
Everything below is best read with that as the constant constraint.

---

## 2. The 15-minute live demo arc

The architecture is tuned for this on-stage sequence, end to end:

1. **Open clean.** A clean riff to establish the dry guitar sound.
2. **Instrument Carousel.** The same recognizable riff played through five
   instrument timbres in turn (organ, distorted, synth/piano, 8-bit, auto-wah,
   choir).
3. **The pivot.** "What if it could speak?"
4. **Whole-clip speech.** Step on a pedal → the guitar plays back a full pre-
   baked phrase through the vocoder. *Sounds like* the guitar is speaking, but
   the timing is the clip's, not yours.
5. **Note-triggered speech.** Step on the next pedal → the same phrase, split
   one-word-per-pluck. Nothing speaks until you play. *You* pace the sentence.
6. **Pitched singing.** Step on another pedal → instead of a noise floor, the
   carrier is a sawtooth oscillator that tracks the guitar's pitch. Now the
   spoken voice *sings the note you just played*.
7. **Sing mode.** Vibrato + chromatic snap on top — speech turns into sung
   speech, in tune.
8. **Panic pedal.** Always available; returns to dry clean guitar in ≤30 ms.

The structure of the demo and the code mirror each other deliberately: each
pedal step is a clear "before / after" the audience can hear, so the *evolution
of the idea* lands as the show progresses, not in slides.

---

## 3. Glossary

Audience-friendly defs for terms used below.

- **Vocoder / channel vocoder.** A speech-synthesis technique invented for
  telephony (Bell Labs, 1939). Splits one audio signal (the "modulator," here
  TTS speech) into a stack of narrow frequency bands, measures each band's
  loudness over time, and uses those loudness curves to shape the matching bands
  of a second audio signal (the "carrier," here the guitar). The carrier ends up
  "shaped" like the modulator's spectrum — so a guitar can sound like a voice.
  Daft Punk's robot vocals are the canonical example.
- **Carrier / modulator.** The carrier is what you *hear* (its timbre dominates
  the output). The modulator is what *shapes* it (its envelope dominates the
  output's loudness). Swapping them is the whole "talking guitar" trick.
- **Sibilance / fricatives.** Sharp consonant sounds (s, sh, t, f). They live
  mostly above ~4 kHz and are mostly unvoiced (noise, not pitch). A vocoder
  driven by a periodic carrier often loses them, so a real vocoder injects a
  separate noise band when the modulator has high unvoiced energy.
- **TTS (text-to-speech).** Software that turns text into spoken audio. Here we
  use three independent backends; details below.
- **Channel bands / Bark scale.** Speech intelligibility lives in non-uniformly
  spaced bands across frequency; the Bark scale matches the human ear better
  than equally spaced bins. The vocoder uses ~24 log-spaced bands from 80 Hz to
  10 kHz.
- **Real-time-safe / RT-safe.** Code that can run on the audio thread (the
  callback called every ~3 ms) without ever allocating memory, taking a lock,
  doing file I/O, or blocking. Violating this causes audible glitches at best
  and silence/crashes at worst.
- **DSP.** Digital Signal Processing — the math of audio (filters, oscillators,
  envelopes, FFTs, etc.).
- **JUCE.** A C++ framework used by virtually all commercial audio plugins on
  macOS/Windows. Provides real-time-safe primitives, plugin formats (VST3/AU/
  AAX/Standalone), GUI components, and MIDI plumbing.
- **AU / AUv2.** Apple's plugin format for hosts like Logic Pro and GarageBand.
  AUv2 loads in-process — the plugin runs inside Logic. AUv3 runs sandboxed in
  a separate process. We use AUv2 (reasons in §10).
- **F0 / pitch.** The fundamental frequency of a note. A plucked low-E string
  is F0 ≈ 82 Hz; the higher harmonics sit at integer multiples (164, 246, …).
- **YIN.** An autocorrelation-based pitch detector (de Cheveigné & Kawahara,
  2002). Robust on plucked strings where the fundamental is weaker than the 2nd
  or 3rd harmonic (very common on guitar low E).
- **PolyBLEP.** "Polynomial Band-Limited Step." A ~30-LOC trick to make a
  digital sawtooth oscillator anti-aliased and clean across the audible range,
  without per-sample FFT. Necessary because a raw digital saw produces aliased
  garbage at high pitches.
- **Onset detection.** The DSP problem of "did a new note just start?". An
  envelope follower with hysteresis + debounce.
- **Karplus-Strong.** A cheap, classic algorithm that synthesizes a convincing
  plucked-string sound from a short noise burst fed through a comb filter. Used
  in our tests to generate synthetic guitar plucks.
- **Granular pitch shifting.** A time-domain pitch-shifting trick — chop the
  audio into ~20–40 ms grains and replay them at a different rate, crossfading
  between them. Cheap, allocation-free, sounds OK on sustained material, audibly
  warbly on percussive material.
- **Forced alignment.** ML technique that takes audio + a text transcript and
  returns per-word (or per-phoneme) start/end timestamps. We avoid the heavy
  dependency and approximate it with energy-gap segmentation in v1.
- **Whisper / whisper.cpp.** OpenAI's open-weights speech-to-text model;
  `whisper.cpp` is the local C++ port that runs on Apple Silicon without a GPU.

---

## 4. The stack — and the alternatives that were rejected

**Language / framework: C++ with JUCE.**

- JUCE has the most production miles in live audio. The "no allocations on the
  audio thread" discipline is built into its idioms; the plugin-format export is
  free.
- *Swift + AVAudioEngine* — rejected: smaller live-audio track record, harder
  to share with a future plugin/Windows build.
- *Rust + cpal* — rejected: real-time-safety story is younger; commercial
  plugin formats aren't first-class.
- *Faust* — rejected: great for the DSP itself, awkward for the MIDI / state /
  GUI / file-I/O glue that this project is mostly made of.

**Form factor: standalone macOS app first, AU plugin second.**

- The standalone was the v1 because Logic was originally going to be hostile to
  the file-I/O / GUI / subprocess / network needs of the demo.
- AU was deferred but then *did* land (commit `9d81619`, June 2026) — the
  standalone proved the dev-iteration loop, and JUCE makes the AU export
  basically free once the rest is solid. See §10.

**Vocoder: classic channel vocoder, swappable behind `IVocoder`.**

- v1 ships `ChannelVocoder` — 24 log-spaced bands, biquad / state-variable
  filters, per-band envelope follower, sibilance noise channel. No FFT, no
  block-based processing. Latency is just filter group delay (few ms).
- v2 reserves the option to swap in a `NeuralVocoder` wrapping ONNX Runtime +
  an RVC model (real-time voice conversion). Same audio graph, same scene
  system. Not built; the interface seam exists so future-me can.

**TTS: three independent sources behind `ITTSSource`, with per-scene
fallback.**

- `PrebakedTTSSource` — loads `.wav` files baked offline (XTTS v2 / StyleTTS 2)
  by `tools/tts_prebake/`. Highest quality, zero risk live (it's just a wav
  player).
- `AppleTTSSource` — wraps `AVSpeechSynthesizer` via an Objective-C++ `.mm`
  file. OS-managed neural voices; needs a pumped main run loop, so it works in
  standalone + Logic but times out in headless `auval`.
- `PiperTTSSource` — spawns the bundled [Piper](https://github.com/rhasspy/piper)
  CLI binary as a subprocess and pipes text → PCM through stdio.

**Why three.** Defense in depth — any one source can break on stage (a model
file goes missing, a subprocess crashes, a voice gets uninstalled) and the show
keeps going. Each scene's JSON declares a `fallback` source; the chain walks
one hop on failure. This was directly motivated by the "cannot crash" mandate:
single-vendor dependencies are single points of failure.

**Build / asset story.**

- CMake + Ninja, JUCE as a git submodule, Catch2 v3 as a git submodule.
- The Piper binary and voice are gitignored; `./scripts/fetch_piper.sh`
  downloads them once. Build copies them into the .app and .component bundles
  automatically.

---

## 5. Top-level architecture

The audio graph is small and explicit. Read this in conjunction with §6.

```
                          ┌─────────────────────────────┐
Guitar in ──► InputStage ─┤ DC block, gate, gain         │
                          └─────────────────────────────┘
                                  │
                ┌─────────────────┼──────────────────┐
                ▼                 ▼                  ▼
       ┌─────────────────┐ ┌────────────────┐ ┌──────────────────┐
       │ Carousel        │ │ Vocoder        │ │ (clean dry pass) │
       │ (instrument     │ │ carrier=guitar │ │                  │
       │  scenes 1–5)    │ │ mod=TTS clip   │ │                  │
       └─────────────────┘ │ or per-word    │ │                  │
                ▲          │ stepped clip   │ └──────────────────┘
                │          └────────────────┘
                │                  ▲
                │                  │
        AudioGraph picks ONE wet branch per block (no overlap).
        Modulator source: NoteSteppedTTSPlayer (per-pluck) or
        TTSClipPlayer (whole clip), selected per-scene.
        Carrier floor: broadband noise (default) or
        PitchTrackedCarrier sawtooth (when pitch-singing is ON).
                                   │
                                   ▼
                          ┌────────────────┐
                          │ Mixer + master │ ─► Output
                          └────────────────┘
```

**Threading.**

- **Audio thread:** the audio graph. No allocations, no locks, no I/O. Period.
- **Message thread:** GUI, MIDI from the host, scene transitions, asset load.
  Talks to the audio thread via lock-free FIFOs and `std::atomic` snapshots.
- **Prewarm worker:** synthesizes live-TTS clips ahead of time in the
  background so scene activation is instant.
- **Offline:** `tools/tts_prebake/` Python CLI. Runs at design time, never at
  performance time.

**Scenes are JSON.** Each scene is one `assets/scenes/NN_name.json` file
describing which branches are active, vocoder mix, TTS source + text + voice +
fallback, carousel block, trigger mode, etc. Hot-reload watches the directory
during development; live performance is locked.

---

## 6. The evolution of the idea — phase by phase

This is the part that maps to the talk. Each phase is "what we built, what it
let us do, what bit us, what we did next." Notes call out the explicit
trade-offs along the way.

### 6.1 v1 baseline — instrument transforms + whole-clip speech (Phases 1–3)

**Built.** Input stage, channel vocoder, three TTS sources, scene engine,
FCB1010 → scene mapping, the visualizer, the prewarm worker. A scene either
runs the Carousel (instrument timbre) *or* the Vocoder (speaking), never both.
Speaking scenes play the entire TTS clip from sample 0 the moment the pedal is
pressed.

**Why a channel vocoder, not an FFT vocoder.** Classic channel vocoders use a
filter bank instead of an FFT, which means:

- *No block boundaries* — every input sample produces an output sample. FFT
  vocoders have to wait for a full window (~512–1024 samples) and overlap-add,
  which adds latency you can hear.
- *Allocation-free* once `prepare()` is called. The filter coefficients are
  fixed; only state buffers exist.
- *Tractable to reason about live.* When a filter goes unstable, you can
  isolate the band; when an FFT vocoder drops, the failure is opaque.

Cost: a channel vocoder is less natural-sounding than a phase vocoder or
neural vocoder. We pay this on purpose for v1.

**Why three TTS sources.** Already covered in §4 — the per-scene `fallback`
field is the load-bearing piece. If Piper's subprocess dies in the middle of
the show, the next scene activation walks the fallback chain to the prebaked
clip and the audience never notices.

### 6.2 The Instrument Carousel (Phase 4a) — five real pedal patches

**Built.** A new `audio::Carousel` module that runs the live guitar through a
fixed-order chain: `drive → waveshaper → crusher → resonant filter → chorus →
reverb → output trim`. Each stage is bypassed at near-zero cost when its JSON
config block is absent. Built on `juce::dsp` primitives (`WaveShaper`,
`StateVariableTPTFilter`, `Chorus`, `Reverb`) plus bespoke `Crusher` /
`EnvelopeFollower` / `Lfo` modules.

**Five patches.** Organ-ish (later replaced by choir), distortion, synth (later
replaced by piano), 8-bit chiptune, auto-wah.

**Trade-off — what we deliberately deferred.** The "easy wins" came first —
all five v1 patches were ones that *don't* need pitch detection or harmony. A
distortion or auto-wah just reshapes the input; no octave shifts. Octaver,
harmonizer, formant shifter, comb filter were all pushed to Phase 4b because
making them sound good while staying RT-safe and under the latency budget is
the riskiest DSP in the project.

### 6.3 Phase 4b — piano-ish and choir/pad via DIY granular pitch shift

**Built.** Fixed-ratio granular `PitchShifter`, multi-voice `Harmonizer`,
feedback `Comb`, fixed-vowel `Formant` filter. Octave-layered + comb-resonance
for piano-ish, multi-voice harmonizer + vowel formant + big reverb for choir.

**Trade-off — why DIY, not an off-the-shelf library.** Most open pitch-shift
libraries either allocate on first call, drag a heavyweight dependency, or have
latencies far above our 5 ms budget. Hand-rolling 4 simple stages was cheaper
to audit, easier to keep RT-safe, and let us tune grain size by ear.

**Honest limitation we accepted.** "Guitar → grand piano" is genuinely the
weakest target in the project. The octave-layer + comb gets to
"electric-piano-ish," not Steinway. We named it honestly, kept it on the
pedal, and moved on. Choir landed much better — multi-voice detuning + reverb
masks the granular warble.

**Risk we mitigated.** Comb filter feedback can self-oscillate; the
state-variable filter at high resonance can blow up. A *brick-wall output
limiter* sits at the end of the carousel chain to protect the PA. Every demo
pedal runs through it.

### 6.4 The pivot — "guitar triggers a word" (Phase 5a)

This is the moment in the talk the architecture was designed around. The Phase
3 whole-clip mode (a scene plays the full TTS phrase the instant the pedal is
pressed) gets the audience laughing once. The interesting thing is when *the
performer* paces the words.

**Built.**

- `audio::OnsetDetector` — envelope-follower with hysteresis and a debounce
  window. Fires "yes a note just started" on the clean (pre-effects) signal so
  the carousel/distortion doesn't confuse it.
- `audio::WordAligner` — energy-gap word segmentation. Takes the TTS audio +
  the source text (split on whitespace = word count), computes a short-window
  RMS envelope, places word boundaries at the centers of the N-1 largest
  low-energy gaps. Same algorithm runs for all three TTS backends — they all
  produce float PCM, so the segmenter doesn't need engine-specific timing APIs.
- `audio::NoteSteppedTTSPlayer` — note-triggered analogue of `TTSClipPlayer`.
  On each onset, advance `wordIndex_` and reset `segmentPlayPos_` to the next
  word's `startSample`. Between segments: emit silence. Auto-loop back to word
  0 after the last word.
- A scene-config flag `tts.trigger = "auto" | "note"` picks whether the
  modulator is the linear `TTSClipPlayer` (old behavior, scene 6 / "before") or
  the `NoteSteppedTTSPlayer` (new, scenes 7–8 / "after").

**The demo arc that this enabled.** Three speaking scenes mapped to three
pedals, in order: whole-clip (auto), word-by-word (note), word-by-word finale.
Stepping up the FCB walks the audience through the progression of the idea
without slides.

**Why energy-gap segmentation, not phoneme alignment.** Two reasons:

1. *Uniformity.* The three TTS backends have wildly different metadata
   stories. Apple gives you almost nothing, Piper gives you phoneme times,
   prebaked has whatever you compute offline. A uniform algorithm that only
   needs the float PCM (not the backend's introspection) is simpler and is the
   same for all three.
2. *Stability.* No new ML dependency, no fragile process, no latency surprise.

**Cost we accepted.** Energy-gap segmentation gets confused on run-together
speech, soft consonants, or words that elide ("won't", "you're"). We baked the
finale prebaked clip with clear word gaps and called it good.

### 6.5 Bug 1 — "I plucked one note, the guitar said three words"

This is the bit from your own notes — and it's a real, recurring stage-noticed
bug. Two distinct causes were diagnosed (see `docs/superpowers/specs/2026-06-13-word-sync-modes-design.md`):

1. **Onset double-trigger.** A single hard pluck has a noisy attack envelope
   that can cross the detector's threshold *two or three times* in ~30–50 ms.
   The detector advanced the word index every time.
2. **Mid-word interrupt.** Even if onset detection were perfect, fast playing
   would advance to the next word *before the current one finished*. Fast
   strumming = words clipped mid-syllable.

**Fix — word-sync modes.** Replace the single "every onset advances" behavior
with three user-selectable modes:

- **Latch (recommended default).** Once a word starts playing, *ignore* onsets
  until the segment finishes. The next onset *after* playback completes
  advances. Strict 1:1 mapping. No clipping. Cost: rapid plucks queue nothing
  and slow plucks leave silence between words.
- **Advance** (the old, kept as opt-in). Every onset advances and restarts.
  Most responsive; can cut mid-syllable.
- **Syllable.** Requires the scene text to include hyphen markers
  (`"gui-tar gent-ly"`). Each hyphen-bounded fragment is a syllable segment;
  syllable boundaries within a word are placed by *equal subdivision* of the
  word's measured duration. Cost: stress can land in the wrong place
  ("guh-TAR" vs "GUI-tar" — equal subdivision doesn't know which).

A second fix bumps the `OnsetDetector` minimum-interval to ~80 ms — kills the
most common false-double-trigger without affecting real musical playing.

**Per-scene override.** Scene JSON may set `"wordSync": "latch" | "advance" |
"syllable" | "global"`. If a scene requests syllable mode but the text isn't
hyphenated, the player falls back to Latch on whole words and the UI surfaces a
hint. Avoids silent surprises.

**Why hyphen markers, not automatic syllabification.** Auto-syllabification
(hyphenation libraries, CMU phoneme dict + maxent splitting) is *almost* good
enough, and "almost" is what would bite us live. Author-supplied hyphens are
explicit, deterministic, and trivial to author in a JSON file. The cost is one
keystroke per syllable in the scene file.

**What's still missing — and where it goes next.** Equal-duration syllable
subdivision is a v1 placeholder. The real fix for lyrical phrasing is *phoneme-
level alignment*: per-syllable start/end times measured from the actual audio
(Montreal Forced Aligner, whisper-timestamped, or a baked-offline aligner in
the prebake pipeline). That's the natural successor and the next thing to
build if syllable mode demos as "feels close but not musical."

### 6.6 Bug 2 — "Why does it sound atonal?" → pitch-tracked singing carrier

The classic channel vocoder needs a *carrier* with harmonic content for the
vocoder to shape. The default v1 carrier was `guitar + broadband noise` — the
noise floor preserves sibilance on vowels even when the guitar's energy is
elsewhere. Intelligible, but tonally disconnected from your playing. Hearing a
voice atop a sustained guitar chord feels off — like the voice has its own
opinion about pitch.

**Built (spec: `2026-06-12-pitch-singing-design.md`, merged `0bea39d`).**

- `audio::PitchTrackedCarrier` — a self-contained module that:
  1. Runs a **YIN F0 detector** on the clean guitar (window 2048, hop 256,
     threshold 0.15, clamp [40, 2000] Hz, silence guard + warm-up gate).
  2. Drives a **PolyBLEP-anti-aliased sawtooth oscillator** at the detected
     pitch.
  3. **Holds the last pitch** for ~250 ms after the note dies (so the voice
     keeps singing while you let off the string), then linearly **decays
     amplitude** over ~200 ms.
- `AudioGraph` runs the detector *always* (so the on-screen pitch readout is
  live regardless of toggle state). When pitch-singing is ON, the carrier
  becomes `guitar + carrierNoise * pitched_saw` and the vocoder's internal
  noise floor is bypassed.
- Toggle bound to MIDI **CC#80** ≥ 64 (latch). FCB doesn't send CC#80 stock;
  you reprogram one switch to send `Controller 80, value 127` on press.
- A small `NoteReadout` UI strip shows the detected note + cents + Hz at
  30 Hz, *whether the toggle is on or off*. This is intentional: it lets the
  performer (and the audience) see "the app already knows what note I played;
  flipping this switch makes it *use* that note." Standing visibility
  principle (see §11).

**Why YIN, not FFT-based pitch detection.**

- *Robust on plucked strings.* The fundamental of a low-E guitar (~82 Hz) is
  often weaker than the 2nd or 3rd harmonic. FFT/HPS (Harmonic Product
  Spectrum) detectors octave-error here unless you specifically harden them.
  YIN's cumulative-mean-normalized difference function handles it natively.
- *Lower latency.* 2048-sample window + 256-sample hop gives a usable F0 in
  ~30–50 ms. An FFT of comparable resolution needs ~90 ms windows.
- *Cheap.* O(N) per hop, <0.1 ms per frame, no allocation.
- *Tiny.* ~150 LOC. External libs (aubio, librosa) use YIN under the hood;
  taking the dependency only adds risk.

**Why PolyBLEP, not just a digital sawtooth.**
A naive digital sawtooth has discontinuities at wraparound, and those
discontinuities produce aliased components reflected back into the audible
band. At high pitches this sounds like crackly buzz. PolyBLEP adds a tiny
correcting polynomial around the wrap that band-limits the discontinuity — 30
lines of code, no FFT, allocation-free, sounds clean across the guitar's
range.

**Trade-off — monophonic only.** The carrier tracks the dominant fundamental;
a chord picks the lowest stable partial. Polyphonic tracking is a separate,
much larger build (and frankly an open research problem on guitars). For the
demo, mono is sufficient — the speaking voice can't sing a chord anyway.

### 6.7 Polish + Sing mode (the "now it actually sings" pass)

Live tests of the pitch-singing scene revealed two more usability bugs:

1. The raw sawtooth is bright and rough — its energy above 2 kHz makes the
   spoken voice harder to understand, not easier.
2. "Speaking-on-a-pitch" still sounds like robotic narration. The audience
   reads it as "the guitar is speaking," not "the guitar is *singing*."

**Polish (small, mostly tuning).**

- 1-pole IIR lowpass on the saw at ~2 kHz before it hits the carrier. Speech
  intelligibility lives below ~3 kHz; the sibilance you want comes from the
  vocoder's separate noise channel, not the saw's high harmonics.
- Hold time default 1000 ms → **250 ms**. The 1-second hold caused the saw to
  drone at the old pitch across word boundaries; 250 ms keeps the "voice
  carries the last chord" feel without the drone.
- Vocoder envelope follower time constant 15 ms → **25 ms**. Softens
  consonant edges, sustains vowels a bit, less robotic.

**Sing mode (the visible toggle).** A new "M  Sing" pill, independent of
pitch-singing:

- **Vibrato** — 5 Hz sine LFO, ±20 cents, applied per-sample to the saw's F0.
- **Chromatic quantize** — snap the detected F0 to the nearest chromatic
  semitone before the saw consumes it. Order: quantize first, then vibrato
  around the quantized tone.

The independence of `singing` from `pitchSinging` matters: it means a future
scene that routes the pitched carrier somewhere else (e.g. a carousel patch
that pitches the guitar through the same oscillator) inherits Sing mode "for
free."

**Why no chord/scale quantize.** Chromatic is the safe default — you can't
play a "wrong" note relative to *what you played*. Scale quantize ("snap to
A minor") would need a chord-aware UI and a scene-level scale tag, and would
introduce stage failure modes ("the singer is locked to G major; I just played
F#"). Out of v1 scope; the setter API supports it as a future addition.

### 6.8 The AU plugin (Logic Pro)

Originally an explicit non-goal. The standalone proved out the dev iteration
loop in seconds-per-cycle; once that was solid, JUCE's `juce_add_plugin`
target makes AU export almost-free.

**Built (spec: `2026-06-12-au-plugin-design.md`, merged `9d81619`).**

- AU added to `FORMATS` next to `Standalone`, `COPY_PLUGIN_AFTER_BUILD TRUE`
  auto-installs the `.component` to `~/Library/Audio/Plug-Ins/Components/`.
- Host-MIDI → scene plumbing: `processBlock` scans incoming MIDI for Program
  Change, writes the value to an atomic, a message-thread timer polls and runs
  the value through the existing `FCB1010Mapping`. Audio thread never calls
  `activateScene`.
- State persistence (`get/setStateInformation`): scene id + 3 vocoder knobs +
  pitch-singing + sing-mode booleans. JSON-style, backward-compatible — unknown
  keys ignored on load.
- `AssetLocator` uses `dladdr` to find the *plugin's own bundle* (the host's
  bundle path doesn't help). Assets are copied into the .component on build.
- Identity: product **Guitar Speak**, company **Todd B Fisher**, bundle
  `com.toddbfisher.guitarspeak`, AU type `aumf` (Music Effect — because it
  takes MIDI), subtype `GtSp`, manufacturer code `TdBF`. In Logic it appears
  under **Audio Units → Todd B Fisher → Guitar Speak**.

**Why AUv2, not AUv3.**

- AUv2 loads **in-process** in Logic. Which is exactly what lets
  `AVSpeechSynthesizer` (Apple TTS) and the Piper *subprocess* run inside the
  plugin.
- AUv3 runs in a hardened app-extension sandbox that almost certainly blocks
  the subprocess and parts of TTS.
- **The cost:** in-process means a plugin crash takes Logic down with it. So
  the dev workflow is: stabilize in standalone + run `pluginval` strictness-10
  before ever opening Logic with a new build. `auval` + `pluginval` are the
  gates; Logic is the milestone.

**A subtle macOS gotcha (worth a slide).** After a plugin rename, macOS's
`AudioComponentRegistrar` caches the old identity. If `auval` fails right
after a rename, `killall -9 AudioComponentRegistrar` and retry; it's not your
code.

### 6.9 Conversational AI (designed, partially built)

**Direction (spec: `2026-06-12-conversational-ai-design.md`).** A "press a
foot pedal, speak, the guitar replies" pipeline on top of the existing
TTS→vocoder chain.

```
PTT pedal ──► MicCapture (sidechain bus in AU, AudioDeviceManager in standalone)
            ──► whisper.cpp (local STT, ggml-base.en, ~150 MB)
            ──► ILlmClient (Anthropic cloud OR Ollama local, user-selectable)
            ──► PersonaRegistry (6 presets, editable system prompts)
            ──► existing PluginProcessor.enqueueSayText()
            ──► (existing) TTS → ChannelVocoder → guitar speaks the reply
```

**Built so far (per git log).** `ConversationEngine` state machine + tests,
`ConversationBuffer` (rolling 10 messages), `PersonaRegistry` (six presets),
`AnthropicClient` + `OllamaClient` (both behind `ILlmClient`),
`JuceHttpTransport` (mockable `IHttpTransport` seam, no run-loop dependency),
`WhisperTranscriber` (`ITranscriber` interface), `MicCapture` with lock-free
SPSC FIFO + linear-resample to 16 kHz, the AU sidechain bus declaration,
state persistence for AI fields, the conversation panel + AI settings panel,
FCB pedal bindings for PTT / cancel / clear, canned-fallback option for LLM
errors. About a dozen commits, all on `main`.

**Why a state machine, not a callback chain.** Every error path needs to
return to Idle with a visible reason without blocking the audio thread or
leaving the UI in a confusing intermediate state. A state machine makes those
transitions enumerable and testable. The `test_conversation_engine.cpp`
specifically drives every transition: happy path, cancel in each stage, error
in each stage, destructor mid-flight, multiple PTT, clear-during-thinking,
persona swap mid-session, model swap mid-session.

**Why a `IHttpTransport` seam.** `juce::URL::createInputStream` works
identically in Logic and headless `auval`, but it's slow to fake in tests. The
seam lets `FakeHttpTransport` script the responses (including 401/429/5xx/
timeout) in unit tests without ever opening a socket. Catches the "we mocked
the database; prod broke" failure mode in advance.

**Why local-default + cloud-fallback.** Ollama (local llama3.2:3b or similar)
runs the whole loop without an internet connection, so a hotel-wifi-died
demo still ships. Anthropic's Claude Haiku is the cloud option when latency
matters more than offline-ness.

---

## 7. Stability — what "cannot crash" actually meant in practice

Stability is the project's standing constraint, and most of the testing /
process design is downstream of it. The choices that fall out:

- **The audio thread does ZERO of: allocation, locking, file I/O, blocking
  syscalls.** `RealtimeSentinel` is a test harness that hooks `operator new`,
  `operator delete`, and `pthread_mutex_lock` and aborts if any are called from
  a thread registered as the audio thread. The full graph runs through it on
  60 s of synthetic input every test run.
- **Test count > 290** at this writing, with per-module unit tests, golden-file
  scene renders, integration tests, and headless-safety tests for the AU. CI
  blocks on any failure.
- **Manual gates that stay manual.** A 4-hour soak test before a conference; a
  full 15-minute dress rehearsal three times; FCB hot-plug yank tests. These
  are subjective + hardware-dependent and live in a pre-conference checklist.
- **Brick-wall limiter** at the carousel output protects the PA from any
  filter / comb self-oscillation surprise. Earplug safety for the audience is
  not negotiable.
- **`auval` + `pluginval` strictness-10** are AU release gates that catch
  threading / lifecycle issues *outside Logic* — so a bad build never gets to
  Logic in the first place.
- **Graceful degradation everywhere.** TTS source fails → fallback chain.
  Piper missing → noise-floor still works. AI LLM times out → optional canned
  reply. Pitch detection unvoiced → hold-and-decay. FCB unplugged → keyboard
  shortcuts still fire scenes.

---

## 8. The visibility principle

A recurring theme across the UI: every internal decision the app makes should
be visible on stage, and every parameter that affects the sound should be
tweakable with immediate audible feedback.

Concrete examples:

- **`DiagToggleBar`** — pills for V/N/Sib/P/M (vocoder bypass, noise floor,
  sibilance, pitch-singing, sing mode). Each lights when active. Keyboard
  shortcuts mirror them.
- **`VocoderPanel`** — sliders for makeup, carrier-floor, sibilance. The
  carrier-floor label dynamically switches between "Noise floor" and "Pitched
  floor" depending on the pitch-singing toggle — so the operator always sees
  what the knob is controlling *right now*.
- **`NoteReadout`** — note + cents + Hz, live, 30 Hz refresh, *whether the
  toggle is on or off*. This sells the demo as much as it diagnoses bugs.
- **`TtsStatusBar`** — Apple / Piper / Prebaked availability, active source
  for the current scene, "fell back: piper → prebaked" when the chain walked.
- **`WordReadout`** — the current word being spoken, in big text, while the
  guitar speaks it.
- **`StatePill`** for the AI conversation engine — `Idle / Capturing /
  Transcribing / Thinking / Speaking / Error` with the reason text on errors.

This is mostly free UX: it doubles as the demo's pedagogy. The audience sees
what's happening as the performer flips switches.

---

## 9. Open problems and what's next

A few honest things to mention if asked:

- **Phoneme-level alignment.** v1 syllable mode does equal-subdivision; the
  stress is right when the syllables are equal-duration and wrong otherwise.
  Real prosody needs per-syllable timestamps from the audio (Montreal Forced
  Aligner, whisper-timestamped, or a baked-offline aligner in the prebake
  pipeline). This is the most lyrically-impactful next thing to build.
- **Polyphonic pitch tracking.** The carrier is monophonic. Polyphony on
  guitar is an open research problem (overlapping harmonics, sympathetic
  vibration of unplucked strings, transient noise). Not on the v1 roadmap.
- **Phase-vocoder / neural vocoder.** The `IVocoder` interface is the seam.
  v2 would wrap an RVC or so-vits-svc model via ONNX Runtime — selectable
  per-scene. Adds an ML dependency and validation surface; deferred.
- **Audience-text encore.** A small embedded HTTP server + QR code so the
  audience submits a text and the guitar speaks it via the existing live-TTS
  path. The plumbing already exists; just the web piece is missing.
- **Conversational AI hardening.** Code is in tree; live demo arc not yet on
  stage. Hotel-wifi failure modes and Logic sidechain ergonomics are the
  remaining unknowns.
- **A sub-millisecond race** in the carrier-noise save/restore path: if the
  UI slider moves *during* a pitch-singing block, the new value can be
  clobbered for that one block. Self-corrects on the next slider move; tracked
  as a follow-up.

---

## 10. Talking guitar v2 — making the guitar speak *naturally*

The note-triggered speech in v1 is the demo's punchline, but listening hard it
has two recurring problems on stage.

**Problem 1: the syllable boundaries are fictional.** `WordAligner` finds word
boundaries by listening for silence gaps in the baked TTS clip — ~30 ms
release, 15 % peak threshold — and that part works. But *syllables* are then
just split proportionally within each word's bounds:
`start = wordStart + total*s/n` at `WordAligner.cpp:109`. There is no phoneme
awareness anywhere. "Au-to-mat-i-cal-ly" gets six evenly-spaced slices
regardless of which slice actually contains the /m/ or the /æ/. A pluck rarely
lands on a real consonant attack, and the audience hears that mush even if
they can't name it.

**Problem 2: no sustain support.** Each pluck plays a fixed-length slice and
stops. A held chord has nowhere to go — it can't ring the vowel out, it can't
breathe. So the speech feels grid-locked to the right hand even when phrasing
is legato.

### What "naturally" could mean — three target modes

These are not variants of one knob; they're meaningfully different signal
graphs, so each ships as its own scene (FCB1010 footswitch + distinct preset)
so onstage selection is unambiguous.

- **A. Speech-leads, guitar-colors.** The phrase plays through at natural
  speech rate; the guitar modulates timbre, emphasis, and gating but does not
  pace it. A held chord lets a word ring; a flurry of plucks adds rhythmic
  accents on top of words flowing past.
- **B. Guitar-leads, speech-follows.** The pluck still triggers a speech
  event, but the *unit* is a real phoneme-aligned syllable. Vowels stretch
  under sustain; consonants snap on attack.
- **C. Fused.** No baked clip at all. Continuous source-filter synthesis where
  the guitar's pitch and dynamics shape phoneme targets in real time — the
  guitar's sound *is* the talking voice.

Underlying DSP is shared in `src/audio/` — one `SpeechSource`, one alignment
pipeline, one `VocoderBus` — but each mode owns its own control surface.

### v1 stays exactly as it is — it's the exhibit

The current note-stepped behavior is preserved bit-for-bit, renamed
*"Speak v1 — Note-Stepped"*. The demo narrative becomes "here's the naive
approach, here's why it falls apart, here's what we tried." `NoteSteppedTTSPlayer`,
the proportional split in `WordAligner`, the `WordSyncSelector` — all
untouched. A golden-audio smoke test on the v1 scene catches accidental drift
during the v2 build. A pre-recorded backup of v1's quirks covers rooms where
live conditions hide the problem.

### Three ways to build Mode B (the first one to ship)

- **B1 — Phoneme-aligned clip + vowel grain-loop *(recommended)*.** Extract
  phoneme timings from Piper/eSpeak-NG at synthesis time. Group phonemes into
  real syllables by sonority peaks. On pluck, play the syllable's onset
  through its vowel; on sustain, hold the vowel by pitch-synchronous grain
  looping (~20–40 ms grains, PSOLA-style); on release or next pluck, play out
  the consonant coda. Smallest jump from v1's architecture; fixes both
  problems above.
- **B2 — Phoneme-aligned clip + phase-vocoder time-stretch on sustain.** Same
  boundaries, but stretch the vowel itself instead of looping a grain.
  Preserves natural vibrato and breathiness; adds 30–50 ms latency; future
  polish pass, not v1 of v2.
- **B3 — Concatenative phoneme synthesis (no baked clip).** A phoneme/diphone
  database driven directly by guitar onsets and sustain. Most flexible but
  loses prosody; high engineering and asset-pipeline risk for a conference
  deadline.

### "Bake time" is overloaded — three text sources, one pipeline

Phoneme alignment happens at different moments depending on where the text
came from. The same `PhonemeAlignedClip` data structure feeds Mode B's player
either way — only the build moment differs.

| Source | When alignment happens | Latency tolerance |
|---|---|---|
| Demo clips (shipped) | Offline before app start; output is `samples.wav` + `meta.json` with phoneme timings | None — already done |
| Say textbox (user-typed) | On-demand after the user hits Say; ~150–400 ms with Piper on M-series | Imperceptible |
| LLM / conversation scene | On-demand per streamed sentence | Hidden inside LLM response time |

The conversation scene actually benefits the most: today's
`NoteSteppedTTSPlayer` slices LLM output by envelope-guess with no
hand-authored timings to lean on; under Mode B, LLM text gets real phoneme
boundaries automatically.

Two honest constraints. Apple TTS (`AVSpeechSynthesizer`) does not expose
phoneme timings, so Mode B scenes lock the source to Piper; the UI surfaces
this. Streaming-during-synthesis — start playing chunk 1 while chunk 2 is
still rendering — is a v2 of v2; sub-100 ms LLM-to-mouth response is not a
v1-of-v2 goal.

### Status: B1 shipped (2026-06-16)

Scene 10 *"Speak v2 — Guitar-Lead"* ships. The pipeline: text → Piper
(audio) → `espeak-ng` (phoneme labels) → `Syllabifier` (sonority-peak
grouping) → `PhonemeSteppedTTSPlayer` (Attack/Sustain/Coda state
machine with vowel grain-loop sustain). UI: `Ph` pill lights when
v2 is active; `WordReadout` shows `Syl N / M`; `SceneIndicator` strip
grew from 10 to dynamic-count slots to expose ids 10+.

Two honest deviations from §10's original direction:

1. *Phoneme durations are uniform, not real.* espeak-ng's `--pho` flag
   only emits durations for MBROLA voices; for standard voices we use
   `-x --sep=" "` (labels only) and rescale uniformly. Sonority-based
   syllable boundaries are still real; per-phoneme timing is not. B2/B3
   has room to revisit.

2. *v1 is bit-for-bit unchanged.* The `[v1golden]` drift detector
   ran ~36 000 byte-equal assertions on every commit through Phases 5–7;
   nothing in v1 moved.

B2 (phase-vocoder vowel stretch) and B3 (concatenative phoneme synth)
remain future work.
---

## 11. The talk arc, condensed (if you want one slide)

> 1. **Vocoders are 80-year-old phone tech.** Modulator shapes carrier; voice
>    shapes guitar; guitar sounds like it talks.
> 2. **Whole-clip speech is a punchline.** Funny once. Doesn't scale.
> 3. **Note-triggered speech makes the performer pace the words.** One pluck =
>    one word. Now *you* are the speech engine.
> 4. **But a plucked attack double-fires.** Latch mode fixed it; syllable mode
>    is next.
> 5. **The spoken voice on a noise floor sounds atonal.** Detect the guitar's
>    pitch; carrier becomes a pitched sawtooth; the voice sings the note you
>    just played.
> 6. **Speaking-on-a-pitch isn't singing.** Vibrato + chromatic snap pushes it
>    over the line.
> 7. **Stability across performances is the constraint.** Three TTS sources,
>    no allocations on the audio thread, brick-wall limiters, ~290 tests, AU
>    validation gates before Logic.
> 8. **What's next.** Phoneme alignment for real syllable timing.
>    Conversational AI for "press pedal, speak, guitar replies." Audience-text
>    encore.

---

## 12. Appendix — file pointers for follow-up reading

- Top design: [`docs/superpowers/specs/2026-05-29-while-my-guitar-gently-speaks-design.md`](../superpowers/specs/2026-05-29-while-my-guitar-gently-speaks-design.md)
- Carousel A (5 patches): [`docs/superpowers/specs/2026-05-31-instrument-carousel-a-design.md`](../superpowers/specs/2026-05-31-instrument-carousel-a-design.md)
- Carousel B (piano + choir): [`docs/superpowers/specs/2026-05-31-instrument-carousel-b-design.md`](../superpowers/specs/2026-05-31-instrument-carousel-b-design.md)
- Note-triggered speech: [`docs/superpowers/specs/2026-05-31-note-triggered-speech-design.md`](../superpowers/specs/2026-05-31-note-triggered-speech-design.md)
- Pitch-tracked singing: [`docs/superpowers/specs/2026-06-12-pitch-singing-design.md`](../superpowers/specs/2026-06-12-pitch-singing-design.md)
- Singing polish + Sing mode: [`docs/superpowers/specs/2026-06-13-singing-polish-design.md`](../superpowers/specs/2026-06-13-singing-polish-design.md)
- Word-sync modes (latch/advance/syllable): [`docs/superpowers/specs/2026-06-13-word-sync-modes-design.md`](../superpowers/specs/2026-06-13-word-sync-modes-design.md)
- AU plugin packaging: [`docs/superpowers/specs/2026-06-12-au-plugin-design.md`](../superpowers/specs/2026-06-12-au-plugin-design.md)
- Conversational AI: [`docs/superpowers/specs/2026-06-12-conversational-ai-design.md`](../superpowers/specs/2026-06-12-conversational-ai-design.md)
- Logic Pro setup (sidechain, API key, Ollama): [`docs/au-logic-setup.md`](../au-logic-setup.md)
- Source tree: `src/audio/` (DSP), `src/scenes/` (scene engine), `src/midi/`
  (FCB), `src/ai/` (conversation), `src/app/` (UI + JUCE processor).
