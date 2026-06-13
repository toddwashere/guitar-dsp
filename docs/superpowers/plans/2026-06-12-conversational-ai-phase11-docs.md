# Phase 11 — Docs, manual checklist, release notes

> Continuation of [2026-06-12-conversational-ai.md](2026-06-12-conversational-ai.md).

**Goal:** Ship-readiness docs — Logic Pro setup, model download instructions, and the manual test checklist that gates a tagged release.

---

### Task 11.1: Extend `docs/au-logic-setup.md` with sidechain steps

**Files:**
- Modify: `docs/au-logic-setup.md`

- [ ] **Step 1: Append a new section**

Add the following at the end of the existing doc:

```markdown
## Conversational AI setup (sidechain mic routing)

Per-project, one-time setup to enable the AI feature in Logic Pro.

1. **Create a mic input track.**
   - Audio track, input = your mic channel on your interface (e.g. Scarlett input 4).
   - **Monitor OFF** — you don't want to hear the mic in the room.
2. **On your guitar track,** insert *Audio Units → Todd B Fisher → Guitar Speak*
   (if not already inserted).
3. **Route the mic to the plugin's sidechain.**
   - In the Guitar Speak plugin window header, click the **Side Chain**
     dropdown → pick your mic input track. Logic shows "Audio 1" (or whatever
     you named the track).
4. **Confirm the plugin shows `Mic: receiving signal`** in its status pill while
   you talk into the mic.
5. **Save the project.** The sidechain routing persists with the song.

That's it for per-project routing. AI Settings (model, persona, API key) are
global — set once, applies everywhere.

### One-time global setup

- **API key:** In Guitar Speak → ⚙ AI Settings, paste your Anthropic API key
  and click **Test Anthropic**. Stored in
  `~/Library/Application Support/Todd B Fisher/Guitar Speak/settings.xml`
  (never in your project).
- **Local model (optional):** Install Ollama (`brew install ollama` then
  `ollama serve` to start) and `ollama pull llama3.2:3b` for a local model.
  Guitar Speak detects running Ollama automatically.

### Whisper STT model

The bundle ships with `ggml-base.en.bin` (~150 MB). If you want smaller or
larger models, drop `.bin` files into
`Guitar Speak.component/Contents/Resources/whisper/` (AU) or the equivalent
inside the standalone bundle, then select them from AI Settings → STT model.
```

- [ ] **Step 2: Commit**

```bash
git add docs/au-logic-setup.md
git commit -m "docs(ai): Logic Pro sidechain mic routing + global AI setup"
```

---

### Task 11.2: Manual test checklist

**Files:**
- Create: `docs/superpowers/manual-test-checklists/conversational-ai.md`

- [ ] **Step 1: Write the checklist**

```markdown
# Conversational AI — manual test checklist

Run before any tagged release. All boxes must be checked.

## Standalone

- [ ] Launch app with no API key set → AI dropdowns are populated; AI feature
      is "Anthropic: key missing"; type-and-say still works; no crash.
- [ ] Set API key in AI Settings → click Test Anthropic → green ✓.
- [ ] Press FCB pedal 9 → state pill goes red `Capturing` → speak
      "tell me about yourself" → release pedal → within 4 seconds the
      guitar speaks a reply.
- [ ] Run 20 consecutive turns without restarting → no slowdown, no memory
      growth in Activity Monitor.
- [ ] Switch model dropdown to an Ollama model with Ollama NOT running →
      status pill shows "Ollama: not running"; press pedal → engine shows
      friendly error in transcript; no crash.
- [ ] Start Ollama (`ollama serve` in a terminal), click Refresh Ollama →
      status flips to "Ollama: N models"; do a turn through `llama3.2:3b` →
      reply within 6 seconds.
- [ ] Unplug network mid-cloud-turn → engine returns to Idle within
      ~10 seconds with a "timed out" reason; no crash.
- [ ] Change scene mid-conversation → the next AI turn uses the new scene's
      voice.
- [ ] Press the Clear-chat pedal long-press → transcript empties; next turn
      starts fresh.

## AU in Logic Pro

- [ ] Insert Guitar Speak on a guitar track in a fresh project → Side Chain
      dropdown is available in the plugin header.
- [ ] Create a mic input track; pick it from the Side Chain dropdown → pill
      shows "Mic: receiving signal" with mic open.
- [ ] Do a complete turn via pedal 9 → reply plays through the guitar track
      output (same as scene-voiced TTS).
- [ ] Save project, close, reopen → persona + model selection restored;
      transcript is empty.
- [ ] Run `auval -v aumf GtSp TdBF` after this build → AU VALIDATION
      SUCCEEDED.
- [ ] Run `pluginval` strictness-10 → no in-process failures (the
      embedded auval sub-test may time out on Apple TTS / AI; ignore per
      existing convention).

## Regression — non-AI paths

- [ ] All carousel scenes still play normally.
- [ ] Note-triggered speech (Phase 5a) still works.
- [ ] FCB scene-change pedals (1–8) still switch scenes — AI pedal bindings
      don't override them.
```

