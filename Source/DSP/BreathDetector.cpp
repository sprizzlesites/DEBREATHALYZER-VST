#include "BreathDetector.h"

namespace
{
    // ---- Feature thresholds -------------------------------------------------
    // These describe the acoustics of breath vs. sibilance and are deliberately
    // NOT user-tunable: they're physics, not taste. Sensitivity moves the
    // decision threshold and the level gate, not these.

    // Every spectral measure below is taken over a FIXED analysis band rather
    // than out to Nyquist. Taking them to Nyquist would make them sample-rate
    // dependent: at 96 kHz the extra (near-empty) top octaves drag a
    // linear-bin centroid upward and dilute the energy ratios, so thresholds
    // calibrated at 44.1 kHz would quietly stop working. Nothing above 12 kHz
    // helps separate breath from sibilance anyway.
    constexpr double analysisCeilingHz = 12000.0;

    // ---- Calibrated against measured frames from a real dry rap vocal -------
    // (143 s, ~8 breaths/min located by level valleys, i.e. independently of
    // any of the spectral features below, so the calibration isn't circular.)
    // Measured p10/p50/p90, breath vs in-phrase sibilance vs voiced:
    //
    //                  breath              sibilant            voiced
    //   hfRatio        .031 / .074 / .124  .022 / .213 / .925  .001/.016/.113
    //   centroid Hz    3589 / 3794 / 4090  2801 / 4394 / 7314  1570/2705/3997
    //   flatness       .510 / .638 / .693  .428 / .581 / .732  .162/.354/.599
    //   harmonicity    .233 / .336 / .375  .140 / .203 / .294  .461/.665/.855
    //   zcr            .087 / .125 / .155  .075 / .173 / .365  .016/.063/.137
    //
    // Two lessons from real audio that synthetic noise got wrong:
    //  - real breath is FAR more periodic than filtered white noise
    //    (harmonicity ~0.34, not ~0.13) - it is resonated by the vocal tract.
    //  - centroid barely separates breath from sibilance (3794 vs 4394); on
    //    synthetic signals it looked decisive. hfRatio is the real workhorse.

    // Voicing. Breath tops out at .375 and voiced starts at .461, so this ramp
    // sits in the gap and is what actually keeps the plugin off the vocal.
    constexpr float harmFullCredit = 0.40f;
    constexpr float harmZero       = 0.54f;

    // Noisiness. Voiced median .354, breath p10 .510 - but real breath frames
    // scatter well below their own median, so the ramp starts lower than the
    // percentiles alone suggest; the level gate picks up the discrimination
    // that costs.
    constexpr float flatZero       = 0.30f;
    constexpr float flatFullCredit = 0.46f;

    // Fraction of in-band energy above 5 kHz - the single strongest
    // discriminator, and a NECESSARY condition (it multiplies the rest).
    constexpr float hfRatioFullCredit = 0.12f;   // breath p90 = .124
    constexpr float hfRatioZero       = 0.30f;
    // Above this the frame is sibilance, full stop - no score can rescue it.
    constexpr float hfRatioVeto       = 0.35f;

    // Centroid and ZCR overlap too much on real material to decide anything on
    // their own; they only trim the score once hfRatio has already passed.
    constexpr float centroidFullCreditHz = 3800.0f;
    constexpr float centroidZeroHz       = 5200.0f;
    constexpr float zcrFullCredit = 0.16f;
    constexpr float zcrZero       = 0.34f;

    // Band edges used for the energy split (Hz).
    constexpr double bandLowHz     = 300.0;    // below this: rumble/fundamental
    constexpr double bandBreathHi  = 3500.0;   // breath band upper edge
    constexpr double bandHfLo      = 5000.0;   // sibilance band lower edge
    constexpr double flatnessLoHz  = 300.0;    // flatness measured over this
    constexpr double flatnessHiHz  = 10000.0;  // range only

    // Maps x to 1.0 at or below `full`, 0.0 at or above `zero`, linear between.
    inline float rampDown (float x, float full, float zero) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (zero - x) / (zero - full));
    }

    // Maps x to 0.0 at or below `zero`, 1.0 at or above `full`.
    inline float rampUp (float x, float zero, float full) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (x - zero) / (full - zero));
    }
}

