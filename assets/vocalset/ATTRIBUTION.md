# VocalSet attribution

The audio files in this directory are a curated subset of **VocalSet: A
Singing Voice Dataset** (v1.2), redistributed under the original
Creative Commons Attribution 4.0 International (CC-BY-4.0) license.

## Citation

> Wilkins, J., Seetharaman, P., Wahl, A., & Pardo, B. (2018). *VocalSet:
> A Singing Voice Dataset.* In *Proceedings of the 19th International
> Society for Music Information Retrieval Conference (ISMIR 2018).*

Dataset: <https://zenodo.org/records/1442513>
License: <https://creativecommons.org/licenses/by/4.0/>

## Subset

Singer `male1` only. Twenty WAV files:

- `long_tones/straight/` — five vowels (a, e, i, o, u), held tones, no vibrato.
- `long_tones/forte/` — same five vowels at loud dynamic.
- `scales/slow_piano/` — slow ascending+descending scales on C and F keys,
  five vowels each (10 files). Used to extract anchor-pitch grains.

Original archive layout preserved so re-derivation from the full
2.6 GB `VocalSetRaw/` tree is traceable.

## Why this subset

- One voice keeps the demo focused and the `.gspeak` bundle small.
- `long_tones/straight` is the workhorse source (vocoder/shifter add
  expression downstream).
- `long_tones/forte` provides a louder-character alternative.
- `slow_piano` scales let us slice anchor-pitch grains across the
  guitar range without needing dedicated multi-anchor recordings.

See [docs/superpowers/specs/2026-06-23-scene-11-sung-vowels-design.md](../../docs/superpowers/specs/2026-06-23-scene-11-sung-vowels-design.md)
for how these source files feed into the `scene11_sung_vowels.gspeak`
bundle and beyond.
