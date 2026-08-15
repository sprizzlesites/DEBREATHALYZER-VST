# DeBreathalyzer

A JUCE VST3/Standalone plugin that detects and reduces breath sounds in vocal
recordings in real time.

## Why not just a noise gate

Breath noise, sibilance, plosives and quiet singing all overlap in level, so
level alone can't tell them apart — a plain threshold gate mutes any quiet
passage (including quiet sung notes) and leaves loud breaths untouched. This
plugin instead classifies short analysis frames using several features and
only ducks frames that look like a breath on all of them, the same general
approach used in the published breath-detection literature (Ruinskiy & Lavner,
*"An Effective Algorithm for Automatic Detection and Exact Demarcation of
Breath Sounds in Speech and Song Signals"*, 2007) and described by iZotope for
RX/Nectar's Breath Control (classifying by harmonic structure rather than
level, then suppressing until sung/spoken content returns).

## Detection

Every ~5.8 ms hop, a 1024-sample analysis window is scored on two independent
questions, and a frame has to pass **both**:

**1. Is this unvoiced noise at all** (rather than a sung or spoken tone)?

- **Spectral flatness** — noise is flat (geometric mean ≈ arithmetic mean);
  voiced content is peaky and harmonic.
- **Harmonicity**, the peak of the normalised autocorrelation in the vocal
  pitch range (70–500 Hz) — high peak means periodic/voiced, low means noisy.

**2. Is that noise low-frequency weighted** — a breath rather than sibilance?

This is the group that actually separates a breath from `/s/` and `/sh/`,
which are every bit as noisy and inharmonic as a breath is:

- **High-frequency energy ratio** (above 5 kHz) — by far the strongest
  discriminator, and a *necessary* condition: it gates the whole group rather
  than being averaged into it. Above 0.35 the frame is vetoed outright.
- **Spectral centroid**, and the fraction of energy in the 300 Hz – 3.5 kHz
  breath band.
- **Zero-crossing rate**, as a cheap corroborating vote.

The thresholds are calibrated against **measured frames from a real dry rap
vocal**, not synthetic noise — breath frames were located by level valleys,
independently of any spectral feature, so the calibration isn't circular:

| | breath | sibilant | voiced |
|---|---|---|---|
| hfRatio | .031 / **.074** / .124 | .022 / **.213** / .925 | .001 / **.016** / .113 |
| centroid Hz | 3589 / **3794** / 4090 | 2801 / **4394** / 7314 | 1570 / **2705** / 3997 |
| harmonicity | .233 / **.336** / .375 | .140 / **.203** / .294 | .461 / **.665** / .855 |

Two things synthetic noise got wrong: real breath is far more *periodic* than
filtered white noise (harmonicity ≈0.34, not ≈0.13 — the vocal tract resonates
it), and centroid barely separates breath from sibilance on real material
(3794 vs 4394) even though it looked decisive on synthesised signals.

The composite score is **smoothed over ~45 ms before thresholding**. Real
per-frame features jitter enough that a breath's raw score flickers either
side of the threshold, which starved the minimum-duration debounce and made
the detector miss most breaths even where every feature looked right on
average. A breath is a sustained event, so the decision is made on a sustained
measurement.

The two groups are **multiplied, not added**. Adding them lets sibilance clear
the threshold on the noise features alone — which is audible as consonants
getting chewed up. All the spectral measures are taken over a fixed analysis
band rather than out to Nyquist, so the thresholds mean the same thing at
44.1 kHz and 96 kHz; the test suite runs the whole battery at 44.1/48/96 kHz
to keep that honest.

A level gate additionally prefers frames quieter than the recent singing, with
a soft upper edge rather than a hard one. On the reference vocal, breaths sit
14–34 dB below local programme level, and this gate is what carries the
breath-vs-sibilance decision in the region where the spectral features overlap
(sibilants occur at speech level; breaths do not). Raise **Sensitivity** for
takes with unusually loud, close-mic'd breathing.

A **Min Length** debounce (default 60 ms) then has to elapse before reduction
engages, so short consonants don't false-trigger; once engaged, gain moves
smoothly (**Attack**/**Release**) toward an adjustable **Reduction** depth in
dB — never a full mute, so a de-breathed take still sounds like a breath
happened, just quieter. The default **Release** is deliberately short (80 ms):
in rap, words follow breaths immediately, and a long release drags the start of
the next word down with the breath.

An optional **Lookahead** (0–20 ms) delays the audio path relative to
detection so the duck can start smoothing in slightly ahead of a breath's
onset; the plugin reports this as processing latency via
`AudioProcessor::setLatencySamples`.

**Listen** solos what's being removed instead of what's kept, for tuning.
**Bypass** passes audio through unmodified (latency-compensated) for A/B.

See `Source/DSP/BreathDetector.h` for the implementation and rationale in
more detail.

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

JUCE is fetched via CMake `FetchContent` — nothing to install beyond CMake and
a C++17 compiler. Artefacts land in `build/DeBreathalyzer_artefacts/Release/`.

### Headless DSP test (no DAW required)

```sh
cmake -B build-test -DCMAKE_BUILD_TYPE=Release -DDeBreathalyzer_BUILD_TESTS=ON
cmake --build build-test --target DeBreathalyzerTest -j
./build-test/DeBreathalyzerTest_artefacts/Release/DeBreathalyzerTest
```

Splices synthetic voiced (harmonic) audio, breaths (low-mid filtered noise)
and sibilants (`/s/` and `/sh/`, HF-weighted noise) together and asserts that
breaths get ducked while voiced material *and sibilants* are left alone — at
44.1, 48 and 96 kHz. The sibilant cases are the important ones: a test with
only "voiced + breath" will happily pass a detector that destroys every
consonant.

### Checking it on a real take

Synthetic noise is not a substitute for a real vocal — most of the defects
found in this detector were invisible to the synthetic tests and obvious on a
real one. The offline renderer processes a file and prints every region it
ducked, so the region list can be checked against where the breaths actually
are:

```sh
cmake --build build-test --target DeBreathalyzerRender -j
ffmpeg -i take.wav -ac 1 -ar 44100 -f f32le take.f32
./build-test/DeBreathalyzerRender_artefacts/Release/DeBreathalyzerRender \
    take.f32 44100 out.f32 removed.f32
ffmpeg -f f32le -ar 44100 -ac 1 -i removed.f32 removed.wav
```

`removed.wav` is the acid test: it holds exactly what the plugin took out, so
any word or `/s/` audible in it is a false positive. Accepts
`--sensitivity/--reduction/--attack/--release/--minlength` overrides.

### Windows VST3, cross-compiled locally (no CI minutes)

```sh
scripts/build-windows.sh DeBreathalyzer
```

Cross-compiles with MinGW, verifies the VST3 actually instantiates under
Wine, and packages `dist/DeBreathalyzer-VST3-Windows-x64.zip`.

### macOS / Linux via CI

`.github/workflows/macos-vst3.yml` builds a universal macOS VST3 only
(minute-frugal); `.github/workflows/build.yml` is the full three-OS matrix.
Both gate on [pluginval](https://github.com/Tracktion/pluginval) before
publishing an artifact.
