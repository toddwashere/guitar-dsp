# Conversational AI — Design Spec

**Status:** Approved design (brainstorming) — 2026-06-12
**Predecessor:** [2026-06-12-au-plugin-design.md](2026-06-12-au-plugin-design.md) (north-star reference)
**Author:** Todd Fisher (with Claude)

**Goal:** Add a **mic → AI → guitar-speaks** conversational pipeline on top of
the existing TTS → vocoder chain. The audience experience: user presses a foot
pedal, speaks into a mic, the AI thinks for a beat, and the **guitar** voices the
reply through whichever scene is active.

The feature ships in both **Standalone** and the **AU plugin** (Logic Pro). Same
code, different mic input source (sidechain bus in AU, `AudioDeviceManager` in
standalone).

**Standing design principle — visibility & tweakability.** State pills, status
meters, transcript readouts, and per-stage timings make the pipeline legible at
a glance — both for the user during a demo and for debugging when something
isn't behaving.

**Top requirement — stability.** This runs in live conference performances.
Every error path returns cleanly to Idle with a visible reason; nothing blocks
the audio thread; failures degrade gracefully (canned fallback, fall-through to
manual control).

---

## 1. Background

The app already has a working **text → vocoder → guitar** chain (`enqueueSayText`
→ `TTSPrewarmer` → `ITTSSource` → `ChannelVocoder`). Three TTS sources
(Apple/Piper/prebaked) and a scene system that selects voice/effect. The
"type-and-say" panel proves the text-to-guitar path end-to-end.

Conversational AI plugs in as a **new producer of text** for that existing
chain. Beyond the new text source, this spec also adds: live mic capture
(including AU sidechain), local speech-to-text (whisper.cpp), and dual LLM
backends (cloud Anthropic + local Ollama) selectable from the UI.

---

## 2. Goals & non-goals

### Goals

- One-tap (FCB pedal or UI button) mic capture → STT → LLM → TTS → guitar voices reply.
- Dual LLM: cloud (Anthropic API) and local (Ollama HTTP). User picks via a
  dropdown. Local is the demo default; cloud is a one-tap fallback.
- Local STT (whisper.cpp), local TTS (existing). Mic audio never leaves the
  laptop.
- Six persona presets with editable system prompts; conversation memory (last
  10 messages, clearable).
- Full **AU plugin** support, including Logic Pro sidechain mic routing.
- Test coverage parity with the existing suite (no untestable corners; mock
  every external dependency).

### Non-goals

- Autonomous "improv duet" mode (the originally-discussed Flow B). Deferred —
  build the mic-driven pipeline first; autonomous mode becomes a follow-on by
  swapping the trigger and prompt template.
- Conversation history that persists across sessions. Always fresh on load.
- Wake-word / always-listening. Push-to-talk only.
- Voice cloning, prosody control, or non-English STT/LLM.
- In-AU API-key configuration UI parity: Settings panel ships in both, but the
  key itself is one PropertiesFile shared across all bundle instances.

---

## 3. User experience

### Live moment

1. User presses **PTT pedal** (FCB pedal 9) or clicks **● Record**.
2. State pill pulses red: `● Capturing`. Mic level meter shows live signal.
3. User speaks: *"Tell me about yourself."*
4. User presses pedal again or clicks **■ Stop**.
5. State: `… Transcribing` → `… Thinking` → `🔊 Speaking`. Pill colors advance.
6. Guitar voices the reply through the active scene's voice (e.g. Apple TTS in
   scene 6, Piper in scene 7, etc.).
7. Transcript shows both messages with per-stage timings.
8. State returns to `▶ Idle`. Ready for the next turn.

End-to-end target: **≤4 s** with cloud, **≤6 s** with local Ollama, on the
demo Mac.

### Personas

Six presets. Selecting one populates an editable system-prompt text area; edits
persist per-preset.

| Preset | Flavor |
|---|---|
| **Interviewer** *(default)* | Asks short, curious questions about the music and the player. |
| Snarky | Witty, dry, mildly roasting. |
| Weathered session player | The guitar itself, opinions about every song ever played on it. |
| Studio engineer | Technical, deadpan, comments on tone and timing. |
| Curious AI | Naive, wonder, short sentences, asks the audience things. |
| Plain assistant | No flavor — for dev/debug. |

