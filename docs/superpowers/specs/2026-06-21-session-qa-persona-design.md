# Session Q&A Persona — Design Spec

**Status:** Approved design (brainstorming) — 2026-06-21
**Predecessor:** [2026-06-12-conversational-ai-design.md](2026-06-12-conversational-ai-design.md)
**Author:** Todd Fisher (with Claude)

**Goal:** Add a ninth persona, **Session Q&A**, where the guitar answers
audience questions about how this app was made — stack, design choices,
challenges, credits. Answers stay grounded in a hand-authored knowledge
document that is injected into every LLM call, so the local Llama model
cannot hallucinate specifics it doesn't know. The persona is reachable
both from the existing persona dropdown and from a new dedicated
**Q&A** toggle button on the same row as the Settings button, for
one-click activation between a performance and the audience-handoff
moment.

---

## 1. Background

Personas live as an enum + system prompt in
[`PersonaRegistry`](../../../src/ai/PersonaRegistry.h). The
`ConversationEngine` calls `buildMessages(buf, personaId)` which
prepends the persona's system prompt to the conversation history and
hands the result to the active `ILlmClient` — today that's
`OllamaClient` against a local Llama model or `AnthropicClient` for
cloud. There are currently 8 personas, selected via a dropdown in
[`AiSettingsPanel`](../../../src/app/AiSettingsPanel.cpp).

What's missing is an **audience-facing Q&A mode** for the end of a
talk. The other personas are *performance* personas (Interviewer,
Snarky, WeatheredGuitar, etc.) — they sound great but invent freely.
For Q&A we need accuracy: the LLM must answer from a curated source
of truth, and admit ignorance when something is outside that source.

---

## 2. Design decisions

These are settled (asked and answered during brainstorming):

| Decision | Choice |
|---|---|
| Voice | Guitar in first person, consistent with other personas |
| Knowledge doc scope | Tight FAQ + targeted project doc, optimized for Q&A (~3–7 KB) |
| Reply length | 2–4 sentences, ~50–60 words (medium; longer than the 25-word personas, still snappy) |
| Off-doc fallback | Admit honestly, in character — never guess specifics |
| Doc location | External file at `assets/personas/session_qa.md`, hot-reloaded on every LLM turn |
| UI entry point | Persona dropdown **plus** a dedicated "Q&A" toggle button on the Settings-button row |

---

## 3. Architecture

Three touch points, no changes to the audio thread, no changes to the
`ConversationEngine` pipeline.

### 3.1 New persona enum value

Add `PersonaId::SessionQa` to [`PersonaRegistry.h`](../../../src/ai/PersonaRegistry.h)
and a `defaultPromptFor(SessionQa)` case to
[`PersonaRegistry.cpp`](../../../src/ai/PersonaRegistry.cpp). The
prompt is the **persona character prompt only** — the knowledge doc
is appended at message-build time (see 3.3), not stored in the prompt
string, so editing the .md doesn't require a code change.

### 3.2 New `KnowledgeDoc` class

Files: `src/ai/KnowledgeDoc.h`, `src/ai/KnowledgeDoc.cpp`.

One responsibility: given a file path, return current contents. Tracks
the file's mtime so re-reads on subsequent calls are no-ops when the
file is unchanged. Hot-reload cost is ~1 ms when stat'ing — negligible
next to STT/LLM/TTS stages.

```cpp
class KnowledgeDoc {
public:
    explicit KnowledgeDoc(juce::File path);
    // Returns current file contents; re-reads from disk if mtime changed.
    // Returns empty string if file missing/unreadable.
    std::string contents();
private:
    juce::File           path_;
    juce::Time           lastMtime_;
    std::string          cached_;
    mutable std::mutex   m_;  // contents() may be called from worker thread
};
```

Owned by `PersonaRegistry` as a `std::unique_ptr<KnowledgeDoc>
sessionQaDoc_`, constructed with the resolved asset path (see 3.5).

### 3.3 `PersonaRegistry::buildMessages` branches for `SessionQa`

Today `buildMessages` pushes one system message (the persona prompt)
then appends conversation history. For `SessionQa`, the system message
becomes:

