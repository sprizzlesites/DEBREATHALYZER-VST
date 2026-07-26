#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/BreathDetector.h"

namespace ParamIDs
{
    static const juce::String sensitivity { "sensitivity" };
    static const juce::String reduction   { "reduction" };
    static const juce::String attack      { "attack" };
    static const juce::String release     { "release" };
    static const juce::String minLength   { "minLength" };
    static const juce::String lookahead   { "lookahead" };
    static const juce::String bypass      { "bypass" };
    static const juce::String listen      { "listen" };
}

/** A small ring buffer the audio thread writes into and the editor polls on
    a timer, for the scrolling waveform / breath-highlight display. One
    entry per processBlock call; the editor doesn't need per-sample data. */
struct ScopeBuffer
{
    static constexpr int size = 1024;

    struct Point { float level = 0.0f; float gain = 1.0f; };
    std::array<Point, size> points {};
    std::atomic<int> writeIndex { 0 };

    void push (float level, float gain) noexcept
    {
        const int idx = writeIndex.load (std::memory_order_relaxed);
        points[(size_t) idx] = { level, gain };
        writeIndex.store ((idx + 1) % size, std::memory_order_release);
    }
};

class DeBreathalyzerProcessor : public juce::AudioProcessor
{
public:
    DeBreathalyzerProcessor();
    ~DeBreathalyzerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    ScopeBuffer scopeBuffer;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateDetectorParameters();

    BreathDetector detector;
    juce::AudioBuffer<float> monoDetectionBuffer;
    juce::AudioBuffer<float> gainBuffer;

    // Per-channel lookahead delay so the audio the gain is applied to lines
    // up with the (causal, backward-looking) analysis that decided the gain.
    std::vector<juce::dsp::DelayLine<float>> lookaheadDelays;
    int currentLatencySamples = 0;

    std::atomic<float>* sensitivityParam = nullptr;
    std::atomic<float>* reductionParam   = nullptr;
    std::atomic<float>* attackParam      = nullptr;
    std::atomic<float>* releaseParam     = nullptr;
    std::atomic<float>* minLengthParam   = nullptr;
    std::atomic<float>* lookaheadParam   = nullptr;
    std::atomic<float>* bypassParam      = nullptr;
    std::atomic<float>* listenParam      = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeBreathalyzerProcessor)
};