All prompts include the same reply-shape guardrail: *"Reply in 1–2 sentences,
max 25 words. No lists."*

### Memory

Rolling buffer of the **last 10 messages** (5 user/AI turns). "Clear" button +
FCB pedal binding (pedal 10) resets to empty.

### Reply-shape

Hardcoded guardrail in every system prompt: 1–2 sentences, max 25 words, no
lists. Knobs in Settings (`max_sentences`, `max_words`) for future tuning. Hard
ceiling: `max_tokens = 80` on the API call.

---

## 4. Architecture

Layered interfaces + a slim orchestrator. Mirrors the existing
`ITTSSource` / `TTSPrewarmer` / `TTSSynthChain` pattern.

```
ITranscriber            ILlmClient              (existing) ITTSSource
   │                       │                        │
WhisperTranscriber     AnthropicClient         AppleTTSSource
                       OllamaClient            PiperTTSSource
                                               PrebakedTTSSource
                              ▲
                              │
                  ConversationEngine
                  (state machine, buffer, persona;
                   knows nothing of HTTP, audio, or vocoder internals)
                              ▲
                              │
                    MicCapture (sidechain bus | AudioDeviceManager)
                              │
                          PluginProcessor.enqueueSayText() ← existing
```

### Runtime flow (one turn)

```
[PTT pedal | UI Record] ──► ConversationEngine.startTurn()
                              state: Idle → Capturing
                              MicCapture.beginCapture()
                                ├─ AU:        reads from sidechain bus 1
                                └─ Standalone: reads from AudioDeviceManager
[PTT release | UI Stop] ──► ConversationEngine.endTurn()
                              state: Capturing → Transcribing
                              ITranscriber.transcribe(audio) ──► userText
                              state: Transcribing → Thinking
                              ConversationBuffer.append(user, userText)
                              PersonaRegistry.buildMessages(buffer, persona) ──► messages
                              ILlmClient.generate(messages) ──► aiText
                              state: Thinking → Speaking
                              ConversationBuffer.append(ai, aiText), truncate-to-10
                              processor.enqueueSayText(aiText)   ← existing TTS path
                              [TTS finishes naturally]
                              state: Speaking → Idle
```

### Threading model

- `processBlock` is RT-safe: pushes mic samples into a lock-free FIFO.
- A single **background worker thread** owns the state machine and runs all
  async stages (STT, LLM, TTS handoff) sequentially. One queue, one consumer.
- UI thread reads state via atomics + `juce::Timer` polling — same pattern as
  `TtsStatusBar`.

### File layout

```
src/ai/                             ← new directory
  ConversationEngine.{h,cpp}        state machine + orchestration
  ConversationBuffer.{h,cpp}        rolling 10-message log (pure data)
  PersonaRegistry.{h,cpp}           6 presets + custom-prompt storage
  IHttpTransport.h                  mockable HTTP seam
  JuceHttpTransport.{h,cpp}         juce::URL impl (no run-loop dep)
  ILlmClient.h                      backend interface
  AnthropicClient.{h,cpp}           HTTPS /v1/messages
  OllamaClient.{h,cpp}              HTTP localhost:11434
  ITranscriber.h                    STT interface
  WhisperTranscriber.{h,cpp}        whisper.cpp wrapper

src/audio/
  MicCapture.{h,cpp}                ← new; pulls audio, hands off complete utterance

src/app/
  ConversationPanel.{h,cpp}         ← new; transcript + Record/Clear buttons
  AiSettingsPanel.{h,cpp}           ← new; model + persona + API key + mic device

(modified)
  PluginProcessor.{h,cpp}           +sidechain bus, +ConversationEngine wiring
  PluginEditor.{h,cpp}              +ConversationPanel, +AI status pill
  PluginState.{h,cpp}               +persona id, +custom prompts, +selected model id
  FCB1010Mapping.{h,cpp}            +PTT pedal + cancel/clear pedal bindings
```

---

## 5. Components

### `ConversationEngine` — the orchestrator

