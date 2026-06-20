#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto gainId = "gain";
constexpr auto thresholdId = "threshold";
constexpr auto confidenceId = "confidence";
constexpr auto transposeId = "transpose";
constexpr auto velocityId = "velocity";
constexpr auto smoothingId = "smoothing";
}

VoiceMelodyAudioProcessor::VoiceMelodyAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout VoiceMelodyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> (gainId, "Input Gain", juce::NormalisableRange<float> (0.1f, 4.0f, 0.01f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (thresholdId, "Gate Threshold", juce::NormalisableRange<float> (0.002f, 0.08f, 0.001f), 0.012f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (confidenceId, "Pitch Confidence", juce::NormalisableRange<float> (0.2f, 0.95f, 0.01f), 0.72f));
    params.push_back (std::make_unique<juce::AudioParameterInt> (transposeId, "Transpose", -24, 24, 0));
    params.push_back (std::make_unique<juce::AudioParameterInt> (velocityId, "Velocity", 1, 127, 96));
    params.push_back (std::make_unique<juce::AudioParameterInt> (smoothingId, "Note Smoothing", 1, 8, 3));
    return { params.begin(), params.end() };
}

void VoiceMelodyAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pitchDetector.prepare (sampleRate, samplesPerBlock);
    activeNote = -1;
    stableNote = -1;
    stableCount = 0;
}

void VoiceMelodyAudioProcessor::releaseResources()
{
    pitchDetector.reset();
}

bool VoiceMelodyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainInput = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    return mainInput == mainOutput && (mainInput == juce::AudioChannelSet::mono() || mainInput == juce::AudioChannelSet::stereo());
}

int VoiceMelodyAudioProcessor::frequencyToMidiNote (float frequencyHz)
{
    return juce::roundToInt (69.0f + 12.0f * std::log2 (frequencyHz / 440.0f));
}

void VoiceMelodyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    const auto gain = parameters.getRawParameterValue (gainId)->load();
    buffer.applyGain (gain);

    const auto result = pitchDetector.processBlock (buffer);
    const auto threshold = parameters.getRawParameterValue (thresholdId)->load();
    const auto minConfidence = parameters.getRawParameterValue (confidenceId)->load();
    const auto transpose = static_cast<int> (parameters.getRawParameterValue (transposeId)->load());
    const auto velocity = static_cast<juce::uint8> (parameters.getRawParameterValue (velocityId)->load());
    const auto smoothing = static_cast<int> (parameters.getRawParameterValue (smoothingId)->load());

    auto nextNote = -1;
    if (result.has_value() && result->rms >= threshold && result->confidence >= minConfidence)
        nextNote = juce::jlimit (0, 127, frequencyToMidiNote (result->frequencyHz) + transpose);

    if (nextNote == stableNote)
        ++stableCount;
    else
    {
        stableNote = nextNote;
        stableCount = 1;
    }

    const auto acceptedNote = stableCount >= smoothing ? stableNote : activeNote;

    if (acceptedNote != activeNote)
    {
        if (activeNote >= 0)
            midiMessages.addEvent (juce::MidiMessage::noteOff (1, activeNote), 0);

        activeNote = acceptedNote;

        if (activeNote >= 0)
            midiMessages.addEvent (juce::MidiMessage::noteOn (1, activeNote, velocity), 0);
    }

    if (result.has_value())
        lastFrequency.store (result->frequencyHz);
    else
        lastFrequency.store (0.0f);

    lastMidiNote.store (activeNote);
    gateOpen.store (activeNote >= 0);

    buffer.clear();
}

void VoiceMelodyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream (destData, false);
    parameters.state.writeToStream (stream);
}

void VoiceMelodyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto tree = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes)); tree.isValid())
        parameters.replaceState (tree);
}

juce::AudioProcessorEditor* VoiceMelodyAudioProcessor::createEditor()
{
    return new VoiceMelodyAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoiceMelodyAudioProcessor();
}
