// Chain-level tests: these drive the real BqtAudioProcessor through processBlock.
//
// tests/dsp_tests.cpp covers the pure helpers in BqtDsp.h and deliberately does not link JUCE.
// That left the actual processing chain untested, which is how the drive -> 0 coloration step
// hid for so long: the bare curve function really does return its input at zero drive, so the
// "zero-drive bypass" assertion passed while the chain around it snapped roughly 1.9 dB of tilt
// into place the instant drive left zero. Everything here exercises the chain, not the helpers.

#include "../src/BqtEditorLogic.h"
#include "../src/BqtPresetManager.h"
#include "../src/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const char* what)
{
    if (! condition)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

void setParam(BqtAudioProcessor& processor, const juce::String& id, float plainValue)
{
    auto* parameter = processor.state().getParameter(id);
    jassert(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

// Runs `seconds` of a sine through the processor and returns the output of the left channel.
std::vector<float> render(BqtAudioProcessor& processor, double frequency, float amplitude, double seconds)
{
    const auto total = static_cast<int>(sampleRate * seconds);
    std::vector<float> out;
    out.reserve(static_cast<size_t>(total));

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    auto phase = 0.0;
    const auto increment = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

    for (int done = 0; done < total; done += blockSize)
    {
        const auto count = juce::jmin(blockSize, total - done);
        buffer.setSize(2, count, false, false, true);
        for (int i = 0; i < count; ++i)
        {
            const auto value = amplitude * static_cast<float>(std::sin(phase));
            buffer.setSample(0, i, value);
            buffer.setSample(1, i, value);
            phase += increment;
        }

        processor.processBlock(buffer, midi);
        for (int i = 0; i < count; ++i)
            out.push_back(buffer.getSample(0, i));
    }

    return out;
}

std::unique_ptr<BqtAudioProcessor> makeProcessor()
{
    auto processor = std::make_unique<BqtAudioProcessor>();
    processor->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    processor->prepareToPlay(sampleRate, blockSize);
    return processor;
}

double rms(const std::vector<float>& samples, size_t from)
{
    auto sum = 0.0;
    for (auto i = from; i < samples.size(); ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(sum / static_cast<double>(samples.size() - from));
}

double mean(const std::vector<float>& samples, size_t from)
{
    auto sum = 0.0;
    for (auto i = from; i < samples.size(); ++i)
        sum += samples[i];
    return sum / static_cast<double>(samples.size() - from);
}

// ============================================================================================
// State and preset persistence tests (host state round trip, factory and user presets).
// Kept in their own block, called once from main, so they stay apart from the DSP tests.
// ============================================================================================

const auto& workflowParameterIds = bqt::workflowParameterIds();

juce::Array<juce::RangedAudioParameter*> rangedParameters(BqtAudioProcessor& processor)
{
    juce::Array<juce::RangedAudioParameter*> result;
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            result.add(ranged);
    return result;
}

float plainValue(BqtAudioProcessor& processor, const juce::String& id)
{
    auto* parameter = processor.state().getParameter(id);
    jassert(parameter != nullptr);
    return parameter->convertFrom0to1(parameter->getValue());
}

bool isAtDefault(BqtAudioProcessor& processor, const juce::String& id)
{
    auto* parameter = processor.state().getParameter(id);
    return parameter != nullptr && std::abs(parameter->getValue() - parameter->getDefaultValue()) < 1.0e-6f;
}

// Moves every parameter to a distinct non-default position. `seed` varies the pattern so two
// processors can be put in different states. Discrete parameters snap via the range.
void scrambleParameters(BqtAudioProcessor& processor, int seed)
{
    auto index = 0;
    for (auto* ranged : rangedParameters(processor))
    {
        // Snap through the range: AudioParameterBool keeps an unsnapped normalised value as-is.
        const auto raw = std::fmod(0.137f * static_cast<float>(index + 1 + seed * 7) + 0.21f, 1.0f);
        auto snapped = ranged->convertTo0to1(ranged->convertFrom0to1(raw));
        if (std::abs(snapped - ranged->getDefaultValue()) < 1.0e-6f)
            snapped = ranged->getDefaultValue() < 0.5f ? 1.0f : 0.0f;
        ranged->setValueNotifyingHost(snapped);
        ++index;
    }
}

bool parametersMatch(BqtAudioProcessor& a, BqtAudioProcessor& b, const juce::StringArray& skip = {})
{
    auto same = true;
    for (auto* ranged : rangedParameters(a))
    {
        const auto id = ranged->getParameterID();
        if (skip.contains(id))
            continue;
        auto* other = b.state().getParameter(id);
        same = same && other != nullptr && std::abs(ranged->getValue() - other->getValue()) < 1.0e-6f;
        if (other != nullptr && std::abs(ranged->getValue() - other->getValue()) >= 1.0e-6f)
            std::printf("  (%s: %.4f vs %.4f)\n", id.toRawUTF8(), ranged->getValue(), other->getValue());
    }
    return same;
}

std::unique_ptr<juce::XmlElement> savedStateXml(BqtAudioProcessor& processor)
{
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    return juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
}

void loadStateXml(BqtAudioProcessor& processor, const juce::XmlElement& xml)
{
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(xml, block);
    processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
}

juce::XmlElement* findParamChild(juce::XmlElement& xml, const juce::String& id)
{
    for (auto* child : xml.getChildIterator())
        if (child->hasTagName("PARAM") && child->getStringAttribute("id") == id)
            return child;
    return nullptr;
}

void runStateTests()
{
    // Host state round trip: every parameter survives get -> set into a fresh processor.
    {
        auto source = makeProcessor();
        scrambleParameters(*source, 0);
        juce::MemoryBlock block;
        source->getStateInformation(block);

        auto target = makeProcessor();
        target->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        check(parametersMatch(*source, *target), "host state round-trips every parameter");
    }

    // Non-finite values in host state are stripped: the parameter gets its default, never NaN.
    {
        auto source = makeProcessor();
        auto xml = savedStateXml(*source);
        check(xml != nullptr, "host state parses as XML");
        if (xml != nullptr)
        {
            findParamChild(*xml, "aDrive")->setAttribute("value", "nan");
            findParamChild(*xml, "bMix")->setAttribute("value", "inf");
            findParamChild(*xml, "aSatType")->setAttribute("value", "-inf");

            auto target = makeProcessor();
            scrambleParameters(*target, 1);
            loadStateXml(*target, *xml);

            check(isAtDefault(*target, "aDrive") && isAtDefault(*target, "bMix") && isAtDefault(*target, "aSatType"),
                  "non-finite host state values fall back to the parameter default");

            auto allFinite = true;
            for (auto* ranged : rangedParameters(*target))
                allFinite = allFinite && std::isfinite(ranged->getValue())
                         && std::isfinite(ranged->convertFrom0to1(ranged->getValue()));
            check(allFinite, "no NaN/Inf reaches a parameter from host state");
        }
    }

    // State missing some parameters: those get their defaults, not the previous session's value.
    {
        auto source = makeProcessor();
        scrambleParameters(*source, 0);
        auto xml = savedStateXml(*source);
        if (xml != nullptr)
        {
            const juce::StringArray dropped { "aDrive", "vintage", "bHighFreq", "osRender" };
            for (const auto& id : dropped)
                xml->removeChildElement(findParamChild(*xml, id), true);

            auto target = makeProcessor();
            scrambleParameters(*target, 2);
            loadStateXml(*target, *xml);

            auto droppedDefault = true;
            for (const auto& id : dropped)
                droppedDefault = droppedDefault && isAtDefault(*target, id);
            check(droppedDefault, "parameters missing from host state load their defaults");
            check(parametersMatch(*source, *target, dropped), "parameters present in host state still load");
        }
    }

    // A foreign root tag is rejected outright; the current state is left unchanged.
    {
        auto source = makeProcessor();
        auto xml = savedStateXml(*source);
        if (xml != nullptr)
        {
            xml->setTagName("NOTBQST");
            findParamChild(*xml, "aDrive")->setAttribute("value", 12.0);

            auto target = makeProcessor();
            scrambleParameters(*target, 3);
            auto reference = makeProcessor();
            scrambleParameters(*reference, 3);
            loadStateXml(*target, *xml);
            check(parametersMatch(*reference, *target), "host state with a foreign root tag is rejected");
        }
    }
}

void runFactoryPresetTests()
{
    // setParameter silently ignores unknown IDs, so a typo in factoryPresets[] would ship unnoticed.
    auto processor = makeProcessor();
    const auto ids = BqtPresetManager::getFactoryPresetParameterIds();
    check(! ids.isEmpty(), "factory presets reference parameters");
    for (const auto& id : ids)
    {
        const auto exists = processor->state().getParameter(id) != nullptr;
        check(exists, "every factory preset parameter ID exists in the layout");
        if (! exists)
            std::printf("  (unknown factory preset parameter '%s')\n", id.toRawUTF8());
        check(! workflowParameterIds.contains(id), "factory presets do not set workflow parameters");
    }
}

void writePresetFile(const juce::File& file, const juce::String& rootTag, int version,
                     std::initializer_list<std::pair<const char*, const char*>> params)
{
    juce::XmlElement xml(rootTag);
    if (version >= 0)
        xml.setAttribute("bqstPresetVersion", version);
    for (const auto& [id, value] : params)
    {
        auto* child = xml.createNewChildElement("PARAM");
        child->setAttribute("id", id);
        child->setAttribute("value", value);
    }
    xml.writeTo(file);
}

void runUserPresetTests()
{
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("BqstChainTests-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
    directory.createDirectory();

    // Round trip: musical parameters travel; workflow parameters are neither stored nor changed.
    {
        auto source = makeProcessor();
        scrambleParameters(*source, 0);
        BqtPresetManager sourcePresets(source->state());
        const auto file = directory.getChildFile("RoundTrip.bqstpreset");
        check(sourcePresets.saveUserPreset(file), "user preset saves");

        if (auto xml = juce::parseXML(file))
        {
            auto storesWorkflow = false;
            for (const auto& id : workflowParameterIds)
                storesWorkflow = storesWorkflow || findParamChild(*xml, id) != nullptr;
            check(! storesWorkflow, "user preset does not store workflow parameters");
            check(xml->getIntAttribute("bqstPresetVersion", 0) >= 1, "user preset is version stamped");
        }
        else
        {
            check(false, "saved user preset parses as XML");
        }

        auto target = makeProcessor();
        scrambleParameters(*target, 4);
        std::vector<float> workflowBefore;
        for (const auto& id : workflowParameterIds)
            workflowBefore.push_back(plainValue(*target, id));

        BqtPresetManager targetPresets(target->state());
        check(targetPresets.loadPresetFile(file), "user preset loads");
        check(parametersMatch(*source, *target, workflowParameterIds), "user preset round-trips every musical parameter");

        auto workflowKept = true;
        for (int i = 0; i < workflowParameterIds.size(); ++i)
            workflowKept = workflowKept && plainValue(*target, workflowParameterIds[i]) == workflowBefore[static_cast<size_t>(i)];
        check(workflowKept, "loading a user preset leaves workflow parameters unchanged");
    }

    // Rejections: a preset from a newer format, and a file with a foreign root tag.
    {
        auto processor = makeProcessor();
        const auto rootTag = processor->state().state.getType().toString();
        const auto future = directory.getChildFile("Future.bqstpreset");
        const auto foreign = directory.getChildFile("Foreign.bqstpreset");
        writePresetFile(future, rootTag, 99, { { "aDrive", "12" } });
        writePresetFile(foreign, "NOTBQST", 1, { { "aDrive", "12" } });

        scrambleParameters(*processor, 5);
        auto reference = makeProcessor();
        scrambleParameters(*reference, 5);

        BqtPresetManager presets(processor->state());
        check(! presets.loadPresetFile(future), "a preset from a newer format version is rejected");
        check(parametersMatch(*reference, *processor), "a rejected newer preset changes nothing");
        check(! presets.loadPresetFile(foreign), "a preset with a foreign root tag is rejected");
        check(parametersMatch(*reference, *processor), "a rejected foreign preset changes nothing");
    }

    // Workflow parameters inside a (hand-edited) preset are ignored; out-of-range and non-finite
    // values are clamped / dropped rather than reaching the DSP.
    {
        auto processor = makeProcessor();
        const auto rootTag = processor->state().state.getType().toString();
        const auto file = directory.getChildFile("Hostile.bqstpreset");
        writePresetFile(file, rootTag, 1,
                        { { "osRealtime", "0" }, { "bypass", "1" }, { "eqBypass", "1" },
                          { "aDrive", "1000" }, { "bDrive", "-50" }, { "aMix", "250" },
                          { "aSatType", "7" }, { "inputTrim", "-99" }, { "bMix", "nan" } });

        setParam(*processor, "osRealtime", 3.0f);
        setParam(*processor, "bMix", 20.0f);
        BqtPresetManager presets(processor->state());
        check(presets.loadPresetFile(file), "a hand-edited current-version preset loads");

        check(std::abs(plainValue(*processor, "osRealtime") - 3.0f) < 1.0e-4f
                  && plainValue(*processor, "bypass") < 0.5f && plainValue(*processor, "eqBypass") < 0.5f,
              "workflow parameters in a preset file are not applied");
        check(std::abs(plainValue(*processor, "aDrive") - 18.0f) < 1.0e-4f
                  && std::abs(plainValue(*processor, "bDrive")) < 1.0e-4f
                  && std::abs(plainValue(*processor, "aMix") - 100.0f) < 1.0e-4f
                  && std::abs(plainValue(*processor, "aSatType") - 1.0f) < 1.0e-4f
                  && std::abs(plainValue(*processor, "inputTrim") + 12.0f) < 1.0e-4f,
              "out-of-range preset values are clamped to the parameter range");
        check(isAtDefault(*processor, "bMix"), "a non-finite preset value leaves the parameter at its default");
    }

    directory.deleteRecursively();
}

// ============================================================================================
// Editor decision logic (BqtEditorLogic.h): undo parameter list and diff, link-gesture rules.
// ============================================================================================

bqt::editor::ParameterSnapshot captureSnapshot(BqtAudioProcessor& processor, const juce::StringArray& ids)
{
    bqt::editor::ParameterSnapshot snapshot;
    for (const auto& id : ids)
        snapshot.emplace_back(id, processor.state().getParameter(id)->getValue());
    return snapshot;
}

void runEditorLogicTests()
{
    using namespace bqt::editor;

    {
        auto processor = std::make_unique<BqtAudioProcessor>();
        const auto ids = undoableParameterIds(*processor);

        auto noWorkflow = true;
        for (const auto& id : workflowParameterIds)
            noWorkflow = noWorkflow && ! ids.contains(id);
        check(noWorkflow, "undo never records workflow parameters");

        check(ids.size() + workflowParameterIds.size() == rangedParameters(*processor).size(),
              "undo records every non-workflow parameter");
        check(ids.contains("aSatType") && ids.contains("bSatType") && ids.contains("eqLink") && ids.contains("inputTrim"),
              "undo records musical and link parameters");

        // An undo step taken before a drive move and a bypass/oversampling change restores only
        // the drive: workflow state is not in the snapshot, and nothing else differs.
        const auto before = captureSnapshot(*processor, ids);
        setParam(*processor, "aDrive", 9.0f);
        setParam(*processor, "bypass", 1.0f);
        setParam(*processor, "osRealtime", 3.0f);
        const auto changed = changedSnapshotEntries(before, captureSnapshot(*processor, ids));
        check(changed.size() == 1 && changed[0].first == "aDrive", "undo restore writes only the changed parameter");
        check(changedSnapshotEntries(before, before).empty(), "undo restore of an unchanged state writes nothing");
    }

    {
        const ParameterSnapshot a { { "x", 0.5f }, { "y", 0.25f }, { "z", 1.0f } };
        const ParameterSnapshot b { { "x", 0.5f + snapshotTolerance * 0.5f }, { "y", 0.75f } };
        const auto changed = changedSnapshotEntries(a, b);
        check(changed.size() == 2 && changed[0].first == "y" && changed[1].first == "z",
              "snapshot diff ignores sub-tolerance noise and includes entries missing from the current state");
        check(snapshotsMatch(a, a) && ! snapshotsMatch(a, b), "snapshotsMatch compares ids and values");
    }

    {
        UserGestureTracker gestures;
        int knob = 0;
        int other = 0;
        check(! gestures.isActive(&knob), "no gesture before a drag starts");
        gestures.begin(&knob);
        gestures.begin(&knob); // double-click reset nested inside the mouse-down drag
        check(! gestures.end(&knob) && gestures.isActive(&knob), "a nested drag end keeps the outer gesture open");
        check(! gestures.isActive(&other), "a gesture belongs to its own control");
        check(gestures.end(&knob) && ! gestures.isActive(&knob), "the outermost drag end closes the gesture");
        check(gestures.end(&knob), "an unmatched drag end is harmless");

        check(shouldMirrorLinkedEdit(true, true, false), "a user gesture on a linked control mirrors");
        check(! shouldMirrorLinkedEdit(true, false, false),
              "automation or state restore reaching a linked control (even under the mouse) does not mirror");
        check(! shouldMirrorLinkedEdit(false, true, false), "an unlinked control does not mirror");
        check(! shouldMirrorLinkedEdit(true, true, true), "the mirrored write does not mirror back");

        check(nextSatTypeIndex(0) == 1 && nextSatTypeIndex(1) == 0, "sat type toggles between cream and grit");
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // State and preset persistence (see the block above main).
    runStateTests();
    runFactoryPresetTests();
    runUserPresetTests();
    runEditorLogicTests();

    // 1. Continuity of the FULL chain as drive -> 0. This is the regression that motivated the
    //    whole test target: the coloration filters used to engage at full strength the instant
    //    drive left zero. Checked for both curves and with vintage both off and on.
    for (auto vintage : { false, true })
        for (const char* type : { "Cream", "Grit" })
        {
            auto silent = makeProcessor();
            setParam(*silent, "vintage", vintage ? 1.0f : 0.0f);
            setParam(*silent, "aSatType", type[0] == 'C' ? 0.0f : 1.0f);
            setParam(*silent, "bSatType", type[0] == 'C' ? 0.0f : 1.0f);
            setParam(*silent, "aDrive", 0.0f);
            setParam(*silent, "bDrive", 0.0f);
            const auto atZero = render(*silent, 220.0, 0.4f, 0.35);

            auto nudged = makeProcessor();
            setParam(*nudged, "vintage", vintage ? 1.0f : 0.0f);
            setParam(*nudged, "aSatType", type[0] == 'C' ? 0.0f : 1.0f);
            setParam(*nudged, "bSatType", type[0] == 'C' ? 0.0f : 1.0f);
            setParam(*nudged, "aDrive", 0.05f);
            setParam(*nudged, "bDrive", 0.05f);
            const auto atEpsilon = render(*nudged, 220.0, 0.4f, 0.35);

            auto worst = 0.0f;
            for (size_t i = atZero.size() / 2; i < atZero.size(); ++i)
                worst = std::fmax(worst, std::abs(atZero[i] - atEpsilon[i]));

            check(worst < 0.002f, "chain is continuous as drive approaches zero");
            if (worst >= 0.002f)
                std::printf("  (%s, vintage %s: peak difference %.5f)\n", type, vintage ? "on" : "off", worst);
        }

    // 2. Zero drive is a true bypass of the saturation stage: unity gain, no coloration.
    {
        auto processor = makeProcessor();
        setParam(*processor, "aDrive", 0.0f);
        setParam(*processor, "bDrive", 0.0f);
        const auto out = render(*processor, 1000.0, 0.5f, 0.3);
        check(std::abs(rms(out, out.size() / 2) - 0.5 / std::sqrt(2.0)) < 0.002,
              "zero drive passes the signal at unity");
    }

    // 3. The asymmetric curves must not leave a DC offset on the output (fixed by the wet-path
    //    DC blocker). Measured -0.059 at full Cream drive before the fix.
    for (const char* type : { "Cream", "Grit" })
    {
        auto processor = makeProcessor();
        setParam(*processor, "aSatType", type[0] == 'C' ? 0.0f : 1.0f);
        setParam(*processor, "bSatType", type[0] == 'C' ? 0.0f : 1.0f);
        setParam(*processor, "aDrive", 18.0f);
        setParam(*processor, "bDrive", 18.0f);
        const auto out = render(*processor, 110.0, 0.9f, 0.6);
        const auto offset = std::abs(mean(out, out.size() / 2));
        check(offset < 2.0e-3, "saturation leaves no DC offset");
        if (offset >= 2.0e-3)
            std::printf("  (%s: DC offset %.5f)\n", type, offset);
    }

    // 4. A parameter ramp must take the same wall-clock time at every oversampling factor.
    //    The smoothers are consumed per oversampled sample, so before the fix a 20 ms ramp
    //    finished in 10 ms at 2x and 2.5 ms at 8x.
    {
        std::vector<double> rampSamples;
        for (float os : { 0.0f, 1.0f, 3.0f })
        {
            auto processor = makeProcessor();
            setParam(*processor, "osRealtime", os);
            setParam(*processor, "aOutputTrim", 0.0f);
            setParam(*processor, "bOutputTrim", 0.0f);
            render(*processor, 1000.0, 0.5f, 0.2); // settle

            setParam(*processor, "aOutputTrim", -12.0f);
            setParam(*processor, "bOutputTrim", -12.0f);
            const auto out = render(*processor, 1000.0, 0.5f, 0.12);

            // Find where the envelope has fallen 90% of the way to the new level.
            const auto startLevel = 0.5, endLevel = 0.5 * std::pow(10.0, -12.0 / 20.0);
            const auto threshold = static_cast<float>(startLevel - 0.9 * (startLevel - endLevel));
            size_t crossing = 0;
            for (size_t i = 0; i + 64 < out.size(); ++i)
            {
                auto peak = 0.0f;
                for (size_t j = i; j < i + 64; ++j)
                    peak = std::fmax(peak, std::abs(out[j]));
                if (peak < threshold) { crossing = i; break; }
            }
            rampSamples.push_back(static_cast<double>(crossing) / sampleRate);
        }

        const auto spread = *std::max_element(rampSamples.begin(), rampSamples.end())
                          - *std::min_element(rampSamples.begin(), rampSamples.end());
        check(spread < 0.006, "parameter ramp length does not depend on the oversampling factor");
        std::printf("ramp to -12 dB: OS off %.1f ms, 2x %.1f ms, 8x %.1f ms\n",
                    rampSamples[0] * 1000.0, rampSamples[1] * 1000.0, rampSamples[2] * 1000.0);
    }

    // 5. M/S encode -> decode must round-trip to unity. Both stages in M/S, everything neutral.
    {
        auto processor = makeProcessor();
        setParam(*processor, "eqMode", 1.0f);
        setParam(*processor, "satMode", 1.0f);
        setParam(*processor, "aDrive", 0.0f);
        setParam(*processor, "bDrive", 0.0f);
        const auto out = render(*processor, 440.0, 0.5f, 0.3);
        check(std::abs(rms(out, out.size() / 2) - 0.5 / std::sqrt(2.0)) < 0.002,
              "M/S encode and decode round-trip to unity");
    }

    // 6. A host block larger than the one prepareToPlay was given must not corrupt memory.
    //    juce::dsp::Oversampling only jasserts its bounds, so this used to write out of range.
    {
        auto processor = makeProcessor();
        setParam(*processor, "osRealtime", 3.0f); // 8x, the largest internal buffer
        juce::AudioBuffer<float> oversized(2, blockSize * 4);
        juce::MidiBuffer midi;
        oversized.clear();
        for (int i = 0; i < oversized.getNumSamples(); ++i)
        {
            const auto v = 0.4f * static_cast<float>(std::sin(0.05 * i));
            oversized.setSample(0, i, v);
            oversized.setSample(1, i, v);
        }
        processor->processBlock(oversized, midi);

        auto finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < oversized.getNumSamples(); ++i)
                finite = finite && std::isfinite(oversized.getSample(ch, i));
        check(finite, "an oversized host block is handled without corruption");
    }

    // 7. A zero-length block must not poison the meters (0/0 used to latch NaN forever).
    {
        auto processor = makeProcessor();
        juce::AudioBuffer<float> empty(2, 0);
        juce::MidiBuffer midi;
        processor->processBlock(empty, midi);
        render(*processor, 1000.0, 0.5f, 0.1);
        check(std::isfinite(processor->getMeterLevel(0)) && std::isfinite(processor->getMeterLevel(1)),
              "a zero-length block does not latch NaN into the meters");
    }

    // 8. Flipping a discrete switch mid-signal must not produce a step discontinuity. Each of
    //    these used to jump coefficients or reroute channels with live filter state.
    {
        struct Switch { const char* id; float from; float to; };
        const Switch switches[] {
            { "eqMode",    0.0f, 1.0f },   // L/R -> M/S
            { "satMode",   0.0f, 1.0f },
            { "eqBypass",  0.0f, 1.0f },
            { "satBypass", 0.0f, 1.0f },
            { "vintage",   0.0f, 1.0f },
            { "aHighFreq", 0.0f, 7.0f },   // 1.6 kHz -> 18 kHz
            { "aLowFreq",  0.0f, 7.0f },
            { "aSatType",  0.0f, 1.0f },
        };

        for (const auto& item : switches)
        {
            auto processor = makeProcessor();
            setParam(*processor, "aDrive", 6.0f);
            setParam(*processor, "bDrive", 6.0f);
            setParam(*processor, "aHighGain", 4.0f);
            setParam(*processor, "aLowGain", 4.0f);
            setParam(*processor, item.id, item.from);

            juce::AudioBuffer<float> buffer(2, blockSize);
            juce::MidiBuffer midi;
            auto phase = 0.0;
            const auto increment = 2.0 * juce::MathConstants<double>::pi * 220.0 / sampleRate;
            std::vector<float> out;

            const auto totalBlocks = 60;
            for (int block = 0; block < totalBlocks; ++block)
            {
                if (block == totalBlocks / 3)
                    setParam(*processor, item.id, item.to);

                for (int i = 0; i < blockSize; ++i)
                {
                    const auto v = 0.5f * static_cast<float>(std::sin(phase));
                    buffer.setSample(0, i, v);
                    buffer.setSample(1, i, v);
                    phase += increment;
                }
                processor->processBlock(buffer, midi);
                for (int i = 0; i < blockSize; ++i)
                    out.push_back(buffer.getSample(0, i));
            }

            // Largest sample-to-sample jump. A 220 Hz sine at 0.5 moves at most ~0.015 per
            // sample, so anything much above that is a discontinuity rather than signal.
            auto worstStep = 0.0f;
            for (size_t i = 1; i < out.size(); ++i)
                worstStep = std::fmax(worstStep, std::abs(out[i] - out[i - 1]));

            check(worstStep < 0.05f, "discrete switch does not produce a step discontinuity");
            if (worstStep >= 0.05f)
                std::printf("  (%s: worst sample step %.4f)\n", item.id, worstStep);
        }
    }

    // Loading the Default factory preset restores every musical parameter to its layout default
    // and leaves workflow parameters (oversampling, bypass) untouched.
    {
        auto processor = makeProcessor();
        BqtPresetManager presets(processor->state());
        setParam(*processor, "aDrive", 11.0f);
        setParam(*processor, "vintage", 1.0f);
        setParam(*processor, "bMix", 30.0f);
        setParam(*processor, "osRealtime", 3.0f);
        presets.loadPreset(0);

        auto allDefault = true;
        for (auto* p : processor->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const auto id = ranged->getParameterID();
                if (id == "osRealtime")
                    check(std::abs(ranged->convertFrom0to1(ranged->getValue()) - 3.0f) < 1.0e-4f,
                          "loading a preset leaves oversampling alone");
                else if (id != "osRender" && id != "eqBypass" && id != "satBypass" && id != "bypass")
                    allDefault = allDefault && std::abs(ranged->getValue() - ranged->getDefaultValue()) < 1.0e-6f;
            }
        check(allDefault, "Default preset restores every musical parameter to its layout default");
    }

    // Review focus 1: switching the oversampling factor with Cream engaged stays click-free.
    {
        auto processor = makeProcessor();
        setParam(*processor, "aDrive", 9.0f);
        setParam(*processor, "bDrive", 9.0f);
        auto out = render(*processor, 220.0, 0.5f, 0.3);
        setParam(*processor, "osRealtime", 3.0f);
        const auto after = render(*processor, 220.0, 0.5f, 0.3);
        out.insert(out.end(), after.begin(), after.end());
        auto worstStep = 0.0f;
        auto finite = true;
        for (size_t i = 1; i < out.size(); ++i)
        {
            finite = finite && std::isfinite(out[i]);
            worstStep = std::fmax(worstStep, std::abs(out[i] - out[i - 1]));
        }
        check(finite && worstStep < 0.03f, "oversampling switch with Cream is click-free");
        if (worstStep >= 0.03f)
            std::printf("  (oversampling switch with Cream: worst sample step %.4f)\n", worstStep);
    }

    // Review focus 2: loud then silence decays to silence.
    {
        auto processor = makeProcessor();
        setParam(*processor, "aDrive", 18.0f);
        setParam(*processor, "bDrive", 18.0f);
        render(*processor, 60.0, 0.9f, 0.5);
        const auto tail = render(*processor, 60.0, 0.0f, 1.0);
        auto last = 0.0f;
        for (size_t i = tail.size() - 4800; i < tail.size(); ++i)
            last = std::fmax(last, std::abs(tail[i]));
        check(std::isfinite(last) && last < 1.0e-6f, "Cream state decays to silence");
    }

    // Review focus 3: very hot input stays bounded.
    {
        auto processor = makeProcessor();
        setParam(*processor, "inputTrim", 12.0f);
        setParam(*processor, "aDrive", 18.0f);
        setParam(*processor, "bDrive", 18.0f);
        const auto out = render(*processor, 50.0, 1.0f, 0.5);
        auto peak = 0.0f;
        auto finite = true;
        for (auto v : out)
        {
            finite = finite && std::isfinite(v);
            peak = std::fmax(peak, std::abs(v));
        }
        check(finite && peak < 4.0f, "hot input through Cream stays bounded");
    }

    // Review focus 4: M/S with mono input creates no side content.
    {
        auto processor = makeProcessor();
        setParam(*processor, "satMode", 1.0f);
        setParam(*processor, "aDrive", 9.0f);
        setParam(*processor, "bDrive", 9.0f);
        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;
        auto worst = 0.0f;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = 0.5f * std::sin(static_cast<float>(b * blockSize + i) * 0.03f);
                buffer.setSample(0, i, v);
                buffer.setSample(1, i, v);
            }
            processor->processBlock(buffer, midi);
            for (int i = 0; i < blockSize; ++i)
                worst = std::fmax(worst, std::abs(buffer.getSample(0, i) - buffer.getSample(1, i)));
        }
        check(worst < 1.0e-5f, "Cream in M/S keeps mono input mono");
    }

    // Review focus 5: fast drive automation across zero is click-free.
    {
        auto processor = makeProcessor();
        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;
        auto phase = 0.0;
        auto previous = 0.0f, worstStep = 0.0f;
        const auto blocks = static_cast<int>(sampleRate) / blockSize;
        for (int b = 0; b < blocks; ++b)
        {
            const auto t = static_cast<float>(b) / static_cast<float>(blocks);
            const auto knob = 18.0f * (t < 0.5f ? 2.0f * t : 2.0f * (1.0f - t));
            setParam(*processor, "aDrive", knob);
            setParam(*processor, "bDrive", knob);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = 0.5f * static_cast<float>(std::sin(phase));
                buffer.setSample(0, i, v);
                buffer.setSample(1, i, v);
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / sampleRate;
            }
            processor->processBlock(buffer, midi);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = buffer.getSample(0, i);
                if (b > 2)
                    worstStep = std::fmax(worstStep, std::abs(v - previous));
                previous = v;
            }
        }
        check(worstStep < 0.05f, "drive automation across zero is click-free");
    }

    // Structural fades, autogain toggle and oversampling switch at arbitrary host block sizes.
    // Renders a 220 Hz 0.5 sine in `hostBlock`-sized blocks, applying `change` to the processor
    // once `switchAt` seconds have elapsed.
    {
        const auto renderWithSwitch = [](int hostBlock, double seconds, double switchAt,
                                         const std::function<void(BqtAudioProcessor&)>& setup,
                                         const std::function<void(BqtAudioProcessor&)>& change)
        {
            BqtAudioProcessor processor;
            processor.setPlayConfigDetails(2, 2, sampleRate, hostBlock);
            processor.prepareToPlay(sampleRate, hostBlock);
            setup(processor);

            juce::AudioBuffer<float> buffer(2, hostBlock);
            juce::MidiBuffer midi;
            auto phase = 0.0;
            const auto increment = 2.0 * juce::MathConstants<double>::pi * 220.0 / sampleRate;
            const auto total = static_cast<int>(sampleRate * seconds);
            const auto switchSample = static_cast<int>(sampleRate * switchAt);
            auto switched = false;
            std::vector<float> out;

            for (int done = 0; done < total; done += hostBlock)
            {
                if (! switched && done >= switchSample)
                {
                    change(processor);
                    switched = true;
                }

                for (int i = 0; i < hostBlock; ++i)
                {
                    const auto v = 0.5f * static_cast<float>(std::sin(phase));
                    buffer.setSample(0, i, v);
                    buffer.setSample(1, i, v);
                    phase += increment;
                }
                processor.processBlock(buffer, midi);
                for (int i = 0; i < hostBlock; ++i)
                    out.push_back(buffer.getSample(0, i));
            }
            return out;
        };

        const auto worstStepOf = [](const std::vector<float>& out)
        {
            auto worst = 0.0f;
            for (size_t i = 1; i < out.size(); ++i)
                worst = std::isfinite(out[i]) ? std::fmax(worst, std::abs(out[i] - out[i - 1])) : 1.0e9f;
            return worst;
        };

        const auto longestSilenceOf = [](const std::vector<float>& out)
        {
            size_t longest = 0, run = 0;
            for (auto v : out)
            {
                run = std::abs(v) < 1.0e-6f ? run + 1 : 0;
                longest = std::max(longest, run);
            }
            return longest;
        };

        // A discrete switch fades out, adopts and fades back in. The fade reached zero mid-block
        // but the new config was only adopted at the next block, so the output sat at exact
        // silence for the rest of the host block (1857 samples at 2048).
        for (const char* id : { "vintage", "aSatType" })
            for (int hostBlock : { 256, 1024, 2048 })
            {
                const auto out = renderWithSwitch(hostBlock, 0.5, 0.2,
                    [](BqtAudioProcessor& p) { setParam(p, "aDrive", 9.0f); setParam(p, "bDrive", 9.0f); },
                    [id](BqtAudioProcessor& p) { setParam(p, id, 1.0f); });
                const auto silence = longestSilenceOf(out);
                const auto step = worstStepOf(out);
                check(static_cast<double>(silence) < 0.006 * sampleRate,
                      "discrete switch silence is bounded by the fade at any host block size");
                check(step < 0.05f, "discrete switch at a large host block has no step discontinuity");
                std::printf("switch %s @%d: longest silence %zu samples, worst step %.4f\n",
                            id, hostBlock, silence, step);
            }

        // Toggling autogain at full drive used to jump the wet level instantly (worst step 0.53
        // on Grit, 0.095 on Cream). Grit at 18 dB with autogain off is driven hard enough that its
        // own waveform steps ~0.14 per sample, so the toggle is bounded by the steady-state step on
        // either side of it as well as by the absolute 0.05.
        for (float type : { 0.0f, 1.0f })
            for (float from : { 0.0f, 1.0f })
            {
                const auto out = renderWithSwitch(256, 0.5, 0.2,
                    [type, from](BqtAudioProcessor& p)
                    {
                        setParam(p, "aSatType", type); setParam(p, "bSatType", type);
                        setParam(p, "aDrive", 18.0f); setParam(p, "bDrive", 18.0f);
                        setParam(p, "autoGain", from);
                    },
                    [from](BqtAudioProcessor& p) { setParam(p, "autoGain", 1.0f - from); });
                const auto at = [&out](double seconds) { return out.begin() + static_cast<long>(seconds * sampleRate); };
                const auto steady = std::fmax(worstStepOf({ at(0.1), at(0.2) }), worstStepOf({ at(0.3), out.end() }));
                const auto step = worstStepOf(out);
                const auto limit = std::fmax(0.05f, 1.1f * steady);
                check(step < limit, "autogain toggle is smoothed");
                std::printf("autogain %s %s: worst step %.4f (steady-state %.4f)\n", type < 0.5f ? "Cream" : "Grit",
                            from < 0.5f ? "off->on" : "on->off", step, steady);
            }

        // Oversampling factor changes fade through the structural transition; the new oversampler
        // starts from zero state with a different latency, which clicked even at zero drive.
        struct OsCase { float drive; float from; float to; };
        for (const auto& item : { OsCase { 9.0f, 0.0f, 3.0f }, OsCase { 0.0f, 0.0f, 1.0f },
                                  OsCase { 0.0f, 1.0f, 3.0f }, OsCase { 0.0f, 3.0f, 0.0f } })
        {
            const auto out = renderWithSwitch(256, 0.6, 0.3,
                [item](BqtAudioProcessor& p)
                {
                    setParam(p, "aDrive", item.drive); setParam(p, "bDrive", item.drive);
                    setParam(p, "osRealtime", item.from);
                },
                [item](BqtAudioProcessor& p) { setParam(p, "osRealtime", item.to); });
            const auto step = worstStepOf(out);
            check(step < 0.03f, "oversampling switch is click-free");
            std::printf("oversampling %.0f->%.0f at drive %.0f: worst step %.4f\n",
                        item.from, item.to, item.drive, step);
        }
    }

    if (failures == 0)
    {
        std::printf("All chain tests passed.\n");
        return 0;
    }

    std::printf("%d chain test(s) failed.\n", failures);
    return 1;
}
