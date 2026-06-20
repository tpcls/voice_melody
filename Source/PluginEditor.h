#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class VoiceMelodyAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit VoiceMelodyAudioProcessorEditor (VoiceMelodyAudioProcessor&);
    ~VoiceMelodyAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void addSlider (juce::Slider&, juce::Label&, const juce::String&);

    VoiceMelodyAudioProcessor& processor;
    juce::Slider gainSlider;
    juce::Slider thresholdSlider;
    juce::Slider confidenceSlider;
    juce::Slider transposeSlider;
    juce::Slider velocitySlider;
    juce::Slider smoothingSlider;
    juce::Label gainLabel;
    juce::Label thresholdLabel;
    juce::Label confidenceLabel;
    juce::Label transposeLabel;
    juce::Label velocityLabel;
    juce::Label smoothingLabel;
    juce::Label statusLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment;
    std::unique_ptr<SliderAttachment> confidenceAttachment;
    std::unique_ptr<SliderAttachment> transposeAttachment;
    std::unique_ptr<SliderAttachment> velocityAttachment;
    std::unique_ptr<SliderAttachment> smoothingAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceMelodyAudioProcessorEditor)
};
