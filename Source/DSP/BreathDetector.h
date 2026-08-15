#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

/**
    Real-time breath detection and gain-reduction ("ducking", not a hard gate)
    for a mono-summed vocal signal.

    Design notes (why this isn't just a threshold gate):

    Breath noise, unvoiced sibilance and voiced singing all sit at different,
    overlapping loudness levels, so level alone can't tell them apart — a
    simple downward expander/gate (the original DeBreathalyzer implementation
    in this repo before this rewrite) mutes ANY quiet passage, including
    quiet sung notes, and leaves loud breaths untouched. Published breath
    detectors (Ruinskiy & Lavner, "An Effective Algorithm for Automatic
    Detection and Exact Demarcation of Breath Sounds", 2007) and iZotope's
    own description of RX/Nectar's Breath Control instead classify short
    analysis frames using several spectral/temporal features and the frame's
    harmonic structure, then apply duration constraints so short transients
    (plosives, "t"/"k" stops) don't false-trigger.

    This class follows the same shape, adapted to be causal/streamable for a
    real-time plugin instead of an offline whole-file analysis:

    The features fall into two groups, and a frame must satisfy BOTH:

    1. Is this unvoiced noise at all, rather than a sung/spoken tone?
       - spectral flatness (noise is flat; voiced content is peaky/harmonic)
       - harmonicity, via the peak of the normalised autocorrelation in the
         vocal pitch range (low peak = inharmonic = noise-like)

    2. Is that noise LOW-FREQUENCY weighted, i.e. a breath rather than
       sibilance? This is the group that actually separates breath from
       "s"/"sh", which are every bit as noisy and inharmonic as a breath:
       - spectral centroid (breath ~1-2.5 kHz; /s/ ~6-8 kHz)
       - fraction of energy above 5 kHz (high for sibilance, low for
         breath), with a hard veto above 0.5
       - zero-crossing rate, as a cheap corroborating vote

    The two groups are MULTIPLIED, not added. Adding them (as an earlier
    version of this file did) lets sibilance clear the threshold on the noise
    features alone, since it scores just as high there as breath does — which
    shows up in use as consonants being chewed up.

    A level gate additionally prefers frames quieter than the recent singing,
    but its upper edge is deliberately soft: a close-mic'd breath can sit only
    a few dB below the vocal, and a hard gate there discards exactly those
    (the main source of missed breaths). A minimum continuous-detection
    duration (debounce) then has to elapse before reduction engages, which is
    what keeps short consonants from triggering it. Once engaged, gain moves
    smoothly (attack/release, in ms) toward an adjustable reduction depth in
    dB — never a full mute — so the result still sounds like a breath
    happened, just quieter, rather than an edited-out silence.
*/
class BreathDetector
{
public:
    BreathDetector();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    struct Parameters
    {
        float sensitivity = 0.5f;    // 0..1: higher catches more/quieter breaths
        float reductionDb = -18.0f;  // ducking depth applied to detected breaths
        float attackMs    = 8.0f;    // time to duck in once engaged
        float releaseMs   = 80.0f;   // time to recover once a breath ends
        float minLengthMs = 60.0f;   // continuous detection required to engage
        float lookaheadMs = 5.0f;    // reported via getLatencySamples()
    };
    void setParameters (const Parameters& newParams);

    // Feed `numSamples` of a mono detection signal (e.g. the average of the
    // input channels) and receive a per-sample gain multiplier (0..1) in
    // gainOut. gainOut is aligned so that it should be multiplied directly
    // onto a copy of the SAME input delayed by getLatencySamples() samples —
    // the caller owns that delay line; this class only decides the gain.
    void process (const float* detectionSignal, float* gainOut, int numSamples);

    int getLatencySamples() const noexcept { return lookaheadSamples; }

    // For UI metering only; safe to read from the message thread without
    // additional synchronisation (single writer, benign tearing on read).
    float getCurrentBreathScore() const noexcept { return lastScore; }
    float getCurrentGain() const noexcept { return currentGain; }

private:
    void analyseFrame();
    void updateTimeConstants();

    double sampleRate = 44100.0;
    Parameters params;

    static constexpr int fftOrder = 10;              // 1024-point analysis window
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = 256;              // ~5.8 ms hop @ 44.1 kHz

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize,
                                                  juce::dsp::WindowingFunction<float>::hann };

    std::vector<float> history;       // circular buffer of raw samples, size fftSize
    int historyWrite = 0;
    int samplesSinceHop = 0;

    std::vector<float> windowedTime;  // scratch: time-ordered copy of history, size fftSize
    std::vector<float> fftScratch;    // scratch: size fftSize * 2 for JUCE FFT
    std::vector<float> decimated;     // scratch: 2x-decimated copy for the autocorrelation

    // Slow envelope of "how loud the performance normally is", used so the
    // energy gate finds passages quieter than the singing rather than an
    // absolute level. Seeded to a plausible mid level rather than zero so a
    // breath before the first phrase can still be caught.
    float loudPassageEnvelope = 0.063f; // ~ -24 dBFS

    bool candidateActive = false;
    int candidateHopSamples = 0;
    int minLengthSamplesTarget = 0;

    float targetGain = 1.0f;
    float currentGain = 1.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;

    // The score is smoothed across hops before thresholding: real breath
    // frames jitter, and thresholding the raw per-hop score starved the
    // minimum-duration debounce so most breaths never engaged.
    float smoothedScore = 0.0f;
    float scoreSmoothCoeff = 0.0f;
    static constexpr float scoreSmoothingMs = 45.0f;

    float lastScore = 0.0f;

    int lookaheadSamples = 0;
};
