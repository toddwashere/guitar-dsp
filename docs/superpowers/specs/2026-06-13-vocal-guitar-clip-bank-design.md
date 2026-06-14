# Vocal Guitar — Clip Bank (Scene 2)

**Status:** Design approved 2026-06-13. Implementation pending.

**Companion specs:**
- [Mic Talkbox (Scene 3)](2026-06-13-mic-talkbox-design.md)
- [Auto-Vocal Formant (Scene 4)](2026-06-13-auto-vocal-formant-design.md)

## Problem

Jack Black's "mouth-guitar" bit — vocalizing a guitar solo with the voice
("WEEDLY-WEEDLY-WEEEE", "NER-NER-NERRR") — is a comedy staple that lands because
the *voice* shapes the *guitar phrasing*. This app already turns the guitar into
a speaker via channel vocoder + TTS-clip modulator. The natural extension is to
swap the TTS modulator for a bank of short vocal-guitar samples, so each pick
attack triggers a different cartoonish vocalization on top of the played note.

Goal: a Scene-2 carousel patch where every pick gives the guitar a different
Jack-Black-style vocal character, hands-free, demo-stable.

## Non-goals

- Real-time mic vocalizing → guitar (that is Phase B / Scene 3).
- Formant-LFO autowah without a modulator clip (that is Phase C / Scene 4).
- Pitch-tracking the vocal sample's pitch (the guitar carrier already carries
  pitch; the sample contributes spectral character only).
- Continuous "long form" vocalizations chopped at syllable boundaries — Phase A
  is exclusively about *short, atomic* clips, sidestepping the segmentation
  problems we hit on long TTS clips.

## Approach

A new component, `ClipBankPlayer`, sits alongside `NoteSteppedTTSPlayer` in
`AudioGraph` as a third `ModulatorSource`. It owns a fixed-size, ordered bank of
short audio clips (≤ ~1.5 s each). On each detected guitar onset it:

1. Advances the bank cursor (sequential, wraps after the last clip).
2. Triggers playback of the new clip from sample 0.
3. Plays the clip sample-by-sample as the vocoder modulator output until the
   clip ends, then outputs silence (the vocoder smoothly bleeds out via its
   envelope-follower time constant).

The bank is loaded at scene activation from a directory of WAV files. No
syllable timings, no segmentation, no aligner — each file is one atomic vocal
hit. This is the entire abstraction.

The user's guitar is the carrier. The carousel preset for Scene 2 dresses the
carrier for rock-solo character (drive, light crusher, a 5th + octave
harmonizer voice, modest reverb).

### Why not extend NoteSteppedTTSPlayer?

`NoteSteppedTTSPlayer` advances *within* a single segmented clip on each onset
(word-by-word). `ClipBankPlayer` advances *between* whole clips on each onset.
Same trigger semantics (onset), different units of advancement (clip vs.
segment). Forcing them into one class would muddy the abstraction: the segment
machinery (`TTSClip::words`, `syllableTimingsMs`, WordSyncMode) doesn't apply
when each unit is already its own file. Sibling classes, shared `OnsetDetector`
discipline.

## Architecture

### New components

#### `src/audio/ClipBankPlayer.{h,cpp}`

```cpp
namespace guitar_dsp::audio {

// Plays a bank of short audio clips, advancing one clip per detected guitar
// onset. Each clip is an atomic unit — no internal segmentation. After the
// active clip finishes, output is silence until the next onset.
//
// RT-safe in process(); allocates only on setBank (message thread).
class ClipBankPlayer {
public:
    ClipBankPlayer();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message thread. Swap the active bank atomically. Pass an empty vector
    // to clear. Bank order is the playback order.
    void setBank(std::vector<TTSClipPtr> clips);

    // Message thread. Reset cursor to clip 0; next onset plays clip 0.
    // RT-safe via pending flag, mirrors NoteSteppedTTSPlayer::rewind().
    void rewind() noexcept;

    // Audio thread.
    //   onsetSrc = clean guitar (drives OnsetDetector)
    //   modOut   = vocoder modulator output for this block
    void process(const float* onsetSrc, float* modOut, std::size_t numSamples) noexcept;

    // UI: current bank cursor (clip index), or -1 if idle.
    int currentClipIndex() const noexcept;
    int bankSize() const noexcept;

private:
    OnsetDetector onset_;

    // Double-buffered bank for RT-safe swap (same pattern as TTSClipPlayer).
    std::atomic<bool> newBankFlag_ {false};
    std::vector<TTSClipPtr> pendingBank_;
    std::vector<TTSClipPtr> activeBank_;

    int         cursor_     = -1;   // last-triggered clip index
    std::size_t playPos_    = 0;    // sample offset within active clip
    bool        playing_    = false;

    std::atomic<int>  currentClipIndex_ {-1};
    std::atomic<bool> pendingRewind_   {false};
};

} // namespace guitar_dsp::audio
```

