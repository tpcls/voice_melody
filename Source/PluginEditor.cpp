#include "PluginEditor.h"

namespace
{
constexpr int margin = 20;
constexpr int rowHeight = 58;
}

VoiceMelodyAudioProcessorEditor::VoiceMelodyAudioProcessorEditor (VoiceMelodyAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    addSlider (gainSlider, gainLabel, "Input Gain");
    addSlider (thresholdSlider, thresholdLabel, "Gate Threshold");
    addSlider (confidenceSlider, confidenceLabel, "Pitch Confidence");
    addSlider (transposeSlider, transposeLabel, "Transpose");
    addSlider (velocitySlider, velocityLabel, "Velocity");
    addSlider (smoothingSlider, smoothingLabel, "Note Smoothing");

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    statusLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    addAndMakeVisible (statusLabel);

    auto& state = processor.getState();
    gainAttachment = std::make_unique<SliderAttachment> (state, "gain", gainSlider);
    thresholdAttachment = std::make_unique<SliderAttachment> (state, "threshold", thresholdSlider);
    confidenceAttachment = std::make_unique<SliderAttachment> (state, "confidence", confidenceSlider);
    transposeAttachment = std::make_unique<SliderAttachment> (state, "transpose", transposeSlider);
    velocityAttachment = std::make_unique<SliderAttachment> (state, "velocity", velocitySlider);
    smoothingAttachment = std::make_unique<SliderAttachment> (state, "smoothing", smoothingSlider);

    setSize (520, 470);
    startTimerHz (15);
}

void VoiceMelodyAudioProcessorEditor::addSlider (juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 24);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.attachToComponent (&slider, true);
    addAndMakeVisible (label);
}

void VoiceMelodyAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff12151f));
    g.setColour (juce::Colour (0xff7bdff2));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("Voice Melody", margin, 14, getWidth() - margin * 2, 36, juce::Justification::centredLeft);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Sing or hum into an audio track, then route this plugin's MIDI output to an instrument track.",
                margin, 52, getWidth() - margin * 2, 24, juce::Justification::centredLeft);
}

void VoiceMelodyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);
    area.removeFromTop (78);
    statusLabel.setBounds (area.removeFromTop (44));
    area.removeFromTop (12);

    auto placeSlider = [&area] (juce::Slider& slider)
    {
        slider.setBounds (area.removeFromTop (rowHeight).withTrimmedLeft (140));
    };

    placeSlider (gainSlider);
    placeSlider (thresholdSlider);
    placeSlider (confidenceSlider);
    placeSlider (transposeSlider);
    placeSlider (velocitySlider);
    placeSlider (smoothingSlider);
}

void VoiceMelodyAudioProcessorEditor::timerCallback()
{
    const auto frequency = processor.getLastFrequency();
    const auto note = processor.getLastMidiNote();

    if (processor.getGateOpen() && note >= 0)
        statusLabel.setText ("Detected: " + juce::String (frequency, 1) + " Hz  →  MIDI note " + juce::String (note), juce::dontSendNotification);
    else
        statusLabel.setText ("Listening... sing a clear monophonic melody", juce::dontSendNotification);
}
