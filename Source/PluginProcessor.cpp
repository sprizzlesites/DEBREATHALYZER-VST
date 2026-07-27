#include "PluginProcessor.h"
#include "PluginEditor.h"

DeBreathalyzerProcessor::DeBreathalyzerProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    sensitivityParam = apvts.getRawParameterValue (ParamIDs::sensitivity);
    reductionParam   = apvts.getRawParameterValue (ParamIDs::reduction);
    attackParam      = apvts.getRawParameterValue (ParamIDs::attack);
    releaseParam     = apvts.getRawParameterValue (ParamIDs::release);
    minLengthParam   = apvts.getRawParameterValue (ParamIDs::minLength);
    lookaheadParam   = apvts.getRawParameterValue (ParamIDs::lookahead);
    bypassParam      = apvts.getRawParameterValue (ParamIDs::bypass);
    listenParam      = apvts.getRawParameterValue (ParamIDs::listen);
}

juce::AudioProcessorValueTreeState::ParameterLayout DeBreathalyzerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::sensitivity, 1 }, "Sensitivity",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::reduction, 1 }, "Reduction",
        juce::NormalisableRange<float> (-40.0f, 0.0f, 0.1f), -18.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::attack, 1 }, "Attack",
        juce::NormalisableRange<float> (1.0f, 50.0f, 0.1f), 8.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::release, 1 }, "Release",
        juce::NormalisableRange<float> (20.0f, 500.0f, 0.1f), 150.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::minLength, 1 }, "Min Length",
        juce::NormalisableRange<float> (20.0f, 400.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::lookahead, 1 }, "Lookahead",
        juce::NormalisableRange<float> (0.0f, 20.0f, 0.1f), 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::listen, 1 }, "Listen", false));

    return { params.begin(), params.end() };
}

void DeBreathalyzerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    detector.prepare (sampleRate, samplesPerBlock);

    monoDetectionBuffer.setSize (1, samplesPerBlock);
    gainBuffer.setSize (1, samplesPerBlock);

    const int numChannels = juce::jmax (1, getTotalNumInputChannels());
    // Headroom above the parameter's 20ms max so setDelay() never clamps.
    const int maxDelaySamples = (int) std::round (sampleRate * 0.025) + 64;

    lookaheadDelays.clear();
    lookaheadDelays.resize ((size_t) numChannels);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    for (auto& d : lookaheadDelays)
    {
        d.setMaximumDelayInSamples (maxDelaySamples);
        d.prepare (spec);
    }

    currentLatencySamples = -1; // force updateDetectorParameters() to re-apply delay/latency
    updateDetectorParameters();
}

bool DeBreathalyzerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == mainOut;
}

void DeBreathalyzerProcessor::updateDetectorParameters()
{
    BreathDetector::Parameters p;
    p.sensitivity = sensitivityParam != nullptr ? sensitivityParam->load() / 100.0f : 0.5f;
    p.reductionDb = reductionParam != nullptr ? reductionParam->load() : -18.0f;
    p.attackMs    = attackParam    != nullptr ? attackParam->load()    : 8.0f;
    p.releaseMs   = releaseParam   != nullptr ? releaseParam->load()  : 150.0f;
    p.minLengthMs = minLengthParam != nullptr ? minLengthParam->load(): 100.0f;
    p.lookaheadMs = lookaheadParam != nullptr ? lookaheadParam->load(): 5.0f;

    detector.setParameters (p);

    const int newLatency = detector.getLatencySamples();
    if (newLatency != currentLatencySamples)
    {
        currentLatencySamples = newLatency;
        setLatencySamples (currentLatencySamples);
        for (auto& d : lookaheadDelays)
            d.setDelay ((float) currentLatencySamples);
    }
}

void DeBreathalyzerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = getTotalNumInputChannels();

    updateDetectorParameters();

    // Mono detection signal from the dry, just-arrived audio (not delayed).
    monoDetectionBuffer.setSize (1, numSamples, false, false, true);
    monoDetectionBuffer.clear();
    for (int ch = 0; ch < numChannels; ++ch)
        monoDetectionBuffer.addFrom (0, 0, buffer, ch, 0, numSamples,
                                      1.0f / (float) juce::jmax (1, numChannels));

    gainBuffer.setSize (1, numSamples, false, false, true);
    detector.process (monoDetectionBuffer.getReadPointer (0), gainBuffer.getWritePointer (0), numSamples);

    const bool bypassed  = bypassParam != nullptr && bypassParam->load() > 0.5f;
    const bool listening = listenParam != nullptr && listenParam->load() > 0.5f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& delay = lookaheadDelays[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = data[i];
            delay.pushSample (0, dry);
            const float delayed = delay.popSample (0);

            const float g = bypassed ? 1.0f : gainBuffer.getSample (0, i);
            // Listen mode solos what's being removed (1-g) instead of what's kept (g).
            data[i] = listening ? delayed * (1.0f - g) : delayed * g;
        }
    }

    const float peak = monoDetectionBuffer.getMagnitude (0, 0, numSamples);
    float gainSum = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        gainSum += gainBuffer.getSample (0, i);
    scopeBuffer.push (peak, numSamples > 0 ? gainSum / (float) numSamples : 1.0f);
}

juce::AudioProcessorEditor* DeBreathalyzerProcessor::createEditor()
{
    return new DeBreathalyzerEditor (*this);
}

void DeBreathalyzerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DeBreathalyzerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DeBreathalyzerProcessor();
}
