// Headless smoke test for BreathDetector: verifies sung/voiced passages are
// left alone and an interposed breath gets ducked, with no DAW required.
// Build/run with -DDeBreathalyzer_BUILD_TESTS=ON.

#include "DSP/BreathDetector.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace
{
    // A few harmonics of a low voice fundamental - strongly periodic, peaky
    // spectrum, low ZCR: everything a breath is not.
    std::vector<float> makeVoiced (double sampleRate, int numSamples, float targetRms)
    {
        std::vector<float> out ((size_t) numSamples);
        constexpr double fundamental = 150.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double t = (double) i / sampleRate;
            float s = (float) std::sin (2.0 * juce::MathConstants<double>::pi * fundamental * t);
            s += 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * fundamental * 2.0 * t);
            s += 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * fundamental * 3.0 * t);
            out[(size_t) i] = s;
        }

        double sumSq = 0.0;
        for (auto v : out) sumSq += (double) v * v;
        const float measured = (float) std::sqrt (sumSq / (double) numSamples);
        const float scale = targetRms / juce::jmax (1.0e-9f, measured);
        for (auto& v : out) v *= scale;
        return out;
    }

    // Lowpass-filtered noise - broadband/inharmonic like breath turbulence,
    // but rolled off the way real breath noise is (most energy below a few
    // kHz), not full-bandwidth white noise or a bright fricative hiss.
    std::vector<float> makeBreath (int numSamples, float targetRms, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> out ((size_t) numSamples);
        float lp = 0.0f;
        constexpr float a = 0.35f; // one-pole lowpass, cutoff ~3kHz @ 44.1kHz
        for (int i = 0; i < numSamples; ++i)
        {
            lp += a * (dist (rng) - lp);
            out[(size_t) i] = lp;
        }

        double sumSq = 0.0;
        for (auto v : out) sumSq += (double) v * v;
        const float measured = (float) std::sqrt (sumSq / (double) numSamples);
        const float scale = targetRms / juce::jmax (1.0e-9f, measured);
        for (auto& v : out) v *= scale;
        return out;
    }

    float averageGain (const std::vector<float>& gains, int from, int to)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i)
            sum += gains[(size_t) i];
        return (float) (sum / (double) (to - from));
    }
}

int main()
{
    const double sampleRate = 44100.0;

    BreathDetector detector;
    detector.prepare (sampleRate, 512);
    detector.setParameters ({}); // defaults

    const int voicedLen = (int) (sampleRate * 1.0);
    const int breathLen = (int) (sampleRate * 0.4);

    auto voiced1 = makeVoiced (sampleRate, voicedLen, 0.25f);
    auto breath  = makeBreath (breathLen, 0.063f, 1u);
    auto voiced2 = makeVoiced (sampleRate, voicedLen, 0.25f);

    std::vector<float> input;
    input.insert (input.end(), voiced1.begin(), voiced1.end());
    input.insert (input.end(), breath.begin(), breath.end());
    input.insert (input.end(), voiced2.begin(), voiced2.end());

    std::vector<float> gains (input.size());
    detector.process (input.data(), gains.data(), (int) input.size());

    bool ok = true;
    const int warmup = (int) (sampleRate * 0.1);

    const float voiced1Gain = averageGain (gains, warmup, voicedLen);
    const float voiced2Gain = averageGain (gains, voicedLen + breathLen + warmup, (int) input.size());
    std::cout << "voiced #1 avg gain = " << voiced1Gain << " (expect > 0.9)\n";
    std::cout << "voiced #2 avg gain = " << voiced2Gain << " (expect > 0.9)\n";
    if (voiced1Gain < 0.9f) { std::cerr << "FAIL: voiced section 1 was ducked\n"; ok = false; }
    if (voiced2Gain < 0.9f) { std::cerr << "FAIL: voiced section 2 was ducked\n"; ok = false; }

    const int breathBackHalfStart = voicedLen + breathLen / 2;
    const int breathEnd = voicedLen + breathLen;
    const float breathGain = averageGain (gains, breathBackHalfStart, breathEnd);
    std::cout << "breath back-half avg gain = " << breathGain << " (expect < 0.4)\n";
    if (breathGain > 0.4f) { std::cerr << "FAIL: breath was not sufficiently ducked\n"; ok = false; }

    std::cout << (ok ? "BreathDetectorTest: PASSED\n" : "BreathDetectorTest: FAILED\n");
    return ok ? 0 : 1;
}