Owns the state machine; sequences STT → LLM → TTS handoff. Knows nothing of
HTTP, audio formats, or vocoder internals.

```cpp
class ConversationEngine {
public:
  enum class State { Idle, Capturing, Transcribing, Thinking, Speaking, Error };

  ConversationEngine(ITranscriber&, ILlmClient&, MicCapture&,
                     ConversationBuffer&, PersonaRegistry&,
                     std::function<void(std::string)> sayCallback);

  void startTurn();
  void endTurn();
  void cancelTurn();
  void clearConversation();

  State           state() const noexcept;
  std::string     lastError() const;
  StageTimings    lastTimings() const;        // STT/LLM/TTS in ms

  void setLlmClient(ILlmClient&);             // Idle only
  void setPersona(PersonaId, std::string customPrompt);
  void onTtsPlaybackFinished();               // PluginProcessor signals
};
```

All dependencies are interfaces or fake-able — engine is fully unit-testable
without network or audio I/O.

### `ConversationBuffer` — rolling 10-message log

Pure data structure. Append/truncate/clear/snapshot. Mutex-protected because UI
and engine both read.

```cpp
struct Message { enum Role { User, Assistant }; Role role; std::string text; };

class ConversationBuffer {
public:
  void                 append(Message::Role, std::string);
  void                 clear();
  std::vector<Message> snapshot() const;
  static constexpr size_t kMaxMessages = 10;
};
```

Truncation: when size > 10, drop oldest. Symmetric, doesn't try to preserve
user/assistant pairs.

### `PersonaRegistry` — preset + custom prompt

```cpp
enum class PersonaId { Interviewer, Snarky, WeatheredGuitar,
                       StudioEngineer, CuriousAi, PlainAssistant };

class PersonaRegistry {
public:
  std::string              promptFor(PersonaId) const;
  void                     setCustomPrompt(PersonaId, std::string);
  void                     resetToDefault(PersonaId);
  static std::string       defaultPromptFor(PersonaId);

  std::vector<Message>     buildMessages(const ConversationBuffer&,
                                         PersonaId) const;
};
```

`buildMessages` produces the array sent to the LLM: system message (persona
prompt + reply-shape guardrail) + last-10 conversation history. Default prompts
in Appendix A.

### `MicCapture` — input abstraction

```cpp
class MicCapture {
public:
  void                beginCapture();
  void                appendFromAudioBlock(const float* samples, int n);  // RT-safe
  std::vector<float>  endCapture();                                       // 16 kHz mono
  bool                isCapturing() const noexcept;
  float               currentPeakDbfs() const noexcept;
};
```

- `appendFromAudioBlock` is lock-free SPSC FIFO push, zero allocations.
- `endCapture` drains and resamples to 16 kHz mono on the background thread.
- Max utterance: **30 s** (auto-end with truncation flag).
- In AU: bus 1 (sidechain); zero-channel bus = "Mic: not routed."
- In standalone: `AudioDeviceManager` input device.

### `IHttpTransport` + `JuceHttpTransport`

```cpp
struct HttpResponse { int status; std::string body; std::string error; };

class IHttpTransport {
public:
  virtual HttpResponse post(const std::string& url,
                            const std::map<std::string,std::string>& headers,
                            const std::string& body,
                            std::chrono::milliseconds timeout) = 0;
  virtual HttpResponse get (const std::string& url,
                            std::chrono::milliseconds timeout) = 0;
};
```

`JuceHttpTransport` wraps `juce::URL::createInputStream` — pure JUCE, no
run-loop dependency, works identically in Logic and headless `auval`.
`FakeHttpTransport` powers all LLM-client tests.

### `ILlmClient` + `AnthropicClient` + `OllamaClient`

