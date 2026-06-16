#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "BqtDsp.h"

class BqtAudioProcessor final : public juce::AudioProcessor,
                                private juce::AsyncUpdater
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
    const juce::String getProgramName(int) override { return "Default"; }
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
        Filter vintage;
        Filter densityBodyFocus;
        Filter densityPreEmphasis;
        Filter densityDeEmphasis;
        Filter saturationLowGuardPre;
        Filter saturationLowGuardPost;
        Filter transformerLowDrive;
        Filter transformerLowRestore;
        Filter transformerWeight;
        Filter transformerTop;
    };

    void updateFilters();
    void updateSaturationToneFilters();
    void cacheParameterPointers();
    void processChain(float* left, float* right, int numSamples);
    void processEq(float* samples, int numSamples, int sideIndex);
    void processSide(float* samples, int numSamples, int sideIndex);
    void applyLatencyDelay(float* samples, int numSamples, int sideIndex);
    void updateMeter(int sideIndex, const float* samples, int numSamples);
    int getActiveOversamplingIndex() const;
    int computeLatencySamples() const;
    void updateLatency();
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState parameters;
    std::array<SideFilters, 2> filters;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;
    std::array<juce::AudioBuffer<float>, 2> dryBuffers;
    juce::AudioBuffer<float> bypassDryBuffer;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>, 2> dryMixDelays;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputTrimGain;
    juce::SmoothedValue<float> globalBypassMix;
    std::array<juce::SmoothedValue<float>, 2> eqLowGainDb;
    std::array<juce::SmoothedValue<float>, 2> eqHighGainDb;
    std::array<juce::SmoothedValue<float>, 2> driveAmount;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, 2> driveGain;
    std::array<juce::SmoothedValue<float>, 2> saturationMix;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, 2> outputTrimGain;
    std::array<std::atomic<float>, 2> meterLevels {};
    std::array<float, 2> meterRms {};
    double currentSampleRate = 44100.0;
    std::atomic<int> currentLatencySamples { 0 };

    // Raw parameter pointers cached once after construction so the audio thread never
    // builds juce::Strings or does map lookups to read parameter values.
    struct ParameterPointers
    {
        std::array<std::atomic<float>*, 2> lowGain { nullptr, nullptr };
        std::array<std::atomic<float>*, 2> highGain { nullptr, nullptr };
        std::array<std::atomic<float>*, 2> lowFreq { nullptr, nullptr };
        std::array<std::atomic<float>*, 2> highFreq { nullptr, nullptr };
        std::array<std::atomic<float>*, 2> drive { nullptr, nullptr };
        std::array<std::atomic<float>*, 2> satType { nullptr, nullptr };
        std::array<std::atomic<float>*, 2> mix { nullptr, nullptr };
        std::array<std::atomic<float>*, 2> outputTrim { nullptr, nullptr };
        std::atomic<float>* inputTrim = nullptr;
        std::atomic<float>* eqMode = nullptr;
        std::atomic<float>* satMode = nullptr;
        std::atomic<float>* eqBypass = nullptr;
        std::atomic<float>* satBypass = nullptr;
        std::atomic<float>* autoGain = nullptr;
        std::atomic<float>* vintage = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* osRealtime = nullptr;
        std::atomic<float>* osRender = nullptr;
    } paramPtrs;

    // The saturation tone filters depend only on the sample rate and the vintage flag,
    // so they are rebuilt only when one of those actually changes (never per block).
    double satToneSampleRate = 0.0;
    int satToneVintage = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BqtAudioProcessor)
};
