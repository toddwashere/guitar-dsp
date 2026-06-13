# Phase 6 — ConversationEngine (orchestrator + state machine)

> Continuation of [2026-06-12-conversational-ai.md](2026-06-12-conversational-ai.md).

**Goal:** State machine that sequences mic capture → STT → LLM → existing TTS path on a single background thread, with cancellation and timing capture.

---

### Task 6.1: `ConversationEngine` happy path

**Files:**
- Create: `src/ai/ConversationEngine.h`
- Create: `src/ai/ConversationEngine.cpp`
- Create: `tests/unit/ai/test_conversation_engine.cpp`
- Create: `tests/unit/ai/FakeTranscriber.h` (test helper)
- Create: `tests/unit/ai/FakeLlmClient.h`   (test helper)
- Create: `tests/unit/ai/FakeMicCapture.h`  (test helper)

- [ ] **Step 1: Add `MicCapture` adapter interface so the engine can be tested without a real `MicCapture`**

Refactor: in Phase 5, `MicCapture` was a concrete class. The engine needs an injectable interface so the fake can be substituted.

`src/audio/IMicCapture.h`:
```cpp
#pragma once
#include <vector>
namespace guitar_dsp::audio {
class IMicCapture {
public:
    virtual ~IMicCapture() = default;
    virtual void beginCapture() = 0;
    virtual std::vector<float> endCapture() = 0;
    virtual bool  isCapturing() const noexcept = 0;
    virtual float currentPeakDbfs() const noexcept = 0;
    virtual bool  lastResultWasTooShort()  const noexcept = 0;
    virtual bool  lastResultWasTruncated() const noexcept = 0;
};
} // namespace
```

Update `MicCapture.h` to inherit `IMicCapture` and mark its methods `override`. No behavior change.

- [ ] **Step 2: Test helpers**

`tests/unit/ai/FakeTranscriber.h`:
```cpp
#pragma once
#include "ai/ITranscriber.h"
namespace guitar_dsp::ai::test {
class FakeTranscriber : public ITranscriber {
public:
    std::string scriptedText {"hello there"};
    std::string scriptedError;
    std::chrono::milliseconds delay {0};
    int callCount {0};

    TranscriptionResult transcribe(const std::vector<float>&, CancellationToken*) override {
        ++callCount;
        if (delay.count() > 0) std::this_thread::sleep_for(delay);
        return {scriptedText, "en", delay, scriptedError};
    }
    std::string modelName() const override { return "fake"; }
};
} // namespace
```

`tests/unit/ai/FakeLlmClient.h`:
```cpp
#pragma once
#include "ai/ILlmClient.h"
namespace guitar_dsp::ai::test {
class FakeLlmClient : public ILlmClient {
public:
    std::string scriptedText {"I'm a fake."};
    std::string scriptedError;
    std::chrono::milliseconds delay {0};
    LlmRequest  lastRequest;
    int         callCount {0};

    LlmReply generate(const LlmRequest& r, CancellationToken*) override {
        ++callCount; lastRequest = r;
        if (delay.count() > 0) std::this_thread::sleep_for(delay);
        return {scriptedText, scriptedError, 0, 0};
    }
    std::string displayName() const override { return "Fake LLM"; }
    std::string statusText()  const override { return "Fake: ready"; }
    std::string modelId()     const override { return "fake"; }
};
} // namespace
```

`tests/unit/ai/FakeMicCapture.h`:
```cpp
#pragma once
#include "audio/IMicCapture.h"
namespace guitar_dsp::ai::test {
class FakeMicCapture : public guitar_dsp::audio::IMicCapture {
public:
    std::vector<float> scriptedSamples {std::vector<float>(16000, 0.5f)};
    bool tooShort  {false}, truncated {false};
    bool capturing {false};

    void beginCapture()                       override { capturing = true; }
    std::vector<float> endCapture()           override { capturing = false; return scriptedSamples; }
    bool  isCapturing()              const noexcept override { return capturing; }
    float currentPeakDbfs()          const noexcept override { return -20.0f; }
    bool  lastResultWasTooShort()    const noexcept override { return tooShort; }
    bool  lastResultWasTruncated()   const noexcept override { return truncated; }
};
} // namespace
```