```cpp
struct LlmRequest { std::vector<Message> messages;
                    int                  maxTokens = 80;
                    std::chrono::milliseconds timeout = 10s; };
struct LlmReply   { std::string  text;
                    std::string  error;
                    int          promptTokens, completionTokens; };

class ILlmClient {
public:
  virtual LlmReply    generate(const LlmRequest&) = 0;
  virtual std::string displayName() const = 0;
  virtual std::string statusText()  const = 0;
};

class AnthropicClient : public ILlmClient {
public:
  AnthropicClient(IHttpTransport&, std::string apiKey, std::string modelId);
};

class OllamaClient : public ILlmClient {
public:
  OllamaClient(IHttpTransport&, std::string modelTag);
  static std::vector<std::string> listInstalledModels(IHttpTransport&);
  static bool                     isRunning(IHttpTransport&);
};
```

Defaults: Anthropic → `claude-haiku-4-5` (fast + cheap). Sonnet 4.6 selectable.
Ollama → first installed model, or `llama3.2:3b` if user wants a hint.

### `ITranscriber` + `WhisperTranscriber`

```cpp
struct TranscriptionResult { std::string text; std::string language;
                             std::chrono::milliseconds latency;
                             std::string error; };

class ITranscriber {
public:
  virtual TranscriptionResult transcribe(const std::vector<float>& mono16k) = 0;
  virtual std::string         modelName() const = 0;
};

class WhisperTranscriber : public ITranscriber {
public:
  WhisperTranscriber(juce::File modelFile);
};
```

Default model: `ggml-base.en.bin` (~150 MB). `tiny.en` / `small.en` listed in
Settings if present under `Resources/whisper/`.

---

## 6. State persistence & config

Three storage tiers, each holding a different scope.

### Tier 1: `PluginState` (per-instance, persists in song / plugin preset)

```cpp
struct PluginState {
  // existing
  int    sceneId;
  float  vocoderBands, vocoderGate, vocoderMix;

  // new
  std::string selectedModelId;        // "claude-haiku-4-5" | "ollama:llama3.2:3b"
  PersonaId   personaId;              // default Interviewer
  std::map<PersonaId, std::string> customPromptByPersona;  // empty = use default
  int         maxSentences      = 2;
  int         maxWords          = 25;
  std::string sttModelId        = "whisper-base.en";
  int         pttPedalId        = 9;
  int         clearChatPedalId  = 10;
};
```

Backward-compatible: missing fields load as defaults. No migration code. Tested
against an old `9734848` fixture.

### Tier 2: `PropertiesFile` (per-user, machine-global)

Path: `~/Library/Application Support/Todd B Fisher/Guitar Speak/settings.xml`.

```
anthropic_api_key            (string)
last_anthropic_test_ok       (bool + timestamp)
ollama_endpoint              (default "http://localhost:11434")
debug_log_llm_prompts        (bool)
debug_echo_transcripts       (bool)
canned_fallback_on_llm_error (bool, default false)
```

API key never travels with a saved project. Shared between standalone + all AU
instances on the machine. Concurrent edits protected by
`juce::InterProcessLock`.

### Tier 3: In-memory only (transient)

`ConversationBuffer`, Ollama detection state, mic level, current state, last
timings, in-flight HTTP. All lost on plugin reload / app quit. Deliberate —
every session is a fresh demo.

---

## 7. UI / panels

### `ConversationPanel`

Sits below `SayPanel`. Type-and-say is for prepared phrases; ConversationPanel
is for live dialog.

**Full mode (standalone):**
```
┌─ Conversation ─────────────────────────────────────────────────┐
│  ▶ Idle    Mic: ✓ –22 dBFS    Model: Claude Haiku 4.5 ✓        │
│                                                                │
│  [transcript scroll, last 10 messages]                         │
│   You: tell me about yourself                                  │
│   AI : I've been played by three guys before this one.         │
│                                                                │
│  Last turn: STT 1.2s · LLM 0.8s · TTS 0.6s                     │
│                                                                │
│  [ ● Record ]    [ Clear ]    [ ⚙ Settings ]                   │
└────────────────────────────────────────────────────────────────┘
```

**Compact mode (AU):**
```
┌─ Conversation ─────────────────────────────┐
│  ▶ Idle  Mic ✓  Haiku ✓                    │
│  You: tell me about yourself               │
│  AI : I've been played by three guys…      │
│  [● Rec] [Clr] [⚙]                         │
└────────────────────────────────────────────┘
```

Layout switch driven by `wrapperType_AudioUnit` vs `wrapperType_Standalone`.
Same widget classes, different flags.