- [ ] **Step 2: Commit**

```bash
mkdir -p docs/superpowers/manual-test-checklists
git add docs/superpowers/manual-test-checklists/conversational-ai.md
git commit -m "docs(ai): manual test checklist for tagged releases"
```

---

### Task 11.3: README / release-note bullets

**Files:**
- Modify: top-level `README.md` (existing)

- [ ] **Step 1: Add a short section to README under existing feature list**

```markdown
### Conversational AI (new)

Press a foot pedal, speak, the guitar replies. Works in standalone and inside
Logic Pro (AU plugin, sidechain mic). Local Whisper STT, dual LLM backend
(Anthropic cloud + Ollama local — pick from a dropdown), six persona presets
with editable prompts. Bundle grows by ~150 MB for the included Whisper model.

See [docs/au-logic-setup.md](docs/au-logic-setup.md) for one-time per-project
sidechain routing.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README — conversational-AI feature summary + size note"
```

---

### Task 11.4: Memory update for `[[au-plugin-status]]`

**Files:**
- Modify: `~/.claude/projects/-Users-user-GIT-guitar-dsp/memory/au_plugin_status.md`

- [ ] **Step 1: Update the "Future north-stars" line**

Find this line in the AU plugin status memory file:
> **Future north-stars (own specs):** conversational AI (mic → VAD → STT → local LLM → response text → existing TTS→vocoder; two flows: mic→AI and AI→guitar-speaks; manual "type-and-say" already works); pitch-tracked "singing" carrier.

Replace with:
> **Future north-stars:** pitch-tracked "singing" carrier; voice cloning. **Conversational AI shipped** — see `docs/superpowers/specs/2026-06-12-conversational-ai-design.md` and the matching plan. The original "Flow B" autonomous-duet mode is still future.

- [ ] **Step 2: Commit memory file** (separate from project repo — handled by superpowers memory system)

---

## Phase 11 checkpoint — green.

## Final full-suite verification

```bash
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests
```

All ~290 tests pass. Manual checklist run on hardware. Tag release.

## Self-review notes

Run through each spec section ([2026-06-12-conversational-ai-design.md](../specs/2026-06-12-conversational-ai-design.md)) and check it has an implementing task:

| Spec section | Implementing tasks |
|---|---|
| 1 Background | (context, no tasks) |
| 2 Goals | (covered by the whole plan) |
| 3 UX — pedal flow, personas, memory, reply shape | 1.1, 1.2, 6.1, 7.2, 9.1, 9.2 |
| 4 Architecture | 0.1 + every component task |
| 5 Components | 1.1, 1.2, 1.4, 2.1, 2.2, 2.3, 3.1, 3.2, 3.3, 4.3, 5.1, 6.1 |
| 6 Persistence — PluginState, PropertiesFile | 1.3, 1.4, 8.3 |
| 7 UI | 7.1, 7.2, 7.3, 10.1 |
| 8 Error handling | 6.2, 10.1, 10.2, 10.3 |
| 9 Logic Pro setup + AU specifics | 5.2, 5.3, 8.1, 8.3, 10.4, 11.1 |
| 10 Testing | every task has TDD; integration in 4.3, 5.3, 8.3, 10.3 |
| 11 Open questions | (out of scope) |

No gaps. Plan is ready for execution.
