#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace elsewhere;

ElsewhereProcessor::ElsewhereProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ELSEWHERE", createLayout())
{
    for (int i = 0; i < numScenes; ++i)
        scenes[(size_t) i] = createScene (i, convolutionQueue);

    sceneParam  = apvts.getRawParameterValue (ParamIDs::scene);
    mixParam    = apvts.getRawParameterValue (ParamIDs::mix);
    vibesParam  = apvts.getRawParameterValue (ParamIDs::vibes);
    actionParam = apvts.getRawParameterValue (ParamIDs::action);
    outputParam = apvts.getRawParameterValue (ParamIDs::output);
    studioParam = apvts.getRawParameterValue (ParamIDs::studio);
}

ElsewhereProcessor::~ElsewhereProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout ElsewhereProcessor::createLayout()
{
    juce::StringArray names;
    for (auto& s : sceneInfos)
        names.add (s.name);

    auto percent = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; });

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamIDs::scene, 1 }, "Place", names, 2));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::mix, 1 }, "Mix",
                                                             juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::vibes, 1 }, "Vibes",
                                                             juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::action, 1 }, "Action", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::output, 1 }, "Volume",
                                                             juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                                                 [] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; })));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::studio, 1 }, "Back To Studio", false));
    return layout;
}

bool ElsewhereProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void ElsewhereProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int block = std::min (maxChunk, std::max (32, samplesPerBlock));
    for (auto& s : scenes)
    {
        s->prepare (sampleRate, block);
        s->reset();
    }

    for (auto* b : { &dry, &wet, &fading })
        b->setSize (2, block, false, true, true);

    current = juce::jlimit (0, numScenes - 1, (int) sceneParam->load());
    previous = -1;
    fade = 1;
    fadeStep = 1.0f / (0.15f * (float) sampleRate);

    mixSmooth.reset (sampleRate, 0.05);
    outSmooth.reset (sampleRate, 0.05);
    studioSmooth.reset (sampleRate, 0.08);
    mixSmooth.setCurrentAndTargetValue (mixParam->load() * 0.01f);
    outSmooth.setCurrentAndTargetValue (dbToGain (outputParam->load()));
    studioSmooth.setCurrentAndTargetValue (studioParam->load() > 0.5f ? 1.0f : 0.0f);

    bassFilter.setSampleRate (sampleRate);
    bassFilter.lowpass (120.0f, 0.7f);
    bassFilter.reset();
    bassFollower.prepare (sampleRate, 5.0f, 120.0f);
    levelFollower.prepare (sampleRate, 5.0f, 200.0f);
}

void ElsewhereProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int total = buffer.getNumSamples();
    const bool stereo = buffer.getNumChannels() > 1;
    auto* ioL = buffer.getWritePointer (0);
    auto* ioR = stereo ? buffer.getWritePointer (1) : ioL;

    const int wanted = juce::jlimit (0, numScenes - 1, (int) sceneParam->load());
    if (wanted != current)
    {
        // Crossfade from where we were to where we're going.
        previous = current;
        current = wanted;
        scenes[(size_t) current]->reset();
        fade = 0;
        // A held/toggled button from the old scene shouldn't carry over.
        ignoreActionUntilReleased = actionParam->load() > 0.5f;
    }

    const bool actionDown = actionParam->load() > 0.5f;
    if (! actionDown)
        ignoreActionUntilReleased = false;

    Controls controls;
    controls.vibes = vibesParam->load() * 0.01f;
    controls.action = actionDown && ! ignoreActionUntilReleased;

    mixSmooth.setTargetValue (mixParam->load() * 0.01f);
    outSmooth.setTargetValue (dbToGain (outputParam->load()));
    studioSmooth.setTargetValue (studioParam->load() > 0.5f ? 1.0f : 0.0f);

    const float makeupNow  = dbToGain (sceneMakeupDb (current));
    const float makeupPrev = previous >= 0 ? dbToGain (sceneMakeupDb (previous)) : 1.0f;

    for (int start = 0; start < total; start += maxChunk)
    {
        const int n = std::min (maxChunk, total - start);
        const int capacity = wet.getNumSamples();

        for (int sub = 0; sub < n; sub += capacity)
        {
            const int len = std::min (capacity, n - sub);
            const int offset = start + sub;

            auto* dl = dry.getWritePointer (0);  auto* dr = dry.getWritePointer (1);
            auto* wl = wet.getWritePointer (0);  auto* wr = wet.getWritePointer (1);
            for (int i = 0; i < len; ++i)
            {
                dl[i] = wl[i] = ioL[offset + i];
                dr[i] = wr[i] = ioR[offset + i];
                bassFollower.process (bassFilter.process (0, 0.5f * (dl[i] + dr[i])));
            }

            scenes[(size_t) current]->process (wl, wr, len, controls, telemetry);

            const bool crossfading = previous >= 0;
            auto* fl = fading.getWritePointer (0);  auto* fr = fading.getWritePointer (1);
            if (crossfading)
            {
                std::copy (dl, dl + len, fl);
                std::copy (dr, dr + len, fr);
                scenes[(size_t) previous]->process (fl, fr, len, controls, fadingTelemetry);
            }

            for (int i = 0; i < len; ++i)
            {
                float yl = wl[i] * makeupNow, yr = wr[i] * makeupNow;
                if (crossfading)
                {
                    const float gIn = std::sin (fade * 0.5f * pi), gOut = std::cos (fade * 0.5f * pi);
                    yl = yl * gIn + fl[i] * makeupPrev * gOut;
                    yr = yr * gIn + fr[i] * makeupPrev * gOut;
                    fade = std::min (1.0f, fade + fadeStep);
                }

                const float m = mixSmooth.getNextValue();
                yl = lerp (dl[i], yl, m);
                yr = lerp (dr[i], yr, m);

                const float s = studioSmooth.getNextValue();
                const float g = outSmooth.getNextValue();
                yl = softClip (lerp (yl, dl[i], s) * g);
                yr = softClip (lerp (yr, dr[i], s) * g);

                levelFollower.process (std::max (std::abs (yl), std::abs (yr)));
                ioL[offset + i] = yl;
                if (stereo)
                    ioR[offset + i] = yr;
            }

            if (crossfading && fade >= 1.0f)
                previous = -1;
        }
    }

    bassLevel.store (bassFollower.env);
    outputLevel.store (levelFollower.env);
}

void ElsewhereProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void ElsewhereProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ElsewhereProcessor::createEditor()
{
    return new ElsewhereEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ElsewhereProcessor();
}
