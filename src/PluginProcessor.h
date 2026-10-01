#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "BqtDsp.h"

// Set by the BqstChainTests target, which links the processor without the editor or its binary
// assets so DSP behaviour can be asserted headlessly.
#ifndef BQST_HEADLESS_TESTS
 #define BQST_HEADLESS_TESTS 0
#endif

class BqtAudioProcessor final : public juce::AudioProcessor,
                                private juce::Timer
{
public:
    BqtAudioProcessor();
    ~BqtAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return ! BQST_HEADLESS_TESTS; }

    // Designate our own "bypass" parameter as the host bypass. Without this the wrapper supplies
    // its own hidden bypass, so the host's bypass button sidesteps the latency-compensated
    // crossfade in processBlock and the plugin ends up exposing two separate bypasses.
    juce::AudioProcessorParameter* getBypassParameter() const override
    {
        return parameters.getParameter("bypass");
    }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Deliberately a single program. Serving the seven factory presets as host programs was
    // tried and reverted: setCurrentProgram has to write parameters, which then races APVTS
    // state restore, and pluginval caught it as parameters not being restored by
    // setStateInformation. A host calling setCurrentProgram around session load could clobber
    // saved settings, so the Logic factory-preset menu is not worth the risk here.
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
        Filter saturationLowGuardPre;
        Filter saturationLowGuardPost;
        Filter transformerLowDrive;
        Filter transformerLowRestore;
        Filter transformerWeight;
        Filter transformerTop;

        // Every filter the saturation stage uses. Keep this the single list: prepare, reset on
        // structural change and reset on oversampling change all go through it.
        template <typename Fn>
        void forEachSaturationFilter(Fn&& fn)
        {
            fn(vintage);
            fn(saturationLowGuardPre);
            fn(saturationLowGuardPost);
            fn(transformerLowDrive);
            fn(transformerLowRestore);
            fn(transformerWeight);
            fn(transformerTop);
        }

        template <typename Fn>
        void forEachFilter(Fn&& fn)
        {
            fn(lowShelf);
            fn(highShelf);
            forEachSaturationFilter(fn);
        }
    };

    // The discrete switches, latched. Changing any of them steps filter coefficients or reroutes
    // the signal with live filter state, which clicks. The DSP reads these latched values rather
    // than the parameters directly, so a change can be adopted at the bottom of a short fade
    // instead of mid-signal.
    struct StructuralConfig
    {
        int eqMode = 0;
        int satMode = 0;
        bool eqBypassed = false;
        bool satBypassed = false;
        bool vintage = false;
        std::array<int, 2> satType { 0, 0 };
        // Effective shelf frequency indices, with EQ Link already applied (side B = side A's), so
        // a link toggle that changes B's frequency is adopted through the fade like any switch.
        std::array<int, 2> lowFreq { 0, 0 };
        std::array<int, 2> highFreq { 0, 0 };
        // Active oversampler (-1 = off). A factor change swaps in an oversampler with zero state
        // and a different latency, so it is adopted at the bottom of the fade like the rest.
        int oversamplingIndex = -1;

        bool operator==(const StructuralConfig& other) const
        {
            return eqMode == other.eqMode && satMode == other.satMode
                && eqBypassed == other.eqBypassed && satBypassed == other.satBypassed
                && vintage == other.vintage && satType == other.satType
                && lowFreq == other.lowFreq && highFreq == other.highFreq
                && oversamplingIndex == other.oversamplingIndex;
        }
        bool operator!=(const StructuralConfig& other) const { return ! (*this == other); }
    };

    enum class StructuralTransition { idle, fadingOut, fadingIn };

    StructuralConfig readStructuralConfig() const;
    void adoptPendingStructuralConfig(const StructuralConfig& pending, double hostSampleRate);
    void advanceStructuralTransition(double hostSampleRate);
    void applyStructuralTransitionGain(float* left, float* right, int numSamples);
    int samplesUntilFadeBottom() const;

    void updateFilters();
    void updateSaturationToneFilters();
    void resetSaturationState();
    void cacheParameterPointers();
    // Returns how many samples it consumed: never more than numSamples, fewer when a structural
    // fade bottoms out first.
    int processSubBlock(float* left, float* right, int numSamples);
    void processEqStage(float* left, float* right, int numSamples);
    void processSaturationStage(float* left, float* right, int numSamples);
    void processEq(float* samples, int numSamples, int sideIndex);
    void processSide(float* samples, int numSamples, int sideIndex);
    void applyLatencyDelay(float* samples, int numSamples, int sideIndex);
    void updateMeter(int sideIndex, const float* samples, int numSamples);
    int getActiveOversamplingIndex() const;
    void applyOversamplingFactorChange(int oversamplingIndex, double hostSampleRate, bool resetState);
    int computeLatencySamples() const;
    void updateLatency();
    // Message-thread poll for the latency flag raised on the audio thread; see updateLatency().
    // Lives on the processor rather than the editor so it still runs with no editor open.
    void timerCallback() override;


    juce::AudioProcessorValueTreeState parameters;
    std::array<SideFilters, 2> filters;
    // Cream's model (tone shaping, colour ramp and DC blocker are internal), one per side.
    std::array<bqt::CreamModel, 2> creamModels;
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
    // 0..1 blend from unity toward the drive-derived autogain, so toggling autoGain ramps.
    std::array<juce::SmoothedValue<float>, 2> autoGainBlend;
    std::array<std::atomic<float>, 2> meterLevels {};
    std::array<float, 2> meterRms {};
    // One-pole DC blocker state for the wet saturation path, per side.
    std::array<float, 2> dcBlockPreviousInput {};
    std::array<float, 2> dcBlockPreviousOutput {};
    double currentSampleRate = 44100.0;
    // The block size prepareToPlay sized every internal buffer and oversampler from. processBlock
    // splits anything larger into chunks of this size, which makes those sizes provable ceilings
    // and removes the need to ever grow a buffer on the audio thread.
    int preparedBlockSize = 1;
    StructuralConfig activeConfig;
    StructuralTransition structuralTransition = StructuralTransition::idle;
    float structuralGain = 1.0f;
    float structuralStep = 1.0f;
    std::atomic<int> currentLatencySamples { 0 };
    std::atomic<bool> latencyNeedsReporting { false };

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
        std::atomic<float>* eqLink = nullptr;
        std::atomic<float>* satLink = nullptr;
    } paramPtrs;

    // Linking happens here rather than in the editor: while a group is linked, side B reads side
    // A's parameters for it and its own are ignored. So a linked knob move writes one parameter
    // (one host undo step), and B's stored values come back when the group is unlinked.
    size_t eqSourceSide(size_t side) const;
    size_t satSourceSide(size_t side) const;

    // The saturation tone filters depend only on the sample rate and the vintage flag,
    // so they are rebuilt only when one of those actually changes (never per block).
    double satToneSampleRate = 0.0;
    int satToneVintage = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BqtAudioProcessor)
};