- [ ] **Step 3: Engine happy-path tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ai/ConversationEngine.h"
#include "ai/PersonaRegistry.h"
#include "ai/ConversationBuffer.h"
#include "FakeTranscriber.h"
#include "FakeLlmClient.h"
#include "FakeMicCapture.h"

using namespace guitar_dsp::ai;
using namespace guitar_dsp::ai::test;

namespace {
struct Harness {
    FakeTranscriber  stt;
    FakeLlmClient    llm;
    FakeMicCapture   mic;
    ConversationBuffer buf;
    PersonaRegistry  personas;
    std::vector<std::string> spokenTexts;
    ConversationEngine engine;
    Harness()
      : engine(stt, llm, mic, buf, personas,
               [this](std::string s){ spokenTexts.push_back(std::move(s)); }) {}

    void waitForState(ConversationEngine::State target, int ms = 2000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (engine.state() != target) {
            if (std::chrono::steady_clock::now() > deadline) FAIL("timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};
}

TEST_CASE("Engine: happy path Idle→Capturing→Transcribing→Thinking→Speaking→Idle",
          "[ai][engine]") {
    Harness h;
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Speaking);
    h.engine.onTtsPlaybackFinished();
    h.waitForState(ConversationEngine::State::Idle);

    auto snap = h.buf.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].text == "hello there");
    REQUIRE(snap[1].text == "I'm a fake.");
    REQUIRE(h.spokenTexts.size() == 1);
    REQUIRE(h.spokenTexts[0] == "I'm a fake.");
}

TEST_CASE("Engine: startTurn ignored when not Idle",
          "[ai][engine]") {
    Harness h;
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.startTurn();                  // second press from Capturing → endTurn
    h.waitForState(ConversationEngine::State::Speaking);
}

TEST_CASE("Engine: clearConversation empties buffer",
          "[ai][engine]") {
    Harness h;
    h.buf.append(Message::Role::User, "old");
    h.engine.clearConversation();
    REQUIRE(h.buf.snapshot().empty());
}
```

- [ ] **Step 4: Implement**

`src/ai/ConversationEngine.h`:
```cpp
#pragma once
#include "ai/ConversationBuffer.h"
#include "ai/PersonaRegistry.h"
#include "ai/ITranscriber.h"
#include "ai/ILlmClient.h"
#include "ai/CancellationToken.h"
#include "audio/IMicCapture.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>
#include <queue>

namespace guitar_dsp::ai {

struct StageTimings { std::chrono::milliseconds stt{}, llm{}, tts{}; };

class ConversationEngine {
public:
    enum class State { Idle, Capturing, Transcribing, Thinking, Speaking, Error };

    ConversationEngine(ITranscriber&, ILlmClient&, audio::IMicCapture&,
                       ConversationBuffer&, PersonaRegistry&,
                       std::function<void(std::string)> sayCallback);
    ~ConversationEngine();

    void startTurn();
    void endTurn();
    void cancelTurn();
    void clearConversation();
    void onTtsPlaybackFinished();

    State        state() const noexcept { return state_.load(); }
    std::string  lastError() const;
    StageTimings lastTimings() const;

    void setLlmClient(ILlmClient&);
    void setPersona(PersonaId, std::string customPrompt);

private:
    enum class Job { StartTurn, EndTurn, Cancel, Clear, TtsFinished };
    void enqueue(Job);
    void workerLoop();
    void runEndTurn();

    ITranscriber*       stt_;
    ILlmClient*         llm_;
    audio::IMicCapture* mic_;
    ConversationBuffer* buf_;
    PersonaRegistry*    personas_;
    std::function<void(std::string)> say_;
    PersonaId           personaId_ {PersonaId::Interviewer};

    std::atomic<State>  state_ {State::Idle};
    mutable std::mutex  errorMutex_;
    std::string         lastError_;
    StageTimings        lastTimings_;
    CancellationToken   cancel_;

