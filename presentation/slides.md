---
theme: default
title: While My Guitar Gently Speaks
info: |
  Building a real-time talking, singing guitar — and what
  shipping for the stage taught me about building with AI.

  AI Engineering Conference · San Francisco · 2026
class: text-center
drawings:
  persist: false
transition: slide-left
mdc: true
# Hide the built-in editor sidebar — protects the live show from a
# stray click on the pencil icon popping the slide-list drawer open.
editor: false
---

# While My Guitar Gently Speaks

<div class="flex gap-8 pt-12 items-center max-w-3xl mx-auto">

<div class="text-right w-full max-w-[457px]">
  <div class="text-4xl font-semibold">Todd Fisher</div>
  <div class="text-2xl opacity-75 pt-1">Head of Engineering · Philo Ventures</div>
  <div class="flex items-center justify-end gap-2 pt-3 text-lg opacity-80">
    <span>todd-b-fisher</span>
    <svg viewBox="0 0 24 24" class="w-5 h-5" fill="#4DA8F0"><path d="M20.45 20.45h-3.56v-5.57c0-1.33-.02-3.04-1.85-3.04-1.86 0-2.14 1.45-2.14 2.94v5.67H9.35V9h3.41v1.56h.05c.48-.9 1.64-1.85 3.37-1.85 3.6 0 4.27 2.37 4.27 5.46v6.28zM5.34 7.43a2.06 2.06 0 1 1 0-4.13 2.06 2.06 0 0 1 0 4.13zM7.12 20.45H3.56V9h3.56v11.45zM22.22 0H1.77C.79 0 0 .77 0 1.73v20.54C0 23.23.79 24 1.77 24h20.45c.98 0 1.78-.77 1.78-1.73V1.73C24 .77 23.2 0 22.22 0z"/></svg>
  </div>
</div>

<div class="pl-5">
<img src="/todd.jpg" class="w-48 h-48 rounded-full object-cover border-2" />
</div>
</div>

<div class="flex justify-center pt-12">
  <img src="/ai-worldsfair-logo.svg" class="h-12 opacity-90" />
</div>

<!--
Hi, I'm Todd Fisher, Head of Engineering at Philo Ventures. For
the next ~30 minutes I'm going to walk you through a real-time
audio project — a guitar that talks and sings — and what shipping
it for the stage taught me about building with AI.
-->

---
layout: two-cols
class: gap-8
---

# Live performances rock

- **Slipknot's drummer** — flips upside-down mid-set
- **Blue Man Group** — silence as performance
- **The Sphere** — Las Vegas
- **Stranger Things: The First Shadow** — NYC

<div class="pt-8 opacity-75 text-sm">
We've all been blown away at some point.<br/>
Live shows connect us in a way recorded media can't.
</div>

<div class="pt-6 text-xl">
What's the next evolution — with AI?
</div>

::right::

<img src="/slipknot.jpg" class="rounded-lg w-full" />

<!--
Slipknot's drummer is on a rig that flips upside-down. Blue Man
Group turned percussion and silence into a brand. The Sphere is
its own visual category. Stranger Things in NYC is a full
theatrical experience. We've all been amazed at some point.
The question this talk is about: what does the next layer of
live performance look like, now that AI tools are in the kit?
-->

<div class="absolute bottom-4 left-6 text-xs opacity-30">1m</div>

---

# Setting the stage

- Great experiences stay with us forever.
- Creativity × technology yields surprising results
- A lot of interesting things are happening with AI right now
- The tools we have today are opening up new possibilities for **live performance**

<!--
This talk lives at the intersection of all three. I'm a software
engineer who plays guitar; I built a tool that uses TTS, LLMs,
real-time DSP, and a foot controller to make a guitar talk and
sing on stage. The interesting part isn't the AI — it's what live
performance lets you do with it.
-->


---

<div class="absolute inset-0 flex items-center justify-center p-6">
  <img src="/guitar-evolution.png" class="max-h-full max-w-full object-contain rounded-lg" />
