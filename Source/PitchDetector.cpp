#include "PitchDetector.h"

namespace
{
constexpr float minFrequencyHz = 70.0f;
constexpr float maxFrequencyHz = 1000.0f;
constexpr float yinThreshold = 0.16f;
constexpr float silenceRms = 0.01f;
}

void PitchDetector::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    windowSize = juce::jlimit (1024, 4096, juce::nextPowerOfTwo (newMaxBlockSize * 4));
    circularBuffer.setSize (1, windowSize);
    analysisWindow.resize (static_cast<size_t> (windowSize));
    difference.resize (static_cast<size_t> (windowSize / 2));
    cumulativeMean.resize (static_cast<size_t> (windowSize / 2));
    reset();
}

void PitchDetector::reset()
{
    circularBuffer.clear();
    std::fill (analysisWindow.begin(), analysisWindow.end(), 0.0f);
    writePosition = 0;
    filled = false;
}

std::optional<PitchResult> PitchDetector::processBlock (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumSamples() == 0 || circularBuffer.getNumSamples() == 0)
        return std::nullopt;

    const auto numChannels = juce::jmax (1, buffer.getNumChannels());

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float mono = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            mono += buffer.getSample (channel, sample);

        mono /= static_cast<float> (numChannels);
        circularBuffer.setSample (0, writePosition, mono);
        writePosition = (writePosition + 1) % windowSize;

        if (writePosition == 0)
            filled = true;
    }

    if (! filled)
        return std::nullopt;

    for (int i = 0; i < windowSize; ++i)
        analysisWindow[static_cast<size_t> (i)] = circularBuffer.getSample (0, (writePosition + i) % windowSize);

    return detectPitchYin();
}

std::optional<PitchResult> PitchDetector::detectPitchYin()
{
    double sumSquares = 0.0;
    for (const auto sample : analysisWindow)
        sumSquares += static_cast<double> (sample) * sample;

    const auto rms = static_cast<float> (std::sqrt (sumSquares / static_cast<double> (analysisWindow.size())));
    if (rms < silenceRms)
        return std::nullopt;

    const int maxTau = juce::jmin (windowSize / 2 - 1, static_cast<int> (sampleRate / minFrequencyHz));
    const int minTau = juce::jmax (2, static_cast<int> (sampleRate / maxFrequencyHz));

    difference[0] = 0.0f;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        double value = 0.0;
        for (int i = 0; i < windowSize - maxTau; ++i)
        {
            const auto delta = analysisWindow[static_cast<size_t> (i)] - analysisWindow[static_cast<size_t> (i + tau)];
            value += static_cast<double> (delta) * delta;
        }
        difference[static_cast<size_t> (tau)] = static_cast<float> (value);
    }

    cumulativeMean[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        runningSum += difference[static_cast<size_t> (tau)];
        cumulativeMean[static_cast<size_t> (tau)] = runningSum > 0.0f
            ? difference[static_cast<size_t> (tau)] * static_cast<float> (tau) / runningSum
            : 1.0f;
    }

    int tauEstimate = -1;
    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        if (cumulativeMean[static_cast<size_t> (tau)] < yinThreshold)
        {
            while (tau + 1 <= maxTau && cumulativeMean[static_cast<size_t> (tau + 1)] < cumulativeMean[static_cast<size_t> (tau)])
                ++tau;

            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate < 0)
        return std::nullopt;

    auto betterTau = static_cast<float> (tauEstimate);
    if (tauEstimate > 0 && tauEstimate < maxTau)
    {
        const auto left = cumulativeMean[static_cast<size_t> (tauEstimate - 1)];
        const auto centre = cumulativeMean[static_cast<size_t> (tauEstimate)];
        const auto right = cumulativeMean[static_cast<size_t> (tauEstimate + 1)];
        const auto divisor = 2.0f * (2.0f * centre - left - right);
        if (std::abs (divisor) > 1.0e-6f)
            betterTau += (right - left) / divisor;
    }

    const auto frequency = static_cast<float> (sampleRate) / betterTau;
    if (frequency < minFrequencyHz || frequency > maxFrequencyHz)
        return std::nullopt;

    return PitchResult { frequency, juce::jlimit (0.0f, 1.0f, 1.0f - cumulativeMean[static_cast<size_t> (tauEstimate)]), rms };
}
