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

Every ~5.8 ms hop, a 1024-sample analysis window is scored on:

- **Short-time energy**, gated against a slow "how loud is this performance"
  envelope — only passages clearly quieter than the recent singing (but still
  above the noise floor) are plausible breath candidates.
- **Zero-crossing rate**, using a triangular membership band: breath sits
  above voiced vowels and below strong fricatives/sibilance, so both extremes
  score low.
- **Spectral flatness**, from an FFT magnitude spectrum — breath/noise is
  close to flat (geometric mean ≈ arithmetic mean); voiced content is peaky.
- **Harmonicity**, the peak of the normalised autocorrelation in the vocal
  pitch range (70–500 Hz) — high peak means periodic/voiced, low peak means
  inharmonic/noisy.

The four combine into a 0–1 breathiness score compared against a threshold set
by **Sensitivity**. A **Min Length** debounce (default 50 ms) has to elapse
before the reduction engages, so short consonants don't false-trigger; once
engaged, gain moves smoothly (**Attack**/**Release**) toward an adjustable
**Reduction** depth in dB — never a full mute, so a de-breathed take still
sounds like a breath happened, just quieter.

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

Feeds synthetic voiced (harmonic) audio with a synthetic breath (filtered
noise) spliced in, and asserts the voiced sections stay untouched while the
breath is ducked.

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
