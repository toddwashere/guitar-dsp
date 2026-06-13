# Conversational AI — manual test checklist

Run before any tagged release. All boxes must be checked.

## Standalone

- [ ] Launch the standalone app with no API key set — AI dropdowns are populated, AI feature is `Anthropic: key missing`, type-and-say still works, no crash.
- [ ] Set API key in AI Settings → click *Test Anthropic* → green ✓.
- [ ] Press FCB pedal 9 (or click Record) → state pill goes red `Capturing` → speak "tell me about yourself" → release → within ~4 seconds the guitar speaks a reply.
- [ ] Run 20 consecutive turns without restarting — no slowdown, no memory growth in Activity Monitor.
- [ ] Switch model dropdown to an Ollama model with Ollama NOT running → status pill shows `Ollama: not running`; press Record → engine shows friendly error in transcript; no crash.
- [ ] Start Ollama (`ollama serve` in a terminal), click *Refresh Ollama* → status flips to `Ollama: N models`; do a turn through `llama3.2:3b` → reply within ~6 seconds.
- [ ] Unplug network mid-cloud-turn → engine returns to Idle within ~10 seconds with a `timed out` reason; no crash.
- [ ] Change scene mid-conversation → the next AI turn uses the new scene's voice.
- [ ] Press the Clear-chat pedal (or Clear button) → transcript empties; next turn starts fresh.

## AU in Logic Pro

- [ ] Insert Guitar Speak on a guitar track in a fresh project → Side Chain dropdown is available in the plugin header.
- [ ] Create a mic input track; pick it from the Side Chain dropdown → pill shows `Mic: receiving signal` with mic open.
- [ ] Do a complete turn via the FCB PTT pedal → reply plays through the guitar track output (same as scene-voiced TTS).
- [ ] Save project, close, reopen → persona + model selection restored; transcript is empty (history not persisted by design).
- [ ] Run `auval -v aumf GtSp TdBF` after this build → AU VALIDATION SUCCEEDED.
- [ ] Run `pluginval` strictness-10 → no in-process failures (the embedded auval sub-test may time out on AI or TTS calls; ignore per existing convention).

## Regression — non-AI paths

- [ ] All carousel scenes still play normally.
- [ ] Note-triggered speech still works.
- [ ] FCB scene-change pedals (1–8) still switch scenes when AI pedal bindings don't override them (PC 8 and 9 are the AI defaults).