</div>

<!--
Walk the audience through the panels left → right:
1. Acoustic Scream — caveman with an acoustic. The starting point.
2. Electric Distortion — pickup + amp + dirt. The first big leap.
3. The Pedal Chain — fuzz, wah, delay; tone-shaping at the foot.
4. The Talkbox Voice — Peter Frampton, "Do You Feel Like We Do."
   First real "speaking" guitar. The mouth is the resonator.
5. Software Emulation — same effects, now in a plugin host.
6. AI Tools — synthesis + language generation. Where this talk lives.

Ebow gets a mention but isn't pictured; it's the side path.
-->

<div class="absolute bottom-4 left-6 text-xs opacity-30">1m</div>


---

<div class="absolute inset-0 flex items-center justify-center p-6">
  <img src="/halloween-guitar-pumpkin.png" class="max-h-full max-w-full object-contain rounded-lg" />
</div>

<!--
Quick origin story. Every Halloween I had two choices: walk with
the kids or stay home. Found out you can crank a guitar amp loud
outside on Halloween and no one calls the cops. That's how this
whole thread started.
-->

---

<div class="absolute inset-0 flex items-center justify-center p-6">
  <img src="/todd-eddie-munson-cutout.png" class="max-h-full max-w-full object-contain" />
</div>

---

<div class="absolute inset-0 flex items-center justify-center p-6">
  <img src="/eddie-munson-1.jpg" class="max-h-full max-w-full object-contain rounded-lg" />
</div>

<div class="absolute bottom-4 left-0 right-0 text-center text-sm opacity-80 z-10">
  <a
    href="https://www.activeviz.com/stranger-things-lights"
    target="_blank"
    rel="noopener noreferrer"
    class="hover:opacity-100"
  >
    activeviz.com/stranger-things-lights ↗
  </a>
</div>

<!--
One year I dressed as Eddie Munson from Stranger Things season 4.
Pause and read the room: "anyone here watch Stranger Things?"
That night, sitting on the porch in costume, I started asking:
what else could make this better?
-->

<div class="absolute bottom-4 left-6 text-xs opacity-30">3:30</div>


---

<div class="absolute inset-0 flex flex-col items-center justify-center text-center">
  <div class="text-5xl">What if my guitar could</div>
  <div class="text-9xl font-bold pt-6">speak?</div>
</div>

<!--
The pivot question. Pause after "speak" — let it land.
This is the setup for the demo.
-->

---
transition: fade-out
---

# Tools for the job

- **JUCE has the most production miles in live audio.** 
  "No allocations on the audio thread" is built into its idioms. C++ FTW!
- **Plugin formats are free.** AUv2, VST3, AAX — same source.
- **TTS** — Piper (local neural), Apple AVSpeechSynthesizer, prebaked WAV.
- **DSP** — 24-band channel vocoder, YIN pitch detection, PolyBLEP saw.

---
layout: center
---

# Goal: Guitar triggers saying a word

<div class="flex flex-col items-center gap-3 pt-6">

<div class="relative border border-gray-600 rounded-xl px-10 pt-7 pb-6">
  <div class="absolute -top-3 left-5 px-2 text-xs tracking-wide opacity-60 bg-black">Offline / bake this once</div>
  <div class="flex items-center gap-3">
    <div class="rounded-md px-5 py-3 font-medium" style="background:#e0f2fe;color:#0c4a6e">Raw text</div>
    <div class="text-2xl opacity-40">→</div>
    <div class="rounded-md px-5 py-3 font-medium" style="background:#cffafe;color:#164e63">TTS</div>
    <div class="text-2xl opacity-40">→</div>
    <div class="rounded-md px-5 py-3 font-medium" style="background:#ccfbf1;color:#134e4a">Audio clip</div>
  </div>
</div>

<div class="flex flex-col items-center gap-1" style="transform: translateX(80px)">
  <div class="text-2xl opacity-40 leading-none self-start pl-8">↓</div>
</div>

