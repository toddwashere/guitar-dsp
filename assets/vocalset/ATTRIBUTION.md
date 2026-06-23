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

Four singers, each with an identical curated set of WAV files:

| Folder | VocalSet singer | Demo label |
|---|---|---|
| `m1/`  | male1   | "Male 1" (default — warm tenor reference) |
| `m10/` | male10  | "Mighty Man" |
| `f2/`  | female2 | "Female 2" |
| `f8/`  | female8 | "Female 8" |

Per-singer files (20 each):

- `long_tones/straight/` — five vowels (a, e, i, o, u), held tones, no vibrato.
- `long_tones/forte/` — same five vowels at loud dynamic.
- `scales/slow_piano/` — slow ascending+descending scales on C and F keys,
  five vowels each (10 files). Used to extract anchor-pitch grains.

Original VocalSet archive layout is preserved so re-derivation from the
full 2.6 GB `VocalSetRaw/` tree is traceable.

## Why this subset

- One voice per persona keeps the demo focused and per-bundle `.gspeak`
  files small.
- `long_tones/straight` is the workhorse source (vocoder/shifter add
  expression downstream).
- `long_tones/forte` provides a louder-character alternative.
- `slow_piano` scales let us slice anchor-pitch grains across the
  guitar range without needing dedicated multi-anchor recordings.
- VocalSet's official test-singers split is
  `{female2, female8, male3, male5, male10}` — three of our four
  picks come from that polished held-out set.

See [docs/superpowers/specs/2026-06-23-scene-11-sung-vowels-design.md](../../docs/superpowers/specs/2026-06-23-scene-11-sung-vowels-design.md)
for how these source files feed into the per-voice `.gspeak`
bundles and the runtime voice-pack dropdown.