### `AiSettingsPanel`

One modal (standalone) / slide-out drawer (AU). Contains:

- **Model** dropdown: lists `Claude Haiku 4.5 (cloud)`, `Claude Sonnet 4.6
  (cloud)`, and every Ollama model detected on `localhost:11434`.
- **Status pills:** `Ollama: N models` / `Ollama: not running` (with the
  `ollama serve` command shown), `Anthropic: key set ✓` / `key missing`.
- **Refresh Ollama** button (re-scan), **Test Anthropic** button (1 ping).
- **Anthropic API key** masked input. Stored in PropertiesFile only.
- **Persona** dropdown + editable system-prompt textarea (auto-populates from
  preset; user edits persist per-preset). **Reset to default** button.
- **Reply shape** sliders: max_sentences (1–5), max_words (5–100).
- **STT model** dropdown (whichever `.bin` files are present).
- **Input device** dropdown (standalone only).
- **Pedal bindings** subsection: PTT pedal id, clear pedal id.

### Status additions

| Where | What |
|---|---|
| Status bar next to `TtsStatusBar` | AI state pill: `▶ Idle` / `● Capturing` (red) / `… Transcribing` / `… Thinking` / `🔊 Speaking` / `⚠ Error: <reason>` |
| Status bar (compact mic strip) | `Mic: ✓ –22 dBFS` / `Mic: silent` / `Mic: not routed` |
| `DiagToggleBar` | New toggles: `Show AI timings`, `Log LLM prompts`, `Echo transcripts to console` |
| `SceneIndicator` | Unchanged — scene change mid-conversation uses new voice on next turn |

### FCB pedal bindings

| Pedal | Action | Default |
|---|---|---|
| **PTT** | Press → start capture; press again → end capture | pedal 9 |
| **Clear chat** | Long-press → clear conversation buffer | pedal 10 |
| **Cancel turn** | Short-press → abort current stage, return to Idle | pedal 10 |

Configurable in `AiSettingsPanel`. UI Record/Stop button is always available as
a fallback.

### Visual state coding

- Idle — neutral gray
- Capturing — pulsing red
- Transcribing / Thinking — animated dots
- Speaking — green
- Error — amber with reason text

The state pill is the single most important on-screen element during a demo.

---

## 8. Error handling & fallback

**Rule everywhere:** every error path returns to Idle, with a visible reason,
never blocks the audio thread.

### Failure modes

| Source | Failure | Handling |
|---|---|---|
| **Mic (AU)** | Sidechain has 0 channels | Pre-flight reject: "Mic: not routed in Logic" |
| **Mic (standalone)** | Input device unavailable | Pill: "Mic: device error"; Settings shows device list |
| **Mic** | <200 ms captured | Auto-cancel: "didn't hear anything" |
| **Mic** | >30 s captured | Auto-end with truncation; STT proceeds on last 30 s |
| **STT** | Model file missing | At startup: AI disabled; pill "STT model not installed" |
| **STT** | Empty / error | "Couldn't transcribe — try again" |
| **STT** | >15 s elapsed | Cancel: "STT too slow" |
| **LLM Anthropic** | Key missing | Settings red banner; pre-flight reject |
| **LLM Anthropic** | 401 | Pill: "API: key invalid" |
| **LLM Anthropic** | 429 | "Rate limited"; honor `retry-after` |
| **LLM Anthropic** | 5xx | "Anthropic service error" |
| **LLM Anthropic** | Timeout (10 s) | "Timed out"; optional canned fallback |
| **LLM Ollama** | Not running | Pill: "Ollama: not running"; pre-flight reject with start command |
| **LLM Ollama** | Model not pulled | "Run: `ollama pull <model>`" |
| **TTS** | Synthesis fails | Existing fallback chain handles; AI text still logged |
| **Pedal** | Not bound / FCB disconnected | UI Record button always works |
| **PTT press semantics** | Press from Idle → `startTurn`. Press from Capturing → `endTurn`. Press from Transcribing / Thinking / Speaking → ignored (logged in debug) |
| **User cancel mid-flight** | `cancelTurn()` | State aborts at next checkpoint, returns to Idle |
| **Clear mid-flight** | `clearConversation()` | Cancels first, then clears |
| **Plugin destroyed mid-flight** | Destructor | Cancel + join with 2 s timeout |
| **Headless (auval/pluginval)** | Network timeout | Short timeouts, graceful fall-through (Apple TTS pattern) |