<div class="relative border border-gray-600 rounded-xl px-10 pt-7 pb-6">
  <div class="absolute -top-3 left-5 px-2 text-xs tracking-wide opacity-60 bg-black">Live / repeat per pluck</div>
  <div class="flex items-center gap-3">
    <div class="rounded-full px-5 py-3 font-medium" style="background:#fef9c3;color:#713f12">Guitar pluck</div>
    <div class="text-2xl opacity-40">→</div>
    <div class="rounded-md px-5 py-3 font-medium" style="background:#ffedd5;color:#7c2d12">Play audio clip</div>
    <div class="text-2xl opacity-40">→</div>
    <div class="rounded-full w-12 h-12 flex items-center justify-center text-xl" style="background:#fee2e2;color:#7f1d1d">🔊</div>
  </div>
</div>

</div>

<div class="absolute bottom-4 left-6 text-xs opacity-30">Scene 1</div>

---
layout: center
---

# Goal: Say multiple words {.text-center}

<div class="flex flex-col items-center gap-3 pt-6">

<div class="relative border border-gray-600 rounded-xl px-10 pt-7 pb-6">
  <div class="absolute -top-3 left-5 px-2 text-xs tracking-wide opacity-60 bg-black">Offline / bake this once</div>
  <div class="flex items-center gap-3">
    <div class="rounded-md px-5 py-3 font-medium" style="background:#e0f2fe;color:#0c4a6e">Raw text</div>
    <div class="text-2xl opacity-40">→</div>
    <div class="rounded-md px-5 py-3 font-medium" style="background:#cffafe;color:#164e63">TTS</div>
    <div class="text-2xl opacity-40">→</div>
    <div class="rounded-md px-5 py-3 font-medium" style="background:#ccfbf1;color:#134e4a">Audio clip</div>
  </div>
</div>

<div class="flex flex-col items-center gap-1" style="transform: translateX(80px)">
  <div class="text-2xl opacity-40 leading-none self-end pr-9">↓</div>
  <div class="rounded-md px-5 py-3 font-medium" style="background:#dcfce7;color:#14532d">Slice per word</div>
  <div class="text-2xl opacity-40 leading-none self-start pl-8">↓</div>
</div>

<div class="relative border border-gray-600 rounded-xl px-10 pt-7 pb-6">
  <div class="absolute -top-3 left-5 px-2 text-xs tracking-wide opacity-60 bg-black">Live / repeat per pluck</div>
  <div class="flex items-center gap-3">
    <div class="rounded-full px-5 py-3 font-medium" style="background:#fef9c3;color:#713f12">Guitar pluck</div>
    <div class="text-2xl opacity-40">→</div>
    <div class="rounded-md px-5 py-3 font-medium" style="background:#ffedd5;color:#7c2d12">Play next word</div>
    <div class="text-2xl opacity-40">→</div>
    <div class="rounded-full w-12 h-12 flex items-center justify-center text-xl" style="background:#fee2e2;color:#7f1d1d">🔊</div>
  </div>
</div>

</div>

<div class="absolute bottom-4 left-6 text-xs opacity-30">Scene 2</div>

---

# Auto slice the audio clip

We get one blob of PCM back from TTS — no timestamps. To play one word per pluck, we have to find the cuts ourselves. (`Enter: WordAligner`)

```mermaid {scale: 0.8}
flowchart LR
  PCM["TTS PCM<br/>(one blob)"] --> Env["Peak envelope<br/>30 ms release"]
  Env --> Gaps["Find silences<br/>env < 15% peak"]
  Gaps --> Cut["Keep N-1 longest<br/>→ cut points"]
  Cut --> Words["N word segments"]
```

<v-click>

**Energy-gap segmentation, not engine timestamps.** Build a peak envelope, find the longest silences between words, cut there. `N` words → `N-1` cuts.

</v-click>

<v-click>

**The release time is the trick.** A 30 ms envelope release rides over the tiny gaps *inside* a word (s**t**op, **th**ink) but still drops on real ~80 ms inter-word silences. No false cuts mid-word.

