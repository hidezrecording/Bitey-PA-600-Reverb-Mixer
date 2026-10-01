# Bitey PA-600 — reverb mixer plugin

**The pitch:** the sound of Nathan Sabatino's records — Dr. Dog (*Be the Void*,
*B-Room*, *The Psychedelic Swamp*), Flaming Lips, Michael Nau, Wild Child —
in a plugin. A mixer-style insert: two tube channel strips, tape slap, and a
tube-driven spring reverb, aimed at indie/psych-rock/lo-fi producers who want
"that" warm, gritty, slightly-unhinged character without a rack of gear.

Ported from Nathan's working browser prototype (Gemini Studio, Web Audio) to
a framework-free C++17 DSP core, wrapped for VST3/AU/Standalone via JUCE.

## What it is

One stereo insert, mixer-style:

- **2 input channels** — Level, Pad (+10/+4/0/-10 dB), 96 Hz low-cut, Low/High
  EQ, FX (reverb) send. Asymmetric tube-style preamp per channel.
- **Tape slap** — 7.5/15/30 IPS, 1/4"/1/2"/1" tape size, mix. Single-repeat
  echo with speed-dependent delay, flutter, saturation, and dark filtering.
- **Spring reverb** — Drive, Contour, Time, Return. Deterministic synthesized
  spring impulse (seeded — nulls on re-render) through a tube driver stage
  and a 1108/1176-style return amp, via partitioned convolution.
- **Master** — Low/Mid/High EQ with 0.7/1.0/1.4 kHz mid select, Main level,
  global dry/wet Mix, Phase invert, Power (bypass).

Signal chain per the prototype: channel strips → tape slap → spring reverb →
Scully-style line amp → master EQ → bus saturation → brickwall limiter →
dry/wet mix. 4x-oversampled waveshapers throughout; analog-style noise floor.

Ships with three presets named after the records — **this is the marketing**:
- **Be the Void** — drums-forward grit
- **Psychedelic Swamp** — swampy tape + long dark spring
- **B-Room** — cleaner, roomier glue

## Repository layout

```
bitey/
├── dsp/            # BiteyDsp.h/.cpp — the entire sound, framework-free C++17.
│                   #   No JUCE/Steinberg deps: unit-testable, portable to
│                   #   iPlug2, CLAP, or anything else.
├── tests/          # test_dsp.cpp — invariant checks + renders demo/*.wav
│                   #   build: g++ -std=c++17 -O2 -Wall tests/test_dsp.cpp dsp/BiteyDsp.cpp -o build/test_dsp
├── juce/           # VST3/AU/Standalone wrapper (CMake, JUCE 8 via FetchContent)
├── demo/           # dry.wav / wet.wav — rendered before/after (synthetic loop)
├── prototype/     # Nathan's original browser prototype (Gemini Studio / Web
│                   # Audio) — the reference implementation this port follows.
├── .github/workflows/build.yml  # CI: macOS (VST3+AU, arm64+x86_64) + Windows (VST3)
└── README.md
```

## Verified so far

`tests/test_dsp` (12 tests, all passing):
- No NaN/Inf at default and extreme settings; output hard-limited to ±0.99
  (post-clip reconstruction overshoot is caught by a final safety clip)
- Bypass (power off) is bit-identical to dry, delayed exactly 64 samples
- Mix at 0 == dry; reported latency to the host is 64 samples (direct path),
  matching the prototype's dry-path compensation
- Reverb tail behaves (isolated -40 dB tail ~1.5 s at default Time 5);
  Return at 0 kills it; Time changes rebuild the IR without exploding
- Tape slap lands at 136.5 ms against the 134 ms target @ 7.5 IPS
- Level knob measurably drives the preamp; output deterministic across
  instances (seeded spring IR)
- Partitioned convolver is bit-accurate vs naive time-domain convolution;
  oversampled shapers are transparent with a linear curve; shelving/HP
  filters verified in isolation

The JUCE wrapper (parameters, DAW I/O, functional GUI, presets) is written
but **not yet compiled** — it needs a Mac or Windows machine (or Linux with
JUCE's GUI dev libraries) to build. The DSP core compiles clean with
`g++ -std=c++17 -Wall`.

Known limitations:
- Changing Reverb Time rebuilds the spring IR (FFTs + allocations) on the
  audio thread — same as the browser prototype, but a background rebuild with
  crossfade would be the proper retail fix.
- The functional GUI is a placeholder; the visual port of the browser UI
  (custom knobs, VU meters, artwork) is still to do.

## To finish before selling

1. **Build & test** VST3/AU on Mac (Intel + Apple Silicon) and VST3 on Windows,
   in real DAWs (Logic, Ableton, Reaper). Needs repo access (currently 404).
2. **Visual GUI port** from the browser prototype's App.tsx.
3. **Presets**: expand to 15–20, tuned on real multitracks, ideally with a
   couple of Nathan's artist friends beta-testing.
4. **Demo video**: before/after on drums, vocals, full mix — 60–90 seconds.
5. **Copy protection**: keep it simple (license-key file). Fancy DRM costs more
   than it saves at this scale.

## Selling it — the business checklist

- **Framework license.** JUCE 8 is dual-licensed AGPLv3 *or* commercial. A
  closed-source paid plugin needs a commercial tier (verify current terms at
  juce.com). **Alternative:** iPlug2 is free/permissive and the DSP core has
  zero framework dependency, so switching wrappers is cheap.
- **Code signing.** macOS: Apple Developer Program ($99/yr) for notarization —
  without it, AU/VST3 get blocked or scary warnings on modern macOS. Windows:
  a code-signing certificate (~$200–500/yr) to avoid SmartScreen blocks.
- **Where to sell.** Own site + a merchant of record (Lemon Squeezy, Gumroad)
  that handles VAT/sales tax — simplest. Marketplaces (Plugin Boutique,
  ADSR) bring discovery but take ~30%+.
- **Price.** Character plugins typically land $49–$99; intro pricing at $39–49
  with a launch window works well for a first product from an indie name.
- **The actual marketing asset** is Nathan: "1B+ streams of credits, now in a
  box." Artist-quote presets ("Michael Nau's vocal chain") would be gold.

## Open questions for Nathan

- JUCE (paid tiers) vs iPlug2 (free) for the release wrapper?
- Repo access: `hidezrecording/Bitey-PA-600-Reverb-Mixer` 404s — private or
  unpushed?