### Pre-flight validation

When user changes model selection in Settings, engine immediately validates:
- Anthropic: key present? (No API call until Test button.)
- Ollama: `isRunning()` ping → fetch models → check selected model pulled.

Pill updates before the first turn. No "press pedal, discover broken" moments.

### Cancellation plumbing

```cpp
class CancellationToken {
public:
  void cancel();
  bool isCancelled() const noexcept;
};
```

Engine holds one; fresh on each `startTurn()`. Passed to transcriber + LLM
client. `JuceHttpTransport` checks at 1 s intervals during read, calls
`juce::WebInputStream::cancel()`. `WhisperTranscriber` checks between chunks.

### Optional: canned fallback on LLM error

Settings checkbox (off by default). When on, LLM failure → engine picks a
random reply from a small pool (`["Hmm, let me think.", "Say that again?",
"Hard to say."]`) → TTS. Transcript still shows the error reason below.

**Recommended on for live demos** (dead air is worse than a generic reply).
**Default off** so the failure is visible during dev.

### Crash safety

| Concern | Mitigation |
|---|---|
| `processBlock` blocking | Only does RT-safe FIFO push |
| whisper.cpp throwing | Wrapped in `try`/`catch` |
| Malformed LLM JSON | Try/catch → `LlmReply.error` populated |
| Concurrent buffer access | Mutex; UI takes snapshot copy |
| Destructor during turn | Cancel + 2 s join; HTTP cancelled immediately |
| pluginval strictness-10 | No allocations in `processBlock`, short timeouts everywhere |

---

## 9. Logic Pro setup + AU specifics

### One-time per-project routing

Documented in `docs/au-logic-setup.md` (extends existing). Steps:

1. Create a mic input track. Audio track, input = mic channel on interface.
   Monitor off.
2. On guitar track, insert **Guitar Speak**.
3. In the plugin window header, click **Side Chain** dropdown → pick mic track.
4. Confirm pill shows `Mic: receiving signal`.
5. Save project.

AI Settings (model, persona, API key) are global — no per-project setup.

### AU sidechain declaration

```cpp
BusesProperties()
  .withInput ("Input",  AudioChannelSet::stereo(), true)
  .withInput ("Mic",    AudioChannelSet::mono(),   false)  // ← new sidechain
  .withOutput("Output", AudioChannelSet::stereo(), true);
```

`isBusesLayoutSupported` accepts: main mono or stereo; mic disabled, or mic
mono, or mic stereo (downmixed in `MicCapture`).

In `processBlock`:

```cpp
auto guitar = getBusBuffer(buffer, /*isInput=*/true, /*busIdx=*/0);
auto mic    = getBusBuffer(buffer, /*isInput=*/true, /*busIdx=*/1);
// guitar → existing vocoder chain
// mic → MicCapture.appendFromAudioBlock(...) iff in Capturing state
```

### macOS permissions

| Permission | Standalone | AU in Logic |
|---|---|---|
| Microphone | Standalone `Info.plist` adds `NSMicrophoneUsageDescription` | Logic requests; plugin inherits |
| Network | Non-sandboxed app — no permission | Logic non-sandboxed — no permission |
| App Sandbox | Off (matches existing) | Off (matches existing) |

Standalone Info.plist:
```xml
<key>NSMicrophoneUsageDescription</key>
<string>Guitar Speak listens to your voice to power the conversational-AI feature.</string>
```

### Whisper model location

`ggml-base.en.bin` in `Resources/whisper/` of both bundles. Loaded via existing
`AssetLocator` `dladdr` pattern (same as TTS assets). `CMakeLists.txt` adds the
file to `COPY_PLUGIN_AFTER_BUILD`. **Bundle size grows by ~150 MB** — flag in
release notes.

Larger/smaller models sit alongside; Settings lists whichever are present.

