#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/NeonLookAndFeel.h"

/** Scrolling waveform of the vocal signal with the detected/ducked regions
    tinted, plus a live gain-reduction meter. Polls the processor's
    ScopeBuffer on a timer rather than touching audio-thread state directly. */
class BreathScopeDisplay : public juce::Component,
                            private juce::Timer
{
public:
    explicit BreathScopeDisplay (DeBreathalyzerProcessor& p);
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    DeBreathalyzerProcessor& processor;
    int lastReadIndex = 0;
};

class DeBreathalyzerEditor : public juce::AudioProcessorEditor
{
public:
    explicit DeBreathalyzerEditor (DeBreathalyzerProcessor&);
    ~DeBreathalyzerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct KnobWithLabel
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (KnobWithLabel& k, const juce::String& paramID, const juce::String& text, bool pinkGlow);

    DeBreathalyzerProcessor& audioProcessor;
    neon::NeonLookAndFeel lookAndFeel;

    juce::Image platePlate;
    juce::Image logoImage;

    BreathScopeDisplay scope;

    KnobWithLabel sensitivityKnob, reductionKnob, attackKnob, releaseKnob, minLengthKnob, lookaheadKnob;

    juce::TextButton bypassButton { "BYPASS" };
    juce::TextButton listenButton { "LISTEN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment;

    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeBreathalyzerEditor)
};
