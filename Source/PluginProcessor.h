#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"

class VoiceMelodyAudioProcessor final : public juce::AudioProcessor
{
public:
    VoiceMelodyAudioProcessor();
    ~VoiceMelodyAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() { return parameters; }
    float getLastFrequency() const { return lastFrequency.load(); }
    int getLastMidiNote() const { return lastMidiNote.load(); }
    bool getGateOpen() const { return gateOpen.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static int frequencyToMidiNote (float frequencyHz);

    juce::AudioProcessorValueTreeState parameters;
    PitchDetector pitchDetector;
    int activeNote = -1;
    int stableNote = -1;
    int stableCount = 0;
    std::atomic<float> lastFrequency { 0.0f };
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<bool> gateOpen { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceMelodyAudioProcessor)
};
