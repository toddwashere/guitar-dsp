# Scene 6 — Vocal-Guitar Recording Script

Goal: record 10 short "mouth-guitar" vocalizations to replace the placeholder
tones the engine currently ships with. The engine cycles to the next clip on
every guitar pick onset.

## Setup

- Mic: Scarlett input 2 (or any decent dynamic). Get close — 2–4 inches.
- Recorder: QuickTime → File → New Audio Recording is fine. Native macOS,
  exports `.m4a` you can convert with `afconvert`.
- Output target: `assets/clips/vocal-guitar/<key>/audio.wav` — mono, 48 kHz,
  16-bit. The build script in `scripts/build_vocal_guitar_clips.py` produces
  that format; mimic it. If you record in QuickTime, convert with:

  ```bash
  afconvert -f WAVE -d LEI16@48000 -c 1 input.m4a output.wav
  ```

## Performance directions

Channel your inner Jack Black. Stay in character. Aim for energy, not
accuracy — these are cartoon vocal-guitar sounds, not Berklee transcriptions.

Hit each take with conviction. The mic doesn't care; the audience does.

## The 10 clips

Record them in this order, save each as the named file. Approximate length
is a guide; trim to taste.

| # | Folder key | Length | What to do |
|---|------------|--------|------------|
| 0 | `00_wee`            | ~300 ms  | Quick, high "WEE!" — short bright stab |
| 1 | `01_doo`            | ~250 ms  | Low "DOO" — a thumb of bass |
| 2 | `02_ner`            | ~200 ms  | Nasal "NER" — short, snotty |
| 3 | `03_new`            | ~250 ms  | "NEW" with rising pitch — like a guitar bend |
| 4 | `04_yeah`           | ~400 ms  | Big confident "YEAH" — full chest |
| 5 | `05_brrr`           | ~400 ms  | Lip-trill "BRRRR" — like a motor revving |
| 6 | `06_skronk`         | ~500 ms  | Ugly "SKRRONK" — dissonant, throaty |
| 7 | `07_weeeeee`        | ~1200 ms | Long sustained "WEEEEEEEE" — peak hold |
| 8 | `08_ahhhh`          | ~1200 ms | Long "AHHHH" — open, bellowy |
| 9 | `09_ner-ner-ner`    | ~700 ms  | Rapid triplet "ner-ner-NER" — comedy beat |

## Quality bar

- **Mono.** Don't bother with stereo; the vocoder modulator is mono.
- **No silence padding at the start.** Trim aggressively. The clip
  triggers on a pick onset; any leading silence is dead air.
- **Trailing silence OK.** The engine outputs silence after the clip ends.
- **Peak around -6 to -3 dBFS.** Loud enough to drive the vocoder envelope
  follower, not clipped.
- **No background noise.** Quiet room; no AC, no fans, no fridge.
- **In character.** If a take sounds bored, redo it.

## Iteration loop

1. Record one clip
2. Drop into the folder
3. Launch Guitar Speak, switch to Scene 6 ("Moves like Jack Black")
4. Pick a note — hear that clip modulate the guitar
5. If it works, move to the next clip. If not, retake.

You don't have to do all 10 in one sitting. The engine works with any
number of clips ≥ 1; missing clips are silently skipped. So you can ship
incremental.

## Pre-stage QA checklist

- [ ] All 10 files exist and load (check at app launch — stderr logs
      `[PrebakedTTSSource] missing:` for anything broken)
- [ ] Playing a chromatic run: hear 10 distinct vocalizations then wrap
- [ ] Rewind pill resets to clip 0
- [ ] Levels are consistent across clips (no jarring quiet/loud swings)