    std::thread             worker_;
    std::mutex              qMutex_;
    std::condition_variable qCv_;
    std::queue<Job>         queue_;
    std::atomic<bool>       running_ {true};
};

} // namespace
```

`src/ai/ConversationEngine.cpp`:
```cpp
#include "ai/ConversationEngine.h"

namespace guitar_dsp::ai {

ConversationEngine::ConversationEngine(ITranscriber& t, ILlmClient& l,
                                       audio::IMicCapture& m,
                                       ConversationBuffer& b, PersonaRegistry& p,
                                       std::function<void(std::string)> say)
    : stt_(&t), llm_(&l), mic_(&m), buf_(&b), personas_(&p), say_(std::move(say)) {
    worker_ = std::thread(&ConversationEngine::workerLoop, this);
}

ConversationEngine::~ConversationEngine() {
    running_.store(false);
    cancel_.cancel();
    qCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ConversationEngine::enqueue(Job j) {
    { std::lock_guard lk(qMutex_); queue_.push(j); }
    qCv_.notify_one();
}

void ConversationEngine::startTurn() {
    auto s = state_.load();
    if (s == State::Idle)            enqueue(Job::StartTurn);
    else if (s == State::Capturing)  enqueue(Job::EndTurn);
    // else: ignored
}
void ConversationEngine::endTurn()           { enqueue(Job::EndTurn); }
void ConversationEngine::cancelTurn()        { cancel_.cancel(); enqueue(Job::Cancel); }
void ConversationEngine::clearConversation() { enqueue(Job::Clear); }
void ConversationEngine::onTtsPlaybackFinished() { enqueue(Job::TtsFinished); }

std::string ConversationEngine::lastError() const {
    std::lock_guard lk(errorMutex_); return lastError_;
}
StageTimings ConversationEngine::lastTimings() const {
    std::lock_guard lk(errorMutex_); return lastTimings_;
}

void ConversationEngine::setLlmClient(ILlmClient& c) {
    if (state_.load() != State::Idle) return;
    llm_ = &c;
}
void ConversationEngine::setPersona(PersonaId id, std::string custom) {
    personaId_ = id;
    if (! custom.empty()) personas_->setCustomPrompt(id, std::move(custom));
}

void ConversationEngine::workerLoop() {
    while (running_.load()) {
        Job j;
        {
            std::unique_lock lk(qMutex_);
            qCv_.wait(lk, [this]{ return ! queue_.empty() || ! running_.load(); });
            if (! running_.load()) break;
            j = queue_.front(); queue_.pop();
        }
        switch (j) {
            case Job::StartTurn:
                if (state_.load() == State::Idle) {
                    cancel_.reset();
                    mic_->beginCapture();
                    state_.store(State::Capturing);
                }
                break;
            case Job::EndTurn:
                if (state_.load() == State::Capturing) runEndTurn();
                break;
            case Job::Cancel:
                if (mic_->isCapturing()) (void)mic_->endCapture();
                state_.store(State::Idle);
                break;
            case Job::Clear:
                if (mic_->isCapturing()) (void)mic_->endCapture();
                cancel_.cancel();
                buf_->clear();
                state_.store(State::Idle);
                break;
            case Job::TtsFinished:
                if (state_.load() == State::Speaking) state_.store(State::Idle);
                break;
        }
    }
}

void ConversationEngine::runEndTurn() {
    using clock = std::chrono::steady_clock;
    auto samples = mic_->endCapture();
    if (mic_->lastResultWasTooShort()) {
        { std::lock_guard lk(errorMutex_); lastError_ = "didn't hear anything"; }
        state_.store(State::Idle); return;
    }

    state_.store(State::Transcribing);
    auto t0 = clock::now();
    auto stt = stt_->transcribe(samples, &cancel_);
    auto t1 = clock::now();
    if (cancel_.isCancelled()) { state_.store(State::Idle); return; }
    if (! stt.error.empty() || stt.text.empty()) {
        { std::lock_guard lk(errorMutex_); lastError_ = stt.error.empty() ? "couldn't transcribe" : stt.error; }
        state_.store(State::Idle); return;
    }
    buf_->append(Message::Role::User, stt.text);

    state_.store(State::Thinking);
    LlmRequest req;
    req.messages = personas_->buildMessages(*buf_, personaId_);
    auto reply = llm_->generate(req, &cancel_);
    auto t2 = clock::now();
    if (cancel_.isCancelled()) { state_.store(State::Idle); return; }
    if (! reply.error.empty()) {
        { std::lock_guard lk(errorMutex_); lastError_ = reply.error; }
        state_.store(State::Error); return;
    }
    buf_->append(Message::Role::Assistant, reply.text);

    state_.store(State::Speaking);
    auto t3 = clock::now();
    say_(reply.text);

    {
        std::lock_guard lk(errorMutex_);
        lastTimings_.stt = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);
        lastTimings_.llm = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
        lastTimings_.tts = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2);
        lastError_.clear();
    }
}

} // namespace
```

- [ ] **Step 5: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[ai][engine]"
git add src/ai/ConversationEngine.{h,cpp} src/audio/IMicCapture.h \
        src/audio/MicCapture.h tests/unit/ai/test_conversation_engine.cpp \
        tests/unit/ai/FakeTranscriber.h tests/unit/ai/FakeLlmClient.h \
        tests/unit/ai/FakeMicCapture.h \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ai): ConversationEngine state machine + happy-path tests"
```