BreathDetector::BreathDetector()
{
    history.resize ((size_t) fftSize, 0.0f);
    windowedTime.resize ((size_t) fftSize, 0.0f);
    fftScratch.resize ((size_t) fftSize * 2, 0.0f);
    decimated.resize ((size_t) (fftSize / 2), 0.0f);
}

void BreathDetector::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate;
    reset();
    updateTimeConstants();
}

void BreathDetector::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    historyWrite = 0;
    samplesSinceHop = 0;
    loudPassageEnvelope = 0.063f;
    candidateHopSamples = 0;
    targetGain = 1.0f;
    currentGain = 1.0f;
    smoothedScore = 0.0f;
    lastScore = 0.0f;
}

void BreathDetector::setParameters (const Parameters& newParams)
{
    params = newParams;
    updateTimeConstants();
}

void BreathDetector::updateTimeConstants()
{
    const double sr = juce::jmax (1.0, sampleRate);

    attackCoeff  = std::exp (-1.0 / (sr * juce::jmax (0.001, (double) params.attackMs) / 1000.0));
    releaseCoeff = std::exp (-1.0 / (sr * juce::jmax (0.001, (double) params.releaseMs) / 1000.0));

    // One pole per analysis hop (the hop is the score's sample interval).
    scoreSmoothCoeff = (float) std::exp (-(double) hopSize * 1000.0
                                          / (sr * (double) scoreSmoothingMs));

    minLengthSamplesTarget = (int) std::round (sr * params.minLengthMs / 1000.0);
    lookaheadSamples = (int) std::round (sr * params.lookaheadMs / 1000.0);
}

void BreathDetector::process (const float* detectionSignal, float* gainOut, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = detectionSignal[i];

        history[(size_t) historyWrite] = x;
        historyWrite = (historyWrite + 1) % fftSize;

        if (++samplesSinceHop >= hopSize)
        {
            samplesSinceHop = 0;
            analyseFrame();
        }

        // Per-sample one-pole smoothing toward the current target gain,
        // decided once per analysis hop in analyseFrame(). Moving down
        // (toward the reduced gain) uses attack; recovering uses release.
        const float coeff = (targetGain < currentGain) ? attackCoeff : releaseCoeff;
        currentGain = targetGain + coeff * (currentGain - targetGain);

        gainOut[i] = currentGain;
    }
}

