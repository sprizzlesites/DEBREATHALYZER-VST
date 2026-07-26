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

      - short-time energy (RMS, relative to a slow-moving "loud passage"
        envelope so only passages quieter than the singing are candidates)
      - zero-crossing rate (breath sits in a mid ZCR band: above voiced
        vowels, below strong fricative/sibilance ZCR)
      - spectral flatness (breath/noise is close to white/flat; voiced
        content is peaky/harmonic, i.e. NOT flat)
      - harmonicity, via the peak of the normalised autocorrelation in the
        vocal pitch range (low peak = inharmonic = breath-like)

    The four are combined into a 0..1 "breathiness score" and compared
    against a threshold derived from the Sensitivity parameter. A minimum
    continuous-detection duration (debounce) has to elapse before the
    reduction actually engages, which is what keeps short consonants from
    triggering it. Once engaged, gain moves smoothly (attack/release, in ms)
    toward an adjustable reduction depth in dB — never a full mute — so the
    result still sounds like a breath happened, just quieter, rather than an
    edited-out silence.
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
        float releaseMs   = 150.0f;  // time to recover once a breath ends
        float minLengthMs = 50.0f;   // continuous detection required to engage
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

    float lastScore = 0.0f;

    int lookaheadSamples = 0;
};