</v-click>

<v-click>

**Same algorithm for every backend.** Piper, Apple, prebaked WAV — it only needs the samples + word list. Not enough silences? Fall back to evenly spaced cuts.

</v-click>

<div class="absolute bottom-4 left-6 text-xs opacity-30">Scene 0</div>


<!--
The TTS engines hand back raw PCM with no word boundaries. Rather
than ask each engine for timing (Apple won't, Piper won't, the
prebaked WAV can't), I slice uniformly off the audio itself in
WordAligner::align. Build a smoothed peak envelope (instant attack,
~30 ms exponential release), threshold at 15% of the clip peak,
collect the silence runs, keep the N-1 longest as boundaries, and
cut. The 30 ms release is deliberately longer than stop-consonant
gaps inside a word so those don't register as boundaries. Because
it only touches the PCM + the word list, the exact same code works
for all three TTS sources — that's the whole point.
-->

---

# Syllables are tricky!

- **Phoneme alignment** — espeak-ng labels + sonority-peak syllabifier
- espeak gives the **sounds**, but not the **when** — uniform fake durations, so we still have to guess each syllable's position from the audio

<div class="flex justify-center pt-4">
  <img src="/sonority-sequencing-cats-run-fast.png" class="max-h-80 rounded-lg" />
</div>

---

# Manually slice n dice

- Pre baking ensures the best quality.
- First pass: auto-slice
- Visually edit the wave forms.

---
layout: section
---

<div class="absolute inset-0 flex flex-col items-center justify-center text-center">
  <div class="text-5xl">What if my guitar could</div>
  <div class="text-9xl font-bold pt-6 pb-10">sing?</div>
  <div class="text-2xl">
Next Stop: Pitch detection.
</div>
</div>




---

<div class="absolute inset-0 flex items-center justify-center p-6">
  <img src="/frequencies-f0.png" class="max-h-full max-w-full object-contain rounded-lg" />
</div>

---
layout: center
class: text-center
---

<!-- # Finding the fundamental -->

<!-- <div class="grid grid-cols-[1.05fr_1fr] gap-8 pt-2 text-sm"> -->

<!-- <div>

A guitar note is **rich and harmonic** — naive autocorrelation keeps locking onto the wrong octave. **YIN** (de Cheveigné & Kawahara, 2002) fixes that in four steps:

1. **Difference function** — slide the signal against a delayed copy of itself: `d(τ) = Σ (x[j] − x[j+τ])²`. It dips toward zero when the lag `τ` matches the period.
2. **Cumulative mean normalize (CMNDF)** — divide by the running average so tiny lags stop always winning. This is the step that kills octave errors.
3. **Absolute threshold** — take the *first* dip below `0.15`, not the global minimum. That lag is the period `τ`.
4. **Parabolic interpolation** — refine `τ` between samples for sub-Hz precision.

<div class="pt-2 opacity-75">

`f₀ = sampleRate / τ` — clamped to 40–2000 Hz. Nothing below threshold ⇒ **unvoiced**.

</div>

</div> -->

<div class="flex items-center justify-center pt-4">
  <img src="/yin-simple-diagram.png" class="max-h-[72vh] max-w-full rounded-lg" />
</div>

<!-- </div> -->

<!-- <div class="absolute bottom-4 left-6 right-6 text-xs opacity-50">

**Why YIN over a plain FFT?** It runs sample-by-sample with no block latency, stays allocation-free on the audio thread, and the CMNDF step makes it far more robust to octave errors on a harmonically-rich guitar than peak-picking an FFT or raw autocorrelation.

</div> -->

<!--
This is the "how do we know what note you're playing" slide.
Key intuition for steps 1-2: the difference function finds repeats,
but it trivially dips at tiny lags too — the cumulative-mean
normalization is what stops it from snapping an octave (or two) high.
Threshold + parabolic interp are just "pick the right dip" and
"get the decimal places." The output f0 drives the pitched saw
carrier the vocoder shapes — that's how a spoken word comes out *sung*.
Real params live in PitchTrackedCarrier: 2048-sample window,
256-sample hop, threshold 0.15, 40-2000 Hz range.
-->

---
layout: center
---

# The vocoder marries pitch + voice

<div class="flex items-center justify-center gap-4 pt-6 text-sm">
<div class="flex flex-col gap-5">
<div class="relative border border-gray-600 rounded-xl px-6 pt-6 pb-4">
<div class="absolute -top-3 left-5 px-2 text-xs tracking-wide opacity-60 bg-black">Carrier = pitch</div>
<div class="flex items-center gap-2">
<div class="rounded-full px-3 py-2 font-medium" style="background:#fef9c3;color:#713f12">🎸 Guitar</div>
<div class="text-lg opacity-40">→</div>
<div class="rounded-md px-3 py-2 font-medium" style="background:#dbeafe;color:#1e3a8a">YIN&nbsp;f₀</div>
<div class="text-lg opacity-40">→</div>
<div class="rounded-md px-3 py-2 font-medium" style="background:#ccfbf1;color:#134e4a">Pitched saw</div>
</div>
</div>
<div class="relative border border-gray-600 rounded-xl px-6 pt-6 pb-4">
<div class="absolute -top-3 left-5 px-2 text-xs tracking-wide opacity-60 bg-black">Modulator = words</div>
<div class="flex items-center gap-2">
<div class="rounded-md px-3 py-2 font-medium" style="background:#ffedd5;color:#7c2d12">🗣 Voice clip</div>
<div class="text-xs opacity-60">one spoken word</div>
</div>
</div>
</div>
<div class="flex flex-col justify-between self-stretch py-5 text-3xl opacity-40"><div>→</div><div>→</div></div>
<div class="rounded-xl px-6 py-5 text-center" style="background:#ede9fe;color:#5b21b6">
<div class="font-semibold text-base">24-band vocoder</div>
<div class="flex items-end justify-center gap-1 h-12 py-2">
<div class="w-1.5 rounded-t" style="height:40%;background:#7c3aed"></div>
<div class="w-1.5 rounded-t" style="height:75%;background:#7c3aed"></div>
<div class="w-1.5 rounded-t" style="height:55%;background:#7c3aed"></div>
<div class="w-1.5 rounded-t" style="height:95%;background:#7c3aed"></div>
<div class="w-1.5 rounded-t" style="height:60%;background:#7c3aed"></div>
<div class="w-1.5 rounded-t" style="height:80%;background:#7c3aed"></div>
<div class="w-1.5 rounded-t" style="height:45%;background:#7c3aed"></div>
<div class="w-1.5 rounded-t" style="height:70%;background:#7c3aed"></div>
</div>
<div class="text-xs opacity-80">carrier bands × voice envelopes</div>
</div>
<div class="text-3xl opacity-40">→</div>
<div class="rounded-xl px-5 py-4 text-center font-medium" style="background:#fee2e2;color:#7f1d1d">🔊<br/>word, <i>sung</i><br/>at your pitch</div>
</div>

<div class="text-center text-xl pt-6">It's not a blender — it's a <b>stencil</b>. The voice is the <b>shape</b>; your guitar is the <b>paint</b>.</div>

<v-click>
<div class="flex flex-col items-center pt-6">
<div class="text-xs opacity-60 pb-2">…or: the voice works a 24-band EQ on your guitar, hundreds of times a second</div>
<div class="flex items-end gap-2 px-4 py-3 rounded-lg border border-gray-700">
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:62%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:30%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:48%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:12%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:35%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:55%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:25%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:42%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:68%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:38%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:58%"></div></div>
<div class="relative w-4 h-16"><div class="absolute left-1/2 top-0 bottom-0 w-px bg-gray-600"></div><div class="absolute left-0 right-0 h-1.5 rounded bg-sky-400" style="top:78%"></div></div>
</div>
</div>
</v-click>

<div class="absolute bottom-4 left-0 right-0 text-center text-xs opacity-50"><b>ADSR</b> — Attack · Decay · Sustain · Release</div>

<!-- Pitched saw = a synthetic sawtooth oscillator we generate at the guitar's detected pitch. We keep the pitch of your note and throw away its sound, replacing it with a clean, harmonically-rich tone — the perfect raw material for the vocoder to shape into words.

So when you talk to it: it's "YIN tells us what note you played; we rebuild that note as a clean synthesized sawtooth — all harmonics, perfectly in tune — and that's what the vocoder turns into speech." -->

---
layout: center
class: text-center
---

# Time to jam ... {.text-9xl}

<div class="absolute bottom-4 left-6 text-xs opacity-30">Scene 10</div>

---

# Conversations are a two way street

- **STT** — whisper.cpp (local, ~150 MB model, runs on CPU)
- **LLM** — Ollama (local models)

<div class="flex flex-col items-center gap-2 pt-4 text-sm">

<div class="relative border border-gray-600 rounded-xl px-8 pt-6 pb-5">
  <div class="absolute -top-3 left-5 px-2 text-xs tracking-wide opacity-60 bg-black">You speak</div>
  <div class="flex items-center gap-3">
    <div class="rounded-full w-11 h-11 flex items-center justify-center text-lg" style="background:#e0f2fe;color:#0c4a6e">🎤</div>
    <div class="text-xl opacity-40">→</div>
    <div class="rounded-md px-4 py-2 font-medium" style="background:#dbeafe;color:#1e3a8a">STT</div>
    <div class="text-xl opacity-40">→</div>
    <div class="rounded-md px-4 py-2 font-medium" style="background:#e0f2fe;color:#0c4a6e">Raw text</div>
  </div>
</div>

<div class="text-xl opacity-40 leading-none">↓</div>

<div class="relative border border-gray-600 rounded-xl px-8 pt-6 pb-5">
  <div class="absolute -top-3 left-5 px-2 text-xs tracking-wide opacity-60 bg-black">It thinks</div>
  <div class="flex items-center gap-3">
    <div class="rounded-md px-4 py-2 font-medium" style="background:#ede9fe;color:#5b21b6">Local LLM</div>
    <div class="text-xl opacity-40">→</div>
    <div class="rounded-md px-4 py-2 font-medium" style="background:#f3e8ff;color:#6b21a8">Response text</div>
  </div>
</div>

<div class="text-xl opacity-40 leading-none">↓</div>

<div class="relative border border-gray-600 rounded-xl px-8 pt-6 pb-5">
  <div class="absolute -top-3 left-5 px-2 text-xs tracking-wide opacity-60 bg-black">It speaks</div>
  <div class="flex items-center gap-3">
    <div class="rounded-md px-4 py-2 font-medium" style="background:#cffafe;color:#164e63">TTS</div>
    <div class="text-xl opacity-40">→</div>
    <div class="rounded-md px-4 py-2 font-medium" style="background:#dcfce7;color:#14532d">Slice per word</div>
    <div class="text-xl opacity-40">→</div>
    <div class="rounded-full px-4 py-2 font-medium" style="background:#fef9c3;color:#713f12">Guitar pluck</div>
    <div class="text-xl opacity-40">→</div>
    <div class="rounded-full w-11 h-11 flex items-center justify-center text-lg" style="background:#fee2e2;color:#7f1d1d">🔊</div>
  </div>
</div>

<div class="text-xs opacity-60 pt-1">↺ your reply starts the next turn</div>

</div>


<div class="absolute bottom-4 left-6 text-xs opacity-30">Scene 4</div>

---
layout: section
---

# Part 3.5 — From speaking to singing

The next step: drop the TTS, drop the synth carrier, sing with someone's actual voice

---

# VocalSet: real singers, free to use

The university-academic version of "we have a vocal sample pack."

<v-clicks>

- **VocalSet** (Wilkins et al., ISMIR 2018) — 10 hours of professional vocalists, sung vowels at controlled techniques, CC-BY-4.0
- 20 singers, all 5 vowels, recorded in a controlled studio
- Curated a handful into the app and cut each recording into **grains** — short audio fragments, each a single sung note about a second long
- A bundle of grains becomes the source of truth for "what the guitar should sound like when you play this string"

</v-clicks>

<div v-click class="pt-6 opacity-75 text-sm text-center">

The hardest part of "use a real voice" was sourcing it. VocalSet meant zero studio time, zero licensing email chains, zero "I have a friend who sings."

</div>

<!--
The instant you say "use a real human voice instead of TTS," the
first question is where the audio comes from. Recording it yourself
is hours of studio time. Buying a sample pack is licensing
headaches. VocalSet is an academic dataset — professional singers,
controlled conditions, Creative Commons. I grabbed a few singers,
cut their recordings into short fragments — what we'll call grains
— a handful per voice, each a single sung note. The bundle of
grains is the raw material for everything downstream.
-->

---

# Two paths to the same audio

The same grains, two completely different DSP chains, two completely different results.

<div class="grid grid-cols-2 gap-6 pt-2 text-sm">

<div v-click class="p-3 border rounded">

**Vocoder version**

The grain is the modulator; a pitched saw at your guitar's note is the carrier. The voice comes through with the vocoder's synth character on top — still recognizable as "a vocoder talking."

</div>

<div v-click class="p-3 border rounded">

**Direct version**

No vocoder, no saw. A formant-preserving pitch shifter (WORLD) shifts the recorded grain to match your guitar's note. Output is the actual singer's voice, transposed.

</div>

</div>

<div v-click class="pt-6 opacity-75 text-sm text-center">

Shipping both lets the audience hear the difference on the same instrument. Same pluck, same vowel, two outputs. The vocoder version was already free given the existing pipeline; the direct version is where the engineering got interesting.

</div>

<!--
Once we had the grains, two architectural choices. Path one feeds
the grains into the existing vocoder as the modulator — cheap,
ships immediately, sounds vocoder-y because the saw carrier
signature is still there. Path two bypasses the vocoder entirely
and runs a formant-preserving pitch shifter on the grain so the
output IS the singer. Two scenes side by side on the FCB makes the
audible difference between vocoder character and direct-shift
character.
-->

---

# What those 90 seconds were doing

A grain is a recording of someone singing "ah" at, say, 250 Hz. Your guitar might be playing 82 Hz. **Pitch-shift without chipmunk** is the whole problem.

<div v-click class="pt-4 grid grid-cols-2 gap-6 text-sm">

<div>

**WORLD decomposes the grain into three streams:**

```
                ┌─ F0 contour    (Harvest)
   grain.wav  ──┼─ spectral env  (CheapTrick)
                └─ aperiodicity  (D4C)
```

Pitch lives in F0. The vowel character — the *formants* that make "ah" sound like "ah" — lives in the envelope. The noise/breath lives in aperiodicity.

</div>

<div>

**To pitch-shift without chipmunking,** scale the F0 contour and resynthesize. Envelope and aperiodicity stay put:

```
  scaled_F0  ─┐
  envelope   ─┼─ Synthesis ─→ shifted audio
  aperiod    ─┘
```

</div>

</div>

<div v-click class="pt-6 text-sm">

**The 90 seconds:** the Synthesis step is the expensive one, and we run it **once per output pitch we want to support, per grain.** Twenty grains × thirty-seven discrete pitch ratios from −18 to +18 semitones × about a second of WORLD compute per ratio = a few minutes per voice. Done once, written to disk, never repeated.

</div>

<!--
A grain is a short recording — someone singing the vowel "ah" at,
say, 250 Hz, for about a second. Your guitar might be plucking 82
Hz. We need to play that grain at the guitar's pitch without it
sounding like Alvin and the Chipmunks. WORLD is a library that does
this: it decomposes the recording into three independent streams.
Pitch — the F0 contour. Spectral envelope — the formants that make
"ah" sound like "ah" instead of "ee." And aperiodicity — the
breath and noise. Three streams. To shift the pitch, we scale ONLY
the F0 contour, leave the envelope and aperiodicity untouched, and
ask WORLD to re-synthesize. The vowel still sounds like that
singer's "ah," but the pitch matches the guitar. The catch is that
Synthesis is the expensive step, and we want to support every guitar
note from low E to high E. So we pre-render every pitch we might
need — 37 of them, every semitone in a three-octave range — for
every grain in the bundle. Twenty grains times 37 ratios times a
second of compute per ratio. A few minutes per voice. Once.
-->

---

# Things we had to adjust to make it work on stage

The interesting bugs, in the order they bit.

<v-clicks>

- **WORLD allocates heap on every call.** Fatal on the audio thread. So we run it offline at activation time and the audio thread just picks the nearest pre-rendered buffer. Continuous portamento becomes nearest-semitone snap — fine for fretted instruments.

- **The first scene activation froze the app for 90 seconds** doing all that pre-rendering. Moved it to a background thread with a live "Loading vocals..." progress label.

- **Bake-once, load-many.** Persist the pre-render to disk, keyed by a hash of the source audio. First time per voice: ~5 minutes. Every time after: instant. Reinstall the app, the cache survives.

- **Notes kept singing forever** after you stopped playing — the grain ran out its full length regardless of your guitar. Added an amplitude envelope on the guitar input that gates the output when the string goes quiet. "Stop playing" actually stops now.

- **"Three octaves up, same note coming out."** Two compounding causes: pitch detection wasn't even running for the direct-shift path (it lived inside the vocoder branch). And the source slices were each 4 seconds long — long enough to contain two of the singer's scale notes, so a single strum played a rising line. Both fixed.

</v-clicks>

<!--
This is where the engineering got interesting. WORLD allocates heap
memory inside its synthesis call, which means we can't call it on
the audio thread — that's a hard no for real-time. So we pre-render
everything offline. Pre-rendering took ninety seconds and froze the
app, so we moved it to a background thread. It still takes minutes
on first activation, so we cache the result to disk keyed by a
content hash. Reinstall the app — cache survives. The notes kept
ringing forever because we weren't gating output on guitar input
amplitude — added that. The "same note across three octaves" bug
had TWO causes: pitch detection lived inside the vocoder branch
(didn't run for the shifter path), AND the source slices were long
enough to contain multiple sung notes. Each one took an afternoon
to find. The fixes are short. The finding-them part is the cost.
-->

---

# Where to go from here?

---
layout: center
class: text-center
---


<div class="text-8xl pt-6 ">Thank you!</div>

<div class="flex justify-center pt-12">
  <a href="https://www.linkedin.com/in/todd-b-fisher" target="_blank"
     class="flex items-center gap-4 px-8 py-4 rounded-2xl border border-gray-600 hover:border-sky-400 transition-colors !no-underline"
     style="color:inherit">
    <svg viewBox="0 0 24 24" class="w-10 h-10" fill="#4DA8F0"><path d="M20.45 20.45h-3.56v-5.57c0-1.33-.02-3.04-1.85-3.04-1.86 0-2.14 1.45-2.14 2.94v5.67H9.35V9h3.41v1.56h.05c.48-.9 1.64-1.85 3.37-1.85 3.6 0 4.27 2.37 4.27 5.46v6.28zM5.34 7.43a2.06 2.06 0 1 1 0-4.13 2.06 2.06 0 0 1 0 4.13zM7.12 20.45H3.56V9h3.56v11.45zM22.22 0H1.77C.79 0 0 .77 0 1.73v20.54C0 23.23.79 24 1.77 24h20.45c.98 0 1.78-.77 1.78-1.73V1.73C24 .77 23.2 0 22.22 0z"/></svg>
    <span class="text-2xl tracking-wide">todd-b-fisher</span>
  </a>
</div>