---

### Task 6.2: Engine cancellation + error paths

**Files:**
- Modify: `tests/unit/ai/test_conversation_engine.cpp` (more tests)

- [ ] **Step 1: Add tests**

```cpp
TEST_CASE("Engine: cancel during Thinking returns to Idle, no assistant message",
          "[ai][engine][cancel]") {
    Harness h;
    h.llm.delay = std::chrono::milliseconds{1000};
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Thinking);
    h.engine.cancelTurn();
    h.waitForState(ConversationEngine::State::Idle);
    auto s = h.buf.snapshot();
    REQUIRE(s.size() == 1);
    REQUIRE(s[0].role == Message::Role::User);
}

TEST_CASE("Engine: LLM error transitions to Error state with reason",
          "[ai][engine][error]") {
    Harness h;
    h.llm.scriptedError = "API key invalid";
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Error);
    REQUIRE(h.engine.lastError() == "API key invalid");
}

TEST_CASE("Engine: STT empty-text error returns Idle with 'couldn't transcribe'",
          "[ai][engine][error]") {
    Harness h;
    h.stt.scriptedText = "";
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Idle);
    REQUIRE(h.engine.lastError().find("transcribe") != std::string::npos);
}

TEST_CASE("Engine: too-short mic capture surfaces friendly error",
          "[ai][engine][error]") {
    Harness h;
    h.mic.tooShort = true;
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Idle);
    REQUIRE(h.engine.lastError().find("didn't hear") != std::string::npos);
}

TEST_CASE("Engine: destructor mid-turn joins cleanly",
          "[ai][engine][cancel]") {
    auto stt = std::make_unique<FakeTranscriber>();
    auto llm = std::make_unique<FakeLlmClient>();
    llm->delay = std::chrono::seconds{5};
    auto mic = std::make_unique<FakeMicCapture>();
    ConversationBuffer buf;
    PersonaRegistry    personas;
    std::vector<std::string> spoken;
    {
        ConversationEngine engine(*stt, *llm, *mic, buf, personas,
            [&](std::string s){ spoken.push_back(std::move(s)); });
        engine.startTurn();
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        engine.endTurn();
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }   // destructor: must not hang
    SUCCEED();
}

TEST_CASE("Engine: setLlmClient ignored when not Idle",
          "[ai][engine]") {
    Harness h;
    FakeLlmClient other;
    h.llm.delay = std::chrono::milliseconds{500};
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Thinking);
    h.engine.setLlmClient(other);
    h.waitForState(ConversationEngine::State::Speaking);
    REQUIRE(h.llm.callCount == 1);    // original client used
    REQUIRE(other.callCount == 0);
}
```

- [ ] **Step 2: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[ai][engine]"
git add tests/unit/ai/test_conversation_engine.cpp
git commit -m "test(ai): cancel + error-path coverage for ConversationEngine"
```

---

## Phase 6 checkpoint — green.