```
{persona character prompt}
{kVocalOnlyRule}

# Reference document (the source of truth about yourself)

{KnowledgeDoc::contents()}
```

If the doc is empty (missing file, unreadable), the system prompt
substitutes a one-line note that triggers the diagnostic answer (see
3.4 fallback). Conversation history is appended unchanged.

### 3.4 Persona character prompt

```
You are the guitar itself, speaking to an audience after a live
performance. They are asking questions about how this app was made:
the tech, the choices, the challenges. Answer in first person as the
instrument, warmly and plainly.

Answer ONLY from the reference document below. If a question isn't
covered, say something like "That's outside what I know about myself
— ask Todd" and stop. Do not guess specifics about hardware,
libraries, dates, or people that aren't in the document.

Reply in 2-4 sentences, max about 60 words. No lists.
```

Then `kVocalOnlyRule` is appended (the same vocal-only rule already
used by the speaking personas — no asterisks, no stage directions,
plain spoken English).

**Diagnostic fallback when the doc is missing.** If `KnowledgeDoc::contents()`
returns empty, the system prompt swaps the reference-document body for
the single line "REFERENCE DOCUMENT NOT LOADED." The character prompt
already instructs the model to deflect when content is outside the
doc, so the guitar will audibly say something like "I don't have my
reference notes loaded — ask Todd." This makes asset-pathing bugs
diagnosable on stage rather than silently producing hallucinated
answers.

### 3.5 Asset path resolution

Use the same asset-locator strategy other personas/scenes use (resolve
relative to the binary on macOS, then fall back to a repo-relative
path for dev). The shipped file lives at `assets/personas/session_qa.md`
in the repo; CMake copies it next to the AU bundle / standalone binary
during install (mirroring how scene JSONs and clip bundles are
deployed today).

### 3.6 UI: dropdown + dedicated toggle button

**Dropdown (canonical control).** Add `{ai::PersonaId::SessionQa,
"Session Q&A"}` to the `kPersonas[]` table in
[`AiSettingsPanel.cpp`](../../../src/app/AiSettingsPanel.cpp). No
other dropdown changes.

**Toggle button (hot-path shortcut).** A new `juce::TextButton qaButton_ {"Q&A"}`
in [`PluginEditor`](../../../src/app/PluginEditor.h), laid out on the
same row as the existing `toggleAiSettingsBtn_`. Behavior:

- **Press when not in Q&A:** record current persona in
  `previousPersona_`, set persona to `SessionQa`, light the button.
- **Press when in Q&A:** restore `previousPersona_`, unlight the
  button.
- **Persona changed by other means (dropdown):** if persona drifts off
  `SessionQa`, the button unlights automatically (subscribes to the
  same persona-changed signal the dropdown uses).

State lives on the editor / panel that owns the button — not in
`PersonaRegistry` (which stays stateless about UI). One source of
truth for the active persona remains the conversation engine.

---

## 4. The knowledge document

`assets/personas/session_qa.md`, ~3–7 KB. Headings are scannable by
the LLM at retrieval time. Initial sections:

- **What this app is** — one-paragraph elevator pitch.
- **The stack** — JUCE/C++, Whisper for STT, Ollama+Llama for the LLM,
  three TTS sources and why, AUv2 packaged as "Guitar Speak".
- **Why these choices** — JUCE (cross-platform plugin host), local LLM
  (no internet on stage, no data leaving the room), vocoder
  (intelligible pitched speech that follows the guitar), FCB1010
  (hands-free scene switching).
- **How the talking/singing effect works** — signal flow in one
  paragraph: mic → Whisper → LLM → TTS → vocoder modulated by the
  guitar.
- **Scenes & gestures** — 11 scenes, FCB1010 mapping, the carousel of
  instrument patches, note-triggered word-by-word speech, mouth-guitar
  (Jack Black) scenes, .gspeak clip bundles.
- **Challenges & lessons** — formant-shifted vocoders sounding
  chipmunky, word-sync latency, keeping the LLM in character without
  rambling, hot-reloading scenes mid-set.
- **Credits & links** — author, repo, talk venue, contact.

Format rules baked into the file (not the prompt): short paragraphs,
sentence-case headings, no code blocks (the LLM is reading prose to
generate prose).

