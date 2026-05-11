#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "BqtDsp.h"

class BqtAudioProcessor final : public juce::AudioProcessor
{
public:
    BqtAudioProcessor();
    ~BqtAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return parameters; }
    float getMeterLevel(int sideIndex) const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    struct SideFilters
    {
        Filter lowShelf;
        Filter highShelf;
        Filter boom;
        Filter vintage;
    };

    void updateFilters();
    void updateSaturationToneFilters();
    void processSide(float* samples, int numSamples, int sideIndex);
    void processSaturation(float* samples, int numSamples, int sideIndex, float drive, bqt::SaturationType satType, float compensation);
    void updateMeter(int sideIndex, const float* samples, int numSamples);
    int getActiveOversamplingIndex() const;
    void updateLatency();

    juce::AudioProcessorValueTreeState parameters;
    std::array<SideFilters, 2> filters;
    std::array<std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3>, 2> oversamplers;
    std::array<juce::AudioBuffer<float>, 2> dryBuffers;
    std::array<std::atomic<float>, 2> meterLevels {};
    double currentSampleRate = 44100.0;
    int currentLatencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BqtAudioProcessor)
};