void BreathDetector::analyseFrame()
{
    // Unroll the circular history into time order (oldest..newest).
    for (int i = 0; i < fftSize; ++i)
        windowedTime[(size_t) i] = history[(size_t) ((historyWrite + i) % fftSize)];

    // --- short-time energy & zero-crossing rate over the newest hop -------
    const int hopStart = fftSize - hopSize;
    float sumSq = 0.0f;
    int zeroCrossings = 0;
    for (int i = hopStart; i < fftSize; ++i)
    {
        const float s = windowedTime[(size_t) i];
        sumSq += s * s;
        if (i > hopStart)
        {
            const float prev = windowedTime[(size_t) (i - 1)];
            if ((s >= 0.0f) != (prev >= 0.0f))
                ++zeroCrossings;
        }
    }
    const float shortTermRms = std::sqrt (sumSq / (float) hopSize);
    const float zcr = (float) zeroCrossings / (float) juce::jmax (1, hopSize - 1);

    // --- slow "loud passage" envelope, used as the energy-gate reference --
    const float riseTauHops  = 0.3f * (float) sampleRate / (float) hopSize;   // ~300ms rise
    const float fallTauHops  = 4.0f * (float) sampleRate / (float) hopSize;   // ~4s fall
    const float riseCoeff = std::exp (-1.0f / juce::jmax (1.0f, riseTauHops));
    const float fallCoeff = std::exp (-1.0f / juce::jmax (1.0f, fallTauHops));
    if (shortTermRms > loudPassageEnvelope)
        loudPassageEnvelope = shortTermRms + riseCoeff * (loudPassageEnvelope - shortTermRms);
    else
        loudPassageEnvelope = shortTermRms + fallCoeff * (loudPassageEnvelope - shortTermRms);

    // --- spectrum: centroid, band energies, flatness -----------------------
    std::fill (fftScratch.begin(), fftScratch.end(), 0.0f);
    std::copy (windowedTime.begin(), windowedTime.end(), fftScratch.begin());
    window.multiplyWithWindowingTable (fftScratch.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftScratch.data());

    const int numBins = fftSize / 2;
    const double binHz = sampleRate / (double) fftSize;

    double sumMag = 0.0, weightedFreqSum = 0.0;
    double energyTotal = 0.0, energyHigh = 0.0, energyBreathBand = 0.0;
    double flatSumLog = 0.0, flatSumMag = 0.0;
    int flatBins = 0;

    // Stop at the fixed analysis ceiling (or Nyquist, whichever is lower) so
    // every ratio below means the same thing at 44.1 kHz and at 96 kHz.
    const int ceilingBin = juce::jlimit (2, numBins,
                                          (int) std::ceil (analysisCeilingHz / binHz));

    for (int k = 1; k < ceilingBin; ++k) // skip DC
    {
        const double mag  = (double) fftScratch[(size_t) k];
        const double freq = (double) k * binHz;
        const double e    = mag * mag;

        sumMag += mag;
        weightedFreqSum += freq * mag;
        energyTotal += e;

        if (freq >= bandLowHz && freq < bandBreathHi)
            energyBreathBand += e;
        if (freq >= bandHfLo)
            energyHigh += e;

        // Flatness over a speech-relevant band only: DC/rumble and the very
        // top octave otherwise skew the geometric mean and blunt the
        // voiced/unvoiced separation.
        if (freq >= flatnessLoHz && freq <= flatnessHiHz)
        {
            flatSumMag += mag;
            flatSumLog += std::log (mag + 1.0e-9);
            ++flatBins;
        }
    }

    const float centroidHz = (float) (weightedFreqSum / (sumMag + 1.0e-12));
    const float hfRatio    = (float) (energyHigh / (energyTotal + 1.0e-12));
    const float breathBandRatio = (float) (energyBreathBand / (energyTotal + 1.0e-12));

    const double flatArith = flatSumMag / juce::jmax (1, flatBins);
    const double flatGeo   = std::exp (flatSumLog / juce::jmax (1, flatBins));
    const float spectralFlatness = (float) juce::jlimit (0.0, 1.0, flatGeo / (flatArith + 1.0e-12));

    // --- harmonicity: peak normalised autocorrelation in the vocal pitch
    //     range (70-500 Hz). High peak = periodic/voiced, low = noisy.
    //     Computed on a 2x-decimated copy: this is a coarse voicing measure,
    //     not a pitch tracker, and decimating cuts the cost ~4x (which matters
    //     at 96 kHz, where the lag search would otherwise dominate the block).
    const int decCount = fftSize / 2;
    for (int i = 0; i < decCount; ++i)
        decimated[(size_t) i] = 0.5f * (windowedTime[(size_t) (i * 2)]
                                        + windowedTime[(size_t) (i * 2 + 1)]);

    const double decRate = sampleRate * 0.5;
    const int minLag = juce::jmax (1, (int) std::round (decRate / 500.0));
    const int maxLag = juce::jmin (decCount - 1, (int) std::round (decRate / 70.0));

    double acf0 = 0.0;
    for (int i = 0; i < decCount; ++i)
        acf0 += (double) decimated[(size_t) i] * decimated[(size_t) i];

    double peakAcf = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        const int count = decCount - lag;
        for (int i = 0; i < count; ++i)
            sum += (double) decimated[(size_t) i] * decimated[(size_t) (i + lag)];

        peakAcf = juce::jmax (peakAcf, sum / (acf0 + 1.0e-12));
    }
    const float harmonicity = (float) juce::jlimit (0.0, 1.0, peakAcf);

    // --- score -------------------------------------------------------------
    // Two independent questions, multiplied - a frame must pass BOTH:
    //
    //   1. noisiness   - is this unvoiced noise rather than a sung/spoken tone?
    //                    (flatness + inharmonicity)
    //   2. breathShape - is that noise LOW-FREQUENCY weighted, i.e. breath
    //                    rather than sibilance? (centroid + HF ratio + ZCR)
    //
    // Adding these instead of multiplying was the original mistake: sibilance
    // scores just as high as breath on noisiness, so an additive score let
    // /s/ and /esh/ clear the threshold on the noise terms alone.
    // Both halves of "is this noise" must agree - MULTIPLIED, not averaged.
    // Averaging let a voiced frame (flatness .354, harmonicity .665) reach
    // ~0.35, which on real material sat right on the decision threshold: the
    // detector was a hair away from ducking the vocal itself.
    const float noisiness = rampDown (harmonicity, harmFullCredit, harmZero)
                          * rampUp (spectralFlatness, flatZero, flatFullCredit);

    const float centroidScore = rampDown (centroidHz, centroidFullCreditHz, centroidZeroHz);
    const float hfScore       = rampDown (hfRatio, hfRatioFullCredit, hfRatioZero);
    const float zcrScore      = rampDown (zcr, zcrFullCredit, zcrZero);

    // hfRatio gates the whole shape term rather than being averaged into it:
    // on real audio it is the only feature that reliably separates breath from
    // sibilance, so no amount of agreement from the weaker features should be
    // able to outvote it. Centroid and ZCR only trim what hfRatio allows.
    const float breathShape = hfScore * (0.60f + 0.40f * (0.60f * centroidScore
                                                          + 0.40f * zcrScore));

    // A breath also has to actually have energy in the breath band; a frame
    // that is all rumble (or all hiss) is not one. (Breath p10 = .354.)
    const float bandScore = juce::jlimit (0.0f, 1.0f, breathBandRatio / 0.30f);

    // --- level gate --------------------------------------------------------
    // Only passages quieter than the recent singing are plausible breaths, but
    // the upper edge is SOFT: a close-mic'd breath can sit only a few dB below
    // the vocal, and a hard gate there silently discarded exactly those (the
    // main source of missed breaths). Voiced material is already excluded by
    // `noisiness`, so a permissive level gate costs little.
    const float shortTermDb = juce::Decibels::gainToDecibels (shortTermRms, -100.0f);
    const float loudDb = juce::Decibels::gainToDecibels (loudPassageEnvelope, -100.0f);
    const float ceilingDb = loudDb - juce::jmap (params.sensitivity, 0.0f, 1.0f, 14.0f, 2.0f);
    constexpr float floorDb = -68.0f;
    const float riseGate = juce::jlimit (0.0f, 1.0f, (shortTermDb - floorDb) / 6.0f);
    const float fallGate = juce::jlimit (0.0f, 1.0f, (ceilingDb - shortTermDb) / 10.0f);
    const float energyGate = riseGate * (0.30f + 0.70f * fallGate);

    float finalScore = noisiness * breathShape * bandScore * energyGate;

    // Hard sibilance veto: nothing with this much high-frequency energy is a
    // breath, whatever the other features say.
    if (hfRatio >= hfRatioVeto)
        finalScore = 0.0f;

    // Smooth the score across hops before thresholding. Measured on real
    // vocals, the per-frame features jitter enough that a breath's raw score
    // flickers either side of the threshold - so the minimum-duration debounce
    // below almost never accumulated and most breaths were simply missed, even
    // where every individual feature looked right on average. A breath is a
    // sustained event; the decision should be made on a sustained measurement.
    smoothedScore = finalScore + scoreSmoothCoeff * (smoothedScore - finalScore);
    lastScore = smoothedScore;

    const float threshold = juce::jmap (params.sensitivity, 0.0f, 1.0f, 0.55f, 0.20f);
    const bool detectedThisHop = smoothedScore >= threshold;

    // Minimum-duration debounce on the way IN only; the release ramp (in
    // process(), per-sample) handles smoothing on the way out. A leaky
    // integrator rather than a hard reset: breath frames are noisy and the
    // score jitters hop to hop, so one stray miss inside an otherwise
    // continuous breath shouldn't discard all progress toward the minimum
    // length. The leak is faster than the fill, so a sibilant that only
    // flickers into detection never accumulates enough to engage.
    if (detectedThisHop)
        candidateHopSamples += hopSize;
    else
        candidateHopSamples = juce::jmax (0, candidateHopSamples - hopSize * 3);

    const bool engage = candidateHopSamples >= minLengthSamplesTarget;
    targetGain = engage ? juce::Decibels::decibelsToGain (params.reductionDb) : 1.0f;
}