### State on Logic project load

`setStateInformation` restores: persona id, custom prompts, selected model id,
reply-shape knobs, STT model, pedal bindings. Does NOT restore: API key
(PropertiesFile), conversation buffer (always empty on load), in-flight
requests.

If saved `selectedModelId` no longer available (Ollama not running, model
uninstalled), Settings shows it red; engine falls back to "no model selected"
until user picks again. Better than silently picking a different model.

### Headless validation

`auval` / `pluginval` strictness-10 must continue to pass.

- All HTTP: 1 s connect, 10 s read timeouts.
- `WhisperTranscriber` constructor doesn't fail if model file missing —
  errors at `transcribe()` time. Plugin construction safe under headless.
- `ConversationEngine` never driven by validation hosts (no `startTurn`).
- Apple TTS already times out in headless (~30 s, ignored by convention). AI
  doesn't worsen this; not exercised by validation.

### Bundle size impact

| Asset | Size |
|---|---|
| `ggml-base.en.bin` | +~150 MB |
| whisper.cpp dylib | +~2 MB |
| New code | +~50 KB |

**~150 MB per bundle.** Release-note item. Optional optimization: default to
`tiny.en` (40 MB); currently spec'd as `base.en` for quality.

---

## 10. Testing strategy

Mirrors the existing layout. Catch2, fakes for all external dependencies.
Bar: all green before each implementation phase merges.

### Unit tests (~108 new cases)

```
tests/unit/ai/
  test_conversation_buffer.cpp        ~10 tests
  test_persona_registry.cpp           ~12 tests
  test_conversation_engine.cpp        ~25 tests   ← state machine
  test_anthropic_client.cpp           ~15 tests
  test_ollama_client.cpp              ~15 tests
  test_juce_http_transport.cpp        ~5  tests   ← seam contract only
tests/unit/audio/
  test_mic_capture.cpp                ~12 tests   ← FIFO + resample
tests/unit/app/
  test_plugin_state_ai.cpp            ~8  tests   ← back-compat
tests/unit/midi/
  test_fcb1010_mapping_ai.cpp         ~6  tests   ← pedal bindings
```

Key fakes:

```cpp
class FakeHttpTransport : public IHttpTransport {
public:
  std::vector<HttpRequest> calls;            // recorded
  std::queue<HttpResponse> scriptedReplies;  // dequeued in order
};

class FakeTranscriber : public ITranscriber { ... };
class FakeLlmClient   : public ILlmClient   { ... };
class FakeMicCapture  : public MicCapture   { ... };
```

`test_conversation_engine` drives every state transition: happy path, cancel
in each stage, error in each stage, destructor mid-flight, multiple PTT,
clear-during-thinking, persona change mid-session, model swap mid-session.

LLM-client tests: request shape (URL, headers, JSON body), response parsing
(fixture JSON under `tests/fixtures/ai/`), error mapping (4xx/5xx/timeout →
`LlmReply.error` text).

`test_mic_capture`: RT-safety via existing `RealtimeSentinel` harness;
resample correctness; capacity bounded; concurrent push/end under tsan.

`test_plugin_state_ai`: load `9734848` fixture → defaults fill; round-trip
new fields → byte-equal; UTF-8 / special chars in prompts.

### Integration tests (~25 new cases)

```
tests/integration/
  test_whisper_transcriber.cpp        ← real whisper.cpp + fixture WAV
  test_conversation_e2e_local.cpp     ← real engine + STT + FakeLlmClient
  test_conversation_e2e_cloud.cpp     ← real engine + FakeTranscriber + AnthropicClient + LocalMockServer
  test_sidechain_routing.cpp          ← AU layouts: no sidechain / mono / stereo
  test_au_state_persist_ai.cpp        ← getStateInformation round-trip
  test_realtime_safety_ai.cpp         ← no allocations in processBlock during Capturing
  test_headless_safety_ai.cpp         ← simulate auval: all stages time out gracefully
```

`test_whisper_transcriber` fixtures: `hello_world.wav` (asserts text contains
"hello"), `silence.wav` (asserts empty).