The clip type is the existing `TTSClipPtr` (`TTSClip` is just "PCM + optional
segments"; we ignore the segments field). Loading short WAVs goes through the
existing `PrebakedTTSSource::synthesize` which already does resampling.

#### `assets/clips/vocal-guitar/`

A new asset folder. Each subfolder is one clip in the bank:

```
assets/clips/vocal-guitar/
  00_wee/audio.wav        ~300 ms
  01_doo/audio.wav        ~250 ms
  02_ner/audio.wav        ~200 ms
  03_new/audio.wav        ~250 ms
  04_yeah/audio.wav       ~400 ms
  05_brrr/audio.wav       ~400 ms
  06_skronk/audio.wav     ~500 ms
  07_weeeeee/audio.wav   ~1200 ms (sustain)
  08_ahhhh/audio.wav     ~1200 ms (sustain)
  09_ner-ner-ner/audio.wav ~700 ms (triplet)
```

Ten clips. Names are hints; content can be substituted freely by replacing the
WAV. Recording is the preferred authoring path (user voicing into the mic, one
take per clip, trimmed in any audio editor) because Piper TTS does not naturally
produce vocal-guitar onomatopoeia. Piper-synthesized variants are acceptable
fallbacks if recording is unavailable, knowing they'll sound less convincing.

No `meta.json` required for Phase A (no syllable timings). The `PrebakedTTSSource`
will be extended to load a clip without segments — see [Schema](#schema)
changes below.

### Modified components

#### `src/scenes/Scene.h` — `TtsConfig` extensions

```cpp
struct TtsConfig {
    // existing fields…
    std::string source;        // "prebaked" | "apple" | "piper" | "clipBank" | ""
    std::string clip;          // single-clip key (when source != "clipBank")
    std::vector<std::string> bank;  // NEW. clip keys, used when source == "clipBank"
    std::string text;
    // …other existing fields unchanged
};
```

The bank field is a vector of clip keys (e.g. `["00_wee","01_doo",…]`) loaded
from the same `PrebakedTTSSource` root. Empty when `source != "clipBank"`.

#### `src/scenes/SceneLibrary.cpp`

Parse the new `bank` field as a JSON array of strings. Treat `source: "clipBank"`
as valid alongside the existing source names.

#### `src/audio/AudioGraph.{h,cpp}`

```cpp
enum class ModulatorSource {
    Linear,       // existing: whole-clip TTS
    NoteStepped,  // existing: word-stepped TTS
    ClipBank,     // NEW
};

// in AudioGraph:
ClipBankPlayer& clipBankPlayer() { return clipBankPlayer_; }
```

In `process()`, route the modulator from `clipBankPlayer_` when
`modulatorSource_ == ClipBank`, mirroring the existing NoteStepped branch.
Onset source is `postInputBuffer_` (the clean guitar) — same as the existing
`NoteSteppedTTSPlayer` call.

#### `src/scenes/SceneEngine.cpp` (or wherever scene activation lives)

On scene activation with `source == "clipBank"`:
1. Iterate `bank[]`, call `PrebakedTTSSource::synthesize(key)` for each.
2. Collect non-null clips into a `std::vector<TTSClipPtr>`.
3. Call `audioGraph.clipBankPlayer().setBank(std::move(clips))`.
4. Set `audioGraph.setModulatorSource(ClipBank)`.
5. Call `audioGraph.clipBankPlayer().rewind()` so the next onset starts at clip 0.

Failures (missing files, decode errors) log to stderr and skip the missing
clip; the bank is whatever loaded successfully. If the bank is empty,
`process()` emits silence and the scene degrades to dry carrier through the
carousel (acceptable failure mode for a live demo — no crash, no noise).

#### `src/audio/PrebakedTTSSource`

Already loads `<rootDir>/<key>/audio.wav`. To support clips without segments,
ensure `synthesize()` returns a valid `TTSClip` when only `audio.wav` exists
(no `meta.json`). If the current implementation requires `meta.json`, extend it
to treat absence as "no segments, single whole-clip span." Verify in
implementation; the file is small.

The `rootDir` passed in for vocal-guitar bank loads is
`assets/clips/vocal-guitar/`. This can be a second `PrebakedTTSSource` instance
in `AudioGraph` (cleaner) or a parameter to the existing one (smaller change).
Pick whichever lands more cleanly in implementation.

#### `src/app/WordReadout` (UI)

When `source == "clipBank"`, display the cursor as `"clip 03 / 10"` plus the
clip key (e.g. `"new"`). Keep the existing Rewind pill — it now calls
`clipBankPlayer.rewind()` instead of `noteSteppedPlayer.rewind()`. The
P/M/W keyboard shortcuts have no meaning in clip-bank mode (no word-sync, no
pitch/sing toggles relevant) and should be no-ops on this scene.

### Scene 2 JSON

Replace `assets/scenes/02_carousel_distortion.json` with:

```json
{
  "id": 2,
  "name": "Vocal Guitar — clip bank",
  "color": "#e07b00",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": {
    "source": "clipBank",
    "bank": [
      "00_wee", "01_doo", "02_ner", "03_new", "04_yeah",
      "05_brrr", "06_skronk", "07_weeeeee", "08_ahhhh", "09_ner-ner-ner"
    ],
    "trigger": "note",
    "clarity": 0.1
  },
  "carousel": {
    "enabled": true,
    "drive": 8.0,
    "waveshaper": { "type": "tanh", "amount": 1.2 },
    "harmonizer": { "intervals": [7, 12], "detuneCents": [0, 0], "mix": 0.35 },
    "reverb": { "roomSize": 0.25, "wet": 0.12 },
    "outputTrimDb": -3.0
  }
}
```

The carousel preset is the v1 take on "rock solo guitar character." Tunable
during implementation polish.

## Data flow (per audio block)

```
clean guitar ─┬──► ChannelVocoder.carrier
              │
              └──► OnsetDetector ──► (on onset) advance cursor, retrigger clip
                                          │
                                          ▼
                          ClipBankPlayer.activeBank[cursor]
                                          │
                                          ▼
                                ChannelVocoder.modulator
                                          │
                                          ▼
                                  Carousel chain ──► output
```

No new audio-thread allocations. Bank swaps are atomic (double-buffer pointer
swap), same pattern as `TTSClipPlayer::setClip`.

## Testing

**Unit tests** (`tests/`):
- `ClipBankPlayer_AdvancesOnOnset` — feed an onset-shaped buffer, assert cursor
  increments and clip 0 plays from sample 0.
- `ClipBankPlayer_WrapsAfterLast` — after `bank.size()` onsets, cursor returns
  to 0.
- `ClipBankPlayer_EmptyBankSilent` — `setBank({})`, assert modOut is all zero.
- `ClipBankPlayer_RewindResetsCursor` — call rewind(), next onset plays clip 0
  regardless of previous cursor position.
- `ClipBankPlayer_OutputsZeroAfterClipEnds` — onset triggers a 300-sample clip,
  process 600 samples, assert samples 300–599 are zero.
- `SceneLibrary_ParsesBankField` — JSON with `"source":"clipBank","bank":[...]`
  populates `TtsConfig::bank`.

**Manual / demo verification:**
- Load Scene 2, pick a single note: hear vocal character on first pick, hear
  *different* vocal character on second pick. Each pick = next clip in bank.
- Pick a 12-note run: hear 10 distinct vocalizations then wraparound (11th note
  uses clip 0 again).
- Hit Rewind pill: next pick uses clip 0.
- Switch to Scene 1 and back to Scene 2: cursor resets to 0 (scene activation
  calls rewind).
- Disconnect bank files mid-session (rename folder): no crash, dry carrier
  output. Re-load: works again.

## Risk / open questions

- **Clip-recording quality.** This is the biggest unknown. The architectural
  work is straightforward; the polish work is the user recording 10 convincing
  Jack-Black-style hits. Plan a single "clip authoring session" after the
  engine is wired up, with the loop: record → drop in folder → play guitar →
  iterate. If recording quality is the blocker, the bank can be seeded with
  any short percussive vocal sample (gibberish "ba-da-da-doo") to validate the
  engine independently.
- **Onset detector sensitivity.** Reusing the existing `OnsetDetector` settings
  from `NoteSteppedTTSPlayer` is the v1 plan. If pick-attack double-triggers
  cause unwanted clip skipping, re-tune the detector in implementation; this
  has been an issue before in the note-stepped path and is documented.
- **Vocoder makeup gain.** Short percussive modulators may push the vocoder
  envelope follower differently than sustained TTS speech. Expect to retune
  `ChannelVocoder.setOutputGain` for this scene; visible in the existing
  VocoderPanel so tuning is interactive.

## Replaces (no hard deletes)

- `assets/scenes/02_carousel_distortion.json` is *moved* to
  `assets/scenes/archive/02_carousel_distortion.json`, not deleted.
  `SceneLibrary::loadDirectory` uses non-recursive `directory_iterator`, so
  the archive subfolder is inert (the file does not load) but stays at hand
  for reuse, reference, or restoration in seconds. The new Scene 2 JSON
  takes its place under `assets/scenes/`.
- No C++ to delete — the carousel modules the old patch used (waveshaper,
  filter, reverb) remain in use by other scenes.
