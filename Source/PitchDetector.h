#pragma once

#include <JuceHeader.h>
#include <optional>

struct PitchResult
{
    float frequencyHz = 0.0f;
    float confidence = 0.0f;
    float rms = 0.0f;
};

class PitchDetector
{
public:
    void prepare (double newSampleRate, int newMaxBlockSize);
    void reset();
    std::optional<PitchResult> processBlock (const juce::AudioBuffer<float>& buffer);

private:
    std::optional<PitchResult> detectPitchYin();

    double sampleRate = 44100.0;
    int windowSize = 2048;
    int writePosition = 0;
    bool filled = false;
    juce::AudioBuffer<float> circularBuffer;
    std::vector<float> analysisWindow;
    std::vector<float> difference;
    std::vector<float> cumulativeMean;
};
