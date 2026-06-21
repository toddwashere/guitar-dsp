# Session Q&A Persona — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a ninth persona, `Session Q&A`, that lets the guitar answer audience questions about the app grounded in a hot-reloaded knowledge document, with a dedicated toggle button next to the Settings button for one-click activation.

**Architecture:** New `KnowledgeDoc` class reads `assets/personas/session_qa.md` on demand (mtime-checked, thread-safe). `PersonaRegistry` owns a `KnowledgeDoc*` and, when building messages for `PersonaId::SessionQa`, appends the doc contents to the system message under a `# Reference document` heading. `PluginEditor` gains a Q&A toggle button that swaps the active persona via `PluginProcessor::setCurrentPersona`, remembering the previous persona for one-click restore.

**Tech Stack:** C++20, JUCE (juce_core for files/threading), Catch2 for tests, CMake.

**Spec:** [docs/superpowers/specs/2026-06-21-session-qa-persona-design.md](../specs/2026-06-21-session-qa-persona-design.md)

## Global Constraints

- C++20 (matches the rest of the AI module).
- All file I/O happens off the audio thread — `KnowledgeDoc::contents()` is called only from the worker thread inside `PersonaRegistry::buildMessages`.
- `KnowledgeDoc::contents()` must be thread-safe (worker thread reads; UI may also read in tests).
- Reply length cap for `SessionQa`: "2-4 sentences, max about 60 words. No lists." (literal copy — used by tests).
- Off-doc fallback line wording (literal): "That's outside what I know about myself — ask Todd". The model is told to use *something like* this, so tests should not pin the exact wording, but the doc-missing diagnostic substitute IS literal: `REFERENCE DOCUMENT NOT LOADED.`
- Doc location at runtime: resolved via `AssetLocator::resolveForRead("personas/session_qa.md")` — prefers the source tree during dev so hot-reload sees edits without a rebuild; falls back to the bundle `Resources/assets/personas/session_qa.md` in installed builds.
- Asset path inside repo: `assets/personas/session_qa.md`. The existing `guitar_dsp_post_build_copy` does a full `assets/` copy, so no CMake change is needed for the .md itself.
- **Coordination with the in-flight `SongStore` / song-generator work** (uncommitted in `main`'s working tree as of plan-write time): that work also adds personas and edits `PersonaRegistry`, `AiSettingsPanel`, and `PluginProcessor`. Land that work first OR rebase this branch on top of it. New `SessionQa` entries go at the **end** of the `PersonaId` enum and at the **end** of `kPersonas[]` in `AiSettingsPanel.cpp` to minimize line-level conflicts.

---

### Task 1: Set up isolated worktree

**Files:** none (worktree setup)

**Interfaces:**
- Produces: a clean worktree at `.claude/worktrees/session-qa-persona` on branch `worktree-session-qa-persona`, branched from current `main`.

- [ ] **Step 1: Confirm main is in the expected state**

Run: `git -C /Users/user/GIT/guitar-dsp log --oneline -3`
Expected: tip is `0e07db3 docs(spec): Session Q&A persona — design` OR a newer commit that includes the song-generator / SongStore work from the parallel session. If it's `0e07db3`, optionally wait for the parallel session to land its work first, or proceed and resolve trivial enum/array conflicts during rebase.

- [ ] **Step 2: Create the worktree on a new branch**

Run:
```bash
cd /Users/user/GIT/guitar-dsp && \
git worktree add .claude/worktrees/session-qa-persona -b worktree-session-qa-persona main
```
Expected: `Preparing worktree (new branch 'worktree-session-qa-persona')` followed by `HEAD is now at <sha>`.

- [ ] **Step 3: Confirm and cd into the worktree**

Run:
```bash
cd /Users/user/GIT/guitar-dsp/.claude/worktrees/session-qa-persona && \
git rev-parse --abbrev-ref HEAD && pwd
```
Expected: `worktree-session-qa-persona` and the worktree path.

**All subsequent tasks run inside `/Users/user/GIT/guitar-dsp/.claude/worktrees/session-qa-persona`.** Verify with `pwd` before every git or cmake command — session resume can reset CWD to main.

---

### Task 2: `KnowledgeDoc` class — header + minimal impl + test

**Files:**
- Create: `src/ai/KnowledgeDoc.h`
- Create: `src/ai/KnowledgeDoc.cpp`
- Create: `tests/unit/ai/test_knowledge_doc.cpp`
- Modify: `src/CMakeLists.txt` (add `ai/KnowledgeDoc.cpp` to `guitar_dsp_ai` sources)
- Modify: `tests/CMakeLists.txt` (register `unit/ai/test_knowledge_doc.cpp`)

**Interfaces:**
- Produces:
  ```cpp
  namespace guitar_dsp::ai {
  class KnowledgeDoc {
   public:
      explicit KnowledgeDoc(juce::File path);
      std::string contents();  // re-reads if mtime changed; "" if missing
   private:
      juce::File          path_;
      juce::Time          lastMtime_;
      std::string         cached_;
      mutable std::mutex  m_;
  };
  } // namespace
  ```
- Consumes: `juce::File`, `juce::Time` (juce_core).

- [ ] **Step 1: Write the failing test file**

Create `tests/unit/ai/test_knowledge_doc.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ai/KnowledgeDoc.h"
#include <juce_core/juce_core.h>
#include <thread>
#include <chrono>

using guitar_dsp::ai::KnowledgeDoc;

namespace {
juce::File makeTempDocWith(const juce::String& body) {
    auto f = juce::File::createTempFile("knowledge_doc_test.md");
    f.replaceWithText(body);
    return f;
}
} // namespace

TEST_CASE("KnowledgeDoc: returns initial contents", "[ai][knowledge_doc]") {
    auto f = makeTempDocWith("hello world");
    KnowledgeDoc doc(f);
    REQUIRE(doc.contents() == "hello world");
    f.deleteFile();
}

TEST_CASE("KnowledgeDoc: empty string when file missing",
          "[ai][knowledge_doc]") {
    juce::File nope("/tmp/this-file-does-not-exist-xyz.md");
    if (nope.exists()) nope.deleteFile();
    KnowledgeDoc doc(nope);
    REQUIRE(doc.contents().empty());
}

TEST_CASE("KnowledgeDoc: re-reads after mtime change",
          "[ai][knowledge_doc]") {
    auto f = makeTempDocWith("first");
    KnowledgeDoc doc(f);
    REQUIRE(doc.contents() == "first");
    // Filesystem mtime resolution is ~1s on macOS HFS+/APFS in some cases —
    // sleep enough to guarantee a distinct mtime.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    f.replaceWithText("second");
    REQUIRE(doc.contents() == "second");
    f.deleteFile();
}

TEST_CASE("KnowledgeDoc: cached when mtime unchanged",
          "[ai][knowledge_doc]") {
    auto f = makeTempDocWith("cached body");
    KnowledgeDoc doc(f);
    REQUIRE(doc.contents() == "cached body");
    // Calling again with no file change must still return same body.
    REQUIRE(doc.contents() == "cached body");
    f.deleteFile();
}

TEST_CASE("KnowledgeDoc: safe under concurrent contents() calls",
          "[ai][knowledge_doc]") {
    auto f = makeTempDocWith("concurrent body");
    KnowledgeDoc doc(f);
    std::atomic<int> errors{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) {
        ts.emplace_back([&] {
            for (int j = 0; j < 100; ++j) {
                if (doc.contents() != "concurrent body") ++errors;
            }
        });
    }
    for (auto& t : ts) t.join();
    REQUIRE(errors.load() == 0);
    f.deleteFile();
}
```

- [ ] **Step 2: Add the test to `tests/CMakeLists.txt`**

Open `tests/CMakeLists.txt` and add `unit/ai/test_knowledge_doc.cpp` to the list of AI test sources (right after `unit/ai/test_persona_registry.cpp`, line ~62):

```cmake
    unit/ai/test_persona_registry.cpp
    unit/ai/test_knowledge_doc.cpp
```

- [ ] **Step 3: Run the test to verify it fails to compile**

Run: `cmake --build build --target unit_tests 2>&1 | tail -20`
Expected: compile error — `'ai/KnowledgeDoc.h' file not found` or similar.

- [ ] **Step 4: Write the header**

Create `src/ai/KnowledgeDoc.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>

#include <mutex>
#include <string>

namespace guitar_dsp::ai {

// Hot-reloading text file loader. contents() stats the file and re-reads
// when mtime has changed since the last read. Returns "" when the file
// is missing or unreadable. Safe to call from multiple threads.
class KnowledgeDoc {
public:
    explicit KnowledgeDoc(juce::File path);

    std::string contents();

private:
    juce::File         path_;
    juce::Time         lastMtime_;
    std::string        cached_;
    mutable std::mutex m_;
};

} // namespace guitar_dsp::ai
```

- [ ] **Step 5: Write the implementation**

Create `src/ai/KnowledgeDoc.cpp`:

```cpp
#include "ai/KnowledgeDoc.h"

namespace guitar_dsp::ai {

KnowledgeDoc::KnowledgeDoc(juce::File path)
    : path_(std::move(path)) {}

std::string KnowledgeDoc::contents() {
    std::lock_guard<std::mutex> lk(m_);

    if (!path_.existsAsFile()) {
        cached_.clear();
        lastMtime_ = juce::Time{};
        return {};
    }

    const auto mtime = path_.getLastModificationTime();
    if (mtime == lastMtime_ && !cached_.empty()) return cached_;

    cached_     = path_.loadFileAsString().toStdString();
    lastMtime_  = mtime;
    return cached_;
}

} // namespace guitar_dsp::ai
```

- [ ] **Step 6: Add the source to `src/CMakeLists.txt`**

In the `guitar_dsp_ai` library block (line ~70-79), add `ai/KnowledgeDoc.cpp` after `ai/PersonaRegistry.cpp`:

```cmake
add_library(guitar_dsp_ai STATIC
    ai/ConversationBuffer.cpp
    ai/PersonaRegistry.cpp
    ai/KnowledgeDoc.cpp
    ai/AppPreferences.cpp
    ...
```

- [ ] **Step 7: Build and run the tests**

Run: `cmake --build build --target unit_tests -j 2>&1 | tail -10 && ./build/tests/unit_tests "[knowledge_doc]"`
Expected: build succeeds; Catch2 reports `All tests passed (X assertions in 5 test cases)` for the `[knowledge_doc]` tag.

- [ ] **Step 8: Commit**

```bash
cd /Users/user/GIT/guitar-dsp/.claude/worktrees/session-qa-persona && \
git add src/ai/KnowledgeDoc.h src/ai/KnowledgeDoc.cpp \
        tests/unit/ai/test_knowledge_doc.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt && \
git commit -m "feat(ai): hot-reloading KnowledgeDoc text loader"
```

---

### Task 3: Seed the knowledge document

**Files:**
- Create: `assets/personas/session_qa.md`

**Interfaces:** none (asset only).

- [ ] **Step 1: Create the directory and file**

Create `assets/personas/session_qa.md` with this content (Todd will hand-tune between takes — this is a working first draft seeded from project memory + recent commits):

```markdown
# Session Q&A — Reference document for the guitar

These are the facts I know about myself when the audience asks how I was made.

## What this app is

I'm a live guitar plugin called Guitar Speak. I let the guitar speak and sing — words come out of the strings, pitched to whatever's being played. I run as a standalone macOS app and as an Audio Units (AU) plugin inside Logic Pro.

## The stack

- Language: C++20.
- Framework: JUCE (cross-platform audio plugin host) plus juce_dsp for the realtime signal chain.
- Speech-to-text: whisper.cpp running locally — the audience's questions never leave the room.
- LLM: Ollama running Llama locally for everything that ships on stage. Anthropic's cloud Claude is wired in as an alternate backend for development but the demo defaults to local.
- Text-to-speech: three sources, switchable per scene — Apple's AVSpeechSynthesizer (lowest latency on macOS), Piper (offline neural TTS, more natural voice), and prebaked clips (perfect timing for set pieces).
- Vocoder: a channel vocoder I wrote in C++. The TTS voice is the modulator, the guitar is the carrier — that's why the speech is pitched and follows the notes.
- AU plugin: shipped as "Guitar Speak" by "Todd B Fisher".

## Why these choices

- JUCE because one codebase ships both a standalone app and an AU plugin that hosts inside Logic.
- Local LLM because conference WiFi is a coin toss and audience questions shouldn't pass through anyone else's servers. Llama on a laptop is fast enough.
- A vocoder rather than pitch-shifted TTS because intelligible pitched speech needs the spectral envelope of the voice married to the harmonic content of the guitar. Pitch-shifting alone turns into chipmunks or mud.
- An FCB1010 MIDI foot controller for scene switching because both my hands are on the guitar.

## How the talking/singing effect works

The signal flow per turn: microphone in -> whisper.cpp transcribes -> Llama generates a reply -> TTS synthesizes the reply -> the synthesized voice modulates a vocoder whose carrier is the guitar input. Result: the guitar speaks the reply, pitched and phrased to whatever's being played.

For the singing scenes, the same chain runs with a pitch carrier pulled from the guitar's fundamental, plus a word-sync mode that steps through lyrics one syllable per note.

## Scenes and gestures

There are 11 scenes selected from the FCB1010 foot controller. They include a carousel of instrument patches (each scene patches the vocoder and modulators differently), note-triggered word-by-word speech (the core "talking guitar" effect — each picked note advances one word), mouth-guitar scenes inspired by Jack Black (clip-bank playback, mic talkbox, auto-vocal formant), and a bypass scene for raw guitar.

Scenes load from JSON files in `assets/scenes/`. Audio clips and per-clip phoneme timing live in `.gspeak` bundles — zip files containing the wav plus a metadata JSON.

## Challenges and lessons

- Formant-shifted vocoders sound chipmunky. The fix was inverse-Q gain compensation on the JUCE TPT bandpass filters in the formant bank.
- Word-sync latency: the LLM-to-TTS-to-vocoder chain adds enough lag that note-triggered speech feels off. Solution: prewarm TTS for the next word as soon as the previous one starts playing, and align word boundaries to onset detection on the guitar.
- Keeping the LLM in character without rambling: each persona is a tight system prompt with a hard word cap. The cap shapes the response more than the character description does.
- Hot-reloading scenes mid-set: scene JSONs are re-read from disk on every scene change. The dev build reads from the source tree first so edits land immediately.

## Credits and links

- Built by Todd Fisher.
- Repo and full design specs live in the project's `docs/superpowers/` directory.
- This persona was added so I could take audience questions at the end of a set without making things up.
```

- [ ] **Step 2: Commit**

```bash
git add assets/personas/session_qa.md && \
git commit -m "feat(assets): seed session_qa knowledge document"
```

---

### Task 4: Add `SessionQa` enum + default prompt (no doc injection yet)

**Files:**
- Modify: `src/ai/PersonaRegistry.h:10-19` — add `SessionQa` to the enum at the **end**
- Modify: `src/ai/PersonaRegistry.cpp` — add a `case SessionQa:` branch in `defaultPromptFor`
- Modify: `tests/unit/ai/test_persona_registry.cpp` — add a test for the new prompt

**Interfaces:**
- Produces: `PersonaId::SessionQa`, `PersonaRegistry::defaultPromptFor(SessionQa)` returns a non-empty string containing the literal substring `"60 words"`.
- Consumes: the existing `kVocalOnlyRule` in `PersonaRegistry.cpp`.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/ai/test_persona_registry.cpp` (after the existing tests, before the file end):

```cpp
TEST_CASE("PersonaRegistry: SessionQa default prompt has 60-word guardrail",
          "[ai][persona]") {
    auto p = PersonaRegistry::defaultPromptFor(PersonaId::SessionQa);
    REQUIRE_FALSE(p.empty());
    REQUIRE(p.find("60 words") != std::string::npos);
    REQUIRE(p.find("No lists") != std::string::npos);
    // The prompt instructs the model to answer ONLY from the reference doc.
    REQUIRE(p.find("ONLY") != std::string::npos);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target unit_tests -j 2>&1 | tail -20`
Expected: compile error on `PersonaId::SessionQa` (enumerator not declared).

- [ ] **Step 3: Add the enum value**

Edit `src/ai/PersonaRegistry.h:10-19` — append `SessionQa` at the end of the enum (after `SongRockingGuitar` if present in the working tree, or after `PlainAssistant` otherwise):

```cpp
enum class PersonaId {
    Interviewer = 0,
    Snarky,
    WeatheredGuitar,
    StudioEngineer,
    CuriousAi,
    PlainAssistant,
    SongOldGuitar,
    SongRockingGuitar,
    SessionQa,            // audience Q&A grounded in a knowledge doc
};
```

If `SongOldGuitar` / `SongRockingGuitar` aren't there (parallel work hasn't landed), append `SessionQa` after `PlainAssistant`.

- [ ] **Step 4: Add the case branch in `PersonaRegistry::defaultPromptFor`**

Edit `src/ai/PersonaRegistry.cpp` — add this case **before** the song personas (so it groups with the speaking personas that use `kVocalOnlyRule`):

```cpp
case PersonaId::SessionQa:
    return std::string(
           "You are the guitar itself, speaking to an audience after a live "
           "performance. They are asking questions about how this app was made: "
           "the tech, the choices, the challenges. Answer in first person as the "
           "instrument, warmly and plainly. "
           "Answer ONLY from the reference document below. If a question isn't "
           "covered, say something like \"That's outside what I know about "
           "myself — ask Todd\" and stop. Do not guess specifics about hardware, "
           "libraries, dates, or people that aren't in the document. "
           "Reply in 2-4 sentences, max about 60 words. No lists.")
           + kVocalOnlyRule;
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --target unit_tests -j 2>&1 | tail -10 && ./build/tests/unit_tests "[persona]"`
Expected: all `[persona]` tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/ai/PersonaRegistry.h src/ai/PersonaRegistry.cpp \
        tests/unit/ai/test_persona_registry.cpp && \
git commit -m "feat(ai): add SessionQa persona enum + default prompt"
```

---

### Task 5: Inject knowledge doc into `buildMessages` for `SessionQa`

**Files:**
- Modify: `src/ai/PersonaRegistry.h` — add a `KnowledgeDoc* sessionQaDoc_` member + a setter
- Modify: `src/ai/PersonaRegistry.cpp` — special-case `SessionQa` in `buildMessages`
- Modify: `tests/unit/ai/test_persona_registry.cpp` — three new tests

**Interfaces:**
- Produces:
  ```cpp
  // In PersonaRegistry:
  void setSessionQaDoc(KnowledgeDoc* doc);  // nullable; lifetime owned by caller
  ```
- Consumes: `KnowledgeDoc::contents()` from Task 2.

**Why a raw pointer + external ownership:** `PersonaRegistry` is constructed in `PluginProcessor` (and in tests) and lives there for the lifetime of the processor. The KnowledgeDoc also lives in `PluginProcessor`. Storing a raw pointer keeps `PersonaRegistry` constructible without a doc (tests for the 8 existing personas don't need one), and keeps lifetime explicit at the construction site.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/ai/test_persona_registry.cpp`:

```cpp
#include "ai/KnowledgeDoc.h"

using guitar_dsp::ai::KnowledgeDoc;

namespace {
juce::File writeTempPersonaDoc(const juce::String& body) {
    auto f = juce::File::createTempFile("session_qa_persona_test.md");
    f.replaceWithText(body);
    return f;
}
} // namespace

TEST_CASE("PersonaRegistry: SessionQa buildMessages injects doc body under heading",
          "[ai][persona][session_qa]") {
    auto f = writeTempPersonaDoc("My stack is JUCE and C++.");
    KnowledgeDoc doc(f);
    PersonaRegistry r;
    r.setSessionQaDoc(&doc);
    ConversationBuffer b;
    auto msgs = r.buildMessages(b, PersonaId::SessionQa);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].role == Message::Role::System);
    REQUIRE(msgs[0].text.find("# Reference document") != std::string::npos);
    REQUIRE(msgs[0].text.find("My stack is JUCE and C++.") != std::string::npos);
    // Persona prompt must precede the doc.
    REQUIRE(msgs[0].text.find("audience")
            < msgs[0].text.find("# Reference document"));
    f.deleteFile();
}

TEST_CASE("PersonaRegistry: SessionQa with no doc set substitutes diagnostic line",
          "[ai][persona][session_qa]") {
    PersonaRegistry r;  // no setSessionQaDoc
    ConversationBuffer b;
    auto msgs = r.buildMessages(b, PersonaId::SessionQa);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].text.find("REFERENCE DOCUMENT NOT LOADED")
            != std::string::npos);
}

TEST_CASE("PersonaRegistry: SessionQa with empty doc substitutes diagnostic line",
          "[ai][persona][session_qa]") {
    auto f = writeTempPersonaDoc("");
    KnowledgeDoc doc(f);
    PersonaRegistry r;
    r.setSessionQaDoc(&doc);
    ConversationBuffer b;
    auto msgs = r.buildMessages(b, PersonaId::SessionQa);
    REQUIRE(msgs[0].text.find("REFERENCE DOCUMENT NOT LOADED")
            != std::string::npos);
    f.deleteFile();
}

TEST_CASE("PersonaRegistry: non-SessionQa personas unaffected by doc setter",
          "[ai][persona][session_qa]") {
    auto f = writeTempPersonaDoc("Doc body");
    KnowledgeDoc doc(f);
    PersonaRegistry r;
    r.setSessionQaDoc(&doc);
    ConversationBuffer b;
    auto msgs = r.buildMessages(b, PersonaId::Interviewer);
    REQUIRE(msgs[0].text.find("Doc body") == std::string::npos);
    REQUIRE(msgs[0].text.find("# Reference document") == std::string::npos);
    f.deleteFile();
}
```

- [ ] **Step 2: Run the tests to verify they fail to compile**

Run: `cmake --build build --target unit_tests -j 2>&1 | tail -20`
Expected: `'setSessionQaDoc' is not a member of 'PersonaRegistry'`.

- [ ] **Step 3: Update the header**

Edit `src/ai/PersonaRegistry.h`. Add an include and the member + setter:

```cpp
#pragma once
#include "ai/ConversationBuffer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace guitar_dsp::ai {

class KnowledgeDoc;  // forward decl

enum class PersonaId {
    /* ... existing entries ... */
    SessionQa,
};

class PersonaRegistry {
public:
    static std::string defaultPromptFor(PersonaId);

    std::string promptFor(PersonaId) const;
    void        setCustomPrompt(PersonaId, std::string);
    void        resetToDefault(PersonaId);

    // Nullable; lifetime owned by the caller. Only used when persona is
    // SessionQa. When unset or contents() is empty, the reference-document
    // section of the system message becomes "REFERENCE DOCUMENT NOT LOADED."
    // so the on-stage answer is audibly diagnostic.
    void setSessionQaDoc(KnowledgeDoc* doc) noexcept { sessionQaDoc_ = doc; }

    std::vector<Message> buildMessages(const ConversationBuffer&,
                                       PersonaId) const;

private:
    std::unordered_map<int, std::string> overrides_;
    KnowledgeDoc*                        sessionQaDoc_ = nullptr;
};

} // namespace guitar_dsp::ai
```

- [ ] **Step 4: Update `buildMessages` in `PersonaRegistry.cpp`**

Add `#include "ai/KnowledgeDoc.h"` near the top. Replace the body of `buildMessages` with:

```cpp
std::vector<Message> PersonaRegistry::buildMessages(
    const ConversationBuffer& buf, PersonaId id) const {
    std::vector<Message> out;
    std::string system = promptFor(id);

    if (id == PersonaId::SessionQa) {
        std::string doc = sessionQaDoc_ ? sessionQaDoc_->contents()
                                        : std::string{};
        if (doc.empty()) doc = "REFERENCE DOCUMENT NOT LOADED.";
        system += "\n\n# Reference document (the source of truth about yourself)\n\n";
        system += doc;
    }

    out.push_back({Message::Role::System, std::move(system)});
    for (auto& m : buf.snapshot()) out.push_back(m);
    return out;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --target unit_tests -j 2>&1 | tail -10 && ./build/tests/unit_tests "[session_qa]"`
Expected: all `[session_qa]` tests pass; `[persona]` tag still all green.

- [ ] **Step 6: Commit**

```bash
git add src/ai/PersonaRegistry.h src/ai/PersonaRegistry.cpp \
        tests/unit/ai/test_persona_registry.cpp && \
git commit -m "feat(ai): inject knowledge doc into SessionQa system message"
```

---

### Task 6: Construct the KnowledgeDoc in `PluginProcessor` and wire it to the registry

**Files:**
- Modify: `src/app/PluginProcessor.h` — add `KnowledgeDoc sessionQaDoc_` member; include the header
- Modify: `src/app/PluginProcessor.cpp` — initialize the doc with the asset path, call `personaRegistry_.setSessionQaDoc(&sessionQaDoc_)`

**Interfaces:**
- Consumes: `KnowledgeDoc` (Task 2), `PersonaRegistry::setSessionQaDoc` (Task 5), `AssetLocator::resolveForRead` (existing).
- Produces: the registry is now doc-aware in the running app.

- [ ] **Step 1: Find the existing persona registry member in `PluginProcessor.h`**

Run: `grep -n "PersonaRegistry\|personaRegistry_\|personas_" src/app/PluginProcessor.h`
Expected: a line declaring `ai::PersonaRegistry personaRegistry_;` (or similar) somewhere in the private section.

- [ ] **Step 2: Add includes and member to `PluginProcessor.h`**

In the includes block (where `ai/PersonaRegistry.h` is included), add:

```cpp
#include "ai/KnowledgeDoc.h"
```

In the private members section, **after** the line declaring `personaRegistry_` (the order matters — registry's setter is called with `&sessionQaDoc_` in the constructor, so the doc must be constructed first when reading top-down; if registry is declared first, declare the doc above it OR initialize the registry's pointer after construction in the body):

```cpp
// Lives next to personaRegistry_; KnowledgeDoc is mtime-cached so the
// running app picks up edits to the source .md without a rebuild.
ai::KnowledgeDoc sessionQaDoc_ {
    juce::File(juce::String(AssetLocator::resolveForRead(
        "personas/session_qa.md")))
};
```

If the AssetLocator include isn't already present, add `#include "app/AssetLocator.h"`.

- [ ] **Step 3: Wire the doc into the registry in `PluginProcessor.cpp`**

In the `PluginProcessor` constructor body (after `personaRegistry_` is in a usable state, but before `engine_` is constructed — since the engine captures a reference to the registry), add:

```cpp
personaRegistry_.setSessionQaDoc(&sessionQaDoc_);
```

If the constructor already does engine setup in a single block, place this line immediately before that block.

- [ ] **Step 4: Build and run all tests**

Run: `cmake --build build -j 2>&1 | tail -10 && ./build/tests/unit_tests`
Expected: full build succeeds; all tests pass (existing tests unaffected; new `[session_qa]` and `[knowledge_doc]` tests pass).

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp && \
git commit -m "feat(app): wire SessionQa KnowledgeDoc into PluginProcessor"
```

---

### Task 7: Add `Session Q&A` to the persona dropdown

**Files:**
- Modify: `src/app/AiSettingsPanel.cpp:9-17` — append entry to `kPersonas[]`

**Interfaces:** none (UI-only change).

- [ ] **Step 1: Append the entry at the bottom of `kPersonas[]`**

Edit `src/app/AiSettingsPanel.cpp` — `kPersonas[]` already contains the eight existing personas. Append at the end:

```cpp
constexpr PersonaEntry kPersonas[] = {
    /* ... existing 8 entries ... */
    {ai::PersonaId::SessionQa,         "Session Q&A"},
};
```

(Bottom placement minimizes line-level overlap with the parallel session's edits to the same array.)

- [ ] **Step 2: Build**

Run: `cmake --build build -j 2>&1 | tail -10`
Expected: clean build.

- [ ] **Step 3: Run the AiSettingsPanel tests if any exist**

Run: `./build/tests/unit_tests "[ai_settings_panel]"`
Expected: tests pass (likely no specific assertion on the persona count, but verify nothing regresses).

- [ ] **Step 4: Commit**

```bash
git add src/app/AiSettingsPanel.cpp && \
git commit -m "feat(app): add Session Q&A to persona dropdown"
```

---

### Task 8: Add the Q&A toggle button to `PluginEditor`

**Files:**
- Modify: `src/app/PluginEditor.h` — declare `qaButton_`, `previousPersona_`
- Modify: `src/app/PluginEditor.cpp` — `addAndMakeVisible` + click handler + layout + timer sync

**Interfaces:**
- Consumes: `PluginProcessor::setCurrentPersona(PersonaId, std::string)` and `PluginProcessor::currentPersonaId()` (both already exist).

- [ ] **Step 1: Declare the button + previous-persona state in `PluginEditor.h`**

After the existing `juce::TextButton toggleAiSettingsBtn_ {"Settings"};` declaration (around line 62), add:

```cpp
juce::TextButton qaButton_ {"Q&A"};

// Persona to restore when the Q&A toggle is turned off. Captured at the
// moment the toggle is engaged; reflects whatever was active before.
ai::PersonaId    previousPersona_ {ai::PersonaId::Interviewer};
```

- [ ] **Step 2: Wire the button in the `PluginEditor` constructor**

In `src/app/PluginEditor.cpp`, after the `toggleAiSettingsBtn_` setup block (around line 65), add:

```cpp
addAndMakeVisible(qaButton_);
qaButton_.setClickingTogglesState(true);
qaButton_.onClick = [this] {
    const bool wantsQa = qaButton_.getToggleState();
    if (wantsQa) {
        previousPersona_ = processor_.currentPersonaId();
        processor_.setCurrentPersona(ai::PersonaId::SessionQa);
    } else {
        processor_.setCurrentPersona(previousPersona_);
    }
};
```

- [ ] **Step 3: Lay out the button next to the Settings button**

Find the `resized()` lines that position `toggleAiSettingsBtn_` (around lines 182 and 186). The `controlsRow.removeFromRight(100)` form puts a 100-px button on the right edge; the Q&A button goes just to its left.

Replace the `controlsRow.removeFromRight(100)` line:

```cpp
toggleAiSettingsBtn_.setBounds(controlsRow.removeFromRight(100));
controlsRow.removeFromRight(4);
qaButton_.setBounds(controlsRow.removeFromRight(80));
```

For the fallback `bounds.removeFromTop(24)` form (the no-controls-row layout near line 186), append immediately after:

```cpp
toggleAiSettingsBtn_.setBounds(bounds.removeFromTop(24));
qaButton_.setBounds(bounds.removeFromTop(24));
```

- [ ] **Step 4: Keep the toggle state in sync with externally-changed personas**

The editor already has `startTimer(100)` (around line 88) and a `timerCallback`. Find `timerCallback` (likely below the constructor) and add at the top of its body:

```cpp
const bool inQa = processor_.currentPersonaId() == ai::PersonaId::SessionQa;
if (qaButton_.getToggleState() != inQa) {
    qaButton_.setToggleState(inQa, juce::dontSendNotification);
}
```

If `ai/PersonaRegistry.h` isn't already included in `PluginEditor.cpp`, add `#include "ai/PersonaRegistry.h"` near the top.

- [ ] **Step 5: Build and run all tests**

Run: `cmake --build build -j 2>&1 | tail -10 && ./build/tests/unit_tests`
Expected: clean build; all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/app/PluginEditor.h src/app/PluginEditor.cpp && \
git commit -m "feat(app): Q&A toggle button on PluginEditor controls row"
```

---

### Task 9: Manual verification

**Files:** none (verification only)

**Interfaces:** none.

- [ ] **Step 1: Build the standalone app**

Run: `cmake --build build --target guitar_dsp_app_Standalone -j 2>&1 | tail -10`
Expected: clean build, `Standalone.app` produced.

- [ ] **Step 2: Launch standalone, confirm Ollama is up**

Run (in another terminal): `curl -s http://localhost:11434/api/tags >/dev/null && echo OK || echo START_OLLAMA`
Expected: `OK`. If `START_OLLAMA`, run `ollama serve` in a separate shell and re-check.

Launch the app: `open build/src/app/guitar_dsp_app_artefacts/Debug/Standalone/Guitar\ Speak.app` (path may vary by build config — look under `build/src/app/`).

- [ ] **Step 3: Verify the Q&A button is visible next to Settings**

Look at the top-right of the editor. Two buttons: `Q&A` and `Settings`. Click `Q&A` — it should light up (toggle state on).

- [ ] **Step 4: Verify the persona dropdown also reflects the change**

Click `Settings`. The persona dropdown should now show `Session Q&A` as selected. Close Settings.

- [ ] **Step 5: Ask a covered question**

Use the conversational pedal (or the keyboard shortcut equivalent) to ask: *"What's the stack?"*
Expected: the guitar speaks an answer mentioning JUCE, C++, Whisper, Llama, vocoder — drawn from the .md.

- [ ] **Step 6: Ask an uncovered question**

Ask: *"What's your favorite color?"*
Expected: the guitar speaks a deflection like *"That's outside what I know about myself — ask Todd"* (or a close paraphrase). It must NOT fabricate a color or a story.

- [ ] **Step 7: Verify hot-reload of the doc**

Without restarting the app:
1. Edit `assets/personas/session_qa.md` — add a fake fact: append a line like `## Easter egg\n\nThe app's secret codename is Glissando.` and save.
2. Ask: *"What's the secret codename?"*
Expected: the guitar speaks something referencing "Glissando" — proves the doc was re-read after the mtime change.

Revert the edit afterward (`git checkout -- assets/personas/session_qa.md`).

- [ ] **Step 8: Verify missing-doc diagnostic fallback**

1. Move the file aside: `mv assets/personas/session_qa.md /tmp/session_qa.md.bak`
2. Ask any question.
Expected: the guitar speaks something like *"I don't have my reference notes loaded — ask Todd"* — proves the `REFERENCE DOCUMENT NOT LOADED` substitute reaches the model and produces the deflect line.
3. Restore: `mv /tmp/session_qa.md.bak assets/personas/session_qa.md`.

- [ ] **Step 9: Toggle Q&A off and confirm previous persona restored**

Click the `Q&A` button again to turn it off. Open Settings — the dropdown should now show whatever persona was active before (likely `Interviewer` from the default).

- [ ] **Step 10: Final commit if any tuning landed**

If you tweaked the seed doc or the prompt during manual verification:

```bash
git add -p && git commit -m "chore(qa): post-verification tuning"
```

Otherwise no commit needed.

---

## Self-Review

**Spec coverage:**

| Spec section | Implemented by |
|---|---|
| §3.1 New persona enum value | Task 4 |
| §3.2 New `KnowledgeDoc` class | Task 2 |
| §3.3 `buildMessages` branch | Task 5 |
| §3.4 Persona character prompt + diagnostic fallback | Tasks 4, 5 |
| §3.5 Asset path resolution via `AssetLocator::resolveForRead` | Task 6 |
| §3.6 Dropdown entry | Task 7 |
| §3.6 Toggle button + `previousPersona_` + external-change sync | Task 8 |
| §4 Knowledge document seeded | Task 3 |
| §5 Data flow | Verified end-to-end in Task 9 |
| §6 Error handling (missing/empty doc) | Tests in Task 2, Task 5; manual in Task 9 Step 8 |
| §7 Testing (unit + manual) | Tasks 2, 4, 5, 9 |
| §9 Files touched | All addressed |

**Placeholder scan:** No "TBD" / "TODO" / "implement later" remaining. Asset-copy CMake change confirmed unneeded (existing whole-tree `copy_directory` picks up the new file).

**Type consistency:** `setSessionQaDoc(KnowledgeDoc*)` signature consistent across Task 5 (declared) and Task 6 (called). `KnowledgeDoc::contents()` returns `std::string`, consumed as `std::string` in `buildMessages`. `PersonaId::SessionQa` enum value referenced identically in Tasks 4, 5, 7, 8.

**Risk re-check:** Tasks 4 and 7 are the conflict-prone ones with the parallel session. Both place new entries at the **end** of the enum / array to minimize line-level overlap. If the parallel work has not landed by the time this plan executes, the rebase against the eventually-landed branch will be a trivial three-way merge.
