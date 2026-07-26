#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
BreathScopeDisplay::BreathScopeDisplay (DeBreathalyzerProcessor& p) : processor (p)
{
    startTimerHz (30);
}

void BreathScopeDisplay::timerCallback()
{
    repaint();
}

void BreathScopeDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient bg (neon::screenBg.brighter (0.08f), bounds.getX(), bounds.getY(),
                              neon::screenBg.darker (0.3f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRect (bounds);

    g.setColour (juce::Colours::white.withAlpha (0.04f));
    for (int i = 1; i < 4; ++i)
    {
        const float y = bounds.getY() + bounds.getHeight() * (float) i / 4.0f;
        g.drawLine (bounds.getX(), y, bounds.getRight(), y, 1.0f);
    }

    const auto& points = processor.scopeBuffer.points;
    const int numPoints = (int) points.size();
    const int head = processor.scopeBuffer.writeIndex.load (std::memory_order_acquire);

    const float midY = bounds.getCentreY();
    const float halfH = bounds.getHeight() * 0.42f;
    const int visible = juce::jmin (numPoints, juce::jmax (2, (int) bounds.getWidth()));

    auto pointAt = [&] (int i) { return points[(size_t) ((head - visible + i + numPoints * 2) % numPoints)]; };

    juce::Path fillPath, linePath;
    for (int i = 0; i < visible; ++i)
    {
        const auto pt = pointAt (i);
        const float x = bounds.getX() + bounds.getWidth() * (float) i / (float) (visible - 1);
        const float amp = juce::jlimit (0.0f, 1.0f, pt.level) * halfH;

        if (i == 0) { fillPath.startNewSubPath (x, midY - amp); linePath.startNewSubPath (x, midY - amp); }
        else        { fillPath.lineTo (x, midY - amp);          linePath.lineTo (x, midY - amp); }
    }
    for (int i = visible - 1; i >= 0; --i)
    {
        const auto pt = pointAt (i);
        const float x = bounds.getX() + bounds.getWidth() * (float) i / (float) (visible - 1);
        const float amp = juce::jlimit (0.0f, 1.0f, pt.level) * halfH;
        fillPath.lineTo (x, midY + amp);
    }
    fillPath.closeSubPath();

    float avgGain = 1.0f;
    if (visible > 0)
    {
        float sum = 0.0f;
        for (int i = 0; i < visible; ++i)
            sum += pointAt (i).gain;
        avgGain = sum / (float) visible;
    }
    const auto traceColour = neon::neonCyan.interpolatedWith (neon::neonPink,
                                                                1.0f - juce::jlimit (0.0f, 1.0f, avgGain));

    g.setColour (traceColour.withAlpha (0.18f));
    g.fillPath (fillPath);
    g.setColour (traceColour.withAlpha (0.85f));
    g.strokePath (linePath, juce::PathStrokeType (1.4f));

    const float currentGain = numPoints > 0 ? pointAt (visible - 1).gain : 1.0f;
    auto meterArea = bounds.removeFromBottom (10.0f).reduced (4.0f, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (meterArea, 3.0f);
    auto fill = meterArea.withWidth (meterArea.getWidth() * juce::jlimit (0.0f, 1.0f, currentGain));
    g.setColour (traceColour.withAlpha (0.8f));
    g.fillRoundedRectangle (fill, 3.0f);

    neon::glowRoundedRect (g, getLocalBounds().toFloat(), 6.0f, neon::neonCyan, 0.7f);
}

//==============================================================================
DeBreathalyzerEditor::DeBreathalyzerEditor (DeBreathalyzerProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), scope (p)
{
    setLookAndFeel (&lookAndFeel);

    logoImage = neon::cropTransparentBorder (
        juce::ImageFileFormat::loadFrom (DeBreathalyzerAssets::SprizzleLogo_png,
                                          (size_t) DeBreathalyzerAssets::SprizzleLogo_pngSize));

    titleLabel.setText ("DE-BREATHALYZER", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (neon::makeFont (22.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, neon::neonPink);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (scope);

    setupKnob (sensitivityKnob, ParamIDs::sensitivity, "SENSITIVITY", true);
    setupKnob (reductionKnob,   ParamIDs::reduction,   "REDUCTION",   false);
    setupKnob (attackKnob,      ParamIDs::attack,      "ATTACK",      true);
    setupKnob (releaseKnob,     ParamIDs::release,     "RELEASE",     false);
    setupKnob (minLengthKnob,   ParamIDs::minLength,   "MIN LENGTH",  true);
    setupKnob (lookaheadKnob,   ParamIDs::lookahead,   "LOOKAHEAD",   false);

    for (auto* b : { &bypassButton, &listenButton })
    {
        b->getProperties().set ("neonBlack", true);
        b->getProperties().set ("neonPink", true);
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::textColourOffId, neon::textDim);
        b->setColour (juce::TextButton::textColourOnId, neon::neonPink);
        addAndMakeVisible (*b);
    }

    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.apvts, ParamIDs::bypass, bypassButton);
    listenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.apvts, ParamIDs::listen, listenButton);

    setResizable (false, false);
    setSize (780, 520);
}

DeBreathalyzerEditor::~DeBreathalyzerEditor()
{
    setLookAndFeel (nullptr);
}

void DeBreathalyzerEditor::setupKnob (KnobWithLabel& k, const juce::String& paramID,
                                       const juce::String& text, bool pinkGlow)
{
    k.slider.getProperties().set ("glowPink", pinkGlow);
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (neon::makeFont (11.5f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, pinkGlow ? neon::neonCyan : neon::neonPink);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, paramID, k.slider);
}

void DeBreathalyzerEditor::paint (juce::Graphics& g)
{
    g.fillAll (neon::voidBg);

    auto plateBounds = getLocalBounds().toFloat().reduced (10.0f);

    if (! platePlate.isValid() || platePlate.getWidth() != (int) plateBounds.getWidth())
        platePlate = neon::makeChromePlate ((int) plateBounds.getWidth(), (int) plateBounds.getHeight());

    g.drawImage (platePlate, plateBounds);
    neon::glowRoundedRect (g, plateBounds, 14.0f, neon::neonMagenta, 0.5f);

    if (logoImage.isValid())
    {
        const float logoH = 34.0f;
        const float scale = logoH / (float) logoImage.getHeight();
        const float logoW = (float) logoImage.getWidth() * scale;
        g.drawImage (logoImage,
                      juce::Rectangle<float> (plateBounds.getRight() - logoW - 16.0f,
                                               plateBounds.getBottom() - logoH - 12.0f,
                                               logoW, logoH),
                      juce::RectanglePlacement::centred);
    }
}

void DeBreathalyzerEditor::resized()
{
    auto bounds = getLocalBounds().reduced (24, 20);

    auto top = bounds.removeFromTop (34);
    titleLabel.setBounds (top);

    bounds.removeFromTop (10);
    auto screenArea = bounds.removeFromTop (190);
    scope.setBounds (screenArea);

    bounds.removeFromTop (20);

    auto knobArea = bounds.removeFromTop (170);
    juce::Array<KnobWithLabel*> knobs { &sensitivityKnob, &reductionKnob, &attackKnob,
                                         &releaseKnob, &minLengthKnob, &lookaheadKnob };
    const int knobW = knobArea.getWidth() / knobs.size();
    for (int i = 0; i < knobs.size(); ++i)
    {
        auto area = knobArea.removeFromLeft (knobW);
        auto labelArea = area.removeFromBottom (18);
        knobs[i]->slider.setBounds (area.reduced (10));
        knobs[i]->label.setBounds (labelArea);
    }

    bounds.removeFromTop (16);
    auto buttonRow = bounds.removeFromTop (36);
    const int buttonW = 120;
    bypassButton.setBounds (buttonRow.removeFromLeft (buttonW));
    buttonRow.removeFromLeft (12);
    listenButton.setBounds (buttonRow.removeFromLeft (buttonW));
}
