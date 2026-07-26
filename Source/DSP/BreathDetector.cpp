#include "BreathDetector.h"

BreathDetector::BreathDetector()
{
    history.resize ((size_t) fftSize, 0.0f);
    windowedTime.resize ((size_t) fftSize, 0.0f);
    fftScratch.resize ((size_t) fftSize * 2, 0.0f);
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
    candidateActive = false;
    candidateHopSamples = 0;
    targetGain = 1.0f;
    currentGain = 1.0f;
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

    // --- spectral flatness & (unused) centroid via FFT magnitude spectrum -
    std::fill (fftScratch.begin(), fftScratch.end(), 0.0f);
    std::copy (windowedTime.begin(), windowedTime.end(), fftScratch.begin());
    window.multiplyWithWindowingTable (fftScratch.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftScratch.data());

    const int numBins = fftSize / 2;
    double sumMag = 0.0, sumLogMag = 0.0;
    int usedBins = 0;
    for (int k = 1; k < numBins; ++k) // skip DC
    {
        const double mag = (double) fftScratch[(size_t) k];
        sumMag += mag;
        sumLogMag += std::log (mag + 1.0e-9);
        ++usedBins;
    }
    const double arithMean = sumMag / juce::jmax (1, usedBins);
    const double geoMean = std::exp (sumLogMag / juce::jmax (1, usedBins));
    const float spectralFlatness = (float) juce::jlimit (0.0, 1.0, geoMean / (arithMean + 1.0e-9));

    // --- harmonicity: peak normalised autocorrelation in the vocal pitch
    //     range (70-500 Hz). High peak = periodic/voiced, low peak = noisy.
    const int minLag = juce::jmax (1, (int) std::round (sampleRate / 500.0));
    const int maxLag = juce::jmin (fftSize - 1, (int) std::round (sampleRate / 70.0));

    double acf0 = 0.0;
    for (int i = 0; i < fftSize; ++i)
        acf0 += (double) windowedTime[(size_t) i] * windowedTime[(size_t) i];

    double peakAcf = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        const int count = fftSize - lag;
        for (int i = 0; i < count; ++i)
            sum += (double) windowedTime[(size_t) i] * windowedTime[(size_t) (i + lag)];

        const double normalised = sum / (acf0 + 1.0e-9);
        peakAcf = juce::jmax (peakAcf, normalised);
    }
    const float harmonicity = (float) juce::jlimit (0.0, 1.0, peakAcf);

    // --- combine into a 0..1 breathiness score -----------------------------
    const float flatnessScore    = spectralFlatness;
    const float harmonicityScore = 1.0f - harmonicity;

    // ZCR membership: breath sits between voiced vowels (low ZCR) and strong
    // fricatives/sibilance (very high ZCR) - a triangular band gives partial
    // credit near the centre and excludes both extremes.
    constexpr float zcrLo = 0.02f, zcrCentre = 0.15f, zcrHi = 0.42f;
    float zcrScore = 0.0f;
    if (zcr > zcrLo && zcr < zcrHi)
        zcrScore = (zcr <= zcrCentre) ? (zcr - zcrLo) / (zcrCentre - zcrLo)
                                       : (zcrHi - zcr) / (zcrHi - zcrCentre);

    const float compositeScore = juce::jlimit (0.0f, 1.0f,
        0.40f * flatnessScore + 0.35f * harmonicityScore + 0.25f * zcrScore);

    // Energy gate: only passages clearly quieter than the recent loud
    // singing (but still above the noise floor) are plausible breaths. The
    // margin/taper are wide because real breaths range from almost as loud
    // as the singing to barely audible.
    const float shortTermDb = juce::Decibels::gainToDecibels (shortTermRms, -100.0f);
    const float loudDb = juce::Decibels::gainToDecibels (loudPassageEnvelope, -100.0f);
    const float ceilingDb = loudDb - juce::jmap (params.sensitivity, 0.0f, 1.0f, 12.0f, 2.0f);
    constexpr float floorDb = -65.0f;
    constexpr float edgeDb  = 6.0f;
    const float riseGate = juce::jlimit (0.0f, 1.0f, (shortTermDb - floorDb) / edgeDb);
    const float fallGate = juce::jlimit (0.0f, 1.0f, (ceilingDb - shortTermDb) / edgeDb);
    const float energyGate = riseGate * fallGate;

    const float finalScore = compositeScore * energyGate;
    lastScore = finalScore;

    const float threshold = juce::jmap (params.sensitivity, 0.0f, 1.0f, 0.70f, 0.30f);
    const bool detectedThisHop = finalScore >= threshold;

    // Minimum-duration debounce on the way IN only; the release ramp (above,
    // per-sample) already handles smoothing on the way out. A leaky integrator
    // rather than a hard reset: breath frames are noisy and the score jitters
    // hop to hop, so one stray miss inside an otherwise-continuous breath
    // shouldn't throw away all accumulated progress toward the minimum length.
    if (detectedThisHop)
        candidateHopSamples += hopSize;
    else
        candidateHopSamples = juce::jmax (0, candidateHopSamples - hopSize * 3);

    const bool engage = candidateHopSamples >= minLengthSamplesTarget;
    targetGain = engage ? juce::Decibels::decibelsToGain (params.reductionDb) : 1.0f;
}