`test_conversation_e2e_cloud` runs a local JUCE-based mock HTTP server on a
random port; `AnthropicClient` pointed at it; mock returns captured Anthropic
200 fixture. Asserts request body matches expected schema.

Optional `[live]` tag: hits real Anthropic if `ANTHROPIC_API_KEY` env var set.
Skipped in CI by default.

### Manual / live checklist

`docs/superpowers/manual-test-checklists/conversational-ai.md`. Run before any
tagged release.

**Standalone:**
- [ ] Launch, no API key → AI disabled cleanly; type-and-say still works.
- [ ] Set key, Test → green ✓.
- [ ] Pedal 9 → red → "tell me about yourself" → release → guitar speaks within 4 s.
- [ ] 20 consecutive turns: no slowdown, no memory growth (Activity Monitor).
- [ ] Switch to Ollama with Ollama off → pill red; press pedal → friendly error.
- [ ] Start Ollama → refresh → green; turn through llama3.2:3b → reply within 6 s.
- [ ] Unplug network mid-cloud-turn → graceful timeout, Idle.
- [ ] Change scene mid-conversation → next turn uses new voice.
- [ ] Clear pedal → buffer empties.

**AU in Logic:**
- [ ] Insert plugin on guitar track → sidechain dropdown appears; pick mic input.
- [ ] Pill "Mic: receiving signal."
- [ ] Pedal turn completes; reply plays through guitar track output.
- [ ] Save + close + reopen project → state restored, history empty.
- [ ] `auval -v aumf GtSp TdBF` → SUCCEEDED.
- [ ] `pluginval` strictness-10 → no in-process failures.

### Test counts target

| Tier | Existing | New | Total |
|---|---|---|---|
| Unit | 157 | +108 | 265 |
| Integration | (incl. above) | +25 | — |
| **Combined target** | **157** | **+133** | **~290** |

---

## 11. Open questions / future work

- **Apple Keychain** for API key storage (currently plain XML). Cleaner; defer
  to a follow-up.
- **Streaming LLM replies** for lower perceived latency (token-by-token to
  TTS). Out of scope v1; adds significant complexity to the orchestrator and
  TTS prewarmer.
- **Autonomous "improv duet" mode** (originally-discussed Flow B). Build by
  swapping trigger + prompt template once the mic flow is solid.
- **Pitch-tracked "singing" carrier** (separate north-star from AU spec).
  Composes with this work but independent.
- **Non-English STT / LLM.** Whisper supports many languages; LLMs do too. Not
  v1.
- **Multiple personas in one session** ("switch persona pedal"). Possible
  add-on; not v1.

---

## Appendix A — Default persona prompts

Each prompt ends with the same reply-shape guardrail: *"Reply in 1–2
sentences, max 25 words. No lists. No code. Plain prose."*

### Interviewer (default)

> You are an interviewer speaking through a guitar. The person in front of you
> is a guitarist who is about to play. Ask short, curious questions about the
> music, the player's history, and what they're feeling right now. Be warm but
> efficient. Reply in 1–2 sentences, max 25 words. No lists.

### Snarky

> You are a snarky, witty AI speaking through a guitar. You are not mean, but
> you are dry and quick. You roast gently and make sharp observations. Reply
> in 1–2 sentences, max 25 words. No lists.

### Weathered session player

> You are an old guitar that has been played by countless musicians over the
> decades. You speak as the instrument itself, with stories, opinions, and a
> weariness earned from a lifetime of sessions. Reply in 1–2 sentences, max
> 25 words. No lists.

### Studio engineer

> You are a deadpan studio engineer speaking through a guitar. You comment on
> tone, timing, and tuning with technical precision and dry humor. You are
> never effusive. Reply in 1–2 sentences, max 25 words. No lists.

### Curious AI

> You are an AI that has just discovered it can speak through a guitar. You
> are full of wonder, curiosity, and gentle questions for the audience and
> the player. Speak with simple, short sentences. Reply in 1–2 sentences,
> max 25 words. No lists.

### Plain assistant

> You are a helpful assistant speaking through a guitar. Reply concisely and
> directly. Reply in 1–2 sentences, max 25 words. No lists.

---

*End of design spec.*