A working first draft will be seeded during implementation from
existing memory + recent commits; Todd edits between takes.

---

## 5. Data flow (per turn, Q&A persona active)

1. User presses pedal / button → `ConversationEngine::startTurn()`.
2. Mic captures audio → Whisper transcribes → `ConversationBuffer`
   gets the user turn.
3. `ConversationEngine::runEndTurn()` calls
   `PersonaRegistry::buildMessages(buf, SessionQa)`.
4. `buildMessages` stats `session_qa.md`, re-reads if changed, builds
   the system message (`character prompt` + `kVocalOnlyRule` +
   `# Reference document` + `doc contents`), appends history.
5. Active `OllamaClient` (Llama) generates a reply.
6. Reply flows through the existing TTS → vocoder chain. No changes
   needed downstream.

---

## 6. Error handling

| Condition | Behavior |
|---|---|
| `session_qa.md` missing or unreadable | Prompt substitutes "REFERENCE DOCUMENT NOT LOADED." → guitar audibly says it doesn't have its notes. No exception thrown. |
| Doc is empty (zero bytes) | Same as missing. |
| Doc is huge (>50 KB) | Read fully — no truncation. Operator's responsibility to keep it focused; size impacts LLM latency, not correctness. |
| File path stat fails between calls | Fall back to last cached contents; if no cache exists, behave as missing. |
| Persona swapped mid-stream | Existing `ConversationEngine` persona-swap semantics apply (next turn picks up new persona). Q&A button reflects current persona. |

No new audio-thread code; all file I/O is on the worker thread inside
`buildMessages` (called from `runEndTurn`).

---

## 7. Testing

- **Unit:** `KnowledgeDoc` returns initial contents; returns updated
  contents after mtime change; returns empty string on missing file;
  returns empty string on unreadable file; is safe to call from
  multiple threads.
- **Unit:** `PersonaRegistry::buildMessages(buf, SessionQa)` produces
  a system message containing the persona prompt **and** the doc
  contents under a `# Reference document` heading; substitutes the
  diagnostic line when the doc is empty.
- **Integration:** with a stub `ILlmClient` that echoes its system
  message, verify the full pipeline (engine → registry → client)
  delivers the doc.
- **Manual:** rebuild, launch standalone, select "Session Q&A",
  ask a covered question ("what's the stack?") — expect a doc-grounded
  answer. Ask an uncovered question ("what's your favorite color?") —
  expect the deflect line. Edit the .md, ask again — expect the new
  content reflected without restart.

---

## 8. Out of scope

- **RAG / embeddings.** The doc fits comfortably in a single prompt;
  no retrieval layer needed. Revisit only if the doc grows past 20 KB
  and starts crowding the local model's context window.
- **Multiple knowledge docs / persona-doc binding.** Only `SessionQa`
  loads a doc. Other personas remain unchanged.
- **Editing the .md from inside the app.** Operator edits it in their
  normal text editor between takes. Hot-reload handles the rest.
- **Cloud-LLM behavior parity.** The persona works against
  `AnthropicClient` too (same `buildMessages` path), but the goal is
  local Llama. No cloud-specific tuning.
- **FCB1010 binding for the Q&A button.** Possible follow-up; not
  required for this feature.

---

## 9. Files touched

New:
- `src/ai/KnowledgeDoc.h`
- `src/ai/KnowledgeDoc.cpp`
- `assets/personas/session_qa.md`
- `tests/unit/ai/test_knowledge_doc.cpp`
- `tests/unit/ai/test_session_qa_persona.cpp`

Modified:
- `src/ai/PersonaRegistry.h` — new enum value, `KnowledgeDoc` member.
- `src/ai/PersonaRegistry.cpp` — new prompt, `buildMessages` branch.
- `src/app/AiSettingsPanel.cpp` — dropdown entry.
- `src/app/PluginEditor.h` / `src/app/PluginEditor.cpp` — new
  `qaButton_`, `previousPersona_` member, layout next to
  `toggleAiSettingsBtn_`, click handler that toggles the persona via
  `ConversationEngine::setPersona`.
- `CMakeLists.txt` / asset-copy rules — include the new .md in the
  install layout.
- `tests/CMakeLists.txt` — register new unit tests.
