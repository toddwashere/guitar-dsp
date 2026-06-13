# AU plugin in Logic Pro — setup

This is a guide for using the **Guitar Speak** AU plugin (`com.toddbfisher.guitarspeak`) inside Logic Pro 11.

## Install

1. Build the AU:
   ```
   cmake --build build --target guitar_dsp_app_AU
   ```
2. The build step installs the bundle to
   `~/Library/Audio/Plug-Ins/Components/Guitar Speak.component` automatically.
3. Open Logic Pro. If the AU doesn't appear, rebuild the AU cache:
   ```
   killall -9 AudioComponentRegistrar
   ```
   Then rescan in Logic Pro: *Logic Pro → Settings → Plug-In Manager → Reset & Rescan Selection*.

## Insert on a guitar track

- Create or open an audio track that takes your guitar interface input.
- *Audio Units → Todd B Fisher → Guitar Speak* — insert the plugin.
- Play — the existing scene system (PC-message-mapped FCB1010 pedals) drives speaking/vocoder modes the same as the standalone app.

## Conversational-AI setup (sidechain mic routing)

The conversational-AI feature needs a microphone signal **separate** from the guitar input. In Logic Pro this is routed via the plugin's sidechain input bus.

Per-project, one-time:

1. **Create a mic input track.**
   - Audio track, input = the mic channel on your interface (e.g. Scarlett input 4).
   - **Monitor OFF** — you don't want to hear the mic in the room.

2. **On your guitar track**, insert *Audio Units → Todd B Fisher → Guitar Speak* (if not already).

3. **Route the mic to the plugin's sidechain.**
   - In the Guitar Speak plugin window header, click the **Side Chain** dropdown → pick your mic input track. Logic shows "Audio 1" (or whatever you named the track).

4. **Confirm the plugin shows `Mic: receiving signal`** in its status pill while you talk into the mic.

5. **Save the project.** The sidechain routing persists with the song.

That's it for per-project routing. AI Settings (model, persona, API key) are global — set once, applies everywhere.

## One-time global setup

- **Anthropic API key:** In Guitar Speak → ⚙ AI Settings, paste your API key and click *Test Anthropic*. The key is stored in
  `~/Library/Application Support/Todd B Fisher/Guitar Speak/settings.xml`
  (never in the project file, never in the repo).
- **Local model (optional):** Install Ollama (`brew install ollama` then `ollama serve` in a terminal) and `ollama pull llama3.2:3b` for a local model. Guitar Speak detects running Ollama automatically — click *Refresh Ollama* in AI Settings to rescan.

## Whisper STT model

The bundle ships with `ggml-base.en.bin` (~150 MB) for speech-to-text.

If you want smaller or larger models, drop `.bin` files into
`Guitar Speak.component/Contents/Resources/whisper/` (AU) or the equivalent inside the standalone bundle, then select them from AI Settings → STT model.

## FCB1010 AI pedal bindings

By default the AI pedals are **disabled** so the existing 10 scene pedals (program changes 0-9) work as before. To enable, edit the FCB1010 mapping JSON (see `Resources/midi/fcb1010-stock.json` for the format) and add an `aiPedals` block:

```json
{
  "programChangeToScene": {...},
  "aiPedals": {
    "ptt":       8,
    "clearChat": 9
  }
}
```

- `ptt`: a press toggles record start/stop.
- `clearChat`: a press clears the conversation buffer.

Caveat: PC 8 and PC 9 will *also* still trigger their mapped scenes if you leave them in `programChangeToScene`. Remove or remap those entries to dedicate the pedals to AI.

## Tested host

- Logic Pro 11 on macOS 14 (Sonoma).
- AUv2 (the only AU format Logic loads in this CMake setup).
- `auval -v aumf GtSp TdBF` should report SUCCEEDED after install + cache reset.
